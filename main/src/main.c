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

#include "nvs_manager.h"
#include "state.h"
#include "web_page_manager.h"
#include "wifi_manager.h"

#include <esp_log.h>

static const char* TAG = "Main";

void app_main(void) {
  // Initialise shared data
  unit_config_init();
  // Create Tasks
  nvs_manager_t* nvs_manager = nvs_manager_create(NVS_MGMT_P);
  wifi_manager_t* wifi_manager = wifi_manager_create(WIFI_P);
  web_page_manager_t* web_page_manager = web_page_manager_create(AP_WEB_PAGES_P);

  set_nvs_manager(nvs_manager);
  set_wifi_manager(wifi_manager);
  set_web_page_manager(web_page_manager);

  ESP_LOGI(TAG, "NVS requesting state READY");
  nvs_manager_request_state(nvs_manager, NVS_STATE_READY_REQUEST);
  ESP_LOGI(TAG, "NVS waiting on state READY");
  nvs_manager_wait_until_state(nvs_manager, NVS_READY);
  ESP_LOGI(TAG, "Wi-FI requesting state AP");
  wifi_manager_request_state(wifi_manager, WIFI_MANAGER_STATE_AP_REQUEST);
  ESP_LOGI(TAG, "Wi-FI waiting on state AP");
  wifi_manager_wait_until_state(wifi_manager, WIFI_MANAGER_STATE_AP);
  ESP_LOGI(TAG, "Web-page requesting state serving | DNS");
  web_page_manager_request_state(web_page_manager, WEB_PAGE_STATE_SERVING_REQUEST | WEB_PAGE_STATE_DNS_SERVER_REQUEST);
  ESP_LOGI(TAG, "Web-page requesting waiting | DNS");
  web_page_manager_wait_until_state(web_page_manager, WEB_PAGE_STATE_SERVING | WEB_PAGE_STATE_DNS_SERVER_ACTIVE);

  ESP_LOGI(TAG, "Done");
}
