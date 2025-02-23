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

#ifndef WEB_PAGE_MANAGER_H
#define WEB_PAGE_MANAGER_H

#include <esp_err.h>
#include <esp_bit_defs.h>
#include <portmacro.h>

typedef enum
{
  WEB_PAGE_STATE_NONE = BIT0,
  WEB_PAGE_STATE_SERVING = BIT1
} web_page_manager_state_t;

typedef enum
{
  WEB_PAGE_STATE_NONE_REQUEST = BIT0,
  WEB_PAGE_STATE_SERVING_REQUEST = BIT1
} web_page_manager_state_request_t;

typedef struct web_page_manager web_page_manager_t;

web_page_manager_t* web_page_manager_create(UBaseType_t priority);
void web_page_manager_destroy(web_page_manager_t *manager);
esp_err_t web_page_manager_request_state(web_page_manager_t* manager, web_page_manager_state_request_t new_state);
void web_page_manager_wait_until_state(web_page_manager_t const * manager, web_page_manager_state_t wait_state);

/**
 * If critical* failures occur, the ESP will Access Point mode (see wifi_connection)
 * The Wi-Fi SSID and password is set with AP_SSID & AP_PASSWORD
 *
 * From here, a Web-Page will be hosted where the user can define valid Wi-Fi connection as well as
 * URL's for version checking and a URL for OTA firmware update.
 *
 * The config will be stored in NVM and the device rebooted
 */

#endif // WEB_PAGE_MANAGER_H
