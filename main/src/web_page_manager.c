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

#include "web_page_manager.h"

#include "configuration.h"
#include "dns_redirect.h"
#include "nvs_manager.h"
#include "state.h"

#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_spiffs.h>
#include <state.h>

#include <cJSON.h>
#include <esp_timer.h>

#include <stdbool.h>

typedef struct
{
  const char* path;
  const char* data_ptr;
  long content_length;
} file_info_t;

typedef enum
{
  CSS = 0,
  AP_WIFI,
  AP_WIFI_JS,
  AP_OTA,
  AP_OTA_JS,
  AP_USR,
  AP_USR_JS,
  AP_SYS,
  AP_SYS_JS,
  DEFAULT_PAGE,
  CONFIG_TYPE_COUNT
} resource_t;

struct web_page_manager
{
  EventGroupHandle_t request_event_group;
  EventGroupHandle_t state_event_group;
  TaskHandle_t fsm_task_handle;

  file_info_t files[CONFIG_TYPE_COUNT];
  char json_buffer[1024];
  httpd_handle_t server;
  httpd_uri_t handlers[15];
};

static char* TAG = "Web-page Manager";

static const char* const TEXT_HTML = "text/html"; // TYPE
static const char* const TEXT_CSS = "text/css"; // TYPE
static const char* const closing_tags = "</script></body></html>";

static void fsm_task(void* arg);
static esp_err_t transition_to_state(web_page_manager_t* manager, web_page_manager_state_request_t new_state_request);
static esp_err_t init_web_pages(web_page_manager_t* manager);
static esp_err_t load_content(file_info_t* file);
static esp_err_t wifi_handler(httpd_req_t* req);
static esp_err_t ap_wifi_html(httpd_req_t* req);
static void send_json_resp(httpd_req_t* req, int code, const char* msg);
static esp_err_t ota_handler(httpd_req_t* req);
static esp_err_t ap_ota_html(httpd_req_t* req);
static esp_err_t sys_handler(httpd_req_t* req);
static esp_err_t ap_sys_html(httpd_req_t* req);
static esp_err_t user_handler(httpd_req_t* req);
static esp_err_t ap_usr_html(httpd_req_t* req);
static esp_err_t no_content(httpd_req_t* req);
static esp_err_t css_handler(httpd_req_t* req);
static esp_err_t redirect_handler(httpd_req_t* req);
static void restart_timer_callback(void* arg);
static esp_err_t reboot_handler(httpd_req_t* req);
static esp_err_t wifi_post_handler(httpd_req_t* req);
static esp_err_t ota_post_handler(httpd_req_t* req);
static esp_err_t sys_post_handler(httpd_req_t* req);
static esp_err_t user_post_handler(httpd_req_t* req);
static esp_err_t send_pages(web_page_manager_t const* manager, httpd_req_t* req, const resource_t resources[],
                            size_t num_resouces);
static esp_err_t cleanup_web_page_manager(web_page_manager_t* manager);

