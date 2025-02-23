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

#include "state.h"

#include <esp_log.h>
#include <esp_partition.h>
#include <esp_spiffs.h>
#include <sys/dirent.h>

static const char* TAG = "STATE";

static unit_configuration_t* shared_data = NULL;
static managers_t* shared_managers = NULL;
static SemaphoreHandle_t mutex = NULL;
EventGroupHandle_t system_event_group = NULL;

static void init_spiffs() {
  esp_vfs_spiffs_conf_t conf = {
    .base_path = "/spiffs",
    .partition_label = "ap_storage",
    .max_files = 10, // Adjust as needed
    .format_if_mount_failed = true
  };

  esp_err_t ret = esp_vfs_spiffs_register(&conf);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to mount SPIFFS (%s)", esp_err_to_name(ret));
  }

  size_t total = 0, used = 0;
  ret = esp_spiffs_info("ap_storage", &total, &used);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
  } else {
    ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
  }

  DIR* dir = opendir("/spiffs");
  if (dir != NULL) {
    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
      ESP_LOGI(TAG, "Found file: %s", ent->d_name);
    }
    closedir(dir);
  } else {
    ESP_LOGE(TAG, "Failed to open directory");
  }
}

void unit_config_init() {
  if (shared_data == NULL) {
    // Create the mutex
    mutex = xSemaphoreCreateMutex();
    if (mutex == NULL) {
      ESP_LOGE(TAG, "Failed to create mutex");
      abort();
    }

    // Allocate and initialize the struct
    shared_data = (unit_configuration_t*)calloc(1, sizeof(unit_configuration_t));
    if (shared_data == NULL) {
      ESP_LOGE(TAG, "Failed to allocate memory for Shared-Data struct");
      abort();
    }
  }

  if (shared_managers == NULL) {
    shared_managers = (managers_t*)calloc(1, sizeof(managers_t));
    if (shared_managers == NULL) {
      ESP_LOGE(TAG, "Failed to allocate memory for managers_t");
      abort();
    }
  }

  init_spiffs();
}

unit_configuration_t* unit_config_acquire() {
  if (shared_data == NULL) {
    ESP_LOGE(TAG, "Shared-Data struct not initialized");
    return NULL;
  }

  // Wait indefinitely for the mutex
  xSemaphoreTake(mutex, portMAX_DELAY);
  return shared_data;
}

managers_t* managers_acquire() {
  if (shared_managers == NULL) {
    ESP_LOGE(TAG, "Shared-Managers struct not initialized");
    return NULL;
  }

  xSemaphoreTake(mutex, portMAX_DELAY);
  return shared_managers;
}

void set_nvs_manager(nvs_manager_t* const nvs_manager) {
  if (shared_managers == NULL)
    ESP_LOGE(TAG, "Shared-Managers struct not initialized");
  xSemaphoreTake(mutex, portMAX_DELAY);

  shared_managers->nvs_manager = nvs_manager;

  xSemaphoreGive(mutex);
}

void set_wifi_manager(wifi_manager_t* const wifi_manager) {
  if (shared_managers == NULL)
    ESP_LOGE(TAG, "Shared-Managers struct not initialized");
  xSemaphoreTake(mutex, portMAX_DELAY);

  shared_managers->wifi_manager = wifi_manager;

  xSemaphoreGive(mutex);
}

void set_web_page_manager(web_page_manager_t* const web_page_manager) {
  if (shared_managers == NULL)
    ESP_LOGE(TAG, "Shared-Managers struct not initialized");
  xSemaphoreTake(mutex, portMAX_DELAY);

  shared_managers->web_page_manager = web_page_manager;

  xSemaphoreGive(mutex);
}

void unit_config_release() {
  if (mutex != NULL) {
    xSemaphoreGive(mutex);
  }
}

void managers_release() {
  if (mutex != NULL) {
    xSemaphoreGive(mutex);
  }
}

void unit_config_cleanup() {
  if (shared_data != NULL) {
    free(shared_data);
    shared_data = NULL;
  }
  if (mutex != NULL) {
    vSemaphoreDelete(mutex);
    mutex = NULL;
  }
}

bool is_wifi_connected() {
  const EventBits_t bits = xEventGroupGetBits(system_event_group);
  return bits & WIFI_CONNECTED_BIT;
}
