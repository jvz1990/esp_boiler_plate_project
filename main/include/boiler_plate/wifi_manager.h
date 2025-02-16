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

#include <esp_bit_defs.h>
#include <freertos/FreeRTOS.h>

typedef enum
{
  WIFI_MANAGER_STATE_NONE,
  WIFI_MANAGER_STATE_STA,
  WIFI_MANAGER_STATE_AP,
  WIFI_MANAGER_STATE_AP_STA
} wifi_manager_state_t;

typedef struct wifi_manager wifi_manager_t;

void wifi_manager_destroy(wifi_manager_t* manager);
wifi_manager_t* wifi_manager_create(UBaseType_t priority);
esp_err_t wifi_manager_request_state(wifi_manager_t* manager, wifi_manager_state_t new_state);
void wifi_manager_wait_until_state(wifi_manager_t const * manager, EventBits_t wifi_state);

#define WIFI_MANAGER_REQUEST_NONE_BIT   BIT0
#define WIFI_MANAGER_REQUEST_STA_BIT    BIT1
#define WIFI_MANAGER_REQUEST_AP_BIT     BIT2
#define WIFI_MANAGER_REQUEST_AP_STA_BIT BIT3

#define WIFI_MANAGER_STATE_NONE_BIT            BIT0
#define WIFI_MANAGER_STATE_STA_BIT             BIT1
#define WIFI_MANAGER_STATE_STA_IP_RECEIVED_BIT BIT2
#define WIFI_MANAGER_STATE_AP_BIT              BIT3
#define WIFI_MANAGER_STATE_AP_STA_BIT          BIT4

#endif //WIFI_MANAGER_H
