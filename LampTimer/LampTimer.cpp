/*
Compute Sunset and Sunrise

FIRST COMPUTE the Fractional Year:
γ = (2Pi/365)*(day_of_year-1 + ((hour - 12)/24))
for leap year divide 2Pi by 366


NEXT COMPUTE the Equation of Time:
eqtime = 229.18*(0.000075 + 0.001868cos(γ) – 0.032077sin(γ) – 0.014615cos(2γ)
– 0.040849sin(2γ) )


THEN COMPUTE the solar declination angle
decl = 0.006918 – 0.399912cos(γ) + 0.070257sin(γ) – 0.006758cos(2γ) + 0.000907sin(2γ)
– 0.002697cos(3γ) + 0.00148sin (3γ)


NOW WE NEED the True Solar Time.
First get the Time Offset:
time_offset = eqtime + 4*longitude – 60*timezone

The get True Solar Time
tst = hr*60 + mn + sc/60 + time_offset
  hr is (0-23), min is (0-59), sc is (0-59)


Hour Angle is:
ℎ𝑎 = ±𝑎𝑟𝑐𝑐𝑜𝑠((cos(90.833)/(cos(𝑙𝑎𝑡) * cos(𝑑𝑒𝑐𝑙))) − (tan(𝑙𝑎𝑡) * tan(𝑑𝑒𝑐𝑙)))
  Positive corresponds to sunrise, negative to sunset
*/
#include "ntp_time_client.h"
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
extern "C" { 
  #include "pico/util/datetime.h"
  #include "hardware/rtc.h"
}
#include <math.h>

#include <initializer_list>

