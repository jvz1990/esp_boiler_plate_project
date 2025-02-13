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

#include "deserialisation.h"

#include <esp_log.h>
#include <string.h>

static const char* TAG = "deserialisation";

static const uint8_t* deserialize_block(const uint8_t* buffer, void* data, size_t size);

const uint8_t* deserialize_wifi_settings(wifi_settings_t* settings, const uint8_t* buffer);
const uint8_t* deserialize_connectivity_configuration(connectivity_configuration_t* config, const uint8_t* buffer);
const uint8_t*
deserialize_system_settings_configuration(system_settings_configuration_t* config, const uint8_t* buffer);
const uint8_t* deserialize_user_configuration(user_configuration_t* config, const uint8_t* buffer);

static const uint8_t* deserialize_block(const uint8_t* buffer, void* data, size_t size) {
  memcpy(data, buffer, size);
  return buffer + size;
}

const uint8_t* deserialize_wifi_settings(wifi_settings_t* settings, const uint8_t* buffer) {
  const uint8_t* ptr = buffer;

  ptr = deserialize_block(ptr, &settings->ssid_len, sizeof(settings->ssid_len));
  ptr = deserialize_block(ptr, &settings->password_len, sizeof(settings->password_len));

  if (settings->ssid_len > 0) {
    settings->ssid = calloc(settings->ssid_len + 1, 1);
    ptr = deserialize_block(ptr, settings->ssid, settings->ssid_len);
  } else {
    settings->ssid = NULL;
  }

  if (settings->password_len > 0) {
    settings->password = calloc(settings->password_len + 1, 1);
    ptr = deserialize_block(ptr, settings->password, settings->password_len);
  } else {
    settings->password = NULL;
  }

  return ptr;
}

const uint8_t* deserialize_connectivity_configuration(connectivity_configuration_t* config, const uint8_t* buffer) {
  const uint8_t* ptr = buffer;

  ptr = deserialize_block(ptr, &config->wifi_settings_count, sizeof(config->wifi_settings_count));
  ptr = deserialize_block(ptr, &config->ota_url_len, sizeof(config->ota_url_len));
  ptr = deserialize_block(ptr, &config->version_url_len, sizeof(config->version_url_len));

  if (config->ota_url_len > 0) {
    config->ota_url = malloc(config->ota_url_len);
    ptr = deserialize_block(ptr, config->ota_url, config->ota_url_len);
  } else {
    config->ota_url = NULL;
  }

  if (config->version_url_len > 0) {
    config->version_url = malloc(config->version_url_len);
    ptr = deserialize_block(ptr, config->version_url, config->version_url_len);
  } else {
    config->version_url = NULL;
  }

  if (config->wifi_settings_count > 0) {
    config->wifi_settings = malloc(config->wifi_settings_count * sizeof(wifi_settings_t));
    for (uint8_t i = 0; i < config->wifi_settings_count; i++) {
      ptr = deserialize_wifi_settings(&config->wifi_settings[i], ptr);
    }
  } else {
    config->wifi_settings = NULL;
  }

  return ptr;
}

const uint8_t*
deserialize_system_settings_configuration(system_settings_configuration_t* config, const uint8_t* buffer) {
  const uint8_t* ptr = buffer;

  ptr = deserialize_block(ptr, &config->log_level, sizeof(config->log_level));

  return ptr;
}

const uint8_t* deserialize_user_configuration(user_configuration_t* config, const uint8_t* buffer) {
  const uint8_t* ptr = buffer;

  ptr = deserialize_block(ptr, &config->unit_name_len, sizeof(config->unit_name_len));

  if (config->unit_name_len > 0) {
    config->unit_name = calloc(config->unit_name_len, 1);
    ptr = deserialize_block(ptr, config->unit_name, config->unit_name_len);
  } else {
    config->unit_name = NULL;
  }

  return ptr;
}

const uint8_t* deserialize_unit_configuration(unit_configuration_t* config, const uint8_t* buffer) {
  const uint8_t* ptr = buffer;

  ptr = deserialize_block(ptr, &config->configuration_version, sizeof(config->configuration_version));

  if (config->configuration_version != CONFIGURATION_VERSION) {
    ESP_LOGE(TAG, "Configuration version mismatch, NVS [%d] vs Firmware [%d]", config->configuration_version, CONFIGURATION_VERSION); // TODO versioning
    return NULL;
  }

  ptr = deserialize_connectivity_configuration(&config->con_config, ptr);
  ptr = deserialize_system_settings_configuration(&config->sys_config, ptr);
  ptr = deserialize_user_configuration(&config->user_config, ptr);

  return ptr;
}
