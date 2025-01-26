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

#include "ap_mode_web_pages.h"

#include "configuration.h"

#include <cJSON.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <state.h>

static char* TAG = "AP Mode Web Pages";

static void send_json_resp(httpd_req_t* req, int code, const char* msg);
static void send_nav(httpd_req_t* req, const char* active);

static esp_err_t wifi_handler(httpd_req_t* req);
static esp_err_t wifi_post_handler(httpd_req_t* req);
static esp_err_t ota_handler(httpd_req_t* req);
static esp_err_t ota_post_handler(httpd_req_t* req);
static esp_err_t user_handler(httpd_req_t* req);
static esp_err_t user_post_handler(httpd_req_t* req);
static esp_err_t reboot_handler(httpd_req_t* req);
static esp_err_t generate_204_handler(httpd_req_t* req);

// Updated Common Styles
static const char* COMMON_STYLES =
  "<meta name='viewport' content='width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no'>"
  "<style>"
  "body{margin:0;padding-top:44px;font-family:-apple-system,sans-serif}"
  ".nav{position:fixed;top:0;left:0;right:0;background:#333;display:flex;height:44px}"
  ".nav a{flex:1;color:#fff;text-decoration:none;display:flex;align-items:center;justify-content:center;"
  "font-size:14px;border-right:1px solid #444}"
  ".nav a:last-child{border-right:none}"
  ".nav a.active{background:#666}"
  ".container{padding:15px;width:100%;box-sizing:border-box}"
  "h1{font-size:20px;margin:0 0 15px 0;color:#333}"
  "input{width:100%;height:40px;margin:8px 0;padding:0 12px;border:1px solid #ddd;border-radius:6px;box-sizing:border-box}"
  "button{width:100%;height:40px;margin:8px 0;background:#007aff;color:#fff;border:none;border-radius:6px;font-size:16px}"
  ".network{margin:15px 0;padding:10px;background:#f8f8f8;border-radius:6px}"
  "@media (min-width:600px){.container{width:600px;margin:0 auto}}"
  "@media (max-width:374px){body{font-size:14px}h1{font-size:18px}.nav a{font-size:12px}}"
  ".footer {position: fixed; bottom: 0; left: 0; right: 0; padding: 10px; background: #fff; border-top: 1px solid #ddd}"
  ".reboot-btn {background: #dc3545 !important; margin-top: 15px}"
  "</style>";

static const char* footer =
  "<div class='footer'>"
  "<button class='reboot-btn' onclick='rebootDevice()'>REBOOT DEVICE</button>"
  "</div>"
  "<script>"
  "function rebootDevice() {"
  "  if(confirm('Are you sure you want to reboot?')) {"
  "    fetch('/reboot', {method: 'POST'})"
  "      .then(() => { alert('Rebooting...'); })"
  "      .catch(() => {})"
  "  }"
  "}"
  "</script>";

static void send_nav(httpd_req_t* req, const char* active) {
  const char* nav_fmt =
    "<div class='nav'>"
    "<a href='/wifi' class='%s'>WiFi</a>"
    "<a href='/ota' class='%s'>OTA</a>"
    "<a href='/usercfg' class='%s'>User</a>"
    "</div>";

  char nav[256];
  snprintf(nav, sizeof(nav), nav_fmt,
           strcmp(active, "wifi") ? "" : "active",
           strcmp(active, "ota") ? "" : "active",
           strcmp(active, "usercfg") ? "" : "active");

  httpd_resp_sendstr_chunk(req, nav);
}

static void send_json_resp(httpd_req_t* req, int code, const char* msg) {
  cJSON* r = cJSON_CreateObject();
  cJSON_AddNumberToObject(r, "c", code);
  cJSON_AddStringToObject(r, "m", msg);
  const char* s = cJSON_PrintUnformatted(r);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, s);
  cJSON_Delete(r);
  free((void*)s);
}

