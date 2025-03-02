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

#include "serialisation.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static uint8_t* serialize_block(uint8_t* buffer, const void* data, size_t size);

size_t serialize_wifi_settings(const wifi_settings_t* settings, uint8_t* buffer);
size_t serialize_connectivity_configuration(const connectivity_configuration_t* config, uint8_t* buffer);
size_t serialize_system_settings_configuration(const system_settings_configuration_t* config, uint8_t* buffer);
size_t serialize_user_configuration(const user_configuration_t* config, uint8_t* buffer);

static uint8_t* serialize_block(uint8_t* buffer, const void* data, size_t size) {
  memcpy(buffer, data, size);
  return buffer + size;
}

size_t serialize_wifi_settings(const wifi_settings_t* settings, uint8_t* buffer) {
  uint8_t* ptr = buffer;

  ptr = serialize_block(ptr, &settings->ssid_len, sizeof(settings->ssid_len));
  ptr = serialize_block(ptr, &settings->password_len, sizeof(settings->password_len));

  if (settings->ssid_len > 0 && settings->ssid != NULL) {
    ptr = serialize_block(ptr, settings->ssid, settings->ssid_len);
  }
  if (settings->password_len > 0 && settings->password != NULL) {
    ptr = serialize_block(ptr, settings->password, settings->password_len);
  }

  return ptr - buffer;
}

size_t serialize_connectivity_configuration(const connectivity_configuration_t* config, uint8_t* buffer) {
  uint8_t* ptr = buffer;

  ptr = serialize_block(ptr, &config->wifi_settings_count, sizeof(config->wifi_settings_count));
  ptr = serialize_block(ptr, &config->ota_url_len, sizeof(config->ota_url_len));
  ptr = serialize_block(ptr, &config->version_url_len, sizeof(config->version_url_len));

  if (config->ota_url_len > 0 && config->ota_url != NULL) {
    ptr = serialize_block(ptr, config->ota_url, config->ota_url_len);
  }
  if (config->version_url_len > 0 && config->version_url != NULL) {
    ptr = serialize_block(ptr, config->version_url, config->version_url_len);
  }

  for (uint8_t i = 0; i < config->wifi_settings_count; i++) {
    ptr += serialize_wifi_settings(&config->wifi_settings[i], ptr);
  }

  return ptr - buffer;
}

size_t serialize_system_settings_configuration(const system_settings_configuration_t* config, uint8_t* buffer) {
  uint8_t* ptr = buffer;

  ptr = serialize_block(ptr, &config->log_level, sizeof(config->log_level));

  return ptr - buffer;
}

size_t serialize_user_configuration(const user_configuration_t* config, uint8_t* buffer) {
  uint8_t* ptr = buffer;

  ptr = serialize_block(ptr, &config->unit_name_len, sizeof(config->unit_name_len));

  if (config->unit_name_len > 0 && config->unit_name != NULL) {
    ptr = serialize_block(ptr, config->unit_name, config->unit_name_len);
  }

  return ptr - buffer;
}

size_t serialize_unit_configuration(const unit_configuration_t* config, uint8_t* buffer) {
  uint8_t* ptr = buffer;

  ptr = serialize_block(ptr, &config->configuration_version, sizeof(config->configuration_version));

  ptr += serialize_connectivity_configuration(&config->con_config, ptr);
  ptr += serialize_system_settings_configuration(&config->sys_config, ptr);
  ptr += serialize_user_configuration(&config->user_config, ptr);

  return ptr - buffer;
}

size_t calculate_unit_configuration_size(const unit_configuration_t* config) {
  size_t size = 0;

  size += sizeof(config->configuration_version);

  const connectivity_configuration_t* con_cfg = &config->con_config;

  size += sizeof(con_cfg->wifi_settings_count);

  size += sizeof(con_cfg->ota_url_len);
  size += con_cfg->ota_url_len;

  size += sizeof(con_cfg->version_url_len);
  size += con_cfg->version_url_len;

  for (uint32_t i = 0; i < con_cfg->wifi_settings_count; i++) {
    const wifi_settings_t* wifi_setting = &con_cfg->wifi_settings[i];

    size += sizeof(wifi_setting->ssid_len);
    size += wifi_setting->ssid_len;

    size += sizeof(wifi_setting->password_len);
    size += wifi_setting->password_len;
  }

  const system_settings_configuration_t* sys_cfg = &config->sys_config;
  size += sizeof(system_settings_configuration_t);

  const user_configuration_t* usr_cfg = &config->user_config;

  size += sizeof(usr_cfg->unit_name_len);
  size += usr_cfg->unit_name_len;

  return size;
}
