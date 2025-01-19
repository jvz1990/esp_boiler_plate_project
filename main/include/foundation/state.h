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

// Event bits for shared state
#define NVS_CONFIG_WRITE_REQUEST BIT0 // NVS
#define NVS_CONFIG_READ_REQUEST BIT1
#define NVS_CONFIG_READ_SUCCESSFULLY BIT2
#define GO_INTO_AP_MODE BIT3 // Fail mode
#define START_WEB_AP_WEBPAGE BIT4
#define CONNECT_TO_WIFI_AP_REQUEST BIT5 // Wifi
#define RECEIVED_IP_ADDRESS BIT6
#define CHECK_HTTPS_FIRMWARE_VERSION BIT7 // Firmware versioning
#define FIRMWARE_VERSION_UP_TO_DATE BIT8
#define NEW_FIRMWARE_AVAILABLE BIT9

typedef struct
{
  unit_configuration_t unit_config;
  bool wifi_connected;
} shared_data_t;

// Initialize the singleton (called once)
void shared_data_init();

// Acquire the singleton (blocks if another task is using it)
shared_data_t* shared_data_acquire();

// Release the singleton (must be called after acquire)
void shared_data_release();

// Clean up the singleton (optional)
void shared_data_cleanup();

// Is WiFi connected?
bool is_wifi_connected();

#endif // STATE_H
