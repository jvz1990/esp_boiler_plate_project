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
#include <esp_wifi.h>
#include <esp_wifi_types_generic.h>
#include <state.h>
#include <string.h>

#define WIFI_DELAY_RECONNECT_MS 30000

#define WIFI_SSID "ESP32_Host"
#define WIFI_PASS "password1234"
#define MAX_STA_CONN 1

static const char *TAG = "WIFI_MANAGER";
static uint32_t wifi_retry_count = 0;
static wifi_config_t wifi_config = {0};
static esp_event_handler_instance_t wifi_event_handler_t = NULL;
static esp_event_handler_instance_t ip_event_handler_t = NULL;
static char strongest_wifi_ssid[MAX_SSID_LENGTH] = {0};
static char strongest_wifi_password[MAX_PASSWORD_LENGTH] = {0};

static esp_err_t find_strongest_ssid();
static void wifi_retry_connect();
static void log_wifi_disconnect(uint8_t reason);
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
static void ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
static esp_err_t wifi_init();
static esp_err_t start_wifi_ap_scan();
static void setup_ap_mode();

static esp_err_t find_strongest_ssid() {
  esp_err_t err = ESP_OK;

  uint16_t number_of_networks = 0;
  uint8_t max_retry = 5;
  wifi_ap_record_t *ap_records = NULL;

  while ((err = esp_wifi_scan_get_ap_num(&number_of_networks)) != ESP_OK) {
    if (--max_retry <= 0)
      break;

    if (err == ESP_ERR_WIFI_NOT_INIT) {
      wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
      esp_wifi_init(&cfg);
    }

    if (err == ESP_ERR_WIFI_NOT_STARTED)
      esp_wifi_start();
  }

  if (err == ESP_OK) {
    ESP_LOGI(TAG, "Found %d networks", number_of_networks);

    err = allocate_and_clear_buffer((void **)&ap_records, sizeof(wifi_ap_record_t) * number_of_networks, TAG,
                                    ESP_ERR_NO_MEM);
  }

  if (err == ESP_OK) {
    err = esp_wifi_scan_get_ap_records(&number_of_networks, ap_records);
  }

  bool ssid_found = false;
  int8_t strongest_rssi = -100;
  if (err == ESP_OK) {
    const shared_data_t *shared_data = shared_data_acquire();
    const unit_configuration_t *config = &(shared_data->unit_config);
    strlcpy(strongest_wifi_ssid, config->wifi_settings[0].ssid, MAX_SSID_LENGTH);
    strlcpy(strongest_wifi_password, config->wifi_settings[0].password, MAX_PASSWORD_LENGTH);
    wifi_settings_t const *const wifi_settings = config->wifi_settings;
    for (int i = 0; i < number_of_networks; i++) {
      if (strlen((char *)ap_records[i].ssid) == 0)
        continue; // Skip empty SSIDs
      ESP_LOGI(TAG, "SSID: %s, RSSI: %d dBm", (const char *)ap_records[i].ssid, ap_records[i].rssi);

      for (size_t j = 0; j < config->wifi_configs_count; ++j) {
        // Match scanned SSID with provided Wi-Fi settings
        if (strncmp((char *)ap_records[i].ssid, wifi_settings[j].ssid, sizeof(ap_records[i].ssid)) == 0) {
          if (ap_records[i].rssi > strongest_rssi) {
            strongest_rssi = ap_records[i].rssi;
            strlcpy(strongest_wifi_ssid, (char *)ap_records[i].ssid, MAX_SSID_LENGTH);
            strlcpy(strongest_wifi_password, wifi_settings[j].password, MAX_PASSWORD_LENGTH);
            ssid_found = true;
          }
        }
      }
    }
    shared_data_release();
  }

  if (ssid_found) {
    ESP_LOGI(TAG, "Strongest Wi-Fi SSID: %s, RSSI: %d dBm", strongest_wifi_ssid, strongest_rssi);
    err = ESP_OK;
  } else {
    ESP_LOGW(TAG, "No matching Wi-Fi SSIDs found in provided settings.");
    err = ESP_FAIL;
  }

  if (err == ESP_OK) {
    // Copy SSID and password using strlcpy
    strlcpy((char *)wifi_config.sta.ssid, strongest_wifi_ssid, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, strongest_wifi_password, sizeof(wifi_config.sta.password));
    esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
  } else {
    ESP_LOGE(TAG, "Error: %s", esp_err_to_name(err));
  }

  check_and_free((void **)&ap_records);

  return err;
}