static void html_escape(const char* input, char* output, size_t max_len) {
  const char* src = input;
  char* dest = output;
  while (*src && (size_t)(dest - output) < max_len - 1) {
    switch (*src) {
      case '&':
        strcpy(dest, "&amp;");
        dest += 5;
        break;
      case '<':
        strcpy(dest, "&lt;");
        dest += 4;
        break;
      case '>':
        strcpy(dest, "&gt;");
        dest += 4;
        break;
      case '"':
        strcpy(dest, "&quot;");
        dest += 6;
        break;
      case '\'':
        strcpy(dest, "&#39;");
        dest += 5;
        break;
      default:
        *dest++ = *src;
        break;
    }
    src++;
  }
  *dest = '\0';
}

static esp_err_t wifi_handler(httpd_req_t* req) {
#define STATIC_HTML_LEN 168  // Static parts length (count exactly)
#define MAX_SSID_ESCAPED (MAX_SSID_LENGTH * 6)   // Worst-case escape
#define MAX_PASS_ESCAPED (MAX_PASSWORD_LENGTH * 6)
#define NETWORK_HTML_BUF_SIZE (STATIC_HTML_LEN + MAX_SSID_ESCAPED + MAX_PASS_ESCAPED + 1)

  httpd_resp_set_type(req, "text/html");
  httpd_resp_sendstr_chunk(req, "<!DOCTYPE html><html><head>");
  httpd_resp_sendstr_chunk(req, COMMON_STYLES);
  httpd_resp_sendstr_chunk(req, "</head><body>");
  send_nav(req, "wifi");
  httpd_resp_sendstr_chunk(req, "<div class='container'><h1>Wi-Fi Configuration</h1><div id='networks'>");

  // Acquire configuration
  unit_configuration_t* config = unit_config_acquire();
  if (config) {
    connectivity_configuration_t* con_cfg = &config->con_config;

    for (size_t i = 0; i < con_cfg->wifi_configs_count; i++) {
      char escaped_ssid[MAX_SSID_LENGTH * 6] = {0};
      char escaped_pass[MAX_PASSWORD_LENGTH * 6] = {0};
      wifi_settings_t* ws = &con_cfg->wifi_settings[i];

      html_escape(ws->ssid, escaped_ssid, sizeof(escaped_ssid));
      html_escape(ws->password, escaped_pass, sizeof(escaped_pass));

      char network_html[NETWORK_HTML_BUF_SIZE];
      snprintf(network_html, sizeof(network_html),
               "<div class='network'>"
               "<input name='ssid' placeholder='WiFi Network' value='%s' required>"
               "<input type='password' name='pass' placeholder='Password' value='%s' required>"
               "</div>",
               escaped_ssid, escaped_pass);

      httpd_resp_sendstr_chunk(req, network_html);
    }
    unit_config_release();
  }

  if (!config || config->con_config.wifi_configs_count == 0) {
    httpd_resp_sendstr_chunk(req,
                             "<div class='network'>"
                             "<input name='ssid' placeholder='WiFi Network' required>"
                             "<input type='password' name='pass' placeholder='Password' required>"
                             "</div>");
  }

  httpd_resp_sendstr_chunk(req, "</div>"); // Close networks div

  const char* buttons_script =
    "<button onclick='addNetwork()'>Add Another Network</button>"
    "<button onclick='save()'>Save Settings</button>"
    "<script>"
    "function addNetwork(){"
    "let n=document.createElement('div');"
    "n.className='network';"
    "n.innerHTML=`<input name='ssid' placeholder='WiFi Network' required>"
    "<input type='password' name='pass' placeholder='Password' required>`;"
    "document.getElementById('networks').appendChild(n)}"
    "function save(){"
    "const networks=Array.from(document.querySelectorAll('.network')).map(e=>({"
    "ssid:e.querySelector('input[name=ssid]').value,"
    "pass:e.querySelector('input[name=pass]').value"
    "}));"
    "fetch('/wifi',{"
    "method:'POST',"
    "headers:{'Content-Type':'application/json'},"
    "body:JSON.stringify({networks})"
    "})"
    ".then(r=>r.json())"
    ".then(d=>alert(d.m))"
    ".catch(e=>alert('Error: '+e))}"
    "</script>"
    "</div>";

  httpd_resp_sendstr_chunk(req, buttons_script);
  httpd_resp_sendstr_chunk(req, footer);
  return httpd_resp_sendstr_chunk(req, NULL);
}

