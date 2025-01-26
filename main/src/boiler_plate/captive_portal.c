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

#include "captive_portal.h"

#include <string.h>
#include <sys/socket.h>
#include "esp_log.h"
#include "lwip/netdb.h"

#define DNS_PORT 53
#define RESPONSE_IP { 192, 168, 4, 1 } // Your ESP32's AP IP

static const char* TAG = "Captive Portal";
static TaskHandle_t captive_portal_task_handle = NULL;
static int sock;
static bool keep_running = false;

typedef struct __attribute__((packed))
{
  uint16_t id;
  uint16_t flags;
  uint16_t qdcount;
  uint16_t ancount;
  uint16_t nscount;
  uint16_t arcount;
} dns_header_t;

typedef struct __attribute__((packed))
{
  uint16_t type;
  uint16_t qclass;
} dns_question_t;

typedef struct __attribute__((packed))
{
  uint16_t name_ptr;
  uint16_t type;
  uint16_t qclass;
  uint32_t ttl;
  uint16_t rdlength;
  uint8_t rdata[4];
} dns_answer_t;

// Parse DNS names (supports compression pointers)
static const uint8_t* parse_name(const uint8_t* data, const uint8_t* ptr, char* out, size_t max_len) {
  char* dst = out;
  while (*ptr != 0) {
    if ((*ptr & 0xC0) == 0xC0) {
      uint16_t offset = ntohs(*(uint16_t*)ptr) & 0x3FFF;
      ptr = data + offset;
    } else {
      size_t len = *ptr++;
      if (dst + len + 1 >= out + max_len) return NULL;
      memcpy(dst, ptr, len);
      dst += len;
      *dst++ = '.';
      ptr += len;
    }
  }
  *dst = '\0';
  return ptr + 1;
}

void dns_server_task(void* pvParameters) {
  sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
  struct sockaddr_in addr = {
    .sin_family = AF_INET,
    .sin_port = htons(DNS_PORT),
    .sin_addr.s_addr = INADDR_ANY
  };
  bind(sock, (struct sockaddr*)&addr, sizeof(addr));

  uint8_t buffer[512];
  struct sockaddr_in client_addr;
  socklen_t client_addr_len = sizeof(client_addr);

  while (keep_running) {
    int len = recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr*)&client_addr, &client_addr_len);
    if (len <= 0) continue;

    dns_header_t* header = (dns_header_t*)buffer;
    if (header->flags & 0x8000) continue; // Ignore responses (QR=1)

    char qname[256];
    const uint8_t* ptr = parse_name(buffer, buffer + sizeof(dns_header_t), qname, sizeof(qname));
    if (!ptr || ptr >= buffer + len) continue;

    dns_question_t* question = (dns_question_t*)ptr;
    uint16_t qtype = ntohs(question->type);
    uint16_t qclass = ntohs(question->qclass);

    // Only respond to A-record queries (0x0001) in IN class (0x0001)
    if (qtype != 0x0001 || qclass != 0x0001) continue;

    // Prepare DNS response
    header->flags = htons(0x8180); // QR=1, RA=1, RCODE=0
    header->ancount = htons(1);

    dns_answer_t answer = {
      .name_ptr = htons(0xC00C), // Pointer to original query name
      .type = htons(0x0001),
      .qclass = htons(0x0001),
      .ttl = htonl(60),
      .rdlength = htons(4),
      .rdata = RESPONSE_IP
    };

    memcpy(buffer + len, &answer, sizeof(answer));
    len += sizeof(answer);

    sendto(sock, buffer, len, 0, (struct sockaddr*)&client_addr, client_addr_len);
    taskYIELD();
  }
  close(sock);
  vTaskDelete(NULL);
}

void start_dns_server() {
  if (captive_portal_task_handle == NULL) {
    keep_running = true;
    xTaskCreate(&dns_server_task, "dns_server", 3072, NULL, 5, &captive_portal_task_handle);
  }
}

void stop_dns_server() {
  if (captive_portal_task_handle) {
    keep_running = false;
    vTaskDelete(captive_portal_task_handle);
    close(sock);
    captive_portal_task_handle = NULL;
  }
}
