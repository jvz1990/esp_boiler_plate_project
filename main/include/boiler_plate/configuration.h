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

#define MAX_SSID_LENGTH MAX_SSID_LEN
#define MAX_PASSWORD_LENGTH MAX_PASSPHRASE_LEN
#define MAX_URL_LENGTH 256

#include <esp_wifi_types_generic.h>

typedef struct {
  char ssid[MAX_SSID_LENGTH];
  char password[MAX_PASSWORD_LENGTH];
} wifi_settings_t;

typedef struct {
  uint32_t wifi_configs_count;
  wifi_settings_t* wifi_settings;
  char ota_url[MAX_URL_LENGTH];
  char version_url[MAX_URL_LENGTH];
} connectivity_configuration_t;

typedef struct {
  // Sample user config
  char* unit_name;
  uint8_t unit_name_len;
} user_configuration_t;

typedef struct {
  connectivity_configuration_t con_config;
  bool wifi_connected;
  user_configuration_t user_config;
} unit_configuration_t;

typedef enum
{
  WIFI_P = 1,
  DNS_P,
  AP_WEB_PAGES_P,
  VERSION_CHECKING_P,
  OTA_UPDATE_P,
  NVS_MGMT_P
} TaskPriorities;

#endif // CONFIGURATION_H