web_page_manager_t* web_page_manager_create(UBaseType_t priority) {
  web_page_manager_t* manager = calloc(1, sizeof(web_page_manager_t));
  if (manager == NULL) {
    ESP_LOGE(TAG, "Unable to create web page manager");
    return NULL;
  }

  *manager = (web_page_manager_t){
    .request_event_group = xEventGroupCreate(),
    .state_event_group = xEventGroupCreate(),
    .fsm_task_handle = NULL,
    .files = {
      [CSS] = {"/spiffs/ap_pages.css", NULL, 0},
      [AP_WIFI] = {"/spiffs/ap_wifi.html", NULL, 0},
      [AP_WIFI_JS] = {"/spiffs/ap_wifi.js", NULL, 0},
      [AP_OTA] = {"/spiffs/ap_ota.html", NULL, 0},
      [AP_OTA_JS] = {"/spiffs/ap_ota.js", NULL, 0},
      [AP_USR] = {"/spiffs/ap_usr.html", NULL, 0},
      [AP_USR_JS] = {"/spiffs/ap_usr.js", NULL, 0},
      [AP_SYS] = {"/spiffs/ap_sys.html", NULL, 0},
      [AP_SYS_JS] = {"/spiffs/ap_sys.js", NULL, 0},
      [DEFAULT_PAGE] = {"/spiffs/default_page.html", NULL, 0}
    },
    .json_buffer = {0},
    .server = NULL,
    .handlers = {
      // Wi-Fi
      {.uri = "/wifi", .method = HTTP_GET, .handler = wifi_handler, .user_ctx = manager},
      {.uri = "/ap_wifi.html", .method = HTTP_GET, .handler = ap_wifi_html, .user_ctx = manager},
      // OTA
      {.uri = "/ota", .method = HTTP_GET, .handler = ota_handler, .user_ctx = manager},
      {.uri = "/ap_ota.html", .method = HTTP_GET, .handler = ap_ota_html, .user_ctx = manager},
      // Sys
      {.uri = "/system", .method = HTTP_GET, .handler = sys_handler, .user_ctx = manager},
      {.uri = "/ap_sys.html", .method = HTTP_GET, .handler = ap_sys_html, .user_ctx = manager},
      // Usr
      {.uri = "/usercfg", .method = HTTP_GET, .handler = user_handler, .user_ctx = manager},
      {.uri = "/ap_usr.html", .method = HTTP_GET, .handler = ap_usr_html, .user_ctx = manager},

      {.uri = "/ap_pages.css", .method = HTTP_GET, .handler = css_handler, .user_ctx = manager},
      {.uri = "/*", .method = HTTP_GET, .handler = redirect_handler, .user_ctx = manager},

      {.uri = "/reboot", .method = HTTP_POST, .handler = reboot_handler, .user_ctx = manager},
      {.uri = "/wifi", .method = HTTP_POST, .handler = wifi_post_handler, .user_ctx = manager},
      {.uri = "/ota", .method = HTTP_POST, .handler = ota_post_handler, .user_ctx = manager},
      {.uri = "/system", .method = HTTP_POST, .handler = sys_post_handler, .user_ctx = manager},
      {.uri = "/usercfg", .method = HTTP_POST, .handler = user_post_handler, .user_ctx = manager},

    }
  };

  if (manager->request_event_group == NULL || manager->state_event_group == NULL) {
    free(manager);
    ESP_LOGE(TAG, "Failed to create web page manager");
    return NULL;
  }

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

void web_page_manager_destroy(web_page_manager_t* const manager) {
  if (!manager) return;

  cleanup_web_page_manager(manager);

  if (manager->fsm_task_handle) {
    vTaskDelete(manager->fsm_task_handle);
  }
  if (manager->request_event_group) {
    vEventGroupDelete(manager->request_event_group);
  }
  if (manager->state_event_group) {
    vEventGroupDelete(manager->state_event_group);
  }

  free(manager);
}

esp_err_t web_page_manager_request_state(web_page_manager_t* const manager, web_page_manager_state_request_t new_state) {
  if (manager == NULL) return ESP_ERR_NOT_FOUND;

  xEventGroupSetBits(manager->request_event_group, new_state);

  return ESP_OK;
}

void web_page_manager_wait_until_state(web_page_manager_t const* const manager, web_page_manager_state_t wait_state) {
  if (manager == NULL) return;

  xEventGroupWaitBits(manager->state_event_group, wait_state, pdFALSE, pdFALSE, portMAX_DELAY);
}

static void fsm_task(void* arg) {
  web_page_manager_t* manager = arg;

  if (manager == NULL) {
    ESP_LOGE(TAG, "No web page manager passed to fsm task");
    vTaskDelete(NULL);
    return;
  }

  if (manager->request_event_group == NULL) {
    ESP_LOGE(TAG, "Request event group not created");
    vTaskDelete(NULL);
    return;
  }

  xEventGroupSetBits(manager->state_event_group, WEB_PAGE_STATE_NONE | WEB_PAGE_STATE_DNS_SERVER_NONE);

  while (1) {
    EventBits_t bits = xEventGroupWaitBits(
      manager->request_event_group,
      WEB_PAGE_STATE_NONE_REQUEST | WEB_PAGE_STATE_SERVING_REQUEST | WEB_PAGE_STATE_DNS_SERVER_NONE_REQUEST |
      WEB_PAGE_STATE_DNS_SERVER_REQUEST,
      pdTRUE, pdFALSE, portMAX_DELAY
      );

    const esp_err_t err = transition_to_state(manager, bits);
    if (err != ESP_OK)
      ESP_LOGE(TAG, "State transition failed: %s", esp_err_to_name(err));

    taskYIELD();
  }

  vTaskDelete(NULL);
}

static esp_err_t transition_to_state(web_page_manager_t* manager, web_page_manager_state_request_t new_state_request) {
  EventBits_t current_state = xEventGroupGetBits(manager->state_event_group);

  esp_err_t err = ESP_OK;

  if (current_state & WEB_PAGE_STATE_SERVING && new_state_request & WEB_PAGE_STATE_NONE_REQUEST) {
    err = cleanup_web_page_manager(manager);
    xEventGroupSetBits(manager->state_event_group, WEB_PAGE_STATE_NONE);
  } else if (current_state & WEB_PAGE_STATE_NONE && new_state_request & WEB_PAGE_STATE_SERVING_REQUEST) {
    err = init_web_pages(manager);
    xEventGroupSetBits(manager->state_event_group, WEB_PAGE_STATE_SERVING);
  } else {
    ESP_LOGI(TAG, "No state transition for web-server");
  }

  current_state = xEventGroupGetBits(manager->state_event_group);
  if (current_state & WEB_PAGE_STATE_DNS_SERVER_ACTIVE && new_state_request & WEB_PAGE_STATE_DNS_SERVER_NONE_REQUEST) {
    stop_dns_server();
    xEventGroupSetBits(manager->state_event_group, current_state | WEB_PAGE_STATE_DNS_SERVER_NONE);
  } else if (current_state & WEB_PAGE_STATE_DNS_SERVER_NONE && new_state_request & WEB_PAGE_STATE_DNS_SERVER_REQUEST) {
    start_dns_server();
    xEventGroupSetBits(manager->state_event_group, current_state | WEB_PAGE_STATE_DNS_SERVER_ACTIVE);
  } else {
    ESP_LOGI(TAG, "No state transition for dns-server");
  }

  return err;
}

static esp_err_t init_web_pages(web_page_manager_t* const manager) {
  if (manager == NULL) {
    ESP_LOGE(TAG, "Web page manager is NULL");
    return ESP_ERR_NOT_FOUND;
  }

  const size_t N_HANDLERS = sizeof(manager->handlers) / sizeof(manager->handlers[0]);

  esp_err_t err = ESP_OK;

  for (resource_t i = 0; i < CONFIG_TYPE_COUNT; i++) {
    err = load_content(&manager->files[i]);
    if (err != ESP_OK) return err;
  }

  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.max_uri_handlers = N_HANDLERS;
  cfg.uri_match_fn = httpd_uri_match_wildcard;

  err = httpd_start(&manager->server, &cfg);
  if (err != ESP_OK) return err;

  for (size_t i = 0; i < N_HANDLERS; i++) {
    err = httpd_register_uri_handler(manager->server, &manager->handlers[i]);
    if (err != ESP_OK) return err;
  }

  ESP_LOGI(TAG, "Serving %d  Web-Pages", N_HANDLERS);

  return err;
}

static esp_err_t load_content(file_info_t* const file) {
  if (file->data_ptr != NULL) {
    return ESP_OK;
  }

  FILE* f = fopen(file->path, "r");
  if (f == NULL) {
    ESP_LOGE(TAG, "Failed to open %s", file->path);
    return ESP_ERR_NOT_FOUND;
  }

  fseek(f, 0, SEEK_END);
  file->content_length = ftell(f);
  fseek(f, 0, SEEK_SET);

  char* buffer = (char*)malloc(file->content_length + 1);
  if (buffer == NULL) {
    ESP_LOGE(TAG, "Failed to allocate memory");
    fclose(f);
    return ESP_ERR_NO_MEM;
  }

  const size_t read = fread(buffer, 1, file->content_length, f);
  buffer[read] = '\0';
  fclose(f);

  file->data_ptr = buffer;
  buffer = NULL;

  ESP_LOGI(TAG, "File [%s] loaded successfully, size: %ld bytes", file->path, file->content_length);
  return ESP_OK;
}

static esp_err_t send_pages(web_page_manager_t const* const manager, httpd_req_t* req, const resource_t resources[],
                            size_t num_resouces) {
  if (manager == NULL) {
    ESP_LOGE(TAG, "Web page manager is NULL");
    return ESP_ERR_NOT_FOUND;
  }
  esp_err_t err = ESP_OK;

  for (size_t i = 0; i < num_resouces; i++) {
    resource_t resource = resources[i];
    if (manager->files[resource].data_ptr == NULL) {
      ESP_LOGE(TAG, "File [%s] is NULL", manager->files[i].path);
      return ESP_ERR_NOT_FOUND;
    }
    err = httpd_resp_send_chunk(req, manager->files[resource].data_ptr, manager->files[resource].content_length);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Failed to send chunk");
      return err;
    }
  }

  if (httpd_resp_sendstr_chunk(req, closing_tags) != ESP_OK) {
    ESP_LOGE(TAG, "Failed to send closing tags");
    return ESP_FAIL;
  }

  return httpd_resp_sendstr_chunk(req, NULL);
}

