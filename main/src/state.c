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
static SemaphoreHandle_t state_mutex = NULL;
static SemaphoreHandle_t managers_mutex = NULL;
EventGroupHandle_t system_event_group = NULL;

static void init_spiffs() { // # TODO, relocate?
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
    // Create the state_mutex
    state_mutex = xSemaphoreCreateMutex();
    if (state_mutex == NULL) {
      ESP_LOGE(TAG, "Failed to create state_mutex");
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

    // Create the state_mutex
    managers_mutex = xSemaphoreCreateMutex();
    if (managers_mutex == NULL) {
      ESP_LOGE(TAG, "Failed to create managers_mutex");
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

  // Wait indefinitely for the state_mutex
  xSemaphoreTake(state_mutex, portMAX_DELAY);
  return shared_data;
}

void set_nvs_manager(nvs_manager_t* const nvs_manager) {
  if (shared_managers == NULL)
    ESP_LOGE(TAG, "Shared-Managers struct not initialized");
  xSemaphoreTake(managers_mutex, portMAX_DELAY);

  shared_managers->nvs_manager = nvs_manager;

  xSemaphoreGive(managers_mutex);
}

void set_wifi_manager(wifi_manager_t* const wifi_manager) {
  if (shared_managers == NULL)
    ESP_LOGE(TAG, "Shared-Managers struct not initialized");
  xSemaphoreTake(managers_mutex, portMAX_DELAY);

  shared_managers->wifi_manager = wifi_manager;

  xSemaphoreGive(managers_mutex);
}

void set_web_page_manager(web_page_manager_t* const web_page_manager) {
  if (shared_managers == NULL)
    ESP_LOGE(TAG, "Shared-Managers struct not initialized");
  xSemaphoreTake(managers_mutex, portMAX_DELAY);

  shared_managers->web_page_manager = web_page_manager;

  xSemaphoreGive(managers_mutex);
}

nvs_manager_t* get_nvs_manager() {
  if (shared_managers == NULL) {
    ESP_LOGE(TAG, "Shared-Managers struct not initialized");
    return NULL;
  }

  xSemaphoreTake(managers_mutex, portMAX_DELAY);
  return shared_managers->nvs_manager;
}

wifi_manager_t* get_wifi_manager() {
  if (shared_managers == NULL) {
    ESP_LOGE(TAG, "Shared-Managers struct not initialized");
    return NULL;
  }

  xSemaphoreTake(managers_mutex, portMAX_DELAY);
  return shared_managers->wifi_manager;
}

web_page_manager_t* get_web_page_manager() {
  if (shared_managers == NULL) {
    ESP_LOGE(TAG, "Shared-Managers struct not initialized");
    return NULL;
  }

  xSemaphoreTake(managers_mutex, portMAX_DELAY);
  return shared_managers->web_page_manager;
}

void unit_config_release() {
  if (state_mutex != NULL) {
    xSemaphoreGive(state_mutex);
  }
}

void managers_release() {
  if (managers_mutex != NULL) {
    xSemaphoreGive(managers_mutex);
  }
}

void unit_config_cleanup() {
  if (shared_data != NULL) {
    free(shared_data);
    shared_data = NULL;
  }
  if (state_mutex != NULL) {
    vSemaphoreDelete(state_mutex);
    state_mutex = NULL;
  }
  if (managers_mutex != NULL) {
    vSemaphoreDelete(managers_mutex);
    managers_mutex = NULL;
  }
}
