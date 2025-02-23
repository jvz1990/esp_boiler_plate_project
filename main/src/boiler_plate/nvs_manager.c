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

#include "nvs_manager.h"

#include "allocation.h"
#include "configuration.h"
#include "deserialisation.h"
#include "serialisation.h"
#include "state.h"

#include <esp_log.h>
#include <freertos/projdefs.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <string.h>

struct nvs_manager
{
  nvs_manager_state_t current_state;
  EventGroupHandle_t request_event_group;
  EventGroupHandle_t state_event_group;
  TaskHandle_t fsm_task_handle;

  bool nvs_flash_inited;
};

static const char* TAG = "NVS Manager";

#define CONFIG_NAMESPACE "config_storage"
#define UNIT_CONFIG_KEY "unit_config"

static void fsm_task(void* arg);
static esp_err_t transition_to_state(nvs_manager_t* manager, nvs_manager_state_request_t state_request);

static esp_err_t update_nvs_from_config();
static esp_err_t read_nvs_into_config();

static bool is_config_stored_in_nvs(const char* key);
static void store_unit_default_config_to_nvs();
static esp_err_t initialise_nvs_flash();
static esp_err_t deinitialise_nvs_flash();

nvs_manager_t* nvs_manager_create(UBaseType_t priority) {
  nvs_manager_t* manager = calloc(1, sizeof(nvs_manager_t));
  if (manager == NULL) {
    ESP_LOGE(TAG, "Unable to create nvs manager");
    return NULL;
  }

  *manager = (nvs_manager_t){
    .current_state = NVS_STATE_NONE,
    .request_event_group = xEventGroupCreate(),
    .state_event_group = xEventGroupCreate(),
    .fsm_task_handle = NULL,

    .nvs_flash_inited = false
  };

  if (manager->request_event_group == NULL || manager->state_event_group == NULL) {
    free(manager);
    ESP_LOGE(TAG, "Unable to create nvs manager");
    return NULL;
  }

  xTaskCreate(
    fsm_task,
    TAG,
    2048,
    manager,
    priority,
    &manager->fsm_task_handle
    );

  return manager;
}

void nvs_manager_destroy(nvs_manager_t* const manager) {
  if (manager == NULL) return;

  if (manager->fsm_task_handle) {
    vTaskDelete(manager->fsm_task_handle);
  }
  if (manager->request_event_group) {
    vEventGroupDelete(manager->request_event_group);
  }
  if (manager->state_event_group) {
    vEventGroupDelete(manager->state_event_group);
  }

  if (manager->nvs_flash_inited) {
    deinitialise_nvs_flash();
  }

  free(manager);
}

esp_err_t nvs_manager_request_state(nvs_manager_t* const manager, nvs_manager_state_request_t new_state) {
  if (manager == NULL) return ESP_ERR_NOT_FOUND;

  xEventGroupSetBits(manager->request_event_group, new_state);

  return ESP_OK;
}

void nvs_manager_wait_until_state(nvs_manager_t const* const manager, nvs_manager_state_t wait_state) {
  if (manager == NULL) return;

  xEventGroupWaitBits(manager->state_event_group, wait_state, pdFALSE, pdFALSE, portMAX_DELAY);
}

static void fsm_task(void* arg) {
  nvs_manager_t* manager = arg;

  if (manager == NULL) {
    ESP_LOGE(TAG, "No nvs manager passed to fsm task");
    vTaskDelete(NULL);
    return;
  }


  if (manager->request_event_group == NULL) {
    ESP_LOGE(TAG, "Request event group not created");
    vTaskDelete(NULL);
    return;
  }

  while (1) {
    EventBits_t bits = xEventGroupWaitBits(manager->request_event_group,
                                           NVS_STATE_NONE_REQUEST |
                                           NVS_STATE_READY_REQUEST |
                                           NVS_STATE_READ_REQUEST |
                                           NVS_STATE_WRITE_REQUEST, pdTRUE, pdFALSE, portMAX_DELAY);

    nvs_manager_state_request_t requested_state = NVS_STATE_NONE_REQUEST;
    if (bits & NVS_STATE_NONE_REQUEST) {
      requested_state = NVS_STATE_NONE_REQUEST;
    } else if (bits & NVS_STATE_READY_REQUEST) {
      requested_state = NVS_STATE_READY_REQUEST;
    } else if (bits & NVS_STATE_READ_REQUEST) {
      requested_state = NVS_STATE_READ_REQUEST;
    } else if (bits & NVS_STATE_WRITE_REQUEST) {
      requested_state = NVS_STATE_WRITE_REQUEST;
    } else {
      ESP_LOGE(TAG, "Unknown request state: %lu, ", bits);
      continue;
    }

    if (transition_to_state(manager, requested_state) == ESP_OK) {
      ESP_LOGI(TAG, "Successful state transition: %lu", bits);
    } else {
      ESP_LOGI(TAG, "Unsuccessful state transition: %lu", bits);
    }

    taskYIELD();
  }

  vTaskDelete(NULL);
}

