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

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <freertos/FreeRTOS.h>
#include <esp_bit_defs.h>
#include <portmacro.h>

typedef enum
{
  WIFI_MANAGER_STATE_NONE = BIT0,
  WIFI_MANAGER_STATE_STA = BIT1,
  WIFI_MANAGER_STATE_STA_IP_RECEIVED = BIT2,
  WIFI_MANAGER_STATE_AP = BIT3,
} wifi_manager_state_t;

typedef enum
{
  WIFI_MANAGER_STATE_NONE_REQUEST = BIT0,
  WIFI_MANAGER_STATE_STA_REQUEST = BIT1,
  WIFI_MANAGER_STATE_AP_REQUEST = BIT2,
} wifi_manager_state_request_t;

typedef struct wifi_manager wifi_manager_t;

wifi_manager_t* wifi_manager_create(UBaseType_t priority);
void wifi_manager_destroy(wifi_manager_t* manager);
esp_err_t wifi_manager_request_state(wifi_manager_t* manager, wifi_manager_state_request_t new_state);
void wifi_manager_wait_until_state(wifi_manager_t const * manager, wifi_manager_state_t wifi_state);
wifi_manager_state_t wifi_manager_get_state(wifi_manager_t const* manager);

#endif //WIFI_MANAGER_H
