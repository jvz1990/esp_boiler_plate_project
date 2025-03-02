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

#ifndef NVS_MANAGER_H
#define NVS_MANAGER_H

#include <esp_err.h>
#include <esp_bit_defs.h>
#include <portmacro.h>

typedef enum
{
  NVS_STATE_NONE = BIT0,
  NVS_READY = BIT1,
  NVS_BUSY = BIT2,
} nvs_manager_state_t;

typedef enum
{
  NVS_STATE_NONE_REQUEST = BIT0,
  NVS_STATE_READY_REQUEST = BIT1,
  NVS_STATE_READ_REQUEST = BIT2,
  NVS_STATE_WRITE_REQUEST = BIT3,
} nvs_manager_state_request_t;

typedef struct nvs_manager nvs_manager_t;

nvs_manager_t* nvs_manager_create(UBaseType_t priority);
void nvs_manager_destroy(nvs_manager_t *manager);
esp_err_t nvs_manager_request_state(nvs_manager_t* manager, nvs_manager_state_request_t new_state);
void nvs_manager_wait_until_state(nvs_manager_t const * manager, nvs_manager_state_t wait_state);

#endif // NVS_MANAGER_H