static esp_err_t wifi_handler(httpd_req_t* req) {
  web_page_manager_t* manager = req->user_ctx;
  httpd_resp_set_type(req, TEXT_HTML);
  resource_t pages[] = {DEFAULT_PAGE, AP_WIFI_JS};
  return send_pages(manager, req, pages, sizeof(pages) / sizeof(pages[0]));
}

static esp_err_t ap_wifi_html(httpd_req_t* req) {
  web_page_manager_t* manager = req->user_ctx;
  httpd_resp_set_type(req, TEXT_HTML);
  return httpd_resp_sendstr(req, manager->files[AP_WIFI].data_ptr);
}

static void send_json_resp(httpd_req_t* req, const int code, const char* msg) {
  cJSON* r = cJSON_CreateObject();
  cJSON_AddNumberToObject(r, "c", code);
  cJSON_AddStringToObject(r, "m", msg);
  const char* s = cJSON_PrintUnformatted(r);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, s);
  cJSON_Delete(r);
  free((void*)s);
}

static esp_err_t ota_handler(httpd_req_t* req) {
  web_page_manager_t* manager = req->user_ctx;
  httpd_resp_set_type(req, TEXT_HTML);
  resource_t pages[] = {DEFAULT_PAGE, AP_OTA_JS};
  return send_pages(manager, req, pages, sizeof(pages) / sizeof(pages[0]));
}

