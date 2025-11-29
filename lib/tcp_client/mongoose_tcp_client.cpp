#include "tcp_client/tcp_client.h"
#include "mongoose.h"
//#include "pico/cyw43_arch.h"

#include "mongoose.h"
#include "pico/mutex.h"

#include <malloc.h>
#include <memory>
#include <string_view>
#include <vector>


namespace {  // anonymous namespace

struct mg_mgr mgr;
bool g_is_wifi_init = false;
bool g_is_mrg_init = false;

constexpr tcp_client::TcpErr mongoose_err_to_tcp_err(int mongoose_err) {
  switch (mongoose_err) {
    // case ERR_OK:         return tcp_client::TcpErr::TCP_ERR_OK;
    // case ERR_MEM:        return tcp_client::TcpErr::TCP_ERR_MEM;
    // case ERR_BUF:        return tcp_client::TcpErr::TCP_ERR_BUF;
    // case ERR_TIMEOUT:    return tcp_client::TcpErr::TCP_ERR_TIMEOUT;
    // case ERR_RTE:        return tcp_client::TcpErr::TCP_ERR_RTE;
    // case ERR_INPROGRESS: return tcp_client::TcpErr::TCP_ERR_INPROGRESS;
    // case ERR_VAL:        return tcp_client::TcpErr::TCP_ERR_VAL;
    // case ERR_WOULDBLOCK: return tcp_client::TcpErr::TCP_ERR_WOULDBLOCK;
    // case ERR_USE:        return tcp_client::TcpErr::TCP_ERR_USE;
    // case ERR_ALREADY:    return tcp_client::TcpErr::TCP_ERR_ALREADY;
    // case ERR_ISCONN:     return tcp_client::TcpErr::TCP_ERR_ISCONN;
    // case ERR_CONN:       return tcp_client::TcpErr::TCP_ERR_CONN;
    // case ERR_IF:         return tcp_client::TcpErr::TCP_ERR_IF;
    // case ERR_ABRT:       return tcp_client::TcpErr::TCP_ERR_ABRT;
    // case ERR_RST:        return tcp_client::TcpErr::TCP_ERR_RST;
    // case ERR_CLSD:       return tcp_client::TcpErr::TCP_ERR_CLSD;
    // case ERR_ARG:        return tcp_client::TcpErr::TCP_ERR_ARG;
    default:             return tcp_client::TcpErr::TCP_UNSPECIFIED;
  }
}

void mg_init_fn(struct mg_tcpip_if *ifp, int ev, void *ev_data) {
  switch (ev) {
  case MG_TCPIP_EV_ST_CHG:           // state change                   uint8_t * (&ifp->state)
    MG_INFO(("MG_TCPIP_EV_ST_CHG: %u", *(uint8_t *) ev_data));
    break;
  case MG_TCPIP_EV_DHCP_DNS:         // DHCP DNS assignment            uint32_t *ipaddr
    MG_INFO(("MG_TCPIP_EV_DHCP_DNS: %u", *(uint8_t *) ev_data));
    break;
  case MG_TCPIP_EV_DHCP_SNTP:        // DHCP SNTP assignment           uint32_t *ipaddr
    MG_INFO(("MG_TCPIP_EV_DHCP_SNTP: %u", *(uint8_t *) ev_data));
    break;
  case MG_TCPIP_EV_ARP:              // Got ARP packet                 struct mg_str *
    MG_INFO(("MG_TCPIP_EV_ARP: %u", *(uint8_t *) ev_data));
    break;
  case MG_TCPIP_EV_TIMER_1S:         // 1 second timer                 NULL
    MG_INFO(("MG_TCPIP_EV_TIMER_1S: %u", *(uint8_t *) ev_data));
    break;
  case MG_TCPIP_EV_WIFI_SCAN_RESULT: // Wi-Fi scan results             struct mg_wifi_scan_bss_data *
    MG_INFO(("MG_TCPIP_EV_WIFI_SCAN_RESULT: %u", *(uint8_t *) ev_data));
    break;
  case MG_TCPIP_EV_WIFI_SCAN_END:    // Wi-Fi scan has finished        NULL
    MG_INFO(("MG_TCPIP_EV_WIFI_SCAN_END: %u", *(uint8_t *) ev_data));
    break;
  case MG_TCPIP_EV_WIFI_CONNECT_ERR: // Wi-Fi connect has failed       driver and chip specific
    MG_INFO(("MG_TCPIP_EV_WIFI_CONNECT_ERR: %u", *(uint8_t *) ev_data));
    break;
  case MG_TCPIP_EV_DRIVER:           // Driver event                   driver specific
    MG_INFO(("MG_TCPIP_EV_DRIVER: %u", *(uint8_t *) ev_data));
    break;
  case MG_TCPIP_EV_USER:              // Starting ID for user events
    MG_INFO(("MG_TCPIP_EV_USER: %u", *(uint8_t *) ev_data));
    break;
  default:
    MG_INFO(("UNKNOWN EVENT: %u", *(uint8_t *) ev_data));
    break;
  }
}

// constexpr tcp_client::TcpErr mongoose_err_to_tcp_err(err_t lwip_err) {
//   return lwip_err_to_tcp_err(static_cast<err_enum_t>(lwip_err));
// }


// // ********** DNS RELATED CODE **********
// void gethostbyname_cb(const char *name, const ip_addr_t *ipaddr, void *callback_arg) {
//   mutex* pmtx = reinterpret_cast<mutex*>(callback_arg);
//   mutex_exit(pmtx);
// }

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
// // ********** END DNS RELATED CODE ********** 


// // ********** TcpConnectionImpl **********

// class TcpConnectionImpl : public tcp_client::TcpConnection {
//   friend err_t tcp_connect(struct tcp_pcb*, const ip_addr_t*, u16_t, tcp_connected_fn);	
//  private:
//   struct tcp_pcb *socket_;
//   tcp_client::IReceiver* receiver_;
//   u16_t buffer_len_;
//   u16_t sent_len_;
//   const unsigned char* buffer_pointer_;
//   const unsigned char* buffer_end_;
//   err_t err_;
//   mutex mtx_;
//   bool closed_;
//   u16_t cb_written_ = 0;
//   unsigned int cb_sent_ = 0;
//   unsigned int cb_in_request_ = 0;
//   absolute_time_t start_, end_, complete_;
//   int callback_invocations_ = 0;

//   // Scoped lwip lock manager.
//   class Cyw43ArchLwipLock {
//    public:
//     Cyw43ArchLwipLock() {
//       cyw43_arch_lwip_begin();
//     }
//     ~Cyw43ArchLwipLock() {
//       cyw43_arch_lwip_end();
//     }
//   };

//   // ScopedLock - Same principle as STL scoped_lock.
//   // NOTE: lock is not reentrant.
//   class ScopedLock {
//    private:
//     mutex& mtx_;
  
//    public:
//     ScopedLock(mutex& mtx) : mtx_(mtx) {
//       mutex_enter_blocking(&mtx_);
//     }
//     ~ScopedLock() {
//       mutex_exit(&mtx_);
//     }
//   };

//   // Scoped Mutex lock-and-wait-for-release.
//   // Grabs a (non-recursive) lock on the provided mutex.  On destruction tries to grab
//   // the lock again, with the expectation that another operation will ultimately release.
//   class MutexLockAndWaitForRelease {
//    public:
//     MutexLockAndWaitForRelease(mutex& mtx) : mtx_(mtx) {
//       mutex_enter_blocking(&mtx_);
//     }
//     ~MutexLockAndWaitForRelease() {
//       if (!released) {
//         mutex_enter_blocking(&mtx_);
//         mutex_exit(&mtx_);
//       }
//     }
//     void release() {
//       released = true;
//       mutex_exit(&mtx_);
//     }
//    private:
//     mutex& mtx_;
//     bool released = false;
//   };

//   static err_t tcpClientConnected(void *arg, struct tcp_pcb *tpcb, err_t err) {
//     return reinterpret_cast<TcpConnectionImpl*>(arg)->connectComplete(err);
//   }

//   err_t connectComplete(err_t err) {
//     printf("TcpConnectionImpl::connectComplete called!!\n");
//     err_ = err;
//     mutex_exit(&mtx_);
//     return ERR_OK;
//   }

//   static err_t tcpSent(void *arg, struct tcp_pcb *tpcb, u16_t bytes_sent) {
//     return reinterpret_cast<TcpConnectionImpl*>(arg)->sent(bytes_sent);
//   }

//   static void tcpErr(void *arg, err_t err) {
//     return reinterpret_cast<TcpConnectionImpl*>(arg)->err(err);
//   }

//   static err_t tcpRecv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
//     return reinterpret_cast<TcpConnectionImpl*>(arg)->recv(tpcb, p, err);
//   }

//   void err(err_t err) {
//     printf("TcpConnectionImpl::err called with err: %u!!\n", err);
//     err_ = err;
//     mutex_exit(&mtx_);
//   }

//   err_t sent(u16_t bytes_sent) {
//     cb_sent_ += bytes_sent;
//     ++callback_invocations_;
//     if (buffer_end_ == buffer_pointer_) {
//     // if (cb_sent_ == cb_in_request_) {
//       complete_ = get_absolute_time();
//       // printf("Send complete!  request bytes: %u,  sent bytes: %u.\n", cb_in_request_, cb_sent_);
//       mutex_exit(&mtx_);
//       return ERR_OK;
//     }
//     if (err_t err = sendData(); err != ERR_OK && err != ERR_MEM) {
//       printf("sent:  sendData returned error %d!!!!\n", err);
//       err_ = err;
//       mutex_exit(&mtx_);
//     }
//     return ERR_OK;
//   }

//   err_t recv(struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
//     printf("TcpConnectionImpl::recv called!!\n");
//     if (p == nullptr)
//       closed_ = true;
//     uint16_t bytes_processed = 0;
//     while (p != nullptr) {
//       if (receiver_ != nullptr)
//         receiver_->callback(static_cast<unsigned char*>(p->payload), p->len);
//       bytes_processed += p->len;
//       p = p->next;
//     }
//     tcp_recved(socket_, bytes_processed);
//     printf("Bytes processed = %d\n", bytes_processed);
//     return ERR_OK;

//   }

//   err_t close() {
//     err_t err = ERR_OK;
//     if (socket_ != nullptr) {
//       tcp_arg(socket_, nullptr);
//       //tcp_poll(tcp_pcb_, nullptr, 0);
//       tcp_sent(socket_, nullptr);
//       tcp_err(socket_, nullptr);
//       tcp_recv(socket_, nullptr);
//       err = tcp_close(socket_);
//       if (err != ERR_OK) {
//         printf("close failed %d, calling abort\n", err);
//         tcp_abort(socket_);
//         err = ERR_ABRT;
//       }
//       socket_ = nullptr;
//     }
//     return err;
//   }

//   err_t sendData() {
//     unsigned int bytes_left = buffer_end_ - buffer_pointer_;
//     if (bytes_left == 0)
//       return ERR_OK;
//     err_t err = ERR_OK;
//     unsigned short cb_send_buf = tcp_sndbuf(socket_);
//     uint16_t cb_to_send = (static_cast<unsigned int>(cb_send_buf) < bytes_left)
//         ? cb_send_buf : static_cast<unsigned short>(bytes_left);
//     err = tcp_write(socket_, buffer_pointer_, cb_to_send, TCP_WRITE_FLAG_COPY);

//     if (err != ERR_OK) {
//       if (err != ERR_MEM) {
//         printf("TcpConnectionImpl::send: tcp_write unexpected ERROR: %d!!!\n", err);
//         return err;
//       }
//       printf("TcpConnectionImpl::send: tcp_write ERR_MEM ERROR %d!\n", err);
//       return err;
//     }
//     buffer_pointer_ += cb_to_send;
//     cb_written_ += cb_to_send;
//     // If we wrote the last segment of data just call tcp_output to flush the queue.
//     if (buffer_end_ == buffer_pointer_) {
//       // printf("Last bits sent.  Calling tcp_output!!!\n");
//       if (err_t err = tcp_output(socket_); err != ERR_OK) {
//         printf("TcpConnectionImpl::send: tcp_output ERROR!!!\n");
//         return err;
//       }
//       end_ = get_absolute_time();
//     }
//     return ERR_OK;
//   }

//  public:
//   TcpConnectionImpl(tcp_pcb* socket, tcp_client::IReceiver* receiver) :
//       socket_(socket), buffer_len_(0), sent_len_(0), buffer_pointer_(nullptr), buffer_end_(nullptr),
//       err_(ERR_OK), receiver_(receiver), closed_(false) {
//     tcp_arg(socket_, this);
//     tcp_sent(socket_, TcpConnectionImpl::tcpSent);
//     tcp_err(socket_, TcpConnectionImpl::tcpErr);
//     tcp_recv(socket_, TcpConnectionImpl::tcpRecv);
//     mutex_init(&mtx_);
//   }

//   err_t connect(const ip_addr_t& ipaddr, unsigned short port) {
//     printf("TcpConnectionImpl::connect called!!!\n");
//     {
//       MutexLockAndWaitForRelease lock(mtx_);
//       Cyw43ArchLwipLock cylock;
//       if (err_t err = tcp_connect(socket_, &ipaddr, port, tcpClientConnected); err != ERR_OK) {
//         printf("tcp_connect FAILED!!!\n");
//         lock.release();
//         return err;
//       }
//     }
//     return err_;
//   }

//   tcp_client::TcpErr send(const unsigned char bytes[], unsigned int cb) override {
//     {  // Scope the send mutex here so we can effectively return the resulting error or success.
//       MutexLockAndWaitForRelease send_lock(mtx_);  // Blocks until all bytes are written.
//       err_ = ERR_OK;
        
//       buffer_pointer_ = bytes;
//       buffer_end_ = bytes + cb;
//       cb_in_request_ = cb;
//       cb_sent_ = 0;
//       cb_written_ = 0;
//       callback_invocations_ = 0;
//       err_t err;
//       int err_mem_retry = 0;
//       while (err_mem_retry < 20) {
//         Cyw43ArchLwipLock cylock;
//         start_ = get_absolute_time();
//         if (err_t err = sendData(); err != ERR_OK) {
//           // Check if mem error.  We will sleep and retry once for these.
//           if (err == ERR_MEM && ++err_mem_retry < 20) {
//             sleep_ms(50);
//             continue;
//           }
//           err_ = err;
//           send_lock.release();
//         }
//         // No retryable memory error, so break out of while loop.
//         break;
//       }
//     }
//     return lwip_err_to_tcp_err(err_);
//   }

//   int64_t getWriteTime() override { return absolute_time_diff_us(start_, end_); }

//   int64_t getSentTime() override { return absolute_time_diff_us(start_, complete_); }

//   int callbackInvocations() override { return callback_invocations_; }


//   ~TcpConnectionImpl() {
//     close();
//   }
// };


// // ********** CONNECT RELATED CODE **********
// std::variant<tcp_client::TcpConnection*, tcp_client::TcpErr> connect(
//     const ip_addr_t& ipaddr, unsigned short port, tcp_client::IReceiver* receiver) {
//   printf("connect called!!!\n");
//   tcp_pcb* tcp_socket = tcp_new_ip_type(IP_GET_TYPE(&ipaddr));
//   if (tcp_socket == nullptr)
//     return tcp_client::TcpErr::TCP_ERR_MEM;

//   auto tcp_connection = std::make_unique<TcpConnectionImpl>(tcp_socket, receiver);
//   if (err_t err = tcp_connection->connect(ipaddr, port); err != ERR_OK)
//     return lwip_err_to_tcp_err(err);
//   return tcp_connection.release();
// }
// // ********** END CONNECT RELATED CODE ********** 

}  // anonymous namespace


