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

// 2 keys in the same namespace. No strong justification to use multiple namespaces
#define CONFIG_NAMESPACE "config_storage"
#define UNIT_CONFIG_KEY "unit_config"
#define USER_CONFIG_KEY "user_config"

static esp_err_t update_nvs_from_config();
static esp_err_t read_nvs_into_config();

static bool is_config_stored_in_nvs(const char* key);
static void store_unit_default_config_to_nvs();
static void store_user_default_config_to_nvs();

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
  unit_configuration_t* unit_configuration = unit_config_acquire();
  connectivity_configuration_t* con_cfg = &(unit_configuration->con_config);
  check_and_free(con_cfg->wifi_settings);
  ESP_ERROR_CHECK(
    allocate_and_clear_buffer((void**)&con_cfg->wifi_settings, sizeof(wifi_settings_t), TAG, ESP_ERR_NO_MEM));

  // Copy menu defined vars into conf
  con_cfg->wifi_configs_count = 1;
  strlcpy(con_cfg->wifi_settings[0].ssid, CONFIG_SSID, sizeof(con_cfg->wifi_settings[0].ssid));
  strlcpy(con_cfg->wifi_settings[0].password, CONFIG_PASSWORD, sizeof(con_cfg->wifi_settings[0].password));
  strlcpy(con_cfg->ota_url, CONFIG_OTA_URL, sizeof(con_cfg->ota_url));
  strlcpy(con_cfg->version_url, CONFIG_FIRMWARE_VERSION_ENDPOINT, sizeof(con_cfg->version_url));

  // Serialize the configuration
  const size_t blob_size = sizeof(connectivity_configuration_t) + sizeof(wifi_settings_t);
  uint8_t* serialized_blob = NULL;
  ESP_ERROR_CHECK(allocate_and_clear_buffer((void**)&serialized_blob, blob_size, TAG, ESP_ERR_NO_MEM));

  memcpy(serialized_blob, con_cfg, sizeof(connectivity_configuration_t));
  memcpy(serialized_blob + sizeof(connectivity_configuration_t), con_cfg->wifi_settings, sizeof(wifi_settings_t));

  unit_config_release();

  // Save the serialized blob to NVS
  ESP_ERROR_CHECK(nvs_set_blob(nvs_handle, UNIT_CONFIG_KEY, serialized_blob, blob_size));
  ESP_ERROR_CHECK(nvs_commit(nvs_handle));
  ESP_LOGI(TAG, "Default unit configuration stored in NVS.");

  // Clean up
  check_and_free(serialized_blob);
  nvs_close(nvs_handle);
}

// This can be removed as it used for demonstrational purposes only
static void store_user_default_config_to_nvs() {
  nvs_handle_t nvs_handle;
  ESP_ERROR_CHECK(nvs_open(CONFIG_NAMESPACE, NVS_READWRITE, &nvs_handle));

  unit_configuration_t* unit_configuration = unit_config_acquire();
  user_configuration_t* user_conf = &unit_configuration->user_config;

  // Update the user configuration
  user_conf->unit_name_len = strlen(CONFIG_ESP_NAME) + 1;

  check_and_free(user_conf->unit_name);
  ESP_ERROR_CHECK(allocate_and_clear_buffer((void**)&user_conf->unit_name, user_conf->unit_name_len, TAG, ESP_ERR_NO_MEM));
  strlcpy(user_conf->unit_name, CONFIG_ESP_NAME, user_conf->unit_name_len);

  // Calculate blob size (struct + string data)
  const size_t blob_size = sizeof(user_configuration_t) + user_conf->unit_name_len;
  uint8_t* serialized_blob = NULL;
  ESP_ERROR_CHECK(allocate_and_clear_buffer((void**)&serialized_blob, blob_size, TAG, ESP_ERR_NO_MEM));

  // Serialize struct
  memcpy(serialized_blob, user_conf, sizeof(user_configuration_t));

  // Serialize string data
  memcpy(serialized_blob + sizeof(user_configuration_t), user_conf->unit_name, user_conf->unit_name_len);

  unit_config_release();

  // Save the serialized blob to NVS
  ESP_ERROR_CHECK(nvs_set_blob(nvs_handle, USER_CONFIG_KEY, serialized_blob, blob_size));
  ESP_ERROR_CHECK(nvs_commit(nvs_handle));
  ESP_LOGI(TAG, "Default user configuration stored in NVS");

  // Clean up
  nvs_close(nvs_handle);
  check_and_free(serialized_blob);
}

