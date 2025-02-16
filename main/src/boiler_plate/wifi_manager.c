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

#include "wifi_manager.h"

#include "state.h"

#include <configuration.h>
#include <esp_err.h>
#include <esp_event.h>
#include <esp_event_base.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

#define WIFI_DELAY_RECONNECT_MS 30000 // #TODO refactor into configurable settings

static const char* TAG = "WiFiManager";

struct wifi_manager
{
  wifi_manager_state_t current_state;
  EventGroupHandle_t request_event_group;
  EventGroupHandle_t state_event_group;
  wifi_config_t sta_config;
  wifi_config_t ap_config;
  esp_netif_t* sta_netif;
  esp_netif_t* ap_netif;
  esp_event_handler_instance_t wifi_event_handler_t;
  esp_event_handler_instance_t ip_event_handler_t;
  esp_timer_handle_t retry_timer;
  uint32_t retry_count;
  char ap_ssid[MAX_SSID_LEN];
  char ap_password[MAX_PASSPHRASE_LEN]; // #TODO refactor
  bool sta_config_set;
  bool ap_config_set;
  TaskHandle_t fsm_task_handle;
};

static void fsm_task(void* arg);
static void wifi_event_handler(void* arg, esp_event_base_t base, int32_t event_id, void* data);
static void ip_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static esp_err_t transition_to_state(wifi_manager_t* manager, wifi_manager_state_t new_state);
static void retry_timer_callback(void* arg);
static esp_err_t find_strongest_ssid(wifi_manager_t const* manager);
static esp_err_t start_wifi_scan(wifi_manager_t const* wifi_manager);
static esp_err_t connect_to_sta(wifi_manager_t* wifi_manager);
static void handle_sta_disconnected(wifi_manager_t* wifi_manager, wifi_event_sta_disconnected_t const* event_data);
static void handle_sta_ip_obtained(wifi_manager_t* wifi_manager, ip_event_got_ip_t const* event_data);
static void log_wifi_disconnect(uint8_t reason);

wifi_manager_t* wifi_manager_create(UBaseType_t priority) {
  static bool netif_initliazed = false;
  wifi_manager_t* manager = calloc(1, sizeof(wifi_manager_t));
  if (!manager) return NULL;

  manager->request_event_group = xEventGroupCreate();
  manager->state_event_group = xEventGroupCreate();

  if (!manager->request_event_group || !manager->state_event_group) {
    free(manager);
    return NULL;
  }

  manager->current_state = WIFI_MANAGER_STATE_NONE;

  // #TODO refactor to be configurable
  manager->sta_config_set = true;
  manager->ap_config_set = true;
  strlcpy((char*)manager->ap_config.ap.ssid, CONFIG_AP_SSID, MAX_SSID_LEN);
  strlcpy((char*)manager->ap_config.ap.password, CONFIG_AP_PASSWORD, MAX_PASSPHRASE_LEN);
  manager->ap_config.ap.ssid_len = strlen(CONFIG_AP_SSID);
  manager->ap_config.ap.max_connection = 1;
  manager->ap_config.ap.authmode = strlen(CONFIG_AP_PASSWORD) == 0 ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
  manager->ap_config.ap.pmf_cfg.required = false;

  esp_timer_create_args_t timer_args = {
    .callback = retry_timer_callback,
    .arg = manager,
    .name = "wifi_retry_timer"
  };
  ESP_ERROR_CHECK(esp_timer_create(&timer_args, &manager->retry_timer));

  if (!netif_initliazed) {
    ESP_ERROR_CHECK(esp_netif_init()); // Should only be called once in program life-time
    netif_initliazed = true;
  }

  ESP_ERROR_CHECK(esp_event_loop_create_default());

  ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
    &wifi_event_handler, manager, &manager->wifi_event_handler_t));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID,
    &ip_event_handler, manager, &manager->ip_event_handler_t));

  xTaskCreate(
    fsm_task,
    TAG,
    4096,
    manager,
    priority,
    &manager->fsm_task_handle
    );

  return manager;
}

