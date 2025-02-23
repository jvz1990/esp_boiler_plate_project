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

#ifndef STATE_H
#define STATE_H

#include "freertos/FreeRTOS.h"

#include "configuration.h"

// Shared group event handler
extern EventGroupHandle_t system_event_group;

typedef enum
{
    NVS_REQUEST_WRITE_BIT = (1 << 0),
    NVS_REQUEST_READ_BIT = (1 << 1),
    NVS_READ_SUCCESSFULLY_READ_BIT = (1 << 2),
} nvm_event_bits_t;

typedef enum
{
    WIFI_CONNECTED_BIT = (1 << 3),
    WIFI_DISCONNECTED_BIT = (1 << 4),
    WIFI_REQUEST_STA_MODE_BIT = (1 << 5),
    WIFI_REQUEST_AP_MODE_BIT = (1 << 6),
    WIFI_AP_MODE_BIT = (1 << 7),
    WIFI_SCAN_COMPLETE_BIT = (1 << 8),
    WIFI_RETRYING_BIT = (1 << 9),
    WIFI_INITIALIZED_BIT = (1 << 10),
} wifi_event_bits_t;

typedef enum
{
    FIRMWARE_REQUEST_VERSION_CHECK_BIT = (1 << 11),
    FIRMWARE_VERSION_UP_TO_DATE_BIT = (1 << 12),
    NEW_FIRMWARE_AVAILABLE_BIT = (1 << 13),
} versioning_event_bits_t;

typedef enum
{
    AP_WEB_PAGES_REQUEST_BIT = (1 << 14),
    REBOOT_BIT = (1 << 15),
} misc_event_bits_t;

typedef struct nvs_manager nvs_manager_t;
typedef struct wifi_manager wifi_manager_t;
typedef struct web_page_manager web_page_manager_t;

typedef struct {
    nvs_manager_t* nvs_manager;
    wifi_manager_t* wifi_manager;
    web_page_manager_t* web_page_manager;
} managers_t;

// Initialize the singleton (called once)
void unit_config_init();

// Acquire the singleton (blocks if another task is using it)
unit_configuration_t* unit_config_acquire();

managers_t* managers_acquire();

void set_nvs_manager(nvs_manager_t* nvs_manager);

void set_wifi_manager(wifi_manager_t* wifi_manager);

void set_web_page_manager(web_page_manager_t* web_page_manager);

// Release the singleton (must be called after acquire)
void unit_config_release();

void managers_release();

// Clean up the singleton (optional)
void unit_config_cleanup();

// Is Wi-Fi connected?
bool is_wifi_connected(); // #TODO refactor remove

#endif // STATE_H
