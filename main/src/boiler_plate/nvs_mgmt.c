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

static const char* TAG = "NVS MGMT";

#define CONFIG_NAMESPACE "config_storage"
#define UNIT_CONFIG_KEY "unit_config"

static esp_err_t update_nvs_from_config();
static esp_err_t read_nvs_into_config();

static bool is_config_stored_in_nvs(const char* key);
static void store_unit_default_config_to_nvs();

// Function not error handled. If error occurs here, something is seriously wrong, and you need to manually re-flash
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

// Function not error handled. If error occurs here, something is seriously wrong, and you need to manually re-flash
static void store_unit_default_config_to_nvs() {
  nvs_handle_t nvs_handle;
  ESP_ERROR_CHECK(nvs_open(CONFIG_NAMESPACE, NVS_READWRITE, &nvs_handle));

  // Load initial config from menuconfig
  unit_configuration_t* unit_cfg = unit_config_acquire();
  connectivity_configuration_t* con_cfg = &(unit_cfg->con_config);
  system_settings_configuration_t* sys_cfg = &(unit_cfg->sys_config);
  user_configuration_t* usr_cfg = &(unit_cfg->user_config);

  // Allocate and initialize wifi_settings
  con_cfg->wifi_configs_count = 1;
  check_and_free(con_cfg->wifi_settings);
  ESP_ERROR_CHECK(
    allocate_and_clear_buffer((void**)&con_cfg->wifi_settings, con_cfg->wifi_configs_count * sizeof(wifi_settings_t),
      TAG, ESP_ERR_NO_MEM));

  strlcpy(con_cfg->wifi_settings[0].ssid, CONFIG_SSID, sizeof(con_cfg->wifi_settings[0].ssid));
  strlcpy(con_cfg->wifi_settings[0].password, CONFIG_PASSWORD, sizeof(con_cfg->wifi_settings[0].password));
  strlcpy(con_cfg->ota_url, CONFIG_OTA_URL, sizeof(con_cfg->ota_url));
  strlcpy(con_cfg->version_url, CONFIG_FIRMWARE_VERSION_ENDPOINT, sizeof(con_cfg->version_url));

  unit_cfg->wifi_connected = false;
  sys_cfg->log_level = CONFIG_LOG_DEFAULT_LEVEL;

  // Allocate and copy unit_name
  check_and_free(usr_cfg->unit_name);
  usr_cfg->unit_name_len = strlen(CONFIG_ESP_NAME) + 1;
  ESP_ERROR_CHECK(allocate_and_clear_buffer((void**)&usr_cfg->unit_name, usr_cfg->unit_name_len, TAG, ESP_ERR_NO_MEM));
  memcpy(usr_cfg->unit_name, CONFIG_ESP_NAME, usr_cfg->unit_name_len);

  // Compute serialization size
  size_t wifi_settings_size = con_cfg->wifi_configs_count * sizeof(wifi_settings_t);
  size_t unit_name_size = usr_cfg->unit_name_len;
  size_t blob_size = sizeof(connectivity_configuration_t) + sizeof(system_settings_configuration_t) + sizeof(bool) +
    sizeof(uint8_t) + unit_name_size + wifi_settings_size;

  ESP_LOGI(TAG, "Storing default blob of size: %d", blob_size);

  uint8_t* serialized_blob = NULL;
  ESP_ERROR_CHECK(allocate_and_clear_buffer((void**)&serialized_blob, blob_size, TAG, ESP_ERR_NO_MEM));

  // Serialize data
  uint8_t* ptr = serialized_blob;
  memcpy(ptr, con_cfg, sizeof(connectivity_configuration_t));
  ptr += sizeof(connectivity_configuration_t);
  memcpy(ptr, con_cfg->wifi_settings, wifi_settings_size);
  ptr += wifi_settings_size;
  memcpy(ptr, sys_cfg, sizeof(system_settings_configuration_t));
  ptr += sizeof(system_settings_configuration_t);
  memcpy(ptr, &(unit_cfg->wifi_connected), sizeof(bool));
  ptr += sizeof(bool);
  memcpy(ptr, &(usr_cfg->unit_name_len), sizeof(uint8_t));
  ptr += sizeof(uint8_t);
  memcpy(ptr, usr_cfg->unit_name, unit_name_size);

  unit_config_release();

  // Store in NVS
  ESP_ERROR_CHECK(nvs_set_blob(nvs_handle, UNIT_CONFIG_KEY, serialized_blob, blob_size));
  ESP_ERROR_CHECK(nvs_commit(nvs_handle));

  ESP_LOGI(TAG, "Default unit configuration stored in NVS.");

  // Cleanup
  check_and_free(serialized_blob);
  nvs_close(nvs_handle);
}