void wifi_manager_destroy(wifi_manager_t* manager) {
  if (!manager) return;

  if (manager->wifi_event_handler_t) {
    esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, manager->wifi_event_handler_t);
  }
  if (manager->ip_event_handler_t) {
    esp_event_handler_instance_unregister(IP_EVENT, ESP_EVENT_ANY_ID, manager->ip_event_handler_t);
  }

  if (manager->retry_timer) {
    esp_timer_stop(manager->retry_timer);
    esp_timer_delete(manager->retry_timer);
  }

  if (manager->sta_netif) {
    esp_netif_destroy(manager->sta_netif);
  }
  if (manager->ap_netif) {
    esp_netif_destroy(manager->ap_netif);
  }

  esp_event_loop_delete_default();

  if (manager->fsm_task_handle) {
    vTaskDelete(manager->fsm_task_handle);
  }
  if (manager->request_event_group) {
    vEventGroupDelete(manager->request_event_group);
  }
  if (manager->state_event_group) {
    vEventGroupDelete(manager->state_event_group);
  }


  if (manager->current_state != WIFI_MANAGER_STATE_NONE) {
    esp_wifi_disconnect();
    esp_wifi_stop();
    esp_wifi_deinit();
    esp_netif_destroy(esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"));
    esp_netif_destroy(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"));
  }

  free(manager);
}

esp_err_t wifi_manager_request_state(wifi_manager_t* manager, wifi_manager_state_t new_state) {
  if (!manager) return ESP_ERR_INVALID_ARG;

  EventBits_t bits = 0;
  switch (new_state) {
    case WIFI_MANAGER_STATE_NONE:
      bits = WIFI_MANAGER_REQUEST_NONE_BIT;
      break;
    case WIFI_MANAGER_STATE_STA:
      bits = WIFI_MANAGER_REQUEST_STA_BIT;
      break;
    case WIFI_MANAGER_STATE_AP:
      bits = WIFI_MANAGER_REQUEST_AP_BIT;
      break;
    case WIFI_MANAGER_STATE_AP_STA:
      bits = WIFI_MANAGER_REQUEST_AP_STA_BIT;
      break;
    default:
      return ESP_ERR_INVALID_ARG;
  }

  xEventGroupSetBits(manager->request_event_group, bits);
  return ESP_OK;
}

wifi_manager_state_t wifi_manager_get_state(const wifi_manager_t* manager) {
  return manager ? manager->current_state : WIFI_MANAGER_STATE_NONE;
}

EventGroupHandle_t wifi_manager_get_state_event_group(const wifi_manager_t* manager) {
  return manager ? manager->state_event_group : NULL;
}

static void retry_timer_callback(void* arg) {
  ESP_LOGI(TAG, "Attempting WiFi reconnection");
  esp_wifi_connect();
}

void wifi_manager_wait_until_state(wifi_manager_t const* const manager, const EventBits_t wifi_state) {
  if (manager == NULL) return;

  xEventGroupWaitBits(manager->state_event_group, wifi_state, pdFALSE, pdTRUE, portMAX_DELAY);
}

static esp_err_t find_strongest_ssid(wifi_manager_t const* const manager) {
  ESP_LOGI(TAG, "Finding strongest SSID");

  uint16_t ap_count = 0;
  ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));

  wifi_ap_record_t* ap_records = malloc(sizeof(wifi_ap_record_t) * ap_count);
  if (!ap_records) return ESP_ERR_NO_MEM;

  ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_count, ap_records));

  const unit_configuration_t* config = unit_config_acquire();
  int8_t best_rssi = -127;
  bool found = false;

  for (int i = 0; i < ap_count; i++) {
    for (size_t j = 0; j < config->con_config.wifi_settings_count; j++) {
      const wifi_settings_t* setting = &config->con_config.wifi_settings[j];

      if (strncmp((char*)ap_records[i].ssid, setting->ssid,
                  sizeof(ap_records[i].ssid)) == 0) {
        if (ap_records[i].rssi > best_rssi) {
          best_rssi = ap_records[i].rssi;
          strlcpy((char*)manager->sta_config.sta.ssid,
                  setting->ssid, MAX_SSID_LEN);
          strlcpy((char*)manager->sta_config.sta.password,
                  setting->password, MAX_PASSPHRASE_LEN);
          found = true;
        }
      }
    }
    taskYIELD();
  }

  unit_config_release();
  free(ap_records);
  if (found) {
    ESP_LOGI(TAG, "Found network SSID [%s] at RSSI dBm [%d]", (char*)manager->sta_config.sta.ssid, best_rssi);
  }
  return found ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static void fsm_task(void* arg) {
  wifi_manager_t* manager = arg;

  while (1) {
    EventBits_t bits = xEventGroupWaitBits(
      manager->request_event_group,
      WIFI_MANAGER_REQUEST_NONE_BIT |
      WIFI_MANAGER_REQUEST_STA_BIT |
      WIFI_MANAGER_REQUEST_AP_BIT |
      WIFI_MANAGER_REQUEST_AP_STA_BIT,
      pdTRUE, pdFALSE, portMAX_DELAY
      );

    wifi_manager_state_t requested_state = WIFI_MANAGER_STATE_NONE;
    if (bits & WIFI_MANAGER_REQUEST_NONE_BIT) requested_state = WIFI_MANAGER_STATE_NONE;
    else if (bits & WIFI_MANAGER_REQUEST_STA_BIT) requested_state = WIFI_MANAGER_STATE_STA;
    else if (bits & WIFI_MANAGER_REQUEST_AP_BIT) requested_state = WIFI_MANAGER_STATE_AP;
    else if (bits & WIFI_MANAGER_REQUEST_AP_STA_BIT) requested_state = WIFI_MANAGER_STATE_AP_STA;
    else
      ESP_LOGW(TAG, "Requested state not recognized: %d", requested_state);

    const esp_err_t err = transition_to_state(manager, requested_state);
    if (err != ESP_OK)
      ESP_LOGE(TAG, "State transition failed: %s", esp_err_to_name(err));

    taskYIELD();
  }
}