static esp_err_t wifi_post_handler(httpd_req_t* req) {
  char buf[1024];
  int r = httpd_req_recv(req, buf, sizeof(buf) - 1);
  if (r <= 0) return ESP_FAIL;
  buf[r] = 0;

  cJSON* root = cJSON_Parse(buf);
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
  unit_configuration->con_config.wifi_configs_count = element_count;
  unit_config_release();

  send_json_resp(req, 200, "Saved WiFi");
  xEventGroupSetBits(system_event_group, NVS_CONFIG_WRITE_REQUEST);

  return ESP_OK;
}

static esp_err_t ota_handler(httpd_req_t* req) {
  httpd_resp_set_type(req, "text/html");
  httpd_resp_sendstr_chunk(req, "<!DOCTYPE html><html><head>");
  httpd_resp_sendstr_chunk(req, COMMON_STYLES);
  httpd_resp_sendstr_chunk(req, "</head><body>");
  send_nav(req, "ota");

  httpd_resp_sendstr_chunk(req, "<div class='container'>");
  httpd_resp_sendstr_chunk(req, "<h1>OTA Updates</h1>");

  unit_configuration_t* config = unit_config_acquire();
  char ota_url[MAX_URL_LENGTH];
  char version_url[MAX_URL_LENGTH];
  strncpy(ota_url, config->con_config.ota_url, MAX_URL_LENGTH);
  strncpy(version_url, config->con_config.version_url, MAX_URL_LENGTH);
  unit_config_release(config);

  ota_url[MAX_URL_LENGTH - 1] = '\0';
  version_url[MAX_URL_LENGTH - 1] = '\0';

  char input_buffer[512];

  // OTA URL input
  snprintf(input_buffer, sizeof(input_buffer),
           "<input id='ota_url' type='url' placeholder='OTA Firmware URL' value='%s'>",
           ota_url);
  httpd_resp_sendstr_chunk(req, input_buffer);

  // Version URL input
  snprintf(input_buffer, sizeof(input_buffer),
           "<input id='version_url' type='url' placeholder='Version Check URL' value='%s'>",
           version_url);
  httpd_resp_sendstr_chunk(req, input_buffer);

  httpd_resp_sendstr_chunk(req,
                           "<button onclick='save()'>Save OTA Settings</button>"
                           "<script>"
                           "function save(){"
                           "let o=document.getElementById('ota_url').value,"
                           "v=document.getElementById('version_url').value;"
                           "fetch('/ota',{method:'POST',body:JSON.stringify({ota_url:o,version_url:v})})"
                           ".then(r=>r.json()).then(d=>alert(d.m))}"
                           "</script>"
                           "</div>");

  httpd_resp_sendstr_chunk(req, footer);
  httpd_resp_sendstr_chunk(req, "</body></html>");

  return httpd_resp_sendstr_chunk(req, NULL);
}

static esp_err_t ota_post_handler(httpd_req_t* req) {
  char buf[512];
  int r = httpd_req_recv(req, buf, sizeof(buf) - 1);
  if (r <= 0) return ESP_FAIL;
  buf[r] = 0;

  cJSON* d = cJSON_Parse(buf);
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
  xEventGroupSetBits(system_event_group, NVS_CONFIG_WRITE_REQUEST);
  return ESP_OK;
}

