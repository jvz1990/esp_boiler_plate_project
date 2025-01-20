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

#include "foundation_main.h"

#include <esp_log.h>
#include <fail_mode_web_page.h>
#include <ota_download.h>
#include <version_check.h>

#include "nvs_mgmt.h"
#include "state.h"
#include "wifi_connection.h"

static const char* TAG = "FOUNDATION_MAIN";

typedef enum
{
  WIFI_P = 1,
  RESTORE_WEB_PAGE_P,
  VERSION_CHECKING_P,
  OTA_UPDATE_P,
  NVS_MGMT_P
} TaskPriorities;

void init_foundation() {
  // Initialise shared data
  unit_config_init();

  // Initialise shared system event group
  system_event_group = xEventGroupCreate();
  assert(system_event_group != NULL);

  // Create Tasks
  xTaskCreate(init_nvs_manager, "NVS Manager Task", 2048, NULL, NVS_MGMT_P, NULL);
  xTaskCreate(init_wifi_connection_task, "WIFI Connection Task", 4096, NULL, WIFI_P, NULL);
  xTaskCreate(init_version_checking_task, "HTTPS Version Checking Task", 4096, NULL, VERSION_CHECKING_P, NULL);

  xEventGroupSetBits(system_event_group, NVS_CONFIG_READ_REQUEST);

  while (1) {
    EventBits_t bits = xEventGroupWaitBits(system_event_group,
                                           NVS_CONFIG_READ_SUCCESSFULLY | GO_INTO_AP_MODE | RECEIVED_IP_ADDRESS |
                                             NEW_FIRMWARE_AVAILABLE | FIRMWARE_VERSION_UP_TO_DATE,
                                           pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & GO_INTO_AP_MODE) {
      ESP_LOGE(TAG, "!!! Shutting down tasks and going into failed state mode !!!");
      vTaskDelay(60000 / portTICK_PERIOD_MS);
    }

    if (bits & NVS_CONFIG_READ_SUCCESSFULLY) {
      xEventGroupClearBits(system_event_group, NVS_CONFIG_READ_SUCCESSFULLY);
      ESP_LOGI(TAG, "Sending Connect to WiFi Request");
      xEventGroupSetBits(system_event_group, CONNECT_TO_WIFI_AP_REQUEST);
    }

    if (bits & RECEIVED_IP_ADDRESS) {
      xEventGroupClearBits(system_event_group, RECEIVED_IP_ADDRESS);
      ESP_LOGI(TAG, "Connected to wifi");
      xEventGroupSetBits(system_event_group, CHECK_HTTPS_FIRMWARE_VERSION);
    }

    if (bits & NEW_FIRMWARE_AVAILABLE) {
      xEventGroupClearBits(system_event_group, NEW_FIRMWARE_AVAILABLE);
      ESP_LOGI(TAG, "New firmware available");
      xTaskCreate(init_ota_task, "OTA Download Task", 4096, NULL, OTA_UPDATE_P, NULL);
    }

    if (bits & FIRMWARE_VERSION_UP_TO_DATE) {
      xEventGroupClearBits(system_event_group, FIRMWARE_VERSION_UP_TO_DATE);
      ESP_LOGI(TAG, "Firmware version up to date");
    }

    if (bits & START_WEB_AP_WEBPAGE) {
      xEventGroupClearBits(system_event_group, START_WEB_AP_WEBPAGE);
      xTaskCreate(init_fail_mode_web_page, "Fail Mode Web-Page", 8192, NULL, RESTORE_WEB_PAGE_P, NULL);
      break;
    }

    if (bits == 0) {
      ESP_LOGW(TAG, "Unhandled event bits: %lx", bits);
    }
  }

  vTaskDelete(NULL);
}
