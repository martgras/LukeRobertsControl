#include <Arduino.h>
#include <ArduinoOTA.h>
#include <wifi.h>
#include <ETH.h>
#include <numeric>
#include <stdlib.h>
#include "app_utils.h"
namespace app_utils {

bool AppUtils::inOTA = false;
String AppUtils::hostname_;
String AppUtils::domainname_ = DOMAINNAME;
bool AppUtils::wifi_connected_ = false;
bool AppUtils::eth_connected_ = false;
String AppUtils::ssid_;
String AppUtils::password_;
AppUtils::NetworkMode AppUtils::network_mode_;

uint32_t AppUtils::last_sleep_time = 0;

void AppUtils::begin(bool init_wifi = true) {

  log_d("App begin %d\n", boot_counter);
  inOTA = false;

  if (init_wifi) {
    wifi_connected_ = (start_wifi() == WL_CONNECTED);
    setupOta();
  }
}
//#########################################################################################
/*
#include "driver/adc.h"
#include "esp_wifi.h"
#include "esp_bt.h"
*/

void AppUtils::setup_time() {
  configTzTime(TIMEZONE, NTP_SERVER, "pool.ntp.org");
}

RTC_DATA_ATTR uint16_t AppUtils::boot_counter = 0;

std::function<bool(void)> AppUtils::on_network_connect = []() { return true; };

int AppUtils::wifi_signal_ = 0;

void AppUtils::setupOta(
    std::function<void(unsigned int, unsigned int)> on_progress) {

  ArduinoOTA.onStart([]() {
              inOTA = true;
              String type;
              if (ArduinoOTA.getCommand() == U_FLASH)
                type = "sketch";
              else // U_SPIFFS
                type = "filesystem";

              // NOTE: if updating SPIFFS this would be the place to unmount
              // SPIFFS using SPIFFS.end()
              log_i("Start updating %s", type);
            })
      .onEnd([]() {
        log_i("OTA End");
        inOTA = false;
        delay(2000);
        ESP.restart();
      })
      .onProgress(on_progress)
      .onError([](ota_error_t error) {
        log_e("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR)
          log_e("Auth Failed");
        else if (error == OTA_BEGIN_ERROR)
          log_e("Begin Failed");
        else if (error == OTA_CONNECT_ERROR)
          log_e("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR)
          log_e("Receive Failed");
        else if (error == OTA_END_ERROR)
          log_e("End Failed");
      });
  log_i("- no services advertised");
  log_i("\nReady for OTA ");
  String otaname = hostname_ + domainname_;
  ArduinoOTA.setHostname(otaname.c_str());
  ArduinoOTA.begin();
}

static RTC_DATA_ATTR struct _wifi_cfgbuf {
  byte mac[6];
  byte mode = 0;
  byte chl;
  uint32_t ip;
  uint32_t gw;
  uint32_t msk;
  uint32_t dns;
  uint16_t localPort;
  uint32_t chk;
} wifi_cfgbuf;

bool AppUtils::checkCfg() {
  uint32_t x = 0;
  uint32_t *p = (uint32_t *)wifi_cfgbuf.mac;
  for (uint32_t i = 0; i < sizeof(wifi_cfgbuf) / 4; i++)
    x += p[i];
  log_d("RTC read: chk=%x x=%x ip=%08x mode=%d %s\n", wifi_cfgbuf.chk, x,
        wifi_cfgbuf.ip, wifi_cfgbuf.mode, x == 0 ? "OK" : "FAIL");
  if (x == 0 && wifi_cfgbuf.ip != 0)
    return true;
  p = (uint32_t *)wifi_cfgbuf.mac;

  // bad checksum, init data
  for (uint32_t i = 0; i < 6; i++)
    wifi_cfgbuf.mac[i] = 0xff;
  wifi_cfgbuf.mode = 0; // chk err, reconfig
  wifi_cfgbuf.chl = 0;
  wifi_cfgbuf.ip = IPAddress(0, 0, 0, 0);
  wifi_cfgbuf.gw = IPAddress(0, 0, 0, 0);
  wifi_cfgbuf.msk = IPAddress(255, 255, 255, 0);
  wifi_cfgbuf.dns = IPAddress(0, 0, 0, 0);
  wifi_cfgbuf.localPort = 10000;
  return false;
}

void AppUtils::writecfg(void) {
  // save new info
  uint8_t *bssid = WiFi.BSSID();
  for (uint32_t i = 0; i < sizeof(wifi_cfgbuf.mac); i++)
    wifi_cfgbuf.mac[i] = bssid[i];
  wifi_cfgbuf.chl = WiFi.channel();
  wifi_cfgbuf.ip = WiFi.localIP();
  wifi_cfgbuf.gw = WiFi.gatewayIP();
  wifi_cfgbuf.msk = WiFi.subnetMask();
  wifi_cfgbuf.dns = WiFi.dnsIP();
  // printf("BSSID: %x:%x:%x:%x:%x:%x\n", wifi_cfgbuf.mac[0],
  // wifi_cfgbuf.mac[1],
  // wifi_cfgbuf.mac[2],
  //    wifi_cfgbuf.mac[3], wifi_cfgbuf.mac[4], wifi_cfgbuf.mac[5]);
  // recalculate checksum
  uint32_t x = 0;
  uint32_t *p = (uint32_t *)wifi_cfgbuf.mac;
  for (uint32_t i = 0; i < sizeof(wifi_cfgbuf) / 4 - 1; i++)
    x += p[i];
  wifi_cfgbuf.chk = -x;
  log_d("RTC write: chk=%x x=%x ip=%08x mode=%d\n", wifi_cfgbuf.chk, x,
        wifi_cfgbuf.ip, wifi_cfgbuf.mode);
}

void AppUtils::keep_wifi_alive(void *) {
  for (;;) {
    if (WiFi.status() == WL_CONNECTED) {
      vTaskDelay(10000 / portTICK_PERIOD_MS);
      continue;
    }
    log_w("trying to reconnect wifi");
    start_wifi();
  }
}

void AppUtils::start_network_keepalive() {
  if (network_mode_ == NetworkMode::kWifi) {
    xTaskCreate(keep_wifi_alive,
                "keepWiFiAlive", // Task name
                8192,            // Stack size (bytes)
                nullptr,         // Parameter
                1,               // Task priority
                nullptr          // Task handle
                );
  }
}

//#########################################################################################
uint8_t AppUtils::start_wifi() {
  bool haveConfig = checkCfg();
  network_mode_ = NetworkMode::kWifi;

  static RTC_DATA_ATTR int wifi_connect_count = 0;
  static RTC_DATA_ATTR int wifi_workaround_count = 0;

  ESP_LOGI(TAG, "Cpu : %d", getCpuFrequencyMhz()); // Get CPU clock
  ESP_LOGI(TAG, "Connecting to: %s", ssid_);

  uint8_t connectionStatus;

  // WiFi.disconnect(true);
  connectionStatus = WiFi.status();
  // delay(50);
  WiFi.mode(WIFI_STA); // switch off AP
  WiFi.setAutoConnect(false);
  WiFi.setAutoReconnect(false);
  WiFi.persistent(false);

  // Speeds up Wifi connection quite a bit for my setup (Fritzbox used)
  // Without it roughly every second connection attempt takes forever
  // and sometimes even required a reboot . Using deepsleep instead of
  // reboot also preserves RTC vars and therefore uses the fastconnect path
  // TODO : test if the complicated retry attempt logic in the while loop is
  // still required
  // Ref: https://github.com/espressif/arduino-esp32/issues/2501

  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    log_e("WiFi lost connection. Reason: %d", info.disconnected.reason);
    wifi_connected_ = false;
    if (info.disconnected.reason == 202) {
      wifi_workaround_count++;
      log_e("Connection failed, REBOOT/SLEEP!");
      esp_sleep_enable_timer_wakeup(10);
      delay(100);
      esp_deep_sleep_start();
    }
  }, WiFiEvent_t::SYSTEM_EVENT_STA_DISCONNECTED);

  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    setup_time();
    wifi_connected_ = true;
    on_network_connect();
  }, WiFiEvent_t::SYSTEM_EVENT_STA_GOT_IP);

  unsigned long start = millis();
  // WiFi.setTxPower(WIFI_POWER_MINUS_1dBm );
  bool AttemptConnection = true;

  if (haveConfig &&
      WiFi.config(wifi_cfgbuf.ip, wifi_cfgbuf.gw, wifi_cfgbuf.msk,
                  wifi_cfgbuf.dns)) {
    log_i(" (use fastconnect )");
    WiFi.setHostname(hostname_.c_str());
    WiFi.begin(ssid_.c_str(), password_.c_str(), wifi_cfgbuf.chl,
               wifi_cfgbuf.mac);
  } else {
    log_i(" (fresh start)");
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
    WiFi.setHostname(hostname_.c_str());
    WiFi.begin(ssid_.c_str(), password_.c_str());
  }

  delay(100);
  while (AttemptConnection) {

    connectionStatus = WiFi.status();
    if (millis() > start + 20000) { // Wait 15-secs maximum
      AttemptConnection = false;
    }
    if (connectionStatus == WL_CONNECTED ||
        connectionStatus == WL_CONNECT_FAILED) {
      if (connectionStatus == WL_CONNECT_FAILED) {
        if (millis() > (start + 10000)) {
          WiFi.disconnect(true);
          log_i("WIFI try again ..");
          connectionStatus = WiFi.status();
          delay(500);
          WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
          WiFi.setHostname(hostname_.c_str());
          WiFi.begin(ssid_.c_str(), password_.c_str()); //,0,bssid);
          delay(4500); // from now on retry with a 5 sec interval
        }
      } else {
        AttemptConnection = false;
      }
    }
    delay(1000);
    log_d("WiFi connection status in loop: %d, millis=%ld\n", connectionStatus,
          millis());
  }
  log_d("WiFi connection status: %d\r\n", connectionStatus);
  if (connectionStatus == WL_CONNECTED) {
    wifi_connect_count++;
    wifi_signal_ = WiFi.RSSI();
    log_i("WiFi connected at: %s MAC: %s", WiFi.localIP().toString().c_str(),
          WiFi.BSSIDstr().c_str());

    log_i("Wifi connections %d - workaround used : %d\n", wifi_connect_count,
          wifi_workaround_count);

    writecfg();
  } else {
    log_e("*** WiFi connection FAILED ***");

    switch (connectionStatus) {
    case 0:
      log_i("*** WL_IDLE_STATUS ***");
      break; // temporary status assigned when WiFi.begin() is called and
             // remains active until the number of attempts expires
    case 1:
      log_e("*** WL_NO_SSID_AVAIL ***");
      break; // assigned when no SSID or requested SSID are available
    case 2:
      log_i("*** WL_SCAN_COMPLETED ***");
      break; // assigned when the scan networks is completed
    case 3:
      log_i("*** WL_CONNECTED ***");
      break; // assigned when connected to a WiFi network
    case 4:
      log_e("*** WL_CONNECT_FAILED ***");
      break; // assigned when the connection fails for all the attempts
    case 5:
      log_w("*** WL_CONNECTION_LOST ***");
      break; // assigned when the connection is lost
    case 6:
      log_w("*** WL_DISCONNECTED ***");
      break; // assigned when disconnected from a network;
    default:
      log_e("*** WL_UNKNOWN_CONNECTION_STATUS ***");
      break;
    }
    log_e("*** No Wifi ESP rebooting ***");
    delay(2000);
    ESP.restart();
  }
  // else Serial.println("WiFi connection *** FAILED ***");
  return connectionStatus;
}

