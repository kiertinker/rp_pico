#ifndef TCP_CLIENT_H
#define TCP_CLIENT_H

#include "pico/error.h"
#include "pico/stdlib.h"

#include <stdio.h>
#include <string>
#include <string_view>
#include <variant>

namespace tcp_client {

enum class TcpErr {
  TCP_ERR_OK,          // No error, everything OK.
  TCP_ERR_MEM,         // Out of memory error.
  TCP_ERR_BUF,         // Buffer error.
  TCP_ERR_TIMEOUT,     // Timeout.
  TCP_ERR_RTE,         // Routing problem.
  TCP_ERR_INPROGRESS,  // Operation in progress.
  TCP_ERR_VAL,         // Illegal value.
  TCP_ERR_WOULDBLOCK,  // Operation would block.
  TCP_ERR_USE,         // Address in use.
  TCP_ERR_ALREADY,     // Already connecting.
  TCP_ERR_ISCONN,      // Conn already established.
  TCP_ERR_CONN,        // Not connected.
  TCP_ERR_IF,          // Low-level netif error.
  TCP_ERR_ABRT,        // Connection aborted.
  TCP_ERR_RST,         // Connection reset.
  TCP_ERR_CLSD,        // Connection closed.
  TCP_ERR_ARG,         // Illegal argument.
  TCP_UNSPECIFIED      // Unspecified, lwip error code has no lwip err_enum_t match.
};

// std::string_view tcp_err_to_string(TcpErr err) {
//   switch (err) {
//     case TcpErr::TCP_ERR_OK: return "No error, everything OK.";
//     case TcpErr::TCP_ERR_MEM: return "Out of memory error.";
//     case TcpErr::TCP_ERR_BUF: return "Buffer error.";
  
//     case TcpErr::TCP_ERR_TIMEOUT: return "Timeout.";
//     case TcpErr::TCP_ERR_RTE: return "Routing problem.";
//     case TcpErr::TCP_ERR_INPROGRESS: return "Operation in progress.";
  
//     case TcpErr::TCP_ERR_VAL: return "Illegal value.";
//     case TcpErr::TCP_ERR_WOULDBLOCK: return "Operation would block.";
//     case TcpErr::TCP_ERR_USE: return "Address in use.";
  
//     case TcpErr::TCP_ERR_ALREADY: return "Already connecting.";
//     case TcpErr::TCP_ERR_ISCONN: return "Conn already established.";
//     case TcpErr::TCP_ERR_CONN: return "Not connected.";
  
//     case TcpErr::TCP_ERR_IF: return "Low-level netif error.";
//     case TcpErr::TCP_ERR_ABRT: return "Connection aborted.";
//     case TcpErr::TCP_ERR_RST: return "Connection reset.";
  
//     case TcpErr::TCP_ERR_CLSD: return "Connection closed.";
//     case TcpErr::TCP_ERR_ARG: return "Illegal argument.";
//     case TcpErr::TCP_UNSPECIFIED:
//     default: return "Unspecified, lwip error code has no lwip err_enum_t match.";
//   }
// }

pico_error_codes wifi_init(std::string_view wifi_ssid, std::string_view wifi_pwd);

class IReceiver {
 public:
  virtual ~IReceiver(){};
  virtual void callback(const unsigned char bytes[], unsigned int cb) = 0;
};

class TcpConnection {
 public:
  TcpConnection() {};
  virtual ~TcpConnection() {};
  // Synchronous.  Returns when all data has been sent or an error happens.
  // NOTE: This is not really meant for streaming.  Ideally suited for a complete message on each invocation.
  virtual TcpErr send(const unsigned char bytes[], unsigned int cb) = 0;
  virtual int64_t getWriteTime() = 0;
  virtual int64_t getSentTime() = 0;
  virtual int callbackInvocations() = 0;
};

std::variant<TcpErr, std::string> get_ip_from_hostname(std::string_view hostname);

std::variant<TcpConnection*, TcpErr> connection_to_hostname(
    std::string_view fqdn,
    unsigned short port,
    IReceiver* receiver);  // Must outlive TcpConnection instance.  nullptr for no callback (non-receiving connection).

std::variant<TcpConnection*, TcpErr> connection_to_hostip(
    std::string_view ip_address,
    unsigned short port,
    IReceiver* receiver);  // Must outlive TcpConnection instance.  nullptr for no callback (non-receiving connection).
}

#endif