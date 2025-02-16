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

#include "dns_redirect.h"

#include "state.h"

#include <esp_log.h>
#include <esp_netif.h>
#include <esp_netif_types.h>
#include <lwip/dns.h>
#include <lwip/ip4_addr.h>
#include <lwip/prot/dns.h>
#include <lwip/udp.h>

static struct udp_pcb* dns_pcb = NULL;
static ip4_addr_t ap_ip;
static char* TAG = "DNS Redirect";
static TaskHandle_t dns_task_handle = NULL;

#pragma pack(push, 1)

typedef struct
{
  uint16_t id;
  uint16_t flags;
  uint16_t qdcount;
  uint16_t ancount;
  uint16_t nscount;
  uint16_t arcount;
} dns_header;

typedef struct
{
  uint16_t qtype;
  uint16_t qclass;
} dns_question;

typedef struct
{
  uint16_t name;
  uint16_t type;
  uint16_t class;
  uint32_t ttl;
  uint16_t length;
  ip4_addr_t addr;
} dns_answer;

#pragma pack(pop)

static void dns_recv_callback(void* arg, struct udp_pcb* pcb, struct pbuf* p,
                              const ip_addr_t* addr, u16_t port) {
  if (!p || p->tot_len < sizeof(dns_header)) {
    if (p) pbuf_free(p);
    return;
  }

  struct pbuf* resp = pbuf_alloc(PBUF_TRANSPORT, p->tot_len + sizeof(dns_answer), PBUF_RAM);
  if (!resp) {
    pbuf_free(p);
    return;
  }

  pbuf_copy(resp, p);

  dns_header* hdr = (dns_header*)resp->payload;
  hdr->flags = PP_HTONS(0x8400);
  hdr->ancount = PP_HTONS(1);

  u8_t* payload = (u8_t*)resp->payload;
  u16_t qname_len = 12;
  while (payload[qname_len] != 0 && qname_len < resp->tot_len) qname_len++;
  qname_len += 5;

  dns_answer ans = {
    .name = PP_HTONS(0xC00C),
    .type = PP_HTONS(DNS_RRTYPE_A),
    .class = PP_HTONS(DNS_RRCLASS_IN),
    .ttl = PP_HTONL(60),
    .length = PP_HTONS(sizeof(ip4_addr_t)),
    .addr = ap_ip
  };

  memcpy(&payload[qname_len], &ans, sizeof(ans));
  resp->tot_len = qname_len + sizeof(ans);

  udp_sendto(pcb, resp, addr, port);
  pbuf_free(resp);
  pbuf_free(p);
}

static void dns_server_task() {
  dns_pcb = udp_new_ip_type(IPADDR_TYPE_V4);
  if (!dns_pcb) {
    ESP_LOGE(TAG, "Failed to create DNS PCB");
    vTaskDelete(NULL);
    return;
  }

  if (udp_bind(dns_pcb, IP_ANY_TYPE, 53) != ERR_OK) {
    ESP_LOGE(TAG, "Failed to bind DNS port");
    udp_remove(dns_pcb);
    vTaskDelete(NULL);
    return;
  }

  udp_recv(dns_pcb, dns_recv_callback, NULL);

  esp_netif_ip_info_t ip_info;
  esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"), &ip_info);
  ip4_addr_set_u32(&ap_ip, ip_info.ip.addr);

  while (1) {
    EventBits_t bits = xEventGroupWaitBits(system_event_group, REBOOT_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
    if (bits & REBOOT_BIT) {
      ESP_LOGW(TAG, "RECEIVED REBOOT");
      break;
    }
    taskYIELD();
  }

  udp_remove(dns_pcb);
  dns_pcb = NULL;
  dns_task_handle = NULL;
  ESP_LOGI(TAG, "Done");
  vTaskDelete(NULL);
}

void start_dns_server() {
  if (dns_task_handle == NULL) {
    xTaskCreate(dns_server_task, TAG, 4096, NULL, DNS_P, &dns_task_handle);
  }
}
