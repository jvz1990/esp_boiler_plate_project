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

#include "boiler_plate_main.h"

#include "nvs_manager.h"
#include "state.h"
#include "web_page_manager.h"
#include "wifi_manager.h"

#include <esp_log.h>

static const char* TAG = "Boiler Plate main";

void init_boiler_plate() {
  // Initialise shared data
  unit_config_init();

  // Initialise shared system event group
  /*system_event_group = xEventGroupCreate(); // #TODO remove
  assert(system_event_group != NULL);*/

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
  ESP_LOGI(TAG, "Wi-FI requesting state STA");
  wifi_manager_request_state(wifi_manager, WIFI_MANAGER_STATE_STA_REQUEST);
  wifi_manager_wait_until_state(wifi_manager, WIFI_MANAGER_STATE_STA_IP_RECEIVED);
  web_page_manager_request_state(web_page_manager, WEB_PAGE_STATE_SERVING_REQUEST);
  web_page_manager_wait_until_state(web_page_manager, WEB_PAGE_STATE_SERVING);
  //start_dns_server();

  //xTaskCreate(init_version_checking_task, "HTTPS Version Checking Task", 4096, NULL, VERSION_CHECKING_P, NULL);

  /*while (1) {
    EventBits_t bits = xEventGroupWaitBits(system_event_group,
                                           NVS_READ_SUCCESSFULLY_READ_BIT,
                                           pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & NVS_READ_SUCCESSFULLY_READ_BIT) {
      ESP_LOGI(TAG, "Sending Connect to Wi-Fi Request");
      xEventGroupClearBits(system_event_group, NVS_REQUEST_READ_BIT);


      start_dns_server();
    }*/

    /*if (bits & WIFI_CONNECTED_BIT) {
      xEventGroupClearBits(system_event_group, WIFI_CONNECTED_BIT);
      ESP_LOGI(TAG, "Connected to Wi-Fi");
      xEventGroupSetBits(system_event_group, FIRMWARE_REQUEST_VERSION_CHECK_BIT);
    }

    if (bits & NEW_FIRMWARE_AVAILABLE_BIT) {
      xEventGroupClearBits(system_event_group, NEW_FIRMWARE_AVAILABLE_BIT);
      ESP_LOGI(TAG, "New firmware available");
      xTaskCreate(init_ota_task, "OTA Download Task", 4096, NULL, OTA_UPDATE_P, NULL);
    }

    if (bits & FIRMWARE_VERSION_UP_TO_DATE_BIT) {
      xEventGroupClearBits(system_event_group, FIRMWARE_VERSION_UP_TO_DATE_BIT);
      ESP_LOGI(TAG, "Firmware version up to date");
    }

    if (bits & WIFI_AP_MODE_BIT) {
      xEventGroupClearBits(system_event_group, WIFI_AP_MODE_BIT);
      web_page_manager_request_state(web_page_manager, WEB_PAGE_STATE_SERVING);
      web_page_manager_wait_until_state(web_page_manager, WEB_PAGE_STATE_SERVING);
      start_dns_server();
      break;
    }

    if (bits & AP_WEB_PAGES_REQUEST_BIT) {
      wifi_manager_wait_until_state(wifi_manager, WIFI_MANAGER_STATE_AP_BIT);
      xEventGroupClearBits(system_event_group, AP_WEB_PAGES_REQUEST_BIT);
      xTaskCreate(init_ap_web_pages, "AP Web-Pages", 8192, NULL, AP_WEB_PAGES_P, NULL);
      start_dns_server();
      break;
    }

    if (bits & REBOOT_BIT) {
      ESP_LOGW(TAG, "RECEIVED REBOOT");
      break;
    }*/

    /*taskYIELD();
  }*/


  ESP_LOGI(TAG, "Done");
  vTaskDelete(NULL);
}
