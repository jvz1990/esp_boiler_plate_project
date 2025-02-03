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

#include "ap_mode_web_pages.h"
#include "dns_redirect.h"
#include "ota_download.h"
#include "version_check.h"
#include "nvs_mgmt.h"
#include "state.h"
#include "wifi_connection.h"

#include <esp_log.h>
#include <esp_spiffs.h>

static const char* TAG = "Boiler Plate main";

void init_boiler_plate() {
  // Initialise shared data
  unit_config_init();

  // Initialise shared system event group
  system_event_group = xEventGroupCreate();
  assert(system_event_group != NULL);

  // Create Tasks
  xTaskCreate(init_nvs_manager, "NVS Manager Task", 2048, NULL, NVS_MGMT_P, NULL);
  xTaskCreate(init_wifi_connection_task, "WIFI Connection Task", 4096, NULL, WIFI_P, NULL);
  xTaskCreate(init_version_checking_task, "HTTPS Version Checking Task", 4096, NULL, VERSION_CHECKING_P, NULL);

  xEventGroupSetBits(system_event_group, NVS_REQUEST_READ_BIT);

  while (1) {
    EventBits_t bits = xEventGroupWaitBits(system_event_group,
                                           NVS_READ_SUCCESSFULLY_READ_BIT | WIFI_CONNECTED_BIT |
                                           NEW_FIRMWARE_AVAILABLE_BIT | FIRMWARE_VERSION_UP_TO_DATE_BIT |
                                           AP_WEB_PAGES_REQUEST_BIT | WIFI_AP_MODE_BIT | REBOOT_BIT,
                                           pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & NVS_READ_SUCCESSFULLY_READ_BIT) {
      xEventGroupClearBits(system_event_group, NVS_READ_SUCCESSFULLY_READ_BIT);
      ESP_LOGI(TAG, "Sending Connect to Wi-Fi Request");
      xEventGroupSetBits(system_event_group, WIFI_REQUEST_STA_MODE_BIT);
    }

    if (bits & WIFI_CONNECTED_BIT) {
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
      xTaskCreate(init_ap_web_pages, "AP Web-Pages", 8192, NULL, AP_WEB_PAGES_P, NULL);
      start_dns_server();
      break;
    }

    if (bits & AP_WEB_PAGES_REQUEST_BIT) {
      xEventGroupClearBits(system_event_group, AP_WEB_PAGES_REQUEST_BIT);
      xTaskCreate(init_ap_web_pages, "AP Web-Pages", 8192, NULL, AP_WEB_PAGES_P, NULL);
      start_dns_server();
      break;
    }

    if (bits & REBOOT_BIT) {
      ESP_LOGW(TAG, "RECEIVED REBOOT");
      break;
    }

    taskYIELD();
  }

  ESP_LOGI(TAG, "Done");
  vTaskDelete(NULL);
}