namespace tcp_client {

// ************* WIFI INITIALIZATION *************
pico_error_codes wifi_init(std::string_view wifi_ssid, std::string_view wifi_pwd) {
//   g_is_wifi_init = false;
//   int result = cyw43_arch_init_with_country(CYW43_COUNTRY_USA);
//   if (result != 0) return static_cast<pico_error_codes>(result);

//   cyw43_arch_enable_sta_mode();
//   result = cyw43_arch_wifi_connect_timeout_ms(wifi_ssid.data(), wifi_pwd.data(), CYW43_AUTH_WPA2_AES_PSK, 40000);
//   g_is_wifi_init = !result;
//   return (static_cast<pico_error_codes>(result));
  if (!g_is_mrg_init) {
    mg_mgr_init(&mgr);
    g_is_mrg_init = true;
  }

  struct mg_tcpip_driver_pico_w_data driver_data = {
    .ssid = const_cast<char*>("JKATHOME"),//const_cast<char*>(wifi_ssid.data()),
    .pass = const_cast<char*>("06061969AD")//const_cast<char*>(wifi_pwd.data()),
  };


  // Initialise Mongoose network stack
  // Either set use_dhcp or enter a static config.
  // For static configuration, specify IP/mask/GW in network byte order
  struct mg_tcpip_if mif = {
      .driver = &mg_tcpip_driver_pico_w,
      .driver_data = &driver_data,
      .fn = mg_init_fn,
  };
  mif.mac[0] = 0x02;
  MG_INFO(("Calling mg_tcpip_init..."));
  mg_tcpip_init(&mgr, &mif);
  // MG_TCPIP_DRIVER_INIT(&mgr);
  MG_INFO(("mg_tcpip_init complete."));
  return PICO_OK;
}

// // ************* DNS RESOLUTION *************
// std::variant<TcpErr, std::string> get_ip_from_hostname(std::string_view hostname) {
//   std::variant<err_enum_t, ip_addr_t> ret = get_ip_addr_from_hostname(hostname);
//   switch (ret.index()) {
//     case 0:  return lwip_err_to_tcp_err(std::get<err_enum_t>(ret));
//     case 1:  return ipaddr_ntoa(&std::get<ip_addr_t>(ret));
//   }
//   return TcpErr::TCP_UNSPECIFIED;
// }

// // ************* TCP CONNECTION FUNCTIONS *************
// std::variant<TcpConnection*, TcpErr> connection_to_hostname(std::string_view hostname, unsigned short port, IReceiver* receiver) {
//   std::variant<err_enum_t, ip_addr_t> ret = get_ip_addr_from_hostname(hostname);
//   if (ret.index() == 0)
//     return lwip_err_to_tcp_err(std::get<err_enum_t>(ret));
//   return connect(std::get<ip_addr_t>(ret), port, receiver);
// }

// std::variant<TcpConnection*, TcpErr> connection_to_hostip(std::string_view ip_address, unsigned short port, IReceiver* receiver) {
//   printf("connection_to_hostip called!!!\n");
//   ip_addr_t ipaddr;
//   ipaddr_aton(ip_address.data(), &ipaddr);
//   return connect(ipaddr, port, receiver);
// }

}  // namespace tcp_client