static esp_err_t ap_ota_html(httpd_req_t* req) {
  web_page_manager_t* manager = req->user_ctx;
  httpd_resp_set_type(req, TEXT_HTML);
  return httpd_resp_sendstr(req, manager->files[AP_OTA].data_ptr);
}

static esp_err_t sys_handler(httpd_req_t* req) {
  web_page_manager_t* manager = req->user_ctx;
  httpd_resp_set_type(req, TEXT_HTML);
  resource_t pages[] = {DEFAULT_PAGE, AP_SYS_JS};
  return send_pages(manager, req, pages, sizeof(pages) / sizeof(pages[0]));
}

static esp_err_t ap_sys_html(httpd_req_t* req) {
  web_page_manager_t* manager = req->user_ctx;
  if (manager == NULL) return ESP_ERR_NOT_FOUND;
  httpd_resp_set_type(req, TEXT_HTML);
  return httpd_resp_sendstr(req, manager->files[AP_SYS].data_ptr);
}

static esp_err_t user_handler(httpd_req_t* req) {
  web_page_manager_t* manager = req->user_ctx;
  httpd_resp_set_type(req, TEXT_HTML);
  resource_t pages[] = {DEFAULT_PAGE, AP_USR_JS};
  return send_pages(manager, req, pages, sizeof(pages) / sizeof(pages[0]));
}

static esp_err_t ap_usr_html(httpd_req_t* req) {
  web_page_manager_t* manager = req->user_ctx;
  if (manager == NULL) return ESP_ERR_NOT_FOUND;
  httpd_resp_set_type(req, TEXT_HTML);
  return httpd_resp_sendstr(req, manager->files[AP_USR].data_ptr);
}

