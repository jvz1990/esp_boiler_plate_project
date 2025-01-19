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

#include "nvs_mgmt.h"

#include <allocation.h>

#include "configuration.h"
#include "state.h"

#include <esp_log.h>
#include <freertos/projdefs.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <string.h>

static const char* TAG = "CONFIG_MANAGER";

#define CONFIG_NAMESPACE "config_storage"
#define CONFIG_KEY "configs"

static esp_err_t update_nvs_from_config();
static esp_err_t read_nvs_into_config();

static bool is_config_stored_in_nvs();
static void store_default_config_to_nvs();

// Function not error handled. If error occurs here, something is seriously wrong and you need to manually flash the ESP
static bool is_config_stored_in_nvs() {
  nvs_handle_t nvs_handle;
  ESP_ERROR_CHECK(nvs_open(CONFIG_NAMESPACE, NVS_READWRITE, &nvs_handle));

  // Check if configuration already exists
  size_t required_size = 0;
  const esp_err_t err = nvs_get_blob(nvs_handle, CONFIG_KEY, NULL, &required_size);
  if (err == ESP_OK) {
    ESP_LOGI(TAG, "Configuration exists in NVS");
    nvs_close(nvs_handle);
    return true;
  }

  return false;
}

// Function not error handled. If error occurs here, something is seriously wrong and you need to manually flash the ESP
static void store_default_config_to_nvs() {
  nvs_handle_t nvs_handle;
  ESP_ERROR_CHECK(nvs_open(CONFIG_NAMESPACE, NVS_READWRITE, &nvs_handle));

  // Store initial config from menuconfig
  unit_configuration_t configuration;
  configuration.wifi_configs_count = 1;

  // Allocate memory for wifi_settings
  wifi_settings_t temp_wifi_settings[configuration.wifi_configs_count];
  strlcpy(temp_wifi_settings[0].ssid, CONFIG_SSID, sizeof(temp_wifi_settings[0].ssid));
  strlcpy(temp_wifi_settings[0].password, CONFIG_PASSWORD, sizeof(temp_wifi_settings[0].password));

  strlcpy(configuration.ota_url, CONFIG_OTA_URL, sizeof(configuration.ota_url));
  strlcpy(configuration.version_url, CONFIG_FIRMWARE_VERSION_ENDPOINT, sizeof(configuration.version_url));

  // Serialize the configuration
  const size_t blob_size = sizeof(unit_configuration_t) + configuration.wifi_configs_count * sizeof(wifi_settings_t);
  uint8_t* serialized_blob = (uint8_t*)malloc(blob_size);

  memcpy(serialized_blob, &configuration, sizeof(unit_configuration_t));
  memcpy(serialized_blob + sizeof(unit_configuration_t), temp_wifi_settings,
         configuration.wifi_configs_count * sizeof(wifi_settings_t));

  // Save the serialized blob to NVS
  ESP_ERROR_CHECK(nvs_set_blob(nvs_handle, CONFIG_KEY, serialized_blob, blob_size));
  ESP_ERROR_CHECK(nvs_commit(nvs_handle));
  ESP_LOGI(TAG, "Default configuration stored in NVS.");

  // Copy into shared data
  shared_data_t* shared_data = shared_data_acquire();
  unit_configuration_t* unit_config = &(shared_data->unit_config);
  // Copy OTA URL and version URL to shared data
  strlcpy(unit_config->ota_url, configuration.ota_url, sizeof(unit_config->ota_url));
  strlcpy(unit_config->version_url, configuration.version_url, sizeof(unit_config->version_url));

  // Allocate memory for wifi_settings dynamically and copy from temp_wifi_settings
  unit_config->wifi_settings = (wifi_settings_t*)malloc(configuration.wifi_configs_count * sizeof(wifi_settings_t));
  if (unit_config->wifi_settings != NULL) {
    memcpy(unit_config->wifi_settings, temp_wifi_settings, configuration.wifi_configs_count * sizeof(wifi_settings_t));
  } else {
    ESP_LOGE(TAG, "Failed to allocate memory for wifi_settings in shared data");
  }
  shared_data_release();

  // Clean up
  free(serialized_blob);
  nvs_close(nvs_handle);
}

static esp_err_t update_nvs_from_config() {
  nvs_handle_t nvs_handle;
  esp_err_t err = ESP_OK;
  ESP_ERROR_CHECK(nvs_open(CONFIG_NAMESPACE, NVS_READWRITE, &nvs_handle));

  const shared_data_t* shared_data = shared_data_acquire();
  const unit_configuration_t* config = &(shared_data->unit_config);

  // Calculate the size of the configuration blob
  const size_t config_data_size = sizeof(unit_configuration_t) + (config->wifi_configs_count * sizeof(wifi_settings_t));

  // Allocate memory for the configuration data (unit_configuration + wifi_settings)
  uint8_t* config_data = (uint8_t*)malloc(config_data_size);
  err = allocate_and_clear_buffer((void**) config_data, config_data_size, TAG, ESP_ERR_NO_MEM);

  // Serialize the unit_configuration structure
  if (err == ESP_OK) {
    memcpy(config_data, config, sizeof(unit_configuration_t));

    // Serialize wifi_settings into the blob
    if (config->wifi_configs_count > 0 && config->wifi_settings != NULL) {
      memcpy(config_data + sizeof(unit_configuration_t), config->wifi_settings,
             config->wifi_configs_count * sizeof(wifi_settings_t));
    }

    // Store the serialized configuration in NVS
    err = nvs_set_blob(nvs_handle, CONFIG_KEY, config_data, config_data_size);
  }

  // Commit changes and close NVS
  if (err == ESP_OK) {
    ESP_ERROR_CHECK(nvs_commit(nvs_handle));
  }

  nvs_close(nvs_handle);
  // Clean up allocated memory
  free(config_data);

  shared_data_release();

  if (err != ESP_OK)
    ESP_LOGE(TAG, "Error: %s", esp_err_to_name(err));

  return err;
}