static void wifi_retry_connect() {
  ESP_LOGW(TAG, "Connection failed - retrying in %d ms", WIFI_DELAY_RECONNECT_MS);
  esp_wifi_stop();
  esp_wifi_disconnect();
  vTaskDelay(WIFI_DELAY_RECONNECT_MS / portTICK_PERIOD_MS);
  esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
  esp_wifi_start();
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

static esp_err_t wifi_connect_retry() {
  esp_err_t err = ESP_OK;
  uint8_t retry_count = 5;
  do {
    err = esp_wifi_connect();
    if (err == ESP_ERR_WIFI_NOT_INIT) {
      wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
      esp_wifi_init(&cfg);
    }
    if (err == ESP_ERR_WIFI_NOT_STARTED) {
      esp_wifi_start();
    }
  } while (err != ESP_OK && --retry_count > 0);

  if (err != ESP_OK)
    ESP_LOGE(TAG, "Error: %s", esp_err_to_name(err));

  return err;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  if (event_base != WIFI_EVENT) {
    ESP_LOGW(TAG, "Non-Wifi event in wifi event handler");
    return;
  }

  switch (event_id) {
    case WIFI_EVENT_STA_START: // triggered by; esp_wifi_start()
      ESP_LOGI(TAG, "ESP WiFi started, scanning for Access Points");
      if (wifi_retry_count == 0) {
        if (start_wifi_ap_scan() != ESP_OK)
          xEventGroupSetBits(system_event_group, GO_INTO_AP_MODE);
      } else {
        if (wifi_connect_retry() != ESP_OK)
          xEventGroupSetBits(system_event_group, GO_INTO_AP_MODE);
      }
      break;

    case WIFI_EVENT_AP_START:
      ESP_LOGI(TAG, "WiFi initialised, soft starting AP");
      break;

    case WIFI_EVENT_SCAN_DONE: // triggered by; esp_wifi_scan_start()
      ESP_LOGI(TAG, "ESP WiFi scan completed, finding strongest SSID");
      if (find_strongest_ssid() != ESP_OK)
        xEventGroupSetBits(system_event_group, GO_INTO_AP_MODE);
      ESP_LOGI(TAG, "Connecting to Wi-Fi AP, SSID: %s", (char *)wifi_config.sta.ssid);
      if (wifi_connect_retry() != ESP_OK)
        xEventGroupSetBits(system_event_group, GO_INTO_AP_MODE);
      break;

    case WIFI_EVENT_STA_STOP: // triggered by; esp_wifi_stop()
      ESP_LOGI(TAG, "ESP WiFi stopped");
      break;

    case WIFI_EVENT_STA_CONNECTED: // triggered by; esp_wifi_connect()
      ESP_LOGI(TAG, "ESP WiFi connected");
      break;

    case WIFI_EVENT_STA_DISCONNECTED:
      { // triggerd by; esp_wifi_connect(), or connection fails
        ESP_LOGW(TAG, "ESP WiFi disconnected");
        shared_data_t *shared_data = shared_data_acquire();
        shared_data->wifi_connected = false;
        shared_data_release();

        const wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;

        log_wifi_disconnect(event->reason);

        if (wifi_retry_count < CONFIG_WIFI_RETRIES) {
          wifi_retry_connect();
          wifi_retry_count++;
          if (wifi_retry_count >= CONFIG_WIFI_RETRIES) {
            ESP_LOGE(TAG, "Failed to connect after %d attempts", CONFIG_WIFI_RETRIES);
          } else {
            ESP_LOGI(TAG, "Retry %lu/%d", wifi_retry_count, CONFIG_WIFI_RETRIES);
          }
        } else {
          xEventGroupSetBits(system_event_group, GO_INTO_AP_MODE);
        }
        break;
      }

    case WIFI_EVENT_STA_AUTHMODE_CHANGE: // Triggered by: Authentication mode of the connected access point changes
      ESP_LOGI(TAG, "ESP WiFi Auth Mode changed");
      break;

    case WIFI_EVENT_HOME_CHANNEL_CHANGE:
      ESP_LOGI(TAG, "Home channel changed");
      break;

    default:
      ESP_LOGI(TAG, "Unhandled WiFi event: %ld", event_id);
      break;
  }
}

static void ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  if (event_base != IP_EVENT) {
    ESP_LOGW(TAG, "Non-IP event in IP event handler");
    return;
  }

  switch (event_id) {
    case IP_EVENT_STA_GOT_IP:
      shared_data_t *shared_data = shared_data_acquire();
      shared_data->wifi_connected = true;
      shared_data_release();

      ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
      ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
      wifi_retry_count = 0;
      esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
      xEventGroupSetBits(system_event_group, RECEIVED_IP_ADDRESS);
      break;
    case IP_EVENT_STA_LOST_IP:
      ESP_LOGW(TAG, "STA lost IP event");
      esp_wifi_stop();
      start_wifi_ap_scan();
      break;
    case IP_EVENT_AP_STAIPASSIGNED:
      ESP_LOGI(TAG, "AP Station assigned event");
      break;
    case IP_EVENT_GOT_IP6:
      ESP_LOGI(TAG, "Got IP6 IP 6 event");
      break;
    case IP_EVENT_ETH_GOT_IP:
      ESP_LOGI(TAG, "Got ETH IP event");
      break;
    case IP_EVENT_ETH_LOST_IP:
      ESP_LOGW(TAG, "ETH lost IP event");
      esp_wifi_stop();
      start_wifi_ap_scan();
      break;

    default:
      ESP_LOGI(TAG, "Unhandled IP event: %ld", event_id);
      break;
  }
}