static esp_err_t no_content(httpd_req_t* req) {
  httpd_resp_set_status(req, "204 No Content");
  return httpd_resp_send(req, NULL, 0);
}

static esp_err_t css_handler(httpd_req_t* req) {
  web_page_manager_t* manager = req->user_ctx;
  httpd_resp_set_type(req, TEXT_CSS);
  return httpd_resp_sendstr(req, manager->files[CSS].data_ptr);
}

static esp_err_t redirect_handler(httpd_req_t* req) {
  const char* uri = req->uri;

  ESP_LOGI(TAG, "Requested URL: %s", uri);

  if (strstr(uri, "favicon.ico")) {
    ESP_LOGI(TAG, "Handling favicon.ico");
    return no_content(req);
  }

  if (strstr(uri, "generate_204") || strstr(uri, "gen_204")) {
    ESP_LOGI(TAG, "Handling Android captive portal detection");
    return no_content(req);
  }

  if (strstr(uri, "hotspot-detect")) {
    ESP_LOGI(TAG, "Handling Apple captive portal detection");
    return wifi_handler(req);
  }

  if (strstr(uri, "connecttest.txt")) {
    ESP_LOGI(TAG, "Handling Microsoft captive portal detection");
    return no_content(req);
  }

  ESP_LOGI(TAG, "Defaulting to Wi-Fi page");
  return wifi_handler(req);
}

static void restart_timer_callback(void* arg) {
  esp_restart();
}

static esp_err_t reboot_handler(httpd_req_t* req) {
  httpd_resp_sendstr(req, "Rebooting in 10");

  // Create and start a one-shot timer to restart after 10 seconds
  esp_timer_handle_t restart_timer;
  const esp_timer_create_args_t timer_args = {
    .callback = &restart_timer_callback,
    .name = "restart_timer"
  };

  esp_err_t err = esp_timer_create(&timer_args, &restart_timer);
  if (err != ESP_OK) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create restart timer");
    return err;
  }

  err = esp_timer_start_once(restart_timer, 10 * 1000000); // 10 seconds in microseconds
  if (err != ESP_OK) {
    esp_timer_delete(restart_timer);
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to start restart timer");
    return err;
  }

  web_page_manager_t* manager = req->user_ctx;
  cleanup_web_page_manager(manager);

  esp_vfs_spiffs_unregister("ap_storage");

  return ESP_OK;
}

static esp_err_t wifi_post_handler(httpd_req_t* req) {
  web_page_manager_t* manager = req->user_ctx;
  if (manager == NULL) return ESP_ERR_NOT_FOUND;

  memset(manager->json_buffer, 0, sizeof(manager->json_buffer));
  int r = httpd_req_recv(req, manager->json_buffer, sizeof(manager->json_buffer) - 1);
  if (r <= 0) return ESP_FAIL;
  manager->json_buffer[r] = 0;

  cJSON* root = cJSON_Parse(manager->json_buffer);
  if (!root) {
    send_json_resp(req, 400, "Invalid JSON");
    return ESP_FAIL;
  }

  cJSON* networks = cJSON_GetObjectItem(root, "networks");
  if (!cJSON_IsArray(networks)) {
    cJSON_Delete(root);
    send_json_resp(req, 400, "Expected networks array");
    return ESP_FAIL;
  }

  size_t element_count = cJSON_GetArraySize(networks);
  wifi_settings_t* wifi_settings = (wifi_settings_t*)malloc(sizeof(wifi_settings_t) * element_count);
  if (!wifi_settings) {
    cJSON_Delete(root);
    send_json_resp(req, 500, "Memory allocation failed");
    return ESP_FAIL;
  }

  cJSON* network;
  int element_index = 0;
  cJSON_ArrayForEach(network, networks) {
    cJSON* ssid = cJSON_GetObjectItem(network, "ssid");
    cJSON* pass = cJSON_GetObjectItem(network, "pass");

    if (!cJSON_IsString(ssid) || !cJSON_IsString(pass) ||
      !strlen(ssid->valuestring) || !strlen(pass->valuestring)) {
      cJSON_Delete(root);
      send_json_resp(req, 400, "Invalid network");
      free(wifi_settings);
      return ESP_FAIL;
    }

    strlcpy(wifi_settings[element_index].ssid, ssid->valuestring, sizeof(wifi_settings[element_index].ssid));
    strlcpy(wifi_settings[element_index].password, pass->valuestring, sizeof(wifi_settings[element_index].password));
    element_index++;
  }
  cJSON_Delete(root);

  unit_configuration_t* unit_configuration = unit_config_acquire();
  connectivity_configuration_t* con_config = &(unit_configuration->con_config);
  free(con_config->wifi_settings);
  con_config->wifi_settings = wifi_settings;
  unit_configuration->con_config.wifi_settings_count = element_count;
  unit_config_release();

  send_json_resp(req, 200, "Saved Wi-Fi");

  return ESP_OK;
}