static esp_err_t read_nvs_into_config() {
  nvs_handle_t nvs_handle;
  ESP_ERROR_CHECK(nvs_open(CONFIG_NAMESPACE, NVS_READONLY, &nvs_handle));

  shared_data_t* shared_data = shared_data_acquire();
  unit_configuration_t* config = &(shared_data->unit_config);

  // Get the size of the stored blob
  size_t required_size = 0;
  esp_err_t err = nvs_get_blob(nvs_handle, CONFIG_KEY, NULL, &required_size);
  if (err != ESP_OK || required_size == 0) {
    ESP_LOGE(TAG, "Failed to get configs from NVS or invalid size");
    nvs_close(nvs_handle);
    shared_data_release();
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Required size: %d", required_size);

  // Allocate memory for the configuration data (unit_configuration + wifi_settings)
  uint8_t* config_data = (uint8_t*)malloc(required_size);
  if (config_data == NULL) {
    ESP_LOGE(TAG, "Memory allocation failed for config data");
    nvs_close(nvs_handle);
    shared_data_release();
    return ESP_ERR_NO_MEM;
  }

  // Read the blob into the allocated memory
  err = nvs_get_blob(nvs_handle, CONFIG_KEY, config_data, &required_size);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to read configurations from NVS");
    free(config_data);
    nvs_close(nvs_handle);
    shared_data_release();
    return err;
  }

  // Copy the unit_configuration structure from the blob
  memcpy(config, config_data, sizeof(unit_configuration_t));

  // Allocate memory for wifi_settings based on wifi_configs_count
  if (config->wifi_configs_count > 0) {
    config->wifi_settings = (wifi_settings_t*)malloc(config->wifi_configs_count * sizeof(wifi_settings_t));
    if (config->wifi_settings == NULL) {
      ESP_LOGE(TAG, "Memory allocation failed for wifi_settings");
      free(config_data);
      nvs_close(nvs_handle);
      shared_data_release();
      return ESP_ERR_NO_MEM;
    }

    // Copy the wifi_settings data from the serialized blob
    memcpy(config->wifi_settings, config_data + sizeof(unit_configuration_t),
           config->wifi_configs_count * sizeof(wifi_settings_t));
  }

  // Clean up and close NVS handle
  free(config_data);
  nvs_close(nvs_handle);

  // Log the loaded configuration
  ESP_LOGI(TAG, "Loaded %d Wi-Fi configurations from NVS", (int)config->wifi_configs_count);
  ESP_LOGI(TAG, "Loaded OTA URL: %s", config->ota_url);
  // Print each configuration
  for (size_t i = 0; i < config->wifi_configs_count; i++) {
    ESP_LOGD(TAG, "Config %d: SSID=%s, Password=%s", (int)i, config->wifi_settings[i].ssid,
             config->wifi_settings[i].password);
  }

  shared_data_release();

  return ESP_OK;
}

static void initialise_nvs_flash() {
  const esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
  {
    ESP_LOGW(TAG, "NVS partition was truncated, erasing...");
    ESP_ERROR_CHECK(nvs_flash_erase());
    ESP_ERROR_CHECK(nvs_flash_init());
  }

  ESP_ERROR_CHECK(ret);
}

void init_nvs_manager() {
  initialise_nvs_flash();
  // Store default configuration if not already in NVS
  if (!is_config_stored_in_nvs()) {
    store_default_config_to_nvs();
  }

  ESP_LOGI(TAG, "NVS manager initialized");

  while (1) {
    const EventBits_t bits = xEventGroupWaitBits(system_event_group, NVS_CONFIG_WRITE_REQUEST | NVS_CONFIG_READ_REQUEST | GO_INTO_AP_MODE,
                                           pdFALSE, pdFALSE, portMAX_DELAY);

    // Check if the bit was set
    if (bits & NVS_CONFIG_WRITE_REQUEST) {
      xEventGroupClearBits(system_event_group, NVS_CONFIG_WRITE_REQUEST);
      ESP_LOGI(TAG, "NVS update request received. Writing new NVS");

      // Attempt to update NVS
      if (update_nvs_from_config() == ESP_OK) {
        ESP_LOGI(TAG, "Successfully updated NVS");
      } else {
        ESP_LOGE(TAG, "Critical - Updating NVS from config failed. Going into AP mode.");
        xEventGroupSetBits(system_event_group, GO_INTO_AP_MODE);
      }
    }

    if (bits & NVS_CONFIG_READ_REQUEST) {
      xEventGroupClearBits(system_event_group, NVS_CONFIG_READ_REQUEST);
      ESP_LOGI(TAG, "NVS read request received. Writing nvs into global config");

      // Attempt to global config
      if (read_nvs_into_config() == ESP_OK) {
        xEventGroupSetBits(system_event_group, NVS_CONFIG_READ_SUCCESSFULLY);
      } else {
        ESP_LOGE(TAG, "Critical - Reading NVS into config failed. Going into AP mode.");
        xEventGroupSetBits(system_event_group, GO_INTO_AP_MODE);
      }
    }

    if (bits & GO_INTO_AP_MODE) {
      break;
    }
  }

  vTaskDelete(NULL);
}
