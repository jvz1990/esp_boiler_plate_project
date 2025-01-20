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

static const char* TAG = "STATE";

static unit_configuration_t* shared_data = NULL;
static SemaphoreHandle_t mutex = NULL;
EventGroupHandle_t system_event_group = NULL;

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
}

unit_configuration_t* unit_config_acquire() {
  if (shared_data == NULL) {
    ESP_LOGE(TAG, " Shared-Data struct not initialized");
    return NULL;
  }

  // Wait indefinitely for the mutex
  xSemaphoreTake(mutex, portMAX_DELAY);
  return shared_data;
}

void unit_config_release() {
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
  bool is_connected = false;

  const unit_configuration_t* shared_data = unit_config_acquire();
  is_connected = shared_data->wifi_connected;
  unit_config_release();

  return is_connected;
}
