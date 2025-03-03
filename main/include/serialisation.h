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

#ifndef SERIALISATION_H
#define SERIALISATION_H

#include <stddef.h>
#include <stdint.h>

#include "configuration.h"

size_t serialize_unit_configuration(const unit_configuration_t* config, uint8_t* buffer);

size_t calculate_unit_configuration_size(const unit_configuration_t* config);

#endif //SERIALISATION_H