void AppUtils::eth_event(WiFiEvent_t event) {
  switch (event) {
  case SYSTEM_EVENT_ETH_START:
    log_i("ETH Started");
    // set eth hostname here
    eth_connected_ = false;
    ETH.setHostname(HOSTNAME);
    break;
  case SYSTEM_EVENT_ETH_CONNECTED:
    log_i("ETH Connected");
    break;
  case SYSTEM_EVENT_ETH_GOT_IP:
    log_i("ETH MAC: %s  IPv4: %s", ETH.macAddress().c_str(),
          ETH.localIP().toString().c_str());
    if (ETH.fullDuplex()) {
      log_i("FULL_DUPLEX");
    }
    log_i("Speed: %d", ETH.linkSpeed());

    eth_connected_ = true;
    on_network_connect();
    break;
  case SYSTEM_EVENT_ETH_DISCONNECTED:
    log_i("ETH Disconnected");
    eth_connected_ = false;
    break;
  case SYSTEM_EVENT_ETH_STOP:
    log_i("ETH Stopped");
    eth_connected_ = false;
    break;
  default:
    break;
  }
}

uint8_t AppUtils::start_eth(bool wait_for_connection = true) {

  network_mode_ = NetworkMode::kEthernet;
  WiFi.onEvent(AppUtils::eth_event);
  WiFi.setAutoReconnect(true);
  ETH.begin();
  ETH.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
  ETH.setHostname(HOSTNAME);
  while (!eth_connected_) {
    vTaskDelay(500);
  }
  return eth_connected_ ? 1 : 0;
}

void AppUtils::stop_wifi() {
  log_i("Wifi stopping..");
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
}

void AppUtils::loop() { ArduinoOTA.handle(); }

// Find a json value
// This is NOT a complete json parser
//   the code searches for the name of the value followed by a colon and parses
//   anything else after the colon as the value
//   quotes around strings are removed
//   this won't work for non trival json content where the same name is used
//   nultiple times
//   ok:  { "brightness" : 50 , "scene" : "1" }  is ok
//	 if the same name occcurs more than once the code still returns only the
//first match
//   not working:  { "lamps" : [ {"brightness" : 50 , "scene" : "1" } ,
//   {"brightness" : 22 , "scene" : "1" } ] }"
//
bool find_jsonvalue_(const char *json, const char *name, const char **start,
                     const char **end) {
  char token[64];
  token[0] = '\"';
  strncpy(token + 1, name, sizeof(token) - 2);
  token[strlen(name) + 1] = '\"';
  token[strlen(name) + 2] = '\0';

  const char *ptr = strstr(json, token);
  bool has_quotes = false;
  bool success = false;
  if (ptr) {
    ptr += strlen(token);
    while (*ptr && *ptr != ':') {
      ptr++;
    }
    // found a colon ?
    if (*ptr) {
      ptr++; // skip the colon

      // skip all spaces after colon
      while (*ptr && isspace(*ptr)) {
        ptr++;
      }
      // We should point to the value if numeric or the opening quote
      if (*ptr) {
        if (*ptr == '\'' || *ptr == '\"') {
          has_quotes = true;
          ptr++;
        }
        if (*ptr) {
          *start = ptr;
          *end = ptr;
          if (has_quotes) {
            *end = strpbrk(ptr, "\"\'");
          } else {
            while (*ptr != '\0' && !isspace(*ptr) && *ptr != ',' &&
                   *ptr != '}' && *ptr != ']') {
              ptr++;
            }
            *end = ptr;
          }
          success = true;
        }
      }
    }
  }
  return success;
}

bool get_jsonvalue(const char *json, const char *name, long &result) {

  const char *start = 0, *end = 0;
  bool success = false;
  if (find_jsonvalue_(json, name, &start, &end)) {
    if (*start) {
      // skip leasing zero
      if (*start == '0') {
        success = true;
        result = 0;
        while (*start == '0') {
          start++;
        }
      }
      if (isdigit(*start)) {
        success = true;
        result = strtol(start, nullptr, 10);
      }
    }
  }
  log_v("JSON VAL %s %ld" ,name, result);
  return success;
}

// Get a json value as a number
long get_jsonvalue(const char *json, const char *name) {
  long result = 0;
  if (get_jsonvalue(json, name, result)) {
    return result;
  } else {
    return LONG_MAX;
  }
}

//
// Get a json value a string
//
bool get_jsonvalue(const char *json, const char *name, char *result,
                   size_t result_max) {
  const char *start = 0, *end = 0;

  auto r = find_jsonvalue_(json, name, &start, &end);
  if (r) {
    strncpy(result, start, end - start + 1);
    result[end - start] = '\0';
  }
  return r;
}

} // namepsace deepsleep_app
