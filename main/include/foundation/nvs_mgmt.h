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

#ifndef NVS_MGMT_H
#define NVS_MGMT_H

/*
 * Main Task of NVM Management. It waits on 2 events (bits)
 * 1) Update `unit_configuration_t` stored in NVS from current `unit_configuration_t` stored in current state.
 * 2) Update the current `unit_configuration_t` from what is stored in NVS.
 *
 * This allows for configuration to be restored upon boot. And also to be updated (via https)
 *
 * ! Imported for this task to be initialised first
 */
void init_nvs_manager();

#endif // NVS_MGMT_H