// Update NVS from config
static esp_err_t update_nvs_from_config() {
  nvs_handle_t nvs_handle;
  ESP_ERROR_CHECK(nvs_open(CONFIG_NAMESPACE, NVS_READWRITE, &nvs_handle));

  const unit_configuration_t* unit_cfg = unit_config_acquire();
  const connectivity_configuration_t* con_cfg = &(unit_cfg->con_config);
  const system_settings_configuration_t* sys_cfg = &(unit_cfg->sys_config);
  const user_configuration_t* usr_cfg = &(unit_cfg->user_config);

  // Calculate serialization size (same as original)
  size_t wifi_settings_size = con_cfg->wifi_configs_count * sizeof(wifi_settings_t);
  size_t unit_name_size = usr_cfg->unit_name_len;
  size_t blob_size = sizeof(connectivity_configuration_t) +
    wifi_settings_size +
    sizeof(system_settings_configuration_t) +
    sizeof(bool) +
    sizeof(uint8_t) +
    unit_name_size;

  ESP_LOGI(TAG, "Storing current configuration blob of size: %d", blob_size);

  uint8_t* serialized_blob = NULL;
  ESP_ERROR_CHECK(allocate_and_clear_buffer((void**)&serialized_blob, blob_size, TAG, ESP_ERR_NO_MEM));

  // Serialization (matches original format exactly)
  uint8_t* ptr = serialized_blob;

  // 1. Base connectivity config
  memcpy(ptr, con_cfg, sizeof(connectivity_configuration_t));
  ptr += sizeof(connectivity_configuration_t);

  // 2. WiFi settings array
  memcpy(ptr, con_cfg->wifi_settings, wifi_settings_size);
  ptr += wifi_settings_size;

  // 3. System settings
  memcpy(ptr, sys_cfg, sizeof(system_settings_configuration_t));
  ptr += sizeof(system_settings_configuration_t);

  // 4. Wi-Fi connected status
  memcpy(ptr, &(unit_cfg->wifi_connected), sizeof(bool));
  ptr += sizeof(bool);

  // 5. Unit name length
  memcpy(ptr, &(usr_cfg->unit_name_len), sizeof(uint8_t));
  ptr += sizeof(uint8_t);

  // 6. Unit name string
  memcpy(ptr, usr_cfg->unit_name, unit_name_size);

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
  connectivity_configuration_t* con_cfg = &(unit_cfg->con_config);
  system_settings_configuration_t* sys_cfg = &(unit_cfg->sys_config);
  user_configuration_t* usr_cfg = &(unit_cfg->user_config);

  uint8_t* ptr = serialized_blob;

  // 1. Deserialize connectivity config (except pointer)
  memcpy(con_cfg, ptr, sizeof(connectivity_configuration_t));
  ptr += sizeof(connectivity_configuration_t);

  // Reset pointer before allocation
  con_cfg->wifi_settings = NULL;

  // 2. Deserialize wifi settings array
  size_t wifi_settings_size = con_cfg->wifi_configs_count * sizeof(wifi_settings_t);
  if (wifi_settings_size > (blob_size - (ptr - serialized_blob))) {
    ESP_LOGE(TAG, "Invalid wifi config size");
    return ESP_ERR_INVALID_SIZE;
  }

  check_and_free(con_cfg->wifi_settings);
  ESP_ERROR_CHECK(allocate_and_clear_buffer((void**)&con_cfg->wifi_settings, wifi_settings_size, TAG, ESP_ERR_NO_MEM));
  memcpy(con_cfg->wifi_settings, ptr, wifi_settings_size);
  ptr += wifi_settings_size;

  // 3. Deserialize system settings
  memcpy(sys_cfg, ptr, sizeof(system_settings_configuration_t));
  ptr += sizeof(system_settings_configuration_t);

  // 4. Deserialize wifi_connected
  memcpy(&(unit_cfg->wifi_connected), ptr, sizeof(bool));
  ptr += sizeof(bool);

  // 5. Deserialize unit name length
  uint8_t unit_name_len;
  memcpy(&unit_name_len, ptr, sizeof(uint8_t));
  ptr += sizeof(uint8_t);

  // 6. Deserialize unit name
  if (unit_name_len > (blob_size - (ptr - serialized_blob))) {
    ESP_LOGE(TAG, "Invalid unit name length");
    return ESP_ERR_INVALID_SIZE;
  }

  check_and_free(usr_cfg->unit_name);
  ESP_ERROR_CHECK(allocate_and_clear_buffer((void**)&usr_cfg->unit_name, unit_name_len, TAG, ESP_ERR_NO_MEM));
  memcpy(usr_cfg->unit_name, ptr, unit_name_len);
  usr_cfg->unit_name_len = unit_name_len;

  esp_log_level_set("*", sys_cfg->log_level);

  unit_config_release();
  check_and_free(serialized_blob);
  nvs_close(nvs_handle);
  ESP_LOGI(TAG, "Unit configuration loaded from NVS");

  return ESP_OK;
}

