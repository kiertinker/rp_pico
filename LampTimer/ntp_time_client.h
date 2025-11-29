#include <time.h>
enum NTP_ERR_CODE {
  NTP_ERR_OK,
  NTP_ERR_WIFI_INIT_FAILURE,
  NTP_ERR_WIFI_CONNECT_FAILURE,
  NTP_ERR_DNS_FAILURE,
  NTP_ERR_PCB_CREATE_FAILURE,
  NTP_ERR_UDP_FAILURE,
  NTP_ERR_TIMEOUT,
  NTP_FAIL
};

struct TimeResult {
  time_t seconds_since_epoch;
  struct tm time;
};

NTP_ERR_CODE getNtpTime(TimeResult &result);