// Update NVS from config
static esp_err_t update_nvs_from_config() {
  nvs_handle_t nvs_handle;
  esp_err_t err = ESP_OK;
  ESP_ERROR_CHECK(nvs_open(CONFIG_NAMESPACE, NVS_READWRITE, &nvs_handle));

  const unit_configuration_t* unit_cfg = unit_config_acquire();
  const connectivity_configuration_t* con_cfg = &(unit_cfg->con_config);
  const user_configuration_t* user_conf = &(unit_cfg->user_config);

  // Calculate the size of the configuration blob
  const size_t config_data_size =
    sizeof(connectivity_configuration_t) + (con_cfg->wifi_configs_count * sizeof(wifi_settings_t));

  // Allocate memory for the configuration data (unit_configuration + wifi_settings)
  uint8_t* config_data = NULL;
  err = allocate_and_clear_buffer((void**)&config_data, config_data_size, TAG, ESP_ERR_NO_MEM);

  // Serialize the unit_configuration structure
  if (err == ESP_OK) {
    memcpy(config_data, con_cfg, sizeof(connectivity_configuration_t));

    // Serialize wifi_settings into the blob
    if (con_cfg->wifi_configs_count > 0 && con_cfg->wifi_settings != NULL) {
      memcpy(config_data + sizeof(connectivity_configuration_t), con_cfg->wifi_settings,
             con_cfg->wifi_configs_count * sizeof(wifi_settings_t));
    }

    // Store the serialized configuration in NVS
    nvs_set_blob(nvs_handle, UNIT_CONFIG_KEY, config_data, config_data_size);
  }
  check_and_free(config_data);

  uint8_t* user_config_data = NULL;
  const size_t user_data_size = sizeof(user_configuration_t) + (user_conf->unit_name_len + 1);
  err = allocate_and_clear_buffer((void**)&user_config_data, user_data_size, TAG, ESP_ERR_NO_MEM);
  // Serialize the user_configuration structure
  if (err == ESP_OK) {
    memcpy(user_config_data, user_conf, sizeof(user_configuration_t));

    // Serialize unit_name into the blob
    if (user_conf->unit_name != NULL && user_conf->unit_name_len > 0) {
      memcpy(user_config_data + sizeof(user_configuration_t), user_conf->unit_name, user_conf->unit_name_len);
      user_config_data[sizeof(user_configuration_t) + user_conf->unit_name_len] = '\0';
    } else if (user_conf->unit_name_len == 0) {
      user_config_data[sizeof(user_configuration_t)] = '\0';
    }

    // Store the serialized configuration in NVS
    err = nvs_set_blob(nvs_handle, USER_CONFIG_KEY, user_config_data, user_data_size);
  }
  check_and_free(user_config_data);

  // Commit changes and close NVS
  if (err == ESP_OK) {
    ESP_ERROR_CHECK(nvs_commit(nvs_handle));
  }

  nvs_close(nvs_handle);
  unit_config_release();

  if (err != ESP_OK)
    ESP_LOGE(TAG, "Error: %s", esp_err_to_name(err));

  return err;
}