static esp_err_t connect_to_sta(wifi_manager_t* const wifi_manager) {
  ESP_LOGI(TAG, "Connecting to STA");

  esp_err_t err = find_strongest_ssid(wifi_manager);
  if (err != ESP_OK) return err;

  err = esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_manager->sta_config);
  if (err != ESP_OK) return err;

  err = esp_wifi_connect();
  return err;
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
    case WIFI_REASON_CONNECTION_FAIL:
      ESP_LOGW(TAG, "Connection failed");
      break;
    default:
      ESP_LOGW(TAG, "Disconnected, reason: %d", reason);
      break;
  }
}

static void handle_sta_disconnected(wifi_manager_t* const wifi_manager,
                                    wifi_event_sta_disconnected_t const* const event_data) {
  ESP_LOGI(TAG, "Sta disconnected. Retrying to connect");

  log_wifi_disconnect(event_data->reason);

  if (wifi_manager->retry_count < CONFIG_WIFI_RETRIES) {
    esp_timer_start_once(wifi_manager->retry_timer, WIFI_DELAY_RECONNECT_MS * 1000);
    wifi_manager->retry_count++;
  } else {
    wifi_manager_request_state(wifi_manager, WIFI_MANAGER_STATE_AP); // #TODO make fail mode configurable
  }
}

static void wifi_event_handler(void* arg, esp_event_base_t base, int32_t event_id, void* data) {
  wifi_manager_t* manager = arg;

  ESP_LOGD(TAG, "Received WiFi event %li: ", event_id);

  if (base == WIFI_EVENT) {
    switch (event_id) {
      case WIFI_EVENT_STA_START:
        ESP_ERROR_CHECK(start_wifi_scan(manager)); // #TODO error handle
        break;
      case WIFI_EVENT_SCAN_DONE:
        ESP_ERROR_CHECK(connect_to_sta(manager)); // #TODO error handle
        break;
      case WIFI_EVENT_STA_DISCONNECTED:
        handle_sta_disconnected(manager, data);
        break;
      case WIFI_EVENT_STA_STOP:
      case WIFI_EVENT_AP_STOP:
        wifi_manager_request_state(manager, WIFI_MANAGER_STATE_NONE);
        break;
      // ignore cases
      case WIFI_EVENT_STA_CONNECTED:
      case WIFI_EVENT_HOME_CHANNEL_CHANGE:
        break;
      default:
        ESP_LOGI(TAG, "Unhandled Wi-Fi event id: %ld", event_id);
        break;
    }
  }
}

static void handle_sta_ip_obtained(wifi_manager_t* const wifi_manager, ip_event_got_ip_t const* const event_data) {
  ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event_data->ip_info.ip));

  wifi_manager->retry_count = 0;
  xEventGroupSetBits(wifi_manager->state_event_group, WIFI_MANAGER_STATE_STA_IP_RECEIVED_BIT);
}

static void ip_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
  switch (event_id) {
    case IP_EVENT_STA_GOT_IP:
      handle_sta_ip_obtained(arg, event_data);
      break;
    default:
      ESP_LOGI(TAG, "Unhandled IP event: %ld", event_id);
      break;
  }
}

