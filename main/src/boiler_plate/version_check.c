/*
 * Copyright 2025 Johan van Zyl
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "version_check.h"
#include "allocation.h"
#include "configuration.h"

#include <cJSON.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_tls.h>
#include <state.h>
#include <string.h>
#include <sys/param.h>

static const char* TAG = "VERSION_CHECK";

extern const uint8_t server_cert_pem_start[] asm("_binary_ca_cert_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_ca_cert_pem_end");

#define ESP_NEW_FIRMWARE_VERSION_FOUND 3
#define MAX_VERSION_STRING_LENGTH 32
#define MAX_HTTP_OUTPUT_BUFFER 128
#define JSON_VERSION_TAG "version"

static esp_err_t version_check_http_event_handler(esp_http_client_event_t* evt) {
  static char* output_buffer = NULL; // Buffer for HTTP response
  static int output_len = 0; // Bytes read

  switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
      ESP_LOGE(TAG, "HTTP_EVENT_ERROR");
      break;

    case HTTP_EVENT_ON_CONNECTED:
      ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
      break;

    case HTTP_EVENT_HEADER_SENT:
      ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
      break;

    case HTTP_EVENT_ON_HEADER:
      ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
      break;

    case HTTP_EVENT_ON_DATA:
      ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);

      if (output_len == 0 && evt->user_data) {
        memset(evt->user_data, 0, MAX_HTTP_OUTPUT_BUFFER);
      }

      if (!esp_http_client_is_chunked_response(evt->client)) {
        int copy_len = 0;
        if (evt->user_data) {
          copy_len = MIN(evt->data_len, MAX_HTTP_OUTPUT_BUFFER - output_len);
          if (copy_len) {
            memcpy(evt->user_data + output_len, evt->data, copy_len);
          }
        } else {
          const int content_len = (int)esp_http_client_get_content_length(evt->client);
          if (!output_buffer) {
            output_buffer = (char*)calloc(content_len + 1, sizeof(char));
            if (!output_buffer) {
              ESP_LOGE(TAG, "Failed to allocate memory for output buffer");
              return ESP_FAIL;
            }
          }
          copy_len = MIN(evt->data_len, content_len - output_len);
          if (copy_len) {
            memcpy(output_buffer + output_len, evt->data, copy_len);
          }
        }
        output_len += copy_len;
      }
      break;

    case HTTP_EVENT_ON_FINISH:
      ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
      check_and_free(output_buffer);
      output_len = 0;
      break;

    case HTTP_EVENT_DISCONNECTED:
      ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
      int mbedtls_err = 0;
      const esp_err_t err = esp_tls_get_and_clear_last_error((esp_tls_error_handle_t)evt->data, &mbedtls_err, NULL);
      if (err) {
        ESP_LOGI(TAG, "Last esp error code: 0x%x", err);
        ESP_LOGI(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
      }
      check_and_free(output_buffer);
      output_len = 0;
      break;

    default:
      break;
  }
  return ESP_OK;
}

static esp_err_t parse_https_response(char const* const https_response, char* version) {
  esp_err_t err;

  cJSON* json = cJSON_Parse(https_response);
  if (!json) {
    ESP_LOGE(TAG, "Failed to parse JSON response");
    err = ESP_FAIL;
    return err;
  }

  const cJSON* version_item = cJSON_GetObjectItem(json, JSON_VERSION_TAG);
  if (cJSON_IsString(version_item)) {
    strlcpy(version, version_item->valuestring, MAX_VERSION_STRING_LENGTH);
    ESP_LOGI(TAG, "Server version: %s", version);
    err = ESP_OK;
  } else {
    ESP_LOGE(TAG, "'version' not found in json response");
    err = ESP_FAIL;
  }

  cJSON_Delete(json);
  return err;
}

static esp_err_t get_https_version(char const* const url_version, char* version) {
  char* local_response_buffer = NULL;
  esp_err_t err =
    allocate_and_clear_buffer((void**)&local_response_buffer, MAX_HTTP_OUTPUT_BUFFER, TAG, ESP_ERR_NO_MEM);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to allocate memory for local response");
    return err;
  }

  const esp_http_client_config_t https_config = {
    .url = url_version,
    .cert_pem = (char*)server_cert_pem_start, // TODO move cert to spiffs
    .event_handler = version_check_http_event_handler,
    .user_data = local_response_buffer,
    .method = HTTP_METHOD_GET,
    .disable_auto_redirect = true,
  };

  esp_http_client_handle_t client = esp_http_client_init(&https_config);
  if ((err = esp_http_client_perform(client)) == ESP_OK) {
    ESP_LOGI(TAG, "HTTPS GET request succeeded");
  } else {
    ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
  }

  if (client != NULL) {
    esp_http_client_cleanup(client);
  }

  if (local_response_buffer[0] == '\0') {
    ESP_LOGE(TAG, "Could not read endpoint data");
    err = ESP_FAIL;
  }

  err = parse_https_response(local_response_buffer, version);

  check_and_free(local_response_buffer);
  return err;
}

static esp_err_t check_https_firmware_version() {
  esp_err_t err = {0};
  if (!is_wifi_connected()) {
    ESP_LOGE(TAG, "Wi-Fi not connected");
    return ESP_FAIL;
  }

  // Current version
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_app_desc_t running_app_info;
  if ((err = esp_ota_get_partition_description(running, &running_app_info)) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to get running application description");
    return err;
  }

  // Server version
  char* server_version = NULL;
  err = allocate_and_clear_buffer((void**)&server_version, MAX_HTTP_OUTPUT_BUFFER, TAG, ESP_ERR_NO_MEM);
  if (err != ESP_OK) {
    return err;
  }

  const unit_configuration_t* unit_cfg = unit_config_acquire();
  const connectivity_configuration_t* con_cfg = &(unit_cfg->con_config);
  char const* const version_url = con_cfg->version_url;
  if ((err = get_https_version(version_url, server_version)) == ESP_OK) {
    err = (memcmp(server_version, running_app_info.version, MAX_VERSION_STRING_LENGTH) == 0)
      ? ESP_OK
      : ESP_NEW_FIRMWARE_VERSION_FOUND; // Compare
  }

  check_and_free(server_version);
  unit_config_release();
  return err;
}

void init_version_checking_task() {
  while (1) {
    const EventBits_t bits =
      xEventGroupWaitBits(system_event_group, FIRMWARE_REQUEST_VERSION_CHECK_BIT | REBOOT_BIT, pdFALSE, pdFALSE,
                          portMAX_DELAY);

    if (bits & FIRMWARE_REQUEST_VERSION_CHECK_BIT) {
      xEventGroupClearBits(system_event_group, FIRMWARE_REQUEST_VERSION_CHECK_BIT);
      ESP_LOGI(TAG, "Version check request received.");
      // Check version
      const esp_err_t err = check_https_firmware_version();
      if (err == ESP_OK) {
        xEventGroupSetBits(system_event_group, FIRMWARE_VERSION_UP_TO_DATE_BIT);
      } else if (err == ESP_NEW_FIRMWARE_VERSION_FOUND) {
        xEventGroupSetBits(system_event_group, NEW_FIRMWARE_AVAILABLE_BIT);
      } else {
        ESP_LOGE(TAG, "Error: %s", esp_err_to_name(err));
      }
    }

    if (bits & REBOOT_BIT) {
      ESP_LOGW(TAG, "RECEIVED REBOOT");
      break;
    }

    taskYIELD();
  }

  vTaskDelete(NULL);
  ESP_LOGI(TAG, "Done");
}