// Deserialize NVS blobs into config
static esp_err_t read_nvs_into_config() {
  esp_err_t err = ESP_OK;
  nvs_handle_t nvs_handle = 0;
  uint8_t* config_data = NULL;

  // Open NVS namespace
  err = nvs_open(CONFIG_NAMESPACE, NVS_READONLY, &nvs_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open NVS namespace");
    goto cleanup;
  }

  unit_configuration_t* unit_cfg = unit_config_acquire();
  connectivity_configuration_t* con_cfg = &(unit_cfg->con_config);
  user_configuration_t* usr_cfg = &(unit_cfg->user_config);

  // Read connectivity configuration blob
  size_t required_size = 0;
  err = nvs_get_blob(nvs_handle, UNIT_CONFIG_KEY, NULL, &required_size);
  if (err != ESP_OK || required_size == 0) {
    ESP_LOGE(TAG, "Failed to get connectivity configuration from NVS or invalid size");
    goto cleanup;
  }

  err = allocate_and_clear_buffer((void**)&config_data, required_size, TAG, ESP_ERR_NO_MEM);
  if (err != ESP_OK) {
    goto cleanup;
  }

  err = nvs_get_blob(nvs_handle, UNIT_CONFIG_KEY, config_data, &required_size);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to read connectivity configuration from NVS");
    goto cleanup;
  }

  // Deserialize connectivity configuration
  check_and_free(con_cfg->wifi_settings);
  memcpy(con_cfg, config_data, sizeof(connectivity_configuration_t));

  if (con_cfg->wifi_configs_count > 0) {
    err = allocate_and_clear_buffer((void**)&con_cfg->wifi_settings,
                                    con_cfg->wifi_configs_count * sizeof(wifi_settings_t), TAG, ESP_ERR_NO_MEM);
    if (err != ESP_OK) {
      goto cleanup;
    }

    memcpy(con_cfg->wifi_settings, config_data + sizeof(connectivity_configuration_t),
           con_cfg->wifi_configs_count * sizeof(wifi_settings_t));
  }

  check_and_free(config_data);
  config_data = NULL;

  // Log connectivity configuration
  ESP_LOGI(TAG, "Loaded %d Wi-Fi configurations from NVS", (int)con_cfg->wifi_configs_count);
  ESP_LOGI(TAG, "Loaded OTA URL: %s", con_cfg->ota_url);
  for (size_t i = 0; i < con_cfg->wifi_configs_count; i++) {
    ESP_LOGD(TAG, "Config %d: SSID=%s, Password=%s", (int)i, con_cfg->wifi_settings[i].ssid,
             con_cfg->wifi_settings[i].password);
  }

  // Read user configuration blob
  required_size = 0;
  err = nvs_get_blob(nvs_handle, USER_CONFIG_KEY, NULL, &required_size);
  if (err != ESP_OK || required_size == 0) {
    ESP_LOGE(TAG, "Failed to get user configuration from NVS or invalid size");
    goto cleanup;
  }

  err = allocate_and_clear_buffer((void**)&config_data, required_size, TAG, ESP_ERR_NO_MEM);
  if (err != ESP_OK) {
    goto cleanup;
  }

  err = nvs_get_blob(nvs_handle, USER_CONFIG_KEY, config_data, &required_size);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to read user configuration from NVS");
    goto cleanup;
  }

  check_and_free(usr_cfg->unit_name);
  usr_cfg->unit_name_len = 0;
  memcpy(usr_cfg, config_data, sizeof(user_configuration_t));

  if (usr_cfg->unit_name_len > 0) {
    err = allocate_and_clear_buffer((void**)&usr_cfg->unit_name, usr_cfg->unit_name_len + 1, TAG, ESP_ERR_NO_MEM);
    if (err != ESP_OK) {
      goto cleanup;
    }

    memcpy(usr_cfg->unit_name, config_data + sizeof(user_configuration_t), usr_cfg->unit_name_len + 1);
    ESP_LOGI(TAG, "User specified unit name as %s", usr_cfg->unit_name);
  }

cleanup:
  check_and_free(config_data);
  if (nvs_handle != 0) {
    nvs_close(nvs_handle);
  }
  unit_config_release();
  return err;
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
  if (!is_config_stored_in_nvs(USER_CONFIG_KEY)) {
    store_user_default_config_to_nvs();
  }

  ESP_LOGI(TAG, "NVS manager initialized");

  while (1) {
    const EventBits_t bits =
      xEventGroupWaitBits(system_event_group, NVS_CONFIG_WRITE_REQUEST | NVS_CONFIG_READ_REQUEST | REBOOTING,
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
      ESP_LOGI(TAG, "NVS read request received. Copying NVS into unit config");

      // Attempt to global config
      if (read_nvs_into_config() == ESP_OK) {
        xEventGroupSetBits(system_event_group, NVS_CONFIG_READ_SUCCESSFULLY);
      } else {
        ESP_LOGE(TAG, "Critical - Reading NVS into config failed. Going into AP mode.");
        xEventGroupSetBits(system_event_group, GO_INTO_AP_MODE);
      }
    }

    if (bits & REBOOTING) {
      break;
    }
  }

  vTaskDelete(NULL);
}
