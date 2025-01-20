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

#ifndef ALLOCATION_H
#define ALLOCATION_H

#include <esp_err.h>
#include <esp_log.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Allocates memory for a buffer, clears it, and handles errors.
 * The caller must ensure `buffer` is a pointer and that the function returns `err_code` on failure.
 *
 * Sample:
 * char* buffer = NULL;
 * ESP_ERROR_CHECK(allocate_and_clear_buffer((void** )&buffer, buffer_size, TAG, ESP_ERR_NO_MEM));
 *
 * @param buffer Pointer to the buffer to be allocated.
 * @param size Size of the buffer to allocate.
 * @param tag Tag for logging purposes.
 * @param err_code Error code to return in case of failure.
 * @return ESP_OK on success or err_code on failure.
 */
static inline esp_err_t allocate_and_clear_buffer(void** buffer, size_t size, const char* tag, esp_err_t err_code) {
  *buffer = malloc(size);
  if (*buffer == NULL) {
    ESP_LOGE(tag, "Failed to allocate memory for buffer");
    return err_code;
  }
  memset(*buffer, 0, size);
  return ESP_OK;
}

/**
 * Checks if a pointer is non-NULL, frees it, and sets it to NULL.
 *
 * @param ptr Pointer to free and nullify.
 */
static inline void check_and_free(void* ptr) {
  if (ptr) {
    free(ptr);
    ptr = NULL;
  }
}

#ifdef __cplusplus
}
#endif

#endif // ALLOCATION_H