namespace {

constexpr float PI = 3.14159265359f;
constexpr float degToRad(float deg) { return 2.0f * PI * deg / 360.0f; }
constexpr float LONGITUDE = degToRad(-79.929733f);  // lon & lat of 1260 Bellerock St.
constexpr float LATITUDE = degToRad(40.443184f);
constexpr float ZENITH = degToRad(90.833f);
constexpr float TIMEZONE = -5.0f;  // EST offset from UTC
constexpr int DAYS_IN_MONTHS[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
float radToDeg(float radians) { return radians * 360.0f / PI / 2.0f;  }
// Leap years are evenly div by 4, but not evenly div by 100, unless evenly div by 400
bool isLeapYear(int year) { return ((year % 4) || (!(year % 100) && (year % 400))); }
int daysInYear(int year) { return isLeapYear(year) ? 366 : 365; }
int getYearDay(datetime_t& t) {
  int year_day = static_cast<int>(t.day);
  for (int i = 0; i < static_cast<int>(t.month); ++i) year_day += DAYS_IN_MONTHS[i];
  if (isLeapYear(t.year) && (t.month > 2)) ++year_day;
  printf("getYearDay:  day = %d, month = %d, year day = %d", t.day, t.month, year_day);
  return year_day;
}

// In UTC minutes.  Could be >= 24 hrs.
int computeSunset(int day_of_year, int year) {
  printf("computeSunset:  day_of_year = %d, year = %d\n", day_of_year, year);
  // Fractional year in radians (not accounting for hours)
  float gamma = (2.0f * PI / static_cast<float>(daysInYear(year))) * (static_cast<float>(day_of_year) - 1.0f);
  // Equation of time (in minutes)
  float eqtime = 229.18f *
      (0.000075f + 0.001868f * cosf(gamma) - 0.032077f * sinf(gamma)
       - 0.014615f * cosf(2.0f * gamma) - 0.040849f * sinf(2.0f * gamma));
  // Solar declination angle (in radians)
  float decl = 0.006918f - 0.399912f * cosf(gamma) + 0.070257f * sin(gamma)
      - 0.006758f * cos(2.0f * gamma) + 0.000907f * sinf(2.0f * gamma)
      - 0.002697f * cosf(3.0f * gamma) + 0.00148f * sin(3.0f * gamma);
  // Solar hour angle (in radians)
  float ha = acosf((cosf(ZENITH) / cosf(LATITUDE) / cosf(decl)) - (tanf(LATITUDE) * tanf(decl)));
  // Subtract ha from longitude for sunset (we would add ha to longitude if we wanted sunrise)
  // and adust 5hrs of minutes for EST.
  return static_cast<int>(720.0f - 4.0f * radToDeg(LONGITUDE - ha) - eqtime) - 300;
}

constexpr int RED_LED = 14;
constexpr int GREEN_LED = 15;
constexpr int BLUE_LED = 16;
constexpr int RELAY = 17;

void initGpio() {
  for (auto& pin : { RED_LED, GREEN_LED, BLUE_LED, RELAY }) {
    printf("initGpio:  Initing Pin %d\n", pin);
    gpio_init(pin);
    // Set the pin's direction to output
    gpio_set_dir(pin, GPIO_OUT);
  }
}

void initPins() {
  initGpio();
  gpio_put(RED_LED, true);
  gpio_put(GREEN_LED, true);
  gpio_put(BLUE_LED, true);
  gpio_put(RELAY, false);
}

constexpr int TWO_OCLOCK_MINUTES = 120;  // Since we are doing everything in UTC this is what 2:00EST is in UTC

unsigned int minuteOfDay(datetime_t& t) {
  return static_cast<unsigned int>(t.hour) * 60 + static_cast<unsigned int>(t.min);
}

bool isInLampOnRange(datetime_t t, int sunset) {
  int minutes_now = minuteOfDay(t);
  return (minutes_now <= TWO_OCLOCK_MINUTES) || (sunset <= minutes_now);
}

bool setTime(bool init) {
  int retries = 3;
  bool pins_set = false;

  NTP_ERR_CODE err = NTP_ERR_CODE::NTP_ERR_OK;
  TimeResult result;
  do {
    err = getNtpTime(result);
    if (err != NTP_ERR_CODE::NTP_ERR_OK) {
      printf("setTime:  getNtpTime Failed!!!\n");
      if (!pins_set) {
        gpio_put(RED_LED, true);
        if (init) {
          gpio_put(GREEN_LED, false);
          gpio_put(BLUE_LED, false);
        }
        pins_set = true;
      }
    } else printf("setTime:  Got NTP Time!!!\n");
  } while ((init || --retries) && err != NTP_ERR_CODE::NTP_ERR_OK);
  if (err == NTP_ERR_CODE::NTP_ERR_OK) {
    gpio_put(RED_LED, false);
    gpio_put(GREEN_LED, true);
    printf("NTP time tm: year = %d, mon = %d, mday = %d, hour = %d, min = %d\n", result.time.tm_year, result.time.tm_mon, result.time.tm_mday, result.time.tm_hour, result.time.tm_min);
    datetime_t t = {
        .year = static_cast<short>(result.time.tm_year + 1900),  // tm_year is years since 1900.
        .month = static_cast<signed char>(result.time.tm_mon),
        .day = static_cast<signed char>(result.time.tm_mday),
        .dotw = static_cast<signed char>(result.time.tm_wday),
        .hour = static_cast<signed char>(result.time.tm_hour),
        .min = static_cast<signed char>(result.time.tm_min),
        .sec = static_cast<signed char>(result.time.tm_sec)
    };
    printf("NTP time datetime_t: year = %d, mon = %d, mday = %d, hour = %d, min = %d\n", static_cast<int>(t.year), static_cast<int>(t.month), static_cast<int>(t.day), static_cast<int>(t.hour), static_cast<int>(t.min));
    if (init) rtc_init();
    rtc_set_datetime(&t);
    // clk_sys is >2000x faster than clk_rtc, so datetime is not updated immediately when rtc_get_datetime() is called.
    // The delay is up to 3 RTC clock cycles (which is 64us with the default clock settings)
    sleep_ms(1);
    return true;
  }
  return false;
}

}  // anonymous namespace



int main()
{
  stdio_init_all();
  initPins();
  sleep_ms(5000);

  // Initialization.  At startup we:
  //  - Set the current time (with respect to NTP server).
  //  - Determine whether we are in a lamp on or lamp off time window.
  //  - Set the lamp_on flag to the opposite of the current desired state.
  //      This enables the main loop to toggle LEDs to the current desired state
  //      and set the sleep timer to the next state accordingly.
  setTime(true);
  gpio_put(BLUE_LED, false);
  datetime_t t;
  rtc_get_datetime(&t);
  int sunset = computeSunset(getYearDay(t), t.year);
  printf("Sunset is %02d:%02d.\n", sunset / 60, sunset % 60);
  // We start lamp_on at the opposite of what it should be so the main loop will toggle it otherwise.
  bool lamp_on = false;
  if (!isInLampOnRange(t, sunset)) lamp_on = true;

  // Loop infinitely, toggling LEDs and relay to reflect on/off state transitions,
  // setting sleep timer to the next transition and going to sleep.
  int days_since_time_sync = 0;
  while (true) {
    // Resync the clock to NTP time every 10 days.
    if (++days_since_time_sync > 10) {
      if (setTime(false)) {
        days_since_time_sync = 0;
        gpio_put(RED_LED, false);
      } else {
        gpio_put(RED_LED, true);
      }
    }
    // If the lamp is currently on then we need to turn it off and sleep until it is time to turn it off
    unsigned int sleep_time;
    rtc_get_datetime(&t);
    sunset = computeSunset(getYearDay(t), t.year);
    if (lamp_on) {
      printf("main:  Lamp was ON.  Toggling OFF.");
      gpio_put(RELAY, false);
      gpio_put(BLUE_LED, false);
      sleep_time = sunset - minuteOfDay(t);
    } else {
      printf("main:  Lamp was OFF.  Toggling ON.");
      gpio_put(RELAY, true);
      gpio_put(BLUE_LED, true);
      sleep_time = (minuteOfDay(t) <= TWO_OCLOCK_MINUTES) ?
          (TWO_OCLOCK_MINUTES - minuteOfDay(t)) : (24 * 60 - minuteOfDay(t) + TWO_OCLOCK_MINUTES);
    }
    lamp_on = !lamp_on;
    printf("main:  Sleeping for %d minutes.\n", sleep_time);
    sleep_ms(sleep_time * 60000);
  }
  return 0;
}
