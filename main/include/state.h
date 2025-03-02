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

#ifndef STATE_H
#define STATE_H

#include "freertos/FreeRTOS.h"

#include "configuration.h"

// Shared group event handler

typedef struct nvs_manager nvs_manager_t;
typedef struct wifi_manager wifi_manager_t;
typedef struct web_page_manager web_page_manager_t;

typedef struct {
    nvs_manager_t* nvs_manager;
    wifi_manager_t* wifi_manager;
    web_page_manager_t* web_page_manager;
} managers_t;

// Initialize the singleton (called once)
void unit_config_init();

// Acquire the singleton (blocks if another task is using it)
unit_configuration_t* unit_config_acquire();

void set_nvs_manager(nvs_manager_t* nvs_manager);

void set_wifi_manager(wifi_manager_t* wifi_manager);

void set_web_page_manager(web_page_manager_t* web_page_manager);

nvs_manager_t* get_nvs_manager();

wifi_manager_t* get_wifi_manager();

web_page_manager_t* get_web_page_manager();

// Release the singleton (must be called after acquire)
void unit_config_release();

void managers_release();

// Clean up the singleton (optional)
void unit_config_cleanup();

#endif // STATE_H