static esp_err_t user_handler(httpd_req_t* req) {
  httpd_resp_set_type(req, "text/html");
  httpd_resp_sendstr_chunk(req, "<!DOCTYPE html><html><head>");
  httpd_resp_sendstr_chunk(req, COMMON_STYLES);
  httpd_resp_sendstr_chunk(req, "</head><body>");
  send_nav(req, "usercfg");

  httpd_resp_sendstr_chunk(req, "<div class='container'>");
  httpd_resp_sendstr_chunk(req, "<h1>Device Configuration</h1>");

  unit_configuration_t* config = unit_config_acquire();
  char unit_name[256] = "";
  if (config->user_config.unit_name) {
    strlcpy(unit_name, config->user_config.unit_name, sizeof(unit_name));
  }
  unit_config_release(config);

  char input_buffer[512];
  snprintf(input_buffer, sizeof(input_buffer),
           "<input id='unit_name' placeholder='Device Name (optional)' value='%s'>",
           unit_name);
  httpd_resp_sendstr_chunk(req, input_buffer);

  httpd_resp_sendstr_chunk(req,
    "<button onclick='save()'>Save Settings</button>"
    "<script>"
    "function save(){"
    "let n=document.getElementById('unit_name').value;"
    "fetch('/usercfg',{method:'POST',body:JSON.stringify({unit_name:n})})"
    ".then(r=>r.json()).then(d=>alert(d.m))}"
    "</script>"
    "</div>");

  httpd_resp_sendstr_chunk(req, footer);
  httpd_resp_sendstr_chunk(req, "</body></html>");

  return httpd_resp_sendstr_chunk(req, NULL);
}

static esp_err_t user_post_handler(httpd_req_t* req) {
  char buf[256];
  int r = httpd_req_recv(req, buf, sizeof(buf) - 1);
  if (r <= 0) {
    send_json_resp(req, 400, "Receive error");
    return ESP_FAIL;
  }
  buf[r] = 0;

  cJSON* d = cJSON_Parse(buf);
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
    xEventGroupSetBits(system_event_group, NVS_CONFIG_WRITE_REQUEST);
  }

  send_json_resp(req, 200, unit_name ? "Saved User" : "No changes");

cleanup:
  cJSON_Delete(d);
  return ret;
}

static esp_err_t reboot_handler(httpd_req_t* req) {
  xEventGroupSetBits(system_event_group, REBOOTING);
  httpd_resp_sendstr(req, "Rebooting in 10");

  vTaskDelay(pdMS_TO_TICKS(10000 / portTICK_PERIOD_MS));
  esp_restart();

  return ESP_OK;
}

static esp_err_t generate_204_handler(httpd_req_t* req) {
  httpd_resp_set_status(req, "204 No Content");
  httpd_resp_send(req, NULL, 0);
  return ESP_OK;
}

void init_fail_mode_web_page() {
  static bool already_running = false; // ESP can't exit this state unless rebooted
  if (already_running)
    return;

  httpd_handle_t server;
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();

  httpd_start(&server, &cfg);

  httpd_uri_t wifi = {.uri = "/wifi", .method = HTTP_GET, .handler = wifi_handler};
  httpd_uri_t ota = {.uri = "/ota", .method = HTTP_GET, .handler = ota_handler};
  httpd_uri_t user = {.uri = "/usercfg", .method = HTTP_GET, .handler = user_handler};
  httpd_uri_t reboot = {.uri = "/reboot", .method = HTTP_POST, .handler = reboot_handler};
  httpd_uri_t generate_204_uri = {.uri = "/generate_204", .method = HTTP_GET, .handler = generate_204_handler,
                                  .user_ctx = NULL};

  httpd_uri_t wifi_post = {.uri = "/wifi", .method = HTTP_POST, .handler = wifi_post_handler};
  httpd_uri_t ota_post = {.uri = "/ota", .method = HTTP_POST, .handler = ota_post_handler};
  httpd_uri_t user_post = {.uri = "/usercfg", .method = HTTP_POST, .handler = user_post_handler};

  httpd_register_uri_handler(server, &wifi);
  httpd_register_uri_handler(server, &wifi);
  httpd_register_uri_handler(server, &ota);
  httpd_register_uri_handler(server, &user);
  httpd_register_uri_handler(server, &reboot);
  httpd_register_uri_handler(server, &generate_204_uri);

  httpd_register_uri_handler(server, &wifi_post);
  httpd_register_uri_handler(server, &ota_post);
  httpd_register_uri_handler(server, &user_post);

  already_running = true;

  ESP_LOGI(TAG, "Serving Web-Pages");
  vTaskDelete(NULL);
}