static esp_err_t ota_post_handler(httpd_req_t* req) {
  web_page_manager_t* manager = req->user_ctx;
  if (manager == NULL) return ESP_ERR_NOT_FOUND;

  memset(manager->json_buffer, 0, sizeof(manager->json_buffer));
  int r = httpd_req_recv(req, manager->json_buffer, sizeof(manager->json_buffer) - 1);
  if (r <= 0) return ESP_FAIL;
  manager->json_buffer[r] = 0;

  cJSON* d = cJSON_Parse(manager->json_buffer);
  if (!d) {
    send_json_resp(req, 400, "Invalid data");
    return ESP_FAIL;
  }

  const char* ota_url = cJSON_GetStringValue(cJSON_GetObjectItem(d, "ota_url"));
  const char* version_url = cJSON_GetStringValue(cJSON_GetObjectItem(d, "version_url"));

  unit_configuration_t* config = unit_config_acquire();

  if (ota_url) {
    strlcpy(config->con_config.ota_url, ota_url, MAX_URL_LENGTH);
  }

  if (version_url) {
    strlcpy(config->con_config.version_url, version_url, MAX_URL_LENGTH);
  }

  unit_config_release(config);

  cJSON_Delete(d);
  send_json_resp(req, 200, "OTA configuration saved");
  struct nvs_manager* nvs_manager = get_nvs_manager();
  nvs_manager_request_state(nvs_manager, NVS_STATE_WRITE_REQUEST);
  managers_release();
  return ESP_OK;
}

static esp_err_t sys_post_handler(httpd_req_t* req) {
  web_page_manager_t* manager = req->user_ctx;
  if (manager == NULL) return ESP_ERR_NOT_FOUND;

  memset(manager->json_buffer, 0, sizeof(manager->json_buffer));
  int r = httpd_req_recv(req, manager->json_buffer, sizeof(manager->json_buffer) - 1);
  if (r <= 0) {
    send_json_resp(req, 400, "Receive error");
    return ESP_FAIL;
  }
  manager->json_buffer[r] = 0;

  cJSON* d = cJSON_Parse(manager->json_buffer);
  if (!d) {
    send_json_resp(req, 400, "Invalid JSON");
    return ESP_FAIL;
  }

  esp_err_t ret = ESP_OK;
  char* log_level_ptr = NULL;
  cJSON* logLevel_item = cJSON_GetObjectItem(d, "logLevel");

  if (cJSON_IsString(logLevel_item) && logLevel_item->valuestring != NULL) {
    log_level_ptr = logLevel_item->valuestring;
    size_t len = strlen(log_level_ptr);

    if (len > 15) {
      send_json_resp(req, 400, "Invalid Log Level too long");
      ret = ESP_FAIL;
      goto cleanup;
    }

    unit_configuration_t* config = unit_config_acquire();
    if (!config) {
      send_json_resp(req, 500, "Config lock failed");
      ret = ESP_FAIL;
      goto cleanup;
    }

    system_settings_configuration_t* sys_conf = &(config->sys_config);
    if (strcmp(log_level_ptr, "ESP_LOG_NONE") == 0) {
      sys_conf->log_level = ESP_LOG_NONE;
    } else if (strcmp(log_level_ptr, "ESP_LOG_ERROR") == 0) {
      sys_conf->log_level = ESP_LOG_ERROR;
    } else if (strcmp(log_level_ptr, "ESP_LOG_WARN") == 0) {
      sys_conf->log_level = ESP_LOG_WARN;
    } else if (strcmp(log_level_ptr, "ESP_LOG_INFO") == 0) {
      sys_conf->log_level = ESP_LOG_INFO;
    } else if (strcmp(log_level_ptr, "ESP_LOG_DEBUG") == 0) {
      sys_conf->log_level = ESP_LOG_DEBUG;
    } else if (strcmp(log_level_ptr, "ESP_LOG_VERBOSE") == 0) {
      sys_conf->log_level = ESP_LOG_VERBOSE;
    } else {
      ESP_LOGW(TAG, "Could not parse log level %s", log_level_ptr);
    }

    esp_log_level_set("*", sys_conf->log_level);
    unit_config_release();
    struct nvs_manager* nvs_manager = get_nvs_manager();
    nvs_manager_request_state(nvs_manager, NVS_STATE_WRITE_REQUEST);
    managers_release();
  }

  send_json_resp(req, 200, log_level_ptr ? "Saved Sys Settings" : "No changes");

cleanup:
  cJSON_Delete(d);
  return ret;
}

