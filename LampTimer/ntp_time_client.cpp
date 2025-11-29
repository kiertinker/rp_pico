/**
 * Copyright (c) 2022 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ntp_time_client.h"
#include <string.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/mutex.h"

#include "lwip/dns.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"

#include <string_view>
#include <variant>

constexpr std::string_view NTP_SERVER = "pool.ntp.org";
constexpr size_t NTP_MSG_LEN = 48;
constexpr unsigned short NTP_PORT = 123;
constexpr unsigned int NTP_DELTA = 2208988800;  // seconds between 1 Jan 1900 and 1 Jan 1970
constexpr unsigned int EST_DELTA = 18000;  // Offset from EST to GMT
#define NTP_TEST_TIME (30 * 1000)
#define NTP_RESEND_TIME (10 * 1000)

// Scoped lwip lock manager.
class Cyw43ArchLwipLock {
 public:
  Cyw43ArchLwipLock() { cyw43_arch_lwip_begin(); }
 ~Cyw43ArchLwipLock() { cyw43_arch_lwip_end(); }
};


struct NTP_T {
  ip_addr_t ntp_server_address;
  struct udp_pcb *ntp_pcb;
  absolute_time_t ntp_test_time;
  alarm_id_t ntp_resend_alarm;
  TimeResult result;
  mutex mtx;
  NTP_ERR_CODE err;

  NTP_T(ip_addr_t& ntp_server) : ntp_server_address(ntp_server), err(NTP_ERR_CODE::NTP_ERR_OK) {
    ntp_pcb = udp_new_ip_type(IPADDR_TYPE_ANY);
    if (!ntp_pcb) {
      printf("failed to create pcb\n");
      err = NTP_ERR_CODE::NTP_ERR_PCB_CREATE_FAILURE;
    }
    mutex_init(&mtx);
  }
};

// >>> DNS LOOKUP
void gethostbyname_cb(const char *name, const ip_addr_t *ipaddr, void *callback_arg) {
  mutex* pmtx = reinterpret_cast<mutex*>(callback_arg);
  mutex_exit(pmtx);
}

std::variant<NTP_ERR_CODE, ip_addr_t> getIpAddrFromHostname(std::string_view hostname) {
  mutex mtx;
  mutex_init(&mtx);
  err_enum_t err;
  ip_addr_t ip_addr;
  do {
    if (!mutex_enter_timeout_ms(&mtx, 10000)) {
      err = ERR_TIMEOUT;
      break;
    }
    printf("get_ip_addr_from_hostname:  Calling dns_gethostbyname_addrtype!!!\n");
    {
      Cyw43ArchLwipLock cyw43_lock;
      err = static_cast<err_enum_t>(dns_gethostbyname_addrtype(
          hostname.data(), &ip_addr, gethostbyname_cb, reinterpret_cast<void*>(&mtx), LWIP_DNS_ADDRTYPE_IPV4_IPV6));
    }
    printf("dns_gethostbyname_addrtype:  Finished call to dns_gethostbyname_addrtype, err = %d\n", static_cast<int>(err));
  } while (err == err_enum_t::ERR_INPROGRESS);

  mutex_exit(&mtx);
  if (err != ERR_OK)
    return NTP_ERR_CODE::NTP_ERR_DNS_FAILURE;
  else
    return ip_addr;
}
// DNS LOOKUP <<<

// Make an NTP request
static void ntpRequest(NTP_T& state) {
    printf("ntpRequest...\n");
    Cyw43ArchLwipLock Cyw43_lock;;
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, NTP_MSG_LEN, PBUF_RAM);
    uint8_t *req = (uint8_t *) p->payload;
    memset(req, 0, NTP_MSG_LEN);
    req[0] = 0x1b;
    udp_sendto(state.ntp_pcb, p, &state.ntp_server_address, NTP_PORT);
    pbuf_free(p);
}

// NTP data received
static void ntp_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port) {
  NTP_T *state = (NTP_T*)arg;
  uint8_t mode = pbuf_get_at(p, 0) & 0x7;
  uint8_t stratum = pbuf_get_at(p, 1);
  // Check the result
  if (ip_addr_cmp(addr, &state->ntp_server_address) && port == NTP_PORT && p->tot_len == NTP_MSG_LEN &&
      mode == 0x4 && stratum != 0) {
    printf("ntp_recv:  NTP SUCCESS!!!\n");
    uint8_t seconds_buf[4] = {0};
    pbuf_copy_partial(p, seconds_buf, sizeof(seconds_buf), 40);
    uint32_t seconds_since_1900 = seconds_buf[0] << 24 | seconds_buf[1] << 16 | seconds_buf[2] << 8 | seconds_buf[3];
    printf("ntp_recv:  seconds since 1900 = %u\n", seconds_since_1900);
    uint32_t seconds_since_1970 = seconds_since_1900 - NTP_DELTA;
    printf("ntp_recv:  seconds since 1970 = %u\n", seconds_since_1970);
    printf("ntp_recv:  EST seconds since 1970 = %u\n", seconds_since_1970 - EST_DELTA);
    time_t epoch = static_cast<time_t>(seconds_since_1970 - EST_DELTA);
    printf("ntp_recv:  size of time_t = %u,  epoch seconds = %ld\n", sizeof(epoch), epoch);
    tm* ptm = gmtime(&epoch);
    printf("ntp_recv:  tm.year = *d, tm.yday = %d\n", ptm->tm_year, ptm->tm_yday);
   
    state->result = {epoch, *gmtime(&epoch)};
    state->err = NTP_ERR_CODE::NTP_ERR_OK;
  } else {
    printf("invalid ntp response\n");
    state->err = NTP_ERR_CODE::NTP_ERR_UDP_FAILURE;
  }
  pbuf_free(p);
  mutex_exit(&state->mtx);
}


NTP_ERR_CODE getNtpTime(TimeResult &result) {
  sleep_ms(5000);
  printf("getNtpTime...\n");
  if (cyw43_arch_init()) {
    printf("failed to initialise\n");
    return NTP_ERR_CODE::NTP_ERR_WIFI_INIT_FAILURE;
  }

  cyw43_arch_enable_sta_mode();

  if (cyw43_arch_wifi_connect_timeout_ms("JKATHOME", "06061969AD", CYW43_AUTH_WPA2_AES_PSK, 10000)) {
    printf("failed to connect\n");
    return NTP_ERR_CODE::NTP_ERR_WIFI_CONNECT_FAILURE;
  }
  std::variant<NTP_ERR_CODE, ip_addr_t> dns_result = getIpAddrFromHostname(NTP_SERVER);
  if (dns_result.index() == 0) return std::get<NTP_ERR_CODE>(dns_result);

  NTP_T state(std::get<ip_addr_t>(dns_result));
  if (state.err != NTP_ERR_CODE::NTP_ERR_OK) return state.err;
  udp_recv(state.ntp_pcb, ntp_recv, &state);

  // Make up to three attempts to get a successful UDP result.
  int retries = 3;
  do {
    mutex_enter_blocking(&state.mtx);
    ntpRequest(state); // Cached result
    if (!mutex_enter_timeout_ms(&state.mtx, 10000)) {
      printf("getNtpTime:  Timed out waiting for response.\n");
      mutex_exit(&state.mtx);
      state.err = NTP_ERR_CODE::NTP_ERR_TIMEOUT;
    }
    if (state.err == NTP_ERR_CODE::NTP_ERR_OK) {
      printf("getNtpTime:  Successful exit.\n");
      break;
    }
  } while (--retries);
  cyw43_arch_deinit();
  if (state.err == NTP_ERR_CODE::NTP_ERR_OK) {
    printf("got ntp response (%lld): %02d/%02d/%04d %02d:%02d:%02d\n", state.result.seconds_since_epoch, state.result.time.tm_mday, state.result.time.tm_mon + 1, state.result.time.tm_year + 1900,
        state.result.time.tm_hour, state.result.time.tm_min, state.result.time.tm_sec);
  }
  result = state.result;
  return state.err;
}