static void initialise_nvs_flash() {
  const esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_LOGW(TAG, "NVS partition was truncated, erasing...");
    ESP_ERROR_CHECK(nvs_flash_erase());
    ESP_ERROR_CHECK(nvs_flash_init());
  }

  ESP_ERROR_CHECK(ret);
}

void init_nvs_manager() {
  initialise_nvs_flash();
  // Store default configuration if not already in NVS
  if (!is_config_stored_in_nvs(UNIT_CONFIG_KEY)) {
    store_unit_default_config_to_nvs();
  }

  ESP_LOGI(TAG, "NVS manager initialized");

  while (1) {
    const EventBits_t bits =
      xEventGroupWaitBits(system_event_group, NVS_REQUEST_WRITE_BIT | NVS_REQUEST_READ_BIT | REBOOT_BIT,
                          pdFALSE, pdFALSE, portMAX_DELAY);

    // Check if the bit was set
    if (bits & NVS_REQUEST_WRITE_BIT) {
      xEventGroupClearBits(system_event_group, NVS_REQUEST_WRITE_BIT);
      ESP_LOGI(TAG, "NVS update request received. Writing new NVS");

      // Attempt to update NVS
      if (update_nvs_from_config() == ESP_OK) {
        ESP_LOGI(TAG, "Successfully updated NVS");
      } else {
        ESP_LOGE(TAG, "Critical - Updating NVS from config failed. Going into AP mode.");
        xEventGroupSetBits(system_event_group, WIFI_REQUEST_AP_MODE_BIT);
      }
    }

    if (bits & NVS_REQUEST_READ_BIT) {
      xEventGroupClearBits(system_event_group, NVS_REQUEST_READ_BIT);
      ESP_LOGI(TAG, "NVS read request received. Copying NVS into unit config");

      // Attempt to update global config
      if (read_nvs_into_config() == ESP_OK) {
        xEventGroupSetBits(system_event_group, NVS_READ_SUCCESSFULLY_READ_BIT);
      } else {
        ESP_LOGE(TAG, "Critical - Reading NVS into config failed. Going into AP mode.");
        xEventGroupSetBits(system_event_group, WIFI_REQUEST_AP_MODE_BIT);
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
