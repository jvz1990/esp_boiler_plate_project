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

#include "wifi_connection.h"

#include "allocation.h"
#include "configuration.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <esp_wifi_types_generic.h>
#include <state.h>
#include <string.h>
#include <lwip/sockets.h>

#define WIFI_DELAY_RECONNECT_MS 30000

static const char* TAG = "WIFI_MANAGER";

typedef struct
{
  EventGroupHandle_t system_event_group;
  esp_timer_handle_t retry_timer;
  uint32_t retry_count;
  wifi_config_t wifi_config;
  char ap_ssid[MAX_SSID_LENGTH];
  char ap_password[MAX_PASSWORD_LENGTH];
  esp_event_handler_instance_t wifi_event_handler_t;
  esp_event_handler_instance_t ip_event_handler_t;
} wifi_manager_t;

static wifi_manager_t* wifi_manager = NULL;

static esp_err_t find_strongest_ssid();
static void log_wifi_disconnect(uint8_t reason);
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static void ip_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static void enter_ap_mode();
static esp_err_t initialize_wifi();
static void retry_timer_callback(void* arg);
static wifi_manager_t* create_wifi_manager(EventGroupHandle_t event_group);
static esp_err_t start_wifi_scan();

static void retry_timer_callback(void* arg) {
  ESP_LOGI(TAG, "Attempting WiFi reconnection");
  esp_wifi_connect();
}

static wifi_manager_t* create_wifi_manager(EventGroupHandle_t event_group) {
  wifi_manager_t* manager = calloc(1, sizeof(wifi_manager_t));
  if (!manager) return NULL;

  manager->system_event_group = event_group;

  esp_timer_create_args_t timer_args = {
    .callback = retry_timer_callback,
    .arg = manager,
    .name = "wifi_retry_timer"
  };
  ESP_ERROR_CHECK(esp_timer_create(&timer_args, &manager->retry_timer));

  return manager;
}

static esp_err_t find_strongest_ssid() {
  uint16_t ap_count = 0;
  ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));

  wifi_ap_record_t* ap_records = malloc(sizeof(wifi_ap_record_t) * ap_count);
  if (!ap_records) return ESP_ERR_NO_MEM;

  ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_count, ap_records));

  const unit_configuration_t* config = unit_config_acquire();
  int8_t best_rssi = -127;
  bool found = false;

  for (int i = 0; i < ap_count; i++) {
    for (size_t j = 0; j < config->con_config.wifi_configs_count; j++) {
      const wifi_settings_t* setting = &config->con_config.wifi_settings[j];

      if (strncmp((char*)ap_records[i].ssid, setting->ssid,
                  sizeof(ap_records[i].ssid)) == 0) {
        if (ap_records[i].rssi > best_rssi) {
          best_rssi = ap_records[i].rssi;
          strlcpy((char*)wifi_manager->wifi_config.sta.ssid,
                  setting->ssid, MAX_SSID_LENGTH);
          strlcpy((char*)wifi_manager->wifi_config.sta.password,
                  setting->password, MAX_PASSWORD_LENGTH);
          found = true;
        }
      }
    }
  }

  unit_config_release();
  free(ap_records);
  if (found) {
    ESP_LOGI(TAG, "Found network SSID [%s] at RSSI dBm [%d]", wifi_manager->wifi_config.sta.ssid, best_rssi);
  }
  return found ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static esp_err_t initialize_wifi() {
  esp_err_t ret = esp_netif_init();
  if (ret != ESP_OK) return ret;

  ret = esp_event_loop_create_default();
  if (ret != ESP_OK) return ret;

  esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ret = esp_wifi_init(&cfg);
  if (ret != ESP_OK) return ret;

  // Register event handlers with context
  ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
    &wifi_event_handler, wifi_manager, &wifi_manager->wifi_event_handler_t));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID,
    &ip_event_handler, wifi_manager, &wifi_manager->ip_event_handler_t));

  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_start());

  xEventGroupSetBits(wifi_manager->system_event_group, WIFI_INITIALIZED_BIT);
  return ESP_OK;
}

static esp_err_t start_wifi_scan() {
  const wifi_scan_config_t scan_config = {
    .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    .show_hidden = false,
    .scan_time = {.active = {.min = 1000, .max = 3000}}
  };

  esp_err_t err = esp_wifi_scan_start(&scan_config, false);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Scan failed: %s", esp_err_to_name(err));
    xEventGroupSetBits(wifi_manager->system_event_group, WIFI_REQUEST_AP_MODE_BIT);
  }
  return err;
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
  wifi_manager_t* wm = (wifi_manager_t*)arg;

  switch (event_id) {
    case WIFI_EVENT_STA_START:
      xEventGroupClearBits(wm->system_event_group, WIFI_REQUEST_AP_MODE_BIT);
      start_wifi_scan();
      break;

    case WIFI_EVENT_SCAN_DONE:
      if (find_strongest_ssid() == ESP_OK) {
        ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wm->wifi_config));
        esp_wifi_connect();
      } else {
        xEventGroupSetBits(wm->system_event_group, WIFI_REQUEST_AP_MODE_BIT);
      }
      break;

    case WIFI_EVENT_STA_DISCONNECTED:
    {
      wifi_event_sta_disconnected_t* event = (wifi_event_sta_disconnected_t*)event_data;
      log_wifi_disconnect(event->reason);

      if (wm->retry_count < CONFIG_WIFI_RETRIES) {
        xEventGroupSetBits(wm->system_event_group, WIFI_RETRYING_BIT);
        esp_timer_start_once(wm->retry_timer, WIFI_DELAY_RECONNECT_MS * 1000);
        wm->retry_count++;
      } else {
        xEventGroupSetBits(wm->system_event_group, WIFI_REQUEST_AP_MODE_BIT);
      }
      break;
    }

    case WIFI_EVENT_STA_CONNECTED:
      xEventGroupClearBits(wm->system_event_group, WIFI_RETRYING_BIT);
      break;

    default:
      break;
  }
}