static esp_err_t start_wifi_scan(wifi_manager_t const* const wifi_manager) {
  ESP_LOGI(TAG, "Starting WiFi scan");
  const wifi_scan_config_t scan_config = {
    .ssid = NULL,
    .bssid = NULL,
    .channel = 0,
    .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    .show_hidden = false,
    .scan_time = {.active = {.min = 1000, .max = 3000}}
  };

  esp_err_t err = esp_wifi_scan_start(&scan_config, false);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Scan failed: %s", esp_err_to_name(err));
  }
  return err;
}

static esp_err_t transition_to_state(wifi_manager_t* manager, wifi_manager_state_t new_state) {
  if (manager->current_state == new_state) return ESP_OK;

  // Validate configurations
  if ((new_state == WIFI_MANAGER_STATE_STA || new_state == WIFI_MANAGER_STATE_AP_STA) &&
    !manager->sta_config_set) {
    return ESP_ERR_INVALID_STATE;
  }
  if ((new_state == WIFI_MANAGER_STATE_AP || new_state == WIFI_MANAGER_STATE_AP_STA) &&
    !manager->ap_config_set) {
    return ESP_ERR_INVALID_STATE;
  }

  if (manager->sta_netif) {
    esp_netif_destroy(manager->sta_netif);
    manager->sta_netif = NULL;
  }
  if (manager->ap_netif) {
    esp_netif_destroy(manager->ap_netif);
    manager->ap_netif = NULL;
  }

  esp_err_t err;
  if (manager->current_state != WIFI_MANAGER_STATE_NONE) {
    if ((err = esp_wifi_stop()) != ESP_OK) return err;
  }

  // #TODO refactor transition
  wifi_mode_t target_mode;
  switch (new_state) {
    case WIFI_MANAGER_STATE_NONE:
      target_mode = WIFI_MODE_NULL;
      break;
    case WIFI_MANAGER_STATE_STA:
      target_mode = WIFI_MODE_STA;
      manager->sta_netif = esp_netif_create_default_wifi_sta();
      break;
    case WIFI_MANAGER_STATE_AP:
      target_mode = WIFI_MODE_AP;
      manager->ap_netif = esp_netif_create_default_wifi_ap();
      break;
    case WIFI_MANAGER_STATE_AP_STA:
      target_mode = WIFI_MODE_APSTA;
      manager->sta_netif = esp_netif_create_default_wifi_sta();
      manager->ap_netif = esp_netif_create_default_wifi_ap();
      break;
    default:
      return ESP_ERR_INVALID_ARG;
  }

  if (new_state != WIFI_MANAGER_STATE_NONE) {
    if (manager->current_state == WIFI_MANAGER_STATE_NONE) {
      wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
      if ((err = esp_wifi_init(&cfg)) != ESP_OK) return err;
    }

    if ((err = esp_wifi_set_mode(target_mode)) != ESP_OK) return err;

    // #TODO remove - check if a nicer way to set wifi config
    /*if (target_mode == WIFI_MODE_STA || target_mode == WIFI_MODE_APSTA)
      if ((err = esp_wifi_set_config(ESP_IF_WIFI_STA, &manager->sta_config)) != ESP_OK) return err;*/
    if (target_mode == WIFI_MODE_AP || target_mode == WIFI_MODE_APSTA)
      if ((err = esp_wifi_set_config(ESP_IF_WIFI_AP, &manager->ap_config)) != ESP_OK) return err;

    if ((err = esp_wifi_start()) != ESP_OK) return err;
  } else {
    if ((err = esp_wifi_deinit()) != ESP_OK) return err;
  }

  manager->current_state = new_state;

  // Update state event group
  EventBits_t bits = 0;
  switch (new_state) {
    case WIFI_MANAGER_STATE_NONE:
      bits = WIFI_MANAGER_STATE_NONE_BIT;
      break;
    case WIFI_MANAGER_STATE_STA:
      bits = WIFI_MANAGER_STATE_STA_BIT;
      break;
    case WIFI_MANAGER_STATE_AP:
      bits = WIFI_MANAGER_STATE_AP_BIT;
      break;
    case WIFI_MANAGER_STATE_AP_STA:
      bits = WIFI_MANAGER_STATE_AP_STA_BIT;
      break;
    default:
      ESP_LOGW(TAG, "Unhandled state request: %d", new_state);
      break;
  }
  xEventGroupSetBits(manager->state_event_group, bits);

  return ESP_OK;
}
