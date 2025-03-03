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

#ifndef CONFIGURATION_H
#define CONFIGURATION_H

#define MAX_URL_LENGTH 256

#include <esp_log_level.h>
#include <stddef.h>
#include <stdint.h>

#define CONFIGURATION_VERSION 0

#pragma pack(push, 1)

typedef struct
{
  uint8_t ssid_len;
  uint8_t password_len;
  char* ssid;
  char* password;
} wifi_settings_t;

typedef struct
{
  uint8_t wifi_settings_count;
  uint8_t ota_url_len;
  uint8_t version_url_len;
  char* ota_url;
  char* version_url;
  wifi_settings_t* wifi_settings;
} connectivity_configuration_t;

typedef struct
{
  esp_log_level_t log_level;
} system_settings_configuration_t;

typedef struct
{
  char* unit_name;
  uint8_t unit_name_len;
} user_configuration_t;

typedef struct
{
  uint8_t configuration_version;
  connectivity_configuration_t con_config;
  system_settings_configuration_t sys_config;
  user_configuration_t user_config;
} unit_configuration_t;

#pragma pack(pop)

typedef enum
{
  WIFI_P = 1,
  AP_WEB_PAGES_P,
  OTA_UPDATE_P,
  NVS_MGMT_P
} TaskPriorities;

#endif // CONFIGURATION_H