static esp_err_t start_wifi_ap_scan() {
  wifi_scan_config_t scan_config = {
    .ssid = NULL,
    .bssid = NULL,
    .channel = 0,
    .show_hidden = false,
  };

  esp_err_t err = esp_wifi_scan_start(&scan_config, false);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error: %s", esp_err_to_name(err));
  }

  return err;
}

static esp_err_t wifi_init() {
  esp_err_t err = ESP_OK;

  wifi_mode_t wifi_mode;
  err = esp_wifi_get_mode(&wifi_mode);

  // Check if wifi already connected
  if (err == ESP_OK) {
    switch (wifi_mode) {
      case WIFI_MODE_STA:
        ESP_LOGI("WIFI_MODE", "Wi-Fi is in STA (Station) mode");
        return ESP_OK;
      case WIFI_MODE_AP:
        ESP_LOGI("WIFI_MODE", "Wi-Fi is in AP (Access Point) mode");
        return ESP_OK;
      case WIFI_MODE_APSTA:
        ESP_LOGI("WIFI_MODE", "Wi-Fi is in both AP and STA mode");
        return ESP_OK;
      default:
        ESP_LOGI("WIFI_MODE", "Wi-Fi mode is unknown or not set");
        break;
    }
  } else if (err == ESP_ERR_WIFI_NOT_INIT) {
    // ignore, WiFi will init soon
  } else {
    ESP_LOGE("WIFI_MODE", "Failed to get Wi-Fi mode: %s", esp_err_to_name(err));
  }

  ESP_LOGI(TAG, "Initializing TCP/IP stack");
  err = esp_netif_init();

  if (err == ESP_OK) {
    ESP_LOGI(TAG, "Initializing default event loop");
    err = esp_event_loop_create_default();
  }

  if (err == ESP_OK) {
    ESP_LOGI(TAG, "Creating default WIFI STA");
    assert(esp_netif_create_default_wifi_sta());
  }

  if (wifi_event_handler_t == NULL && err == ESP_OK) {
    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL,
                                              &wifi_event_handler_t);
  }

  if (ip_event_handler_t == NULL && err == ESP_OK) {
    err = esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, &ip_event_handler, NULL, &ip_event_handler_t);
  }

  if (err == ESP_OK) {
    uint8_t max_retries = 5;
    do {
      if (--max_retries <= 0)
        break;

      if ((err = esp_wifi_set_mode(WIFI_MODE_STA)) == ESP_ERR_WIFI_NOT_INIT) {
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        esp_wifi_init(&cfg);
        continue;
      }

      err = esp_wifi_start();
    } while (err != ESP_OK);
  }

  esp_wifi_set_ps(WIFI_PS_NONE);

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Error: %s", esp_err_to_name(err));
  }

  return err;
}

