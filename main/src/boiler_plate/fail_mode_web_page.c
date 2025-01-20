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

#include "fail_mode_web_page.h"

#include "configuration.h"

#include <cJSON.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <state.h>

#define MAX_RESPONSE_SIZE 1024 // This will depend on your config size, EG, how many WiFi Access Points

static char* TAG = "Fail Mode form";

static void parse_form_data(const char* data, httpd_req_t* req);
static esp_err_t form_handler(httpd_req_t* req);
static esp_err_t get_handler(httpd_req_t* req);

// Note; since this is THE fail-mode, error handling is disregarded. If errors occur here, a manual flash is required.

static void parse_form_data(const char* data, httpd_req_t* req) {
  // In form of:
  // {"networks":[{"ssid":"value","password":"value"}],"ota_url":"value","version_url":"value"}
  cJSON* root = cJSON_Parse(data);
  if (!root) {
    ESP_LOGE(TAG, "Error parsing JSON");
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
    return;
  }

  // Acquire shared data
  unit_configuration_t *unit_configuration = unit_config_acquire();
  connectivity_configuration_t *con_config = &(unit_configuration->con_config);
  // Extract networks array
  const cJSON* networks = cJSON_GetObjectItem(root, "networks");
  if (cJSON_IsArray(networks)) {
    const size_t wifi_count = cJSON_GetArraySize(networks);

    // Dynamically allocate memory for wifi_settings based on the count of networks
    wifi_settings_t* tmp_ptr = realloc(con_config->wifi_settings, wifi_count * sizeof(wifi_settings_t));
    if (tmp_ptr == NULL) {
      ESP_LOGE(TAG, "Failed to allocate wifi settings");
      return;
    }
    con_config->wifi_settings = tmp_ptr;
    con_config->wifi_configs_count = wifi_count;

    if (con_config->wifi_settings == NULL) {
      ESP_LOGE(TAG, "Memory allocation failed for wifi_settings");
      unit_config_release();
      cJSON_Delete(root);

      const char response[] = "JSON failed!";
      httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
      return;
    }

    // Iterate through each network in the JSON array
    const cJSON* network = NULL;
    for (size_t i = 0; i < wifi_count; i++) {
      network = cJSON_GetArrayItem(networks, (int)i);
      const cJSON* ssid = cJSON_GetObjectItem(network, "ssid");
      const cJSON* password = cJSON_GetObjectItem(network, "password");

      // Validate the SSID and Password
      if (cJSON_IsString(ssid) && cJSON_IsString(password)) {
        // Update wifi_settings with parsed SSID and Password
        strlcpy(con_config->wifi_settings[i].ssid, ssid->valuestring, sizeof(con_config->wifi_settings[i].ssid));
        strlcpy(con_config->wifi_settings[i].password, password->valuestring, sizeof(con_config->wifi_settings[i].password));

        ESP_LOGI(TAG, "SSID: %s, Password: %s", con_config->wifi_settings[i].ssid, con_config->wifi_settings[i].password);
      } else {
        ESP_LOGW(TAG, "Invalid SSID or Password format for network %d", (int)i);
      }
    }
  }

  // Extract other fields
  const cJSON* ota_url = cJSON_GetObjectItem(root, "ota_url");
  const cJSON* version_url = cJSON_GetObjectItem(root, "version_url");

  if (cJSON_IsString(ota_url)) {
    ESP_LOGI(TAG, "OTA URL: %s", ota_url->valuestring);
    strlcpy(con_config->ota_url, ota_url->valuestring, sizeof(con_config->ota_url));
  }
  if (cJSON_IsString(version_url)) {
    ESP_LOGI(TAG, "Version URL: %s", version_url->valuestring);
    strlcpy(con_config->version_url, version_url->valuestring, sizeof(con_config->version_url));
  }

  // Release data
  unit_config_release();

  // Clean up
  cJSON_Delete(root);

  // Send response back to the client
  const char response[] = "JSON parsed successfully!";
  httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);

  xEventGroupSetBits(system_event_group, NVS_CONFIG_WRITE_REQUEST);
  ESP_LOGW(TAG, "System rebooting in 10 seconds");
  vTaskDelay(10000 / portTICK_PERIOD_MS);
  esp_restart();
}

static esp_err_t form_handler(httpd_req_t* req) {
  char content[MAX_RESPONSE_SIZE];
  const int recv_len = httpd_req_recv(req, content, sizeof(content) - 1);
  if (recv_len <= 0) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive form data");
    return ESP_FAIL;
  }

  content[recv_len] = '\0'; // Null-terminate the received data
  ESP_LOGI(TAG, "Received form data: %s", content);

  // Parse the form data
  parse_form_data(content, req);

  return ESP_OK;
}

static esp_err_t get_handler(httpd_req_t* req) {
  esp_err_t err = ESP_OK;
  // Replace here if you would like
  const char response[] =
    "<!DOCTYPE html><html><head><meta name=\"viewport\" "
    "content=\"width=device-width,initial-scale=1\"></head><body><form id=\"f\" onsubmit=\"h(event)\"><div "
    "id=\"n\"><div><b>Net 1</b><br><input name=\"s[]\" placeholder=\"SSID\" required><input name=\"p[]\" "
    "placeholder=\"Pass\" required><br></div></div><button type=\"button\" onclick=\"a()\">+</button><br><input "
    "name=\"o\" placeholder=\"OTA URL\" required><br><input name=\"v\" placeholder=\"Ver URL\" "
    "required><br><button type=\"submit\">Save</button></form><script>let c=1;function a(){c++;let t='<div><b>Net "
    "'+(c)+'</b><br><input name=\"s[]\" placeholder=\"SSID\" required><input name=\"p[]\" placeholder=\"Pass\" "
    "required><br></div>';document.getElementById('n').insertAdjacentHTML('beforeend',t)}function "
    "h(e){e.preventDefault();let f=new "
    "FormData(e.target),s=f.getAll('s[]'),p=f.getAll('p[]'),d={networks:s.map((s,i)=>({ssid:s,password:p[i]})),ota_"
    "url:f.get('o'),version_url:f.get('v')};fetch('/submit',{method:'POST',headers:{'Content-Type':'application/"
    "json'},body:JSON.stringify(d)})}</script></body></html>";
  err = httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
  ESP_LOGE(TAG, "Error: %s", esp_err_to_name(err));
  return err;
}

void init_fail_mode_web_page() {
  static bool already_running = false; // ESP can't exit this state unless rebooted
  if (already_running)
    return;

  const httpd_config_t config = HTTPD_DEFAULT_CONFIG();

  httpd_handle_t server = NULL;
  if (httpd_start(&server, &config) == ESP_OK) {
    // Register GET handler for the form page
    const httpd_uri_t root_uri = {.uri = "/", .method = HTTP_GET, .handler = get_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &root_uri);

    // Register POST handler for form submission
    const httpd_uri_t form_uri = {.uri = "/submit", .method = HTTP_POST, .handler = form_handler, .user_ctx = NULL};
    httpd_register_uri_handler(server, &form_uri);
  }

  already_running = true;
  vTaskDelete(NULL);
}
