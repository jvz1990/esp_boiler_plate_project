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

#include "ota_download.h"

#include <configuration.h>
#include <esp_app_desc.h>
#include <esp_http_client.h>
#include <esp_https_ota.h>
#include <esp_log.h>
#include <esp_ota_ops.h>
#include <esp_wifi.h>
#include <state.h>

static const char* TAG = "OTA_DOWNLOAD";
extern const uint8_t server_cert_pem_start[] asm("_binary_ca_cert_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_ca_cert_pem_end");

#define ESP_FIRMWARE_UP_TO_DATE (ESP_ERR_HTTPS_OTA_BASE + 2)

static esp_err_t new_version_available(const esp_https_ota_handle_t* ota_handle);
static esp_err_t create_ota_handle(esp_https_ota_handle_t* ota_handle);
static esp_err_t download_firmware(const esp_https_ota_handle_t* ota_handle);
static esp_err_t perform_ota_update();

static esp_err_t new_version_available(const esp_https_ota_handle_t* ota_handle) {
  esp_app_desc_t ota_app = {0};
  esp_app_desc_t current_app = {0};

  if (esp_https_ota_get_img_desc(*ota_handle, &ota_app) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to get image description");
    return ESP_FAIL;
  }

  const esp_partition_t* running = esp_ota_get_running_partition();
  assert(running);
  esp_ota_get_partition_description(running, &current_app);

  ESP_LOGI(TAG, "Running firmware version: %s", current_app.version);
  if (strncmp(ota_app.version, current_app.version, sizeof(ota_app.version)) == 0) {
    ESP_LOGW(TAG, "Current version is the same as new. Skipping update.");
    return ESP_FIRMWARE_UP_TO_DATE;
  }

  return ERR_OK;
}

static esp_err_t create_ota_handle(esp_https_ota_handle_t* ota_handle) {
  unit_configuration_t* unit_cfg = unit_config_acquire();
  connectivity_configuration_t* con_cfg = &(unit_cfg->con_config);

  esp_http_client_config_t config = {.url = con_cfg->ota_url,
                                     .cert_pem = (char*)server_cert_pem_start,
                                     .keep_alive_enable = true,
                                     .skip_cert_common_name_check = true}; // TODO experiment with value
  unit_config_release();

  const esp_https_ota_config_t ota_config = {
    .http_config = &config,
    .partial_http_download = true,
    .max_http_request_size = 4096, // TODO experiment with value
  };

  if (esp_https_ota_begin(&ota_config, ota_handle) != ESP_OK) {
    ESP_LOGE(TAG, "OTA Begin failed");
    return ESP_FAIL;
  }

  return ESP_OK;
}

static esp_err_t download_firmware(const esp_https_ota_handle_t* ota_handle) {
  esp_err_t err = ESP_OK;

  ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
  while (1) {
    const esp_err_t dl_progress = esp_https_ota_perform(*ota_handle);
    const int bytes_read = esp_https_ota_get_image_len_read(*ota_handle);
    const int total_size = esp_https_ota_get_image_size(*ota_handle);

    if (dl_progress == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
      ESP_LOGI(TAG, "Downloading... Progress: %d/%d bytes (%.1f%%)", bytes_read, total_size,
               (total_size > 0) ? (bytes_read * 100.0 / total_size) : 0);
    } else if (dl_progress == ESP_OK) {
      ESP_LOGI(TAG, "Download completed");
      break;
    } else {
      ESP_LOGE(TAG, "Error during download: %s", esp_err_to_name(dl_progress));
      err = dl_progress;
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(50)); // TODO experiment with value
  }

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error: %s", esp_err_to_name(err));
  }
  ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MIN_MODEM));

  return err;
}

static esp_err_t perform_ota_update() {
  ESP_LOGI(TAG, "Starting OTA Update");

  esp_https_ota_handle_t ota_handle = NULL;
  esp_err_t err = create_ota_handle(&ota_handle);
  if (err != ESP_OK)
    return ESP_FAIL;

  if (new_version_available(&ota_handle) == ESP_FIRMWARE_UP_TO_DATE) {
    esp_https_ota_abort(ota_handle);
    return ESP_FIRMWARE_UP_TO_DATE;
  }

  err = download_firmware(&ota_handle);
  if (err != ESP_OK)
    return err;

  if (!esp_https_ota_is_complete_data_received(ota_handle)) {
    ESP_LOGE(TAG, "OTA data not fully received");
    err = ESP_FAIL;
  }

  if (err == ESP_OK && esp_https_ota_finish(ota_handle) == ESP_OK) {
    ESP_LOGI(TAG, "OTA successful, restarting...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
  }

  esp_https_ota_abort(ota_handle);
  return err;
}

void init_ota_task() {
  if (!is_wifi_connected()) {
    ESP_LOGE(TAG, "Wi-Fi not connected");
    vTaskDelete(NULL);
    return;
  }

  ESP_LOGI(TAG, "Initializing OTA Task");

  const esp_err_t response = perform_ota_update();
  if (response == ESP_FAIL) {
    ESP_LOGE(TAG, "Error: %s", esp_err_to_name(response));
  }

  if (response == ESP_FIRMWARE_UP_TO_DATE) {
    ESP_LOGW(TAG, "Firmware is already up-to-date. Please use 'version_check' next time");
  } else {
    ESP_LOGE(TAG, "Unhandled OTA state");
  }

  vTaskDelete(NULL);
  ESP_LOGI(TAG, "Done");
}