static esp_err_t user_post_handler(httpd_req_t* req) {
  web_page_manager_t* manager = req->user_ctx;
  if (manager == NULL) return ESP_ERR_NOT_FOUND;

  memset(manager->json_buffer, 0, sizeof(manager->json_buffer));
  int r = httpd_req_recv(req, manager->json_buffer, sizeof(manager->json_buffer) - 1);
  if (r <= 0) {
    send_json_resp(req, 400, "Receive error");
    return ESP_FAIL;
  }
  manager->json_buffer[r] = 0;

  cJSON* d = cJSON_Parse(manager->json_buffer);
  if (!d) {
    send_json_resp(req, 400, "Invalid JSON");
    return ESP_FAIL;
  }

  esp_err_t ret = ESP_OK;
  char* unit_name = NULL;
  cJSON* name_item = cJSON_GetObjectItem(d, "unit_name");

  if (cJSON_IsString(name_item) && name_item->valuestring != NULL) {
    unit_name = name_item->valuestring;
    size_t len = strlen(unit_name);

    if (len > CONFIG_MAX_ESP_NAME_LEN) {
      send_json_resp(req, 400, "Name too long");
      ret = ESP_FAIL;
      goto cleanup;
    }

    unit_configuration_t* config = unit_config_acquire();
    if (!config) {
      send_json_resp(req, 500, "Config lock failed");
      ret = ESP_FAIL;
      goto cleanup;
    }

    char* new_name = realloc(config->user_config.unit_name, len + 1);
    if (!new_name) {
      unit_config_release();
      send_json_resp(req, 500, "Memory error");
      ret = ESP_FAIL;
      goto cleanup;
    }

    config->user_config.unit_name = new_name;
    strlcpy(config->user_config.unit_name, unit_name, len + 1);
    config->user_config.unit_name_len = len;

    unit_config_release();
    struct nvs_manager* nvs_manager = get_nvs_manager();
    nvs_manager_request_state(nvs_manager, NVS_STATE_WRITE_REQUEST);
    managers_release();
  }

  send_json_resp(req, 200, unit_name ? "Saved User" : "No changes");

cleanup:
  cJSON_Delete(d);
  return ret;
}

static esp_err_t cleanup_web_page_manager(web_page_manager_t* const manager) {
  if (manager == NULL) return ESP_ERR_NOT_FOUND;
  esp_err_t ret = ESP_OK;

  const size_t N_HANDLERS = sizeof(manager->handlers) / sizeof(manager->handlers[0]);

  for (size_t i = 0; i < N_HANDLERS; i++) {
    ret = httpd_unregister_uri_handler(manager->server, manager->handlers[i].uri, manager->handlers[i].method);
    if (ret != ESP_OK) return ret;
  }

  ret = httpd_stop(manager->server);
  if (ret != ESP_OK) return ret;

  for (resource_t i = 0; i < CONFIG_TYPE_COUNT; i++) {
    if (manager->files[i].data_ptr != NULL) {
      free((void*)manager->files[i].data_ptr);
      manager->files[i].data_ptr = NULL;
    }
  }

  memset(manager->json_buffer, 0, sizeof(manager->json_buffer));

  return ret;
}
