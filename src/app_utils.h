#include <functional>
#include <vector>
#include <queue>
#include <chrono>
namespace app_utils {

using Task_Function = std::function<bool(void)>;

class AppUtils {

public:
  enum class NetworkMode { kNone = 0, kWifi = 1, kEthernet = 2 };

  AppUtils() {}

  AppUtils &set_hostname(const char *hostname) {
    hostname_ = hostname;
    return *this;
  }

  AppUtils &set_ota(uint16_t ota_seconds_on_first_boot) { return *this; }

  void begin(bool start_wifi);
  static void begin_sleep();
  static void setup_time();
  static void loop();
  static void
  setupOta(std::function<void(unsigned int, unsigned int)> on_progress =
               [](unsigned int progress, unsigned int total) {
                 log_i("Progress: %u%%\r", (progress / (total / 100)));
               });
#ifndef USE_ETHERNET

  bool request_wifi_start() { return start_wifi(); }

  AppUtils &set_ssid(const char *ssid) {
    ssid_ = ssid;
    return *this;
  }

  AppUtils &set_password(const char *password) {
    password_ = password;
    return *this;
  }
  static int wifi_signal() { return wifi_signal_; }
  static bool start_wifi();
  static void stop_wifi();

  static bool wifi_connected() { return wifi_connected_; }

#else
  static bool start_eth(bool wait_for_connection);
  static bool eth_connected() { return eth_connected_; }
#endif
  static bool network_connected() { return wifi_connected_ || eth_connected_; }
  static bool start_network() {
#ifdef USE_ETHERNET
    return start_eth(true);
#else
    eth_connected_ = false;
    return start_wifi();
#endif
  }

  static void stop_network() {
#ifdef USE_ETHERNET
    return;
#else
    eth_connected_ = false;

    stop_wifi();
#endif
  }
  static void start_network_keepalive();
  static std::function<bool(void)> on_network_connect;

  void static fast_restart() {
    esp_sleep_enable_timer_wakeup(10);
    delay(100);
    esp_deep_sleep_start();
  }

  bool ota_started() { return inOTA; }

private:
  static void keep_wifi_alive(void *parameter);
  static void keep_eth_alive(void *parameter);
  static bool inOTA;
  static RTC_DATA_ATTR uint16_t boot_counter;
  static RTC_DATA_ATTR uint32_t last_sleep_time;

  static String ssid_;
  static String password_;
  static String hostname_;
  static String domainname_;
  static int wifi_signal_;
  static bool eth_connected_;
  static bool wifi_connected_;
  static NetworkMode network_mode_;

  static bool checkCfg();
  static void writecfg();
  static void eth_event(WiFiEvent_t event);
};

long get_jsonvalue(const char *json, const char *name);
bool get_jsonvalue(const char *json, const char *name, long &result);
bool get_jsonvalue(const char *json, const char *name, char *result,
                   size_t result_max);
}