esp_err_t transition_to_state(nvs_manager_t* const manager, nvs_manager_state_request_t state_request) {
  // Validation
  if (manager->current_state == NVS_BUSY) {
    ESP_LOGW(TAG, "NVS busy - cannot perform request");
    return ESP_FAIL;
  }

  if ((manager->current_state == NVS_STATE_NONE) &&
    ((state_request == NVS_STATE_READ_REQUEST) || (state_request == NVS_STATE_WRITE_REQUEST))) {
    ESP_LOGE(TAG, "State change requested denied. Need to request ready first");
    return ESP_FAIL;
  }

  esp_err_t ret = ESP_OK;
  if (manager->current_state == NVS_STATE_NONE && state_request == NVS_STATE_READY_REQUEST) {
    ret = initialise_nvs_flash();
    if (ret == ESP_OK) {
      if (!is_config_stored_in_nvs(UNIT_CONFIG_KEY)) {
        store_unit_default_config_to_nvs();
      }
      read_nvs_into_config();
      manager->nvs_flash_inited = true;
      manager->current_state = NVS_READY;
      xEventGroupSetBits(manager->state_event_group, NVS_READY);
    }
  } else if (manager->current_state == NVS_READY && state_request == NVS_STATE_READ_REQUEST) {
    manager->current_state = NVS_BUSY;
    xEventGroupSetBits(manager->state_event_group, NVS_BUSY);
    ret = read_nvs_into_config();
    manager->current_state = NVS_READY;
    xEventGroupSetBits(manager->state_event_group, NVS_READY);
  } else if (manager->current_state == NVS_READY && state_request == NVS_STATE_WRITE_REQUEST) {
    manager->current_state = NVS_BUSY;
    xEventGroupSetBits(manager->state_event_group, NVS_BUSY);
    ret = update_nvs_from_config();
    manager->current_state = NVS_READY;
    xEventGroupSetBits(manager->state_event_group, NVS_READY);
  } else if (manager->current_state == NVS_READY && state_request == NVS_STATE_NONE_REQUEST) {
    manager->current_state = NVS_BUSY;
    xEventGroupSetBits(manager->state_event_group, NVS_BUSY);
    ret = deinitialise_nvs_flash();
    if (ret == ESP_OK) {
      manager->current_state = NVS_STATE_NONE;
      xEventGroupSetBits(manager->state_event_group, NVS_STATE_NONE);
      manager->nvs_flash_inited = false;
    } else {
      manager->current_state = NVS_READY;
      xEventGroupSetBits(manager->state_event_group, NVS_READY);
    }
  } else {
    ESP_LOGE(TAG, "State change requested denied - unhandled");
    ret = ESP_FAIL;
  }

  return ret;
}

static bool is_config_stored_in_nvs(const char* key) {
  nvs_handle_t nvs_handle;
  ESP_ERROR_CHECK(nvs_open(CONFIG_NAMESPACE, NVS_READWRITE, &nvs_handle));

  // Check if configuration already exists
  size_t required_size = 0;
  const esp_err_t err = nvs_get_blob(nvs_handle, key, NULL, &required_size);
  if (err == ESP_OK) {
    ESP_LOGI(TAG, "Configuration %s exists in NVS", key);
  } else {
    ESP_LOGI(TAG, "Configuration %s does not exist in NVS", key);
  }
  nvs_close(nvs_handle);

  return err == ESP_OK;
}

