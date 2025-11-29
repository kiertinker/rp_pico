#define LWIP_IPV4                       1
#define LWIP_IPV6                       1
#undef __DOXYGEN__
#include "tcp_client/tcp_client.h"
#include "pico/cyw43_arch.h"
#include "pico/mutex.h"

#include <malloc.h>
#include <memory>
#include <string_view>
#include <vector>

static bool g_is_wifi_init = false;
constexpr int CONNECT_MILLISECONDS = 1000;
constexpr int SEND_MILLISECONDS = 50;

namespace {  // anonymous namespace

// constexpr tcp_client::TcpErr lwip_err_to_tcp_err(err_enum_t lwip_err) {
//   switch (lwip_err) {
//     case ERR_OK:         return tcp_client::TcpErr::TCP_ERR_OK;
//     case ERR_MEM:        return tcp_client::TcpErr::TCP_ERR_MEM;
//     case ERR_BUF:        return tcp_client::TcpErr::TCP_ERR_BUF;
//     case ERR_TIMEOUT:    return tcp_client::TcpErr::TCP_ERR_TIMEOUT;
//     case ERR_RTE:        return tcp_client::TcpErr::TCP_ERR_RTE;
//     case ERR_INPROGRESS: return tcp_client::TcpErr::TCP_ERR_INPROGRESS;
//     case ERR_VAL:        return tcp_client::TcpErr::TCP_ERR_VAL;
//     case ERR_WOULDBLOCK: return tcp_client::TcpErr::TCP_ERR_WOULDBLOCK;
//     case ERR_USE:        return tcp_client::TcpErr::TCP_ERR_USE;
//     case ERR_ALREADY:    return tcp_client::TcpErr::TCP_ERR_ALREADY;
//     case ERR_ISCONN:     return tcp_client::TcpErr::TCP_ERR_ISCONN;
//     case ERR_CONN:       return tcp_client::TcpErr::TCP_ERR_CONN;
//     case ERR_IF:         return tcp_client::TcpErr::TCP_ERR_IF;
//     case ERR_ABRT:       return tcp_client::TcpErr::TCP_ERR_ABRT;
//     case ERR_RST:        return tcp_client::TcpErr::TCP_ERR_RST;
//     case ERR_CLSD:       return tcp_client::TcpErr::TCP_ERR_CLSD;
//     case ERR_ARG:        return tcp_client::TcpErr::TCP_ERR_ARG;
//     default:             return tcp_client::TcpErr::TCP_UNSPECIFIED;
//   }
// }

// constexpr tcp_client::TcpErr lwip_err_to_tcp_err(err_t lwip_err) {
//   return lwip_err_to_tcp_err(static_cast<err_enum_t>(lwip_err));
// }


// ********** DNS RELATED CODE **********
void gethostbyname_cb(const char *name, const ip_addr_t *ipaddr, void *callback_arg) {
  mutex* pmtx = reinterpret_cast<mutex*>(callback_arg);
  mutex_exit(pmtx);
}

// std::variant<err_enum_t, ip_addr_t> get_ip_addr_from_hostname(std::string_view hostname) {
//   mutex mtx;
//   mutex_init(&mtx);
//   err_enum_t err;
//   ip_addr_t ip_addr;
//   do {
//     mutex_enter_blocking(&mtx);
//     printf("get_ip_addr_from_hostname:  Calling dns_gethostbyname_addrtype!!!\n");
//     err = static_cast<err_enum_t>(dns_gethostbyname_addrtype(
//         hostname.data(), &ip_addr, gethostbyname_cb, reinterpret_cast<void*>(&mtx), LWIP_DNS_ADDRTYPE_IPV4_IPV6));
//     printf("dns_gethostbyname_addrtype:  Finished call to dns_gethostbyname_addrtype, err = %d\n", static_cast<int>(err));
//   } while (err == err_enum_t::ERR_INPROGRESS);

//   mutex_exit(&mtx);
//   if (err != ERR_OK)
//     return err;
//   else
//     return ip_addr;
// }
// ********** END DNS RELATED CODE ********** 


// ********** TcpConnectionImpl **********

class TcpConnectionImpl : public tcp_client::TcpConnection {
 private:
  tcp_client::IReceiver* receiver_;
  absolute_time_t start_, end_, complete_;


 public:
  TcpConnectionImpl(tcp_client::IReceiver* receiver) : receiver_(receiver) {}

  err_t connect(const ip_addr_t& ipaddr, unsigned short port) {
    printf("TcpConnectionImpl::connect called!!!\n");
    return 0;
  }

  tcp_client::TcpErr send(const unsigned char bytes[], unsigned int cb) override {
    start_ = get_absolute_time();
    sleep_ms(SEND_MILLISECONDS);
    end_ = complete_ = get_absolute_time();
    return tcp_client::TcpErr::TCP_ERR_OK;
  }

  int64_t getWriteTime() override { return absolute_time_diff_us(start_, end_); }

  int64_t getSentTime() override { return absolute_time_diff_us(start_, complete_); }

  int callbackInvocations() override { return 5; }


  ~TcpConnectionImpl() {}
};


// ********** CONNECT RELATED CODE **********
std::variant<tcp_client::TcpConnection*, tcp_client::TcpErr> connect(
    unsigned short port, tcp_client::IReceiver* receiver) {
  printf("connect called!!!\n");

  auto tcp_connection = std::make_unique<TcpConnectionImpl>(receiver);
  sleep_ms(CONNECT_MILLISECONDS);
  return tcp_connection.release();
}
// ********** END CONNECT RELATED CODE ********** 

}  // anonymous namespace


namespace tcp_client {

// ************* WIFI INITIALIZATION *************
pico_error_codes wifi_init(std::string_view wifi_ssid, std::string_view wifi_pwd) {
  g_is_wifi_init = false;
  int result = cyw43_arch_init_with_country(CYW43_COUNTRY_USA);
  if (result != 0) return static_cast<pico_error_codes>(result);

  cyw43_arch_enable_sta_mode();
  result = cyw43_arch_wifi_connect_timeout_ms(wifi_ssid.data(), wifi_pwd.data(), CYW43_AUTH_WPA2_AES_PSK, 40000);
  g_is_wifi_init = !result;
  return (static_cast<pico_error_codes>(result));
}

// ************* DNS RESOLUTION *************
std::variant<TcpErr, std::string> get_ip_from_hostname(std::string_view hostname) {
  return "MOCK_ADDR";
}

// ************* TCP CONNECTION FUNCTIONS *************
std::variant<TcpConnection*, TcpErr> connection_to_hostname(std::string_view hostname, unsigned short port, IReceiver* receiver) {
  return connect(port, receiver);
}

std::variant<TcpConnection*, TcpErr> connection_to_hostip(std::string_view ip_address, unsigned short port, IReceiver* receiver) {
  printf("connection_to_hostip called!!!\n");
  return connect(port, receiver);
}

}  // namespace tcp_client