static void ip_event_handler(void* arg, esp_event_base_t event_base,
                             int32_t event_id, void* event_data) {
  wifi_manager_t* wm = (wifi_manager_t*)arg;

  if (event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
    ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));

    unit_configuration_t* config = unit_config_acquire();
    config->wifi_connected = true;
    unit_config_release();

    wm->retry_count = 0;
    xEventGroupSetBits(wm->system_event_group, WIFI_CONNECTED_BIT);
  }
}

static void log_wifi_disconnect(const uint8_t reason) {
  switch (reason) {
    case WIFI_REASON_AUTH_EXPIRE:
      ESP_LOGW(TAG, "Authentication expired");
      break;
    case WIFI_REASON_AUTH_FAIL:
      ESP_LOGW(TAG, "Authentication failed");
      break;
    case WIFI_REASON_NO_AP_FOUND:
      ESP_LOGW(TAG, "No AP found");
      break;
    case WIFI_REASON_ASSOC_FAIL:
      ESP_LOGW(TAG, "Association failed");
      break;
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
      ESP_LOGW(TAG, "Handshake timeout");
      break;
    case WIFI_REASON_ASSOC_LEAVE:
      ESP_LOGW(TAG, "Deassociated due to leaving");
      break;
    case WIFI_REASON_ASSOC_COMEBACK_TIME_TOO_LONG:
      ESP_LOGW(TAG, "Association comeback time too long");
      break;
    default:
      ESP_LOGW(TAG, "Disconnected, reason: %d", reason);
      break;
  }
}

static void enter_ap_mode() {
  esp_wifi_disconnect();
  esp_wifi_stop();

  esp_netif_create_default_wifi_ap();

  wifi_config_t ap_config = {
    .ap = {
      .ssid = "",
      .password = "",
      .ssid_len = strlen(CONFIG_AP_SSID),
      .max_connection = 1,
      .authmode = strlen(CONFIG_AP_PASSWORD) == 0 ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK,
      .pmf_cfg.required = false
    }
  };

  //  const unit_configuration_t *config = unit_config_acquire();
  strlcpy((char*)ap_config.ap.ssid, CONFIG_AP_SSID, MAX_SSID_LENGTH); // TODO allow AP wifi config
  strlcpy((char*)ap_config.ap.password, CONFIG_AP_PASSWORD, MAX_PASSWORD_LENGTH); // TODO allow AP wifi config
  //   unit_config_release();

  esp_wifi_set_mode(WIFI_MODE_AP);
  esp_wifi_set_config(WIFI_IF_AP, &ap_config);
  esp_wifi_start();

  esp_timer_stop(wifi_manager->retry_timer);
  esp_timer_delete(wifi_manager->retry_timer);

  if (wifi_manager->wifi_event_handler_t != NULL) {
    esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_manager->wifi_event_handler_t);
  }
  wifi_manager->wifi_event_handler_t = NULL;

  if (wifi_manager->wifi_event_handler_t != NULL) {
    esp_event_handler_instance_unregister(IP_EVENT, ESP_EVENT_ANY_ID, wifi_manager->ip_event_handler_t);
  }
  wifi_manager->ip_event_handler_t = NULL;


  xEventGroupSetBits(wifi_manager->system_event_group, WIFI_AP_MODE_BIT);
}

void init_wifi_connection_task() {
  // Logic handled via wifi_event_handler
  while (1) {
    EventBits_t bits = xEventGroupWaitBits(system_event_group,
                                           WIFI_REQUEST_STA_MODE_BIT | WIFI_REQUEST_AP_MODE_BIT | REBOOT_BIT,
                                           pdFALSE,
                                           pdFALSE, portMAX_DELAY);

    if (bits & WIFI_REQUEST_STA_MODE_BIT) {
      xEventGroupClearBits(system_event_group, WIFI_REQUEST_STA_MODE_BIT);
      ESP_LOGI(TAG, "Wifi connect request received. Connecting to Wifi");

      wifi_manager = create_wifi_manager(system_event_group);

      if (initialize_wifi() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize wifi connection task");
        xEventGroupSetBits(system_event_group, WIFI_REQUEST_AP_MODE_BIT);
      }
    }

    if (bits & WIFI_REQUEST_AP_MODE_BIT) {
      xEventGroupClearBits(system_event_group, WIFI_REQUEST_AP_MODE_BIT);
      ESP_LOGI(TAG, "Setting Wi-Fi in AP mode");
      enter_ap_mode();
    }

    if (bits & REBOOT_BIT) {
      ESP_LOGW(TAG, "RECEIVED REBOOT");
      break;
    }
    taskYIELD();
  }

  esp_wifi_disconnect();
  esp_wifi_stop();
  esp_wifi_deinit();
  esp_netif_destroy(esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"));
  esp_netif_destroy(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"));

  if (wifi_manager) {
    free(wifi_manager);
    wifi_manager = NULL;
  }

  ESP_LOGI(TAG, "Done");
  vTaskDelete(NULL);
}