static void store_unit_default_config_to_nvs() {
  nvs_handle_t nvs_handle;
  ESP_ERROR_CHECK(nvs_open(CONFIG_NAMESPACE, NVS_READWRITE, &nvs_handle));

  // Load initial config from menuconfig
  unit_configuration_t* unit_cfg = unit_config_acquire();
  connectivity_configuration_t* con_cfg = &(unit_cfg->con_config);
  system_settings_configuration_t* sys_cfg = &(unit_cfg->sys_config);
  user_configuration_t* usr_cfg = &(unit_cfg->user_config);

  // Initialize connectivity configuration
  con_cfg->wifi_settings_count = 1;
  check_and_free(con_cfg->wifi_settings); // Free existing memory
  con_cfg->wifi_settings = malloc(sizeof(wifi_settings_t)); // Allocate for one wifi setting
  if (!con_cfg->wifi_settings) {
    ESP_LOGE(TAG, "Failed to allocate memory for wifi_settings");
    goto cleanup;
  }
  memset(con_cfg->wifi_settings, 0, sizeof(wifi_settings_t));

  // Allocate and initialize ssid and password
  con_cfg->wifi_settings->ssid_len = strlen(CONFIG_SSID);
  con_cfg->wifi_settings->password_len = strlen(CONFIG_PASSWORD);
  con_cfg->wifi_settings->ssid = malloc(con_cfg->wifi_settings->ssid_len + 1);
  con_cfg->wifi_settings->password = malloc(con_cfg->wifi_settings->password_len + 1);
  if (!con_cfg->wifi_settings->ssid || !con_cfg->wifi_settings->password) {
    ESP_LOGE(TAG, "Failed to allocate memory for ssid or password");
    goto cleanup;
  }
  strlcpy(con_cfg->wifi_settings->ssid, CONFIG_SSID, con_cfg->wifi_settings->ssid_len + 1);
  strlcpy(con_cfg->wifi_settings->password, CONFIG_PASSWORD, con_cfg->wifi_settings->password_len + 1);

  // Allocate and initialize ota_url and version_url
  con_cfg->ota_url_len = strlen(CONFIG_OTA_URL);
  con_cfg->version_url_len = strlen(CONFIG_FIRMWARE_VERSION_ENDPOINT);
  con_cfg->ota_url = malloc(con_cfg->ota_url_len + 1);
  con_cfg->version_url = malloc(con_cfg->version_url_len + 1);
  if (!con_cfg->ota_url || !con_cfg->version_url) {
    ESP_LOGE(TAG, "Failed to allocate memory for ota_url or version_url");
    goto cleanup;
  }
  strlcpy(con_cfg->ota_url, CONFIG_OTA_URL, con_cfg->ota_url_len + 1);
  strlcpy(con_cfg->version_url, CONFIG_FIRMWARE_VERSION_ENDPOINT, con_cfg->version_url_len + 1);

  // Initialize system settings configuration
  sys_cfg->log_level = CONFIG_LOG_DEFAULT_LEVEL;

  // Initialize user configuration
  check_and_free(usr_cfg->unit_name);
  usr_cfg->unit_name_len = strlen(CONFIG_ESP_NAME);
  usr_cfg->unit_name = malloc(usr_cfg->unit_name_len + 1);
  if (!usr_cfg->unit_name) {
    ESP_LOGE(TAG, "Failed to allocate memory for unit_name");
    goto cleanup;
  }
  strlcpy(usr_cfg->unit_name, CONFIG_ESP_NAME, usr_cfg->unit_name_len + 1);

  // Calculate the size of the serialized data
  const size_t buffer_size = calculate_unit_configuration_size(unit_cfg);

  // Allocate buffer dynamically
  uint8_t* buffer = calloc(buffer_size, sizeof(uint8_t));
  if (!buffer) {
    ESP_LOGE(TAG, "Failed to allocate memory for serialization buffer");
    goto cleanup;
  }

  // Serialize the configuration
  size_t serialized_size = serialize_unit_configuration(unit_cfg, buffer);
  if (serialized_size != buffer_size) {
    ESP_LOGE(TAG, "Serialization size mismatch: expected %d, got %d", buffer_size, serialized_size);
    free(buffer);
    goto cleanup;
  }

  // Store in NVS
  ESP_ERROR_CHECK(nvs_set_blob(nvs_handle, UNIT_CONFIG_KEY, buffer, serialized_size));
  ESP_ERROR_CHECK(nvs_commit(nvs_handle));
  ESP_LOGI(TAG, "Default unit configuration stored in NVS.");

  // Free the buffer
  free(buffer);

cleanup:
  // Cleanup dynamically allocated memory
  if (con_cfg->wifi_settings) {
    free(con_cfg->wifi_settings->ssid);
    free(con_cfg->wifi_settings->password);
    free(con_cfg->wifi_settings);
  }
  free(con_cfg->ota_url);
  free(con_cfg->version_url);
  free(usr_cfg->unit_name);
  unit_config_release();
  nvs_close(nvs_handle);
}