// For fail mode - no error handling
static void setup_ap_mode() {
  wifi_disconnect_cleanup();

  vTaskDelay(30000 / portTICK_PERIOD_MS);

  esp_netif_create_default_wifi_ap();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  esp_wifi_init(&cfg);

  strlcpy((char *)wifi_config.sta.ssid, CONFIG_AP_SSID, sizeof(wifi_config.sta.ssid));
  strlcpy((char *)wifi_config.sta.password, CONFIG_AP_PASSWORD, sizeof(wifi_config.sta.password));

  wifi_config.ap.ssid_len = strlen(CONFIG_AP_SSID);
  wifi_config.ap.channel = 1;
  wifi_config.ap.max_connection = 1;
  wifi_config.ap.authmode = (strlen(CONFIG_AP_PASSWORD) == 0) ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI("WiFi", "SoftAP started with SSID: %s", CONFIG_AP_SSID);
  xEventGroupSetBits(system_event_group, START_WEB_AP_WEBPAGE);
}

void wifi_disconnect_cleanup() {
  ESP_LOGI(TAG, "Disconnecting from Wi-Fi and cleaning up...");

  wifi_retry_count = CONFIG_WIFI_RETRIES;

  // Disconnect and stop Wi-Fi
  esp_wifi_disconnect();
  esp_wifi_stop();

  // Clean up event handlers
  if (wifi_event_handler_t != NULL) {
    esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler_t);
    wifi_event_handler_t = NULL;
  }

  if (ip_event_handler_t != NULL) {
    esp_event_handler_instance_unregister(IP_EVENT, ESP_EVENT_ANY_ID, ip_event_handler_t);
    ip_event_handler_t = NULL;
  }

  // Deinitialize Wi-Fi and release resources
  esp_wifi_deinit();

  // Destroy all existing netif instances
  esp_netif_destroy(esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"));
  esp_netif_destroy(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"));

  // Reset the Wi-Fi configuration
  memset(&wifi_config, 0, sizeof(wifi_config_t));

  // Disable power-saving mode if previously enabled
  esp_wifi_set_ps(WIFI_PS_NONE);

  ESP_LOGI(TAG, "Wi-Fi disconnected and resources cleaned up.");
}

void init_wifi_connection_task() {
  // Logic handled via wifi_event_handler
  while (1) {
    EventBits_t bits = xEventGroupWaitBits(system_event_group, CONNECT_TO_WIFI_AP_REQUEST | GO_INTO_AP_MODE, pdFALSE,
                                           pdFALSE, portMAX_DELAY);

    if (bits & CONNECT_TO_WIFI_AP_REQUEST) {
      xEventGroupClearBits(system_event_group, CONNECT_TO_WIFI_AP_REQUEST);
      ESP_LOGI(TAG, "Wifi connect request received. Connecting to Wifi");

      if (wifi_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize wifi connection task");
        xEventGroupSetBits(system_event_group, GO_INTO_AP_MODE);
      }
    }

    if (bits & GO_INTO_AP_MODE) {
      xEventGroupClearBits(system_event_group, GO_INTO_AP_MODE);
      ESP_LOGI(TAG, "Setting WiFi in AP mode");
      setup_ap_mode();
      break;
    }
  }

  vTaskDelete(NULL);
}