// Update NVS from config
static esp_err_t update_nvs_from_config() {
  nvs_handle_t nvs_handle;
  ESP_ERROR_CHECK(nvs_open(CONFIG_NAMESPACE, NVS_READWRITE, &nvs_handle));

  const unit_configuration_t* unit_cfg = unit_config_acquire();
  const size_t blob_size = calculate_unit_configuration_size(unit_cfg);

  uint8_t* serialized_blob = NULL;
  ESP_ERROR_CHECK(allocate_and_clear_buffer((void**)&serialized_blob, blob_size, TAG, ESP_ERR_NO_MEM));

  serialize_unit_configuration(unit_cfg, serialized_blob);

  unit_config_release();

  // Store in NVS
  ESP_ERROR_CHECK(nvs_set_blob(nvs_handle, UNIT_CONFIG_KEY, serialized_blob, blob_size));
  ESP_ERROR_CHECK(nvs_commit(nvs_handle));

  // Cleanup
  check_and_free(serialized_blob);
  nvs_close(nvs_handle);
  ESP_LOGI(TAG, "Current unit configuration stored in NVS");

  return ESP_OK;
}

// Deserialize NVS blobs into config
static esp_err_t read_nvs_into_config() {
  nvs_handle_t nvs_handle;
  ESP_ERROR_CHECK(nvs_open(CONFIG_NAMESPACE, NVS_READONLY, &nvs_handle));

  // Get blob size
  size_t blob_size = 0;
  esp_err_t ret = nvs_get_blob(nvs_handle, UNIT_CONFIG_KEY, NULL, &blob_size);
  if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND) {
    ESP_ERROR_CHECK(ret);
  }
  if (blob_size == 0) {
    nvs_close(nvs_handle);
    return ESP_ERR_NOT_FOUND;
  }

  uint8_t* serialized_blob = NULL;
  ESP_ERROR_CHECK(allocate_and_clear_buffer((void**)&serialized_blob, blob_size, TAG, ESP_ERR_NO_MEM));
  ESP_ERROR_CHECK(nvs_get_blob(nvs_handle, UNIT_CONFIG_KEY, serialized_blob, &blob_size));

  ESP_LOGI(TAG, "Loaded blob of size %d.", blob_size);

  unit_configuration_t* unit_cfg = unit_config_acquire();
  deserialize_unit_configuration(unit_cfg, serialized_blob);

  esp_log_level_set("*", unit_cfg->sys_config.log_level);

  unit_config_release();
  check_and_free(serialized_blob);
  nvs_close(nvs_handle);
  ESP_LOGI(TAG, "Unit configuration loaded from NVS");

  return ESP_OK;
}

static esp_err_t initialise_nvs_flash() {
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_LOGW(TAG, "NVS partition was truncated, erasing...");

    ret = nvs_flash_erase();
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "NVS flash erase failed");
      return ret;
    }

    ret = nvs_flash_init();
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "NVS flash init failed");
    }
  }

  return ret;
}

static esp_err_t deinitialise_nvs_flash() {
  esp_err_t ret = nvs_flash_deinit();

  return ret;
}
