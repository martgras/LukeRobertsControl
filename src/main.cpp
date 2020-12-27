#define ETH_CLK_MODE ETH_CLOCK_GPIO17_OUT
#define ETH_PHY_POWER 12

#include <string>
#include <Arduino.h>
//#include <ETH.h>
//#include <WiFi.h>
#include <AsyncTCP.h>
#include <SPIFFS.h>
#include <ESPAsyncWebServer.h>
#include "app_utils.h"
#include "LukeRobertsBle.h"
#include "esp_wifi.h"
#include "WiFiGeneric.h"
#include "webpages.h"
#include "mqtt_handler.h"

#ifdef ROTARY
#include "rotaryencoder.h"
#endif

using namespace app_utils;
using namespace rotary_encoder;

///#define RELAY_PIN GPIO_NUM_13
WiFiClient network_client;
AppUtils app;

extern boolean ble_connected;
extern uint8_t max_scenes;
extern std::map<uint8_t, std::string> scenes;

// RTC_DATA_ATTR bool powerstate = false;
// RTC_DATA_ATTR int16_t dimlevel = 50;
// RTC_DATA_ATTR int16_t colortemperature = 3000;
// RTC_DATA_ATTR int16_t scene = 01;

// publishing mqtt message is a blocking operation
// use a task instead that sends queues messages

MqttPublish mqtt;

class LR_Ble_Device {
public:
  using Mqtt_Report_Function = std::function<void(int)>;
  using Task_function = std::function<void(void *)>;

  enum slots { kScene = 0, kBrightness = 1, kColortemp = 2 };

  struct State {
    bool power;
    uint8_t scene;
    uint8_t brightness;
    uint16_t mired;
    uint16_t kelvin;
  };

  const State &state() { return state_; }
  BleGattClient &client() { return gatt_client_; }

  LR_Ble_Device() {

    // Setup the cached commands
    //
    BleGattClient::BleCommand cmd;
    cmd = {{0xA0, 0x01, 0x03, 0x00},
           4,
           false,
           [&](int newval) {
             char json[32];
             snprintf(json, sizeof(json), "%d", state().brightness);
             mqtt.queue("stat/" HOSTNAME "/DIMMER", json);
           }};
    gatt_client_.queue_cmd(cmd, (uint8_t)kBrightness, false);

    cmd = {{0xA0, 0x01, 0x04, 0x00, 0x00},
           5,
           false,
           [&](int) {
             char json[32];
             snprintf(json, sizeof(json), "%d", state().mired);
             mqtt.queue("stat/" HOSTNAME "/CT", json);
           }};
    gatt_client_.queue_cmd(cmd, (uint8_t)kColortemp, false);

    cmd = {{0xA0, 0x02, 0x05, 0x00},
           4,
           false,
           [&](int) {
             if (state().power) {
               char json[32];
               snprintf(json, sizeof(json), "%d", state_.scene);
               mqtt.queue("stat/" HOSTNAME "/SCENE", json);
             }
           }};
    gatt_client_.queue_cmd(cmd, (uint8_t)kScene, false);
  }

  uint8_t set_dimmer(unsigned int new_dim_level, bool force_dirty = false) {
    if (new_dim_level > 99)
      new_dim_level = 99;
    if (new_dim_level < 0)
      new_dim_level = 0;
    gatt_client_.cached_commands[kBrightness].is_dirty |=
        force_dirty || (state_.brightness != new_dim_level);
    state_.brightness = new_dim_level;
    gatt_client_.cached_commands[kBrightness].data[3] = state_.brightness;
    log_d("Brightness: %d %d  ",
          gatt_client_.cached_commands[kBrightness].is_dirty,
          state_.brightness);

    sync_powerstate(gatt_client_.cached_commands[kBrightness].is_dirty);
    return state_.brightness;
  }

  unsigned int switch_kelvin_mired(unsigned int value) {
    return (1000000 / value);
  }

  void set_intermmediate_light(uint8_t content_flag, uint16_t duration,
                               uint8_t saturation, uint16_t hue,
                               uint16_t kelvin, uint8_t brightness) {

    BleGattClient::BleCommand cmd;
    cmd.data[0] = 0xA0;
    cmd.data[1] = 0x01;
    cmd.data[2] = 0x02;
    cmd.data[3] = content_flag;
    cmd.data[4] = duration >> 8;
    cmd.data[5] = duration & 0xFF;
    uint8_t brightness_pos = 8;
    if (content_flag & 2) {
      cmd.data[6] = kelvin >> 8;
      cmd.data[7] = kelvin & 0xFF;
      brightness_pos = 8;
      if (duration == 0) { // permanent ?
        state_.brightness = (brightness / 2.56) + 0.5;
        state_.kelvin = kelvin;
        state_.mired = switch_kelvin_mired(kelvin);
        cmd.on_send = [&](int) {
          char json[32];
          snprintf(json, sizeof(json), "%d", state().brightness);
          mqtt.queue("stat/" HOSTNAME "/DIMMER", json);
          snprintf(json, sizeof(json), "%d", state().mired);
          mqtt.queue("stat/" HOSTNAME "/CT", json);
          mqtt.queue("stat/" HOSTNAME "/RESULT", this->create_state_message(),
                     true);
        };
      }
    } else {
      cmd.data[6] = saturation;
      cmd.data[7] = hue >> 8;
      cmd.data[8] = hue & 0xFF;
      brightness_pos = 9;
      if (duration == 0) { // permanent ?
        cmd.on_send = [&, saturation, hue, brightness](int newval) {
          char json[32];
          snprintf(json, sizeof(json), "%d", saturation);
          mqtt.queue("stat/" HOSTNAME "/SATURATION", json);
          snprintf(json, sizeof(json), "%d", hue);
          mqtt.queue("stat/" HOSTNAME "/HUE", json);
          snprintf(json, sizeof(json), "%d", brightness);
          mqtt.queue("stat/" HOSTNAME "/BRIGHTNESS_UP", json);
        };
      }
    }
    cmd.data[brightness_pos] = brightness;
    cmd.is_dirty = true;
    cmd.size = brightness_pos + 1;
    gatt_client_.queue_cmd(cmd);
  }

  void set_intermmediate_uplight(uint16_t duration, uint8_t saturation,
                                 uint16_t hue, uint8_t brightness) {
    set_intermmediate_light(1, duration, saturation, hue, 0, brightness);
  }

  void set_intermmediate_downlight(uint16_t duration, uint16_t kelvin,
                                   uint8_t brightness) {
    set_intermmediate_light(2, duration, 0, 0, kelvin, brightness);
  }

  uint16_t set_colortemperature_mired(unsigned new_colortemperature,
                                      bool force_dirty = false) {
    if (new_colortemperature < 250)
      new_colortemperature = 250;
    if (new_colortemperature > 370)
      new_colortemperature = 370;
    auto kelvin = switch_kelvin_mired(new_colortemperature);
    gatt_client_.cached_commands[kColortemp].data[3] = kelvin >> 8;
    gatt_client_.cached_commands[kColortemp].data[4] = kelvin & 0xFF;
    gatt_client_.cached_commands[kColortemp].is_dirty |=
        force_dirty || (state_.mired != new_colortemperature);
    log_i("Color temperature to %d from %d (%d)", new_colortemperature,
          state_.mired, kelvin);
    state_.mired = new_colortemperature;
    state_.kelvin = switch_kelvin_mired(new_colortemperature);
    return new_colortemperature;
  }

  uint16_t set_colortemperature_kelvin(unsigned new_colortemperature,
                                       bool force_dirty) {
    if (new_colortemperature > 4000)
      new_colortemperature = 4000;
    if (new_colortemperature < 2700)
      new_colortemperature = 2700;
    gatt_client_.cached_commands[kColortemp].data[3] =
        new_colortemperature >> 8;
    gatt_client_.cached_commands[kColortemp].data[4] =
        new_colortemperature & 0xFF;
    gatt_client_.cached_commands[kColortemp].is_dirty |=
        force_dirty || (state_.kelvin != new_colortemperature);
    state_.kelvin = new_colortemperature;
    state_.mired = switch_kelvin_mired(new_colortemperature);
    return new_colortemperature;
  }

  uint8_t set_scene(unsigned int new_scene, bool force_dirty = false) {
    new_scene &= 0xFF;
    gatt_client_.cached_commands[kScene].data[3] = new_scene;
    gatt_client_.cached_commands[kScene].is_dirty |=
        force_dirty || (state_.scene == 0 || state_.scene != new_scene);
    state_.scene = new_scene;
    if (state_.brightness == 0) {
      state_.brightness = SceneBrightnessMapper::map(state_.scene);
    }
    gatt_client_.cached_commands[kBrightness].on_send(state_.brightness);
    return state_.scene;
  }

  bool sync_powerstate(bool powerstate) {
    if (state_.power != powerstate) {
      mqtt.queue("stat/" HOSTNAME "/POWER", powerstate ? "ON" : "OFF");
    }
    state_.power = powerstate;
    return powerstate;
  }

  bool get_powerstate() { return state_.power; }
  bool set_powerstate(bool new_power_state, bool force_dirty = false) {

    if (state_.scene == 0 && new_power_state) {
      // Should only happen during startup
      state_.scene = 0x1;
    }
    gatt_client_.cached_commands[kScene].is_dirty |=
        force_dirty || (new_power_state != state_.power);
    state_.power = new_power_state;

    gatt_client_.cached_commands[kScene].data[3] =
        (new_power_state ? state_.scene : 0);

    if (state_.power && gatt_client_.cached_commands[kScene].is_dirty) {
      set_dimmer(state_.brightness, true);
      //      set_colortemperature_mired(state_.mired,true);
    }
    return state_.power;
  }

  void send_custom(const uint8_t *data, size_t length) {
    BleGattClient::BleCommand custom;
    custom.is_dirty = true;
    custom.size = length;
    for (int i = 0; i < length && i < sizeof(custom.data); i++) {
      custom.data[i] = data[i];
    }
    gatt_client_.queue_cmd(custom);
  }

  const char *create_state_message() {
    static char statemsg[512];
    time_t now = time(0);

    // Convert now to tm struct for local timezone
    tm *t = localtime(&now);

    snprintf(statemsg, sizeof(statemsg),
             "{\"Time\":"
             "\"%04d-%02d-%02dT%02d:%02d:%02d\","
             "\"Heap\":%u,\"IPAddress\":\"%s\",\"POWER\":\"%s\",\"CT\":"
             "%d,\"KELVIN\":%d,\"DIMMER\":%d,\"SCENE\":%d}",
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour,
             t->tm_min, t->tm_sec, ESP.getFreeHeap() / 1024,
             WiFi.localIP().toString().c_str(), state_.power ? "ON" : "OFF",
             state_.mired, state_.kelvin, state_.brightness, state_.scene);
    return statemsg;
  }

  void loop() {
    gatt_client_.loop([&]() {
      mqtt.queue("stat/" HOSTNAME "/RESULT", this->create_state_message(),
                 true);
    });
  }

private:
  volatile bool sending = false;
  BleGattClient gatt_client_;
  State state_;
};

RTC_DATA_ATTR LR_Ble_Device lr;
AsyncWebServer server(80);

void notFound(AsyncWebServerRequest *request) {
  request->send(404, "text/plain", "Not found");
}

///// Command parsing //////////////
bool get_powerstate() { return lr.get_powerstate(); }

bool set_powerstate(bool value) {

  if (mqtt.connected()) {
    char json[32];
    mqtt.queue("stat/" HOSTNAME "/RESULT", lr.create_state_message());
    snprintf(json, sizeof(json), "%d", lr.state().brightness);
    mqtt.queue("stat/" HOSTNAME "/POWER", value ? "ON" : "OFF");
  }
#if (RELAY_PIN == 0)
  lr.set_powerstate(value, true);
#else
  digitalWrite(RELAY_PIN, value ? HIGH : LOW);
  lr.sync_powerstate(value);

#endif

  return get_powerstate();
}
bool set_powerstate(const String &value) {
  if (value.equals("0") || value.equals("off") || value.equals("false") ||
      value.equals("0")) {
    set_powerstate(false);
  } else if (value.equals("1") || value.equals("on") || value.equals("true") ||
             value.equals("1")) {
    set_powerstate(true);
  } else if (value.equals("toggle")) {
    set_powerstate(!get_powerstate());
  }
  return get_powerstate();
}

int get_dimmer_value() { return lr.state().brightness; }

int set_dimmer_value(int new_level) {
  if (new_level > 100) {
    new_level = 100;
  }
  if (new_level < 0) {
    new_level = 0;
  }
  log_i("Set Dimmer Level to %d", new_level);

  lr.set_dimmer(new_level, true);
  return new_level;
}

int set_dimmer_value(const String &value) {
  bool parsing_success = false;
  char *p;
  long numvalue = strtol(value.c_str(), &p, 10);
  auto brightness = lr.state().brightness;
  if (*p) {
    // p points to the char after the last digit. so if value is a number it
    // points to the terminating \0

    // is there another param e.g. "dimmer up 5"
    auto param = value.indexOf(' ');
    String param_value;
    String cmd;
    int step_value = 10;
    if (param > 0) {
      param_value = value.substring(param + 1);
      cmd = value.substring(0, param);
      step_value = atol(param_value.c_str());
    } else {
      cmd = value;
    }
    if (cmd.equals("+") || cmd.equals("up")) {
      parsing_success = true;
      brightness += step_value;
      log_d("Increase Dimmer Level to %d", brightness);
    } else {
      if (cmd.equals("-") || cmd.equals("down")) {
        parsing_success = true;
        brightness -= step_value;
      }
    }
  } else {
    parsing_success = true;
    brightness = (int)numvalue;
  }
  if (parsing_success) {
    set_dimmer_value(brightness);
  }
  return brightness;
}

int get_scene() { return lr.state().scene; }

int set_scene(int numvalue) {
  auto scene = numvalue & 0xFF;
  if (scene > scenes.size() && scene != 0xFF)
    scene = 1;
  if (scene < 1 && scene != 0xFF)
    scene = scenes.size();

  log_i("Set Scene Level to %d", scene);
  lr.set_scene(scene, true);
  return scene;
}

int set_scene(const String &value) {
  bool parsing_success = false;
  char *p;
  long numvalue = strtol(value.c_str(), &p, 10);
  auto scene = lr.state().scene;
  if (*p) {
    log_d("Scene %s", value.c_str());
    // p points to the char after the last digit. so if value is a number it
    // points to the terminating \0

    // is there another param e.g. "scene up 2"
    auto param = value.indexOf(' ');
    String param_value;
    String cmd;
    int step_value = 1;
    if (param > 0) {
      param_value = value.substring(param + 1);
      cmd = value.substring(0, param);
      step_value = atol(param_value.c_str());
    } else {
      cmd = value;
    }
    if (cmd.equals("+") || cmd.equals("up")) {
      parsing_success = true;
      scene += step_value;
      log_i("Increase scene %d", scene);
    } else {
      if (cmd.equals("-") || cmd.equals("down")) {
        parsing_success = true;
        scene -= step_value;
      }
    }
  } else {
    parsing_success = true;
    scene = (uint8_t)numvalue;
  }
  if (parsing_success) {
    set_scene(scene);
  }
  return scene;
}

int set_colortemperature_mired(int numvalue) {
  if (numvalue > 370) {
    numvalue = 370;
  }
  if (numvalue < 250) {
    numvalue = 250;
  }
  log_i("Set Color temperature to %d mired", numvalue);

  lr.set_colortemperature_mired(numvalue, true);
  return lr.state().mired;
}

int set_colortemperature_kelvin(int numvalue) {
  if (numvalue > 4000) {
    numvalue = 4000;
  }
  if (numvalue < 2700) {
    numvalue = 2700;
  }
  log_i("Set Color temperature to %d kelvin", numvalue);

  lr.set_colortemperature_kelvin(numvalue, true);
  return lr.state().kelvin;
}
int set_colortemperature(const String &value, bool use_kelvin = false) {
  bool parsing_success = false;
  char *p;
  long numvalue = strtol(value.c_str(), &p, 10);
  auto current_value = use_kelvin ? lr.state().kelvin : lr.state().mired;
  if (*p && *p != '.') {
    // p points to the char after the last digit. so if value is a number it
    // points to the terminating \0

    // is there another param e.g. "dimmer up 5"
    auto param = value.indexOf(' ');
    String param_value;
    String cmd;
    int step_value = use_kelvin ? 10 : 1;
    if (param > 0) {
      param_value = value.substring(param + 1);
      cmd = value.substring(0, param);
      step_value = atol(param_value.c_str());
    } else {
      cmd = value;
    }
    if (cmd.equals("+") || cmd.equals("up")) {
      parsing_success = true;
      current_value += step_value;
      log_i("Increase Color Temperature to %d", current_value);
    } else {
      if (cmd.equals("-") || cmd.equals("down")) {
        parsing_success = true;
        current_value -= step_value;
      }
    }
  } else {
    parsing_success = true;
    current_value = (int)numvalue;
  }
  if (parsing_success) {
    if (use_kelvin) {
      set_colortemperature_kelvin(current_value);
    } else {
      set_colortemperature_mired(current_value);
    }
  }
  return current_value;
}

int set_scene_brightness(const String &value) {
  char *p;
  strtol(value.c_str(), &p, 10);
  if (*p && *p != '.') {
    log_d("Map Scene %s", value.c_str());
    // p points to the char after the last digit. so if value is a number it
    // points to the terminating \0

    // is there another param e.g. "scene up 2"
    auto param = value.indexOf(' ');

    String param_value;
    String cmd;
    if (param > 0) {
      uint8_t scene;
      int dim_level = 50;
      param_value = value.substring(param + 1);
      cmd = value.substring(0, param);
      scene = atol(cmd.c_str());
      dim_level = atol(param_value.c_str());
      SceneBrightnessMapper::set(scene, dim_level);
    }
  }
  return SceneBrightnessMapper::size();
}

void queue_ble_command(const String &value) {
  int len = value.length() / 2;
  char *p;
  unsigned long byte;
  uint8_t ble_data[16];
  char two_chars[3];
  const char *numptr = value.c_str();
  for (int i = 0; i < len && i < sizeof(ble_data); i++) {
    two_chars[0] = *numptr++;
    two_chars[1] = *numptr++;
    two_chars[2] = '\0';
    byte = strtoul(two_chars, &p, 16);
    ble_data[i] = byte & 0xFF;
    log_v(" BLE custom %d : %d", i, ble_data[i]);
  }

  lr.send_custom(ble_data, len);
}

bool set_uplight(const char *json) {
  long duration = 0;
  long saturation = 0;
  long hue = 0;
  long brightness = 0;

  bool success = false;
  if (!get_jsonvalue(json, "duration", duration)) {
    success = get_jsonvalue(json, "d", duration);
  }

  success = get_jsonvalue(json, "saturation", saturation);

  if (!success) {
    success = get_jsonvalue(json, "s", saturation);
  }
  if (success) {
    success = get_jsonvalue(json, "hue", hue);
    if (!success) {
      success = get_jsonvalue(json, "h", hue);
    }
  }
  if (success) {
    success = get_jsonvalue(json, "brightness", brightness);
    if (!success) {
      success = get_jsonvalue(json, "b", brightness);
    }
  }
  if (success) {
    lr.set_intermmediate_uplight(duration, saturation, hue, brightness);
  }
  return success;
}

bool set_downlight(const char *json) {
  long duration = 0;
  long kelvin = 0;
  long brightness = 0;

  bool success = false;

  char onoff_state[16];
  if (get_jsonvalue(json, "state", onoff_state, sizeof(onoff_state))) {
    String value = onoff_state;
    value.trim();
    value.toLowerCase();
    if (value == "off" || value == "0" || value == "false") {
      set_powerstate(false);
      // exit routine. if state is poweroff no need to parse remaining
      // properties
      return true;
    }
#if (RELAY_PIN == 0)
    lr.set_powerstate(true, false);

#else
    if (value == "on" || value == "1" || value == "true") {
      set_powerstate(true);
    }
#endif
  }
  if (!get_jsonvalue(json, "duration", duration)) {
    success = get_jsonvalue(json, "d", duration);
  }

  success = get_jsonvalue(json, "kelvin", kelvin);
  if (!success) {
    success = get_jsonvalue(json, "k", kelvin);
    if (!success) {
      success = get_jsonvalue(json, "ct", kelvin);
      if (success) {
        kelvin = lr.switch_kelvin_mired(kelvin);
      }
    }
  }
  if (success) {
    success = get_jsonvalue(json, "brightness", brightness);
    if (!success) {
      success = get_jsonvalue(json, "b", brightness);
      if (!success) {
        success = get_jsonvalue(json, "dimmer", brightness);
        brightness = brightness * 255 / 100;
      }
    }
  }
  if (success) {
    lr.set_intermmediate_downlight(duration, kelvin, brightness);
  }
  return success;
}

unsigned long last_mqttping = millis();

bool parse_command(String cmd, String value) {

  log_d("%ld Start Parsing %s %s ", millis(), cmd.c_str(), value.c_str());
  bool has_value = value.length() > 0;

  // is this a json command
  if (cmd.equals("uplight")) {
    if (has_value) {
      set_uplight(value.c_str());
    }
  } else if (cmd.equals("downlight")) {
    if (has_value) {
      set_downlight(value.c_str());
    }
    return true;
  } else if (cmd.equals("power")) {
    if (has_value) {
      set_powerstate(value);
    }
    return true;
  } else if (cmd.equals("dimmer")) {
    if (has_value) {
      set_dimmer_value(value);
    } else { /* return state */
    }
    return true;
  } else if (cmd.equals("scene")) {
    if (has_value) {
      set_scene(value);
    } else { /* return state */
    }
    return true;
  } else if (cmd.equals("ct")) {
    if (has_value) {
      set_colortemperature(value, false);
    } else { /* return state */
    }
    return true;
  } else if (cmd.equals("kelvin")) {
    if (has_value) {
      set_colortemperature(value, true);
    } else { /* return state */
    }
    return true;
  } else if (cmd.equals("mapscene")) {
    if (has_value) {
      set_scene_brightness(value);
    } else { /* return state */
    }
    return true;
  } else if (cmd.equals("ota")) {
    AppUtils::setupOta();
    mqtt.queue("tele/" HOSTNAME "/ota", "waiting for ota start on port 3232");
    return true;
  } else if (cmd.equals("result") || cmd.equals("mqttping")) {
    last_mqttping = millis();
    log_d("got mqtt ping");
    return true;
  } else if (cmd.equals("reboot") || cmd.equals("restart")) {
    log_i("------- REBOOT -------");
    yield();
    delay(500);
    ESP.restart();
  } else if (cmd.equals("blecustom") && has_value) {
    queue_ble_command(value);
  }
  return false;
}
////// PARSING End ///////////

// Replaces placeholder with button section in your web page
String processor(const String &var) {
  // Serial.println(var);
  if (var == "DIMVALUE") {
    return String(lr.state().brightness);
  }
  if (var == "CTVALUE") {
    return String(lr.state().mired);
  }

  if (var == "CHECKED") {
    return lr.state().power ? "checked" : "";
  }
  if (var == "ANAUS") {
    return lr.state().power ? "An" : "Aus";
  }

  if (var == "SCENES") {

    String scene_html = "<br><select id=\"sceneselect\" name=\"scenes\" "
                        "onchange=\"updateScene(this)\"  size=\"" +
                        String(scenes.size() - 1) + String("\" >");
    for (const auto &s : scenes) {
      if (s.first != 0) {
        scene_html += "<option" +
                      String(lr.state().scene == s.first ? " selected" : "") +
                      " value=\"" + String(s.first) + "\">" + s.second.c_str() +
                      "</option>";
      }
    }
    scene_html += "</select>";
    log_v("HTML: %s", scene_html.c_str());
    return scene_html;
  }

  return String();
}

static const char *PARAM_CMD = "cmnd";
#ifdef ROTARY
RotaryEncoderButton rotary;
#endif

void setup() {

//  esp_wifi_stop();
#ifdef USE_ETHERNET
  app.set_hostname(HOSTNAME);
#else
  app.set_hostname(HOSTNAME).set_ssid(WIFISID).set_password(WIFIPASSWORD);
#endif

  app.start_network();
  app.start_network_keepalive();

  while (!app.network_connected()) {
    delay(100);
  }
  mqtt.init(network_client, parse_command);

#ifdef LR_BLEADDRESS
  lr.client().init(NimBLEAddress(LR_BLEADDRESS, 1));
#else
#pragma message(                                                               \
    "NO BLE Device Address provided. Scanning for a Luke Roberts Lamp during startup")
  auto device_addr = scan_for_device();
  log_i("DEVICE : %s", device_addr.toString().c_str());
  lr.client().init(device_addr);
#endif

  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", index_html, processor);
  });

  // Send a GET request to <IP>/get?message=<message>
  server.on("/cm", HTTP_GET, [](AsyncWebServerRequest *request) {
    String message = "";
    if (request->hasParam(PARAM_CMD)) {
      message = request->getParam(PARAM_CMD)->value();
      message.trim();
      message.toLowerCase();
      int position_space = message.indexOf(' ');
      if (position_space != -1) {
        parse_command(message.substring(0, position_space),
                      message.substring(position_space + 1));
      }
      request->send(200, "text/plain", "GET: " + message);
    } else {
      message = "Not a valid command message sent";
      request->send(400, "text/plain", "GET: " + message);
    }
  });

  server.onNotFound(notFound);
  server.begin();

  int mqtt_reconnects = 0;
  last_mqttping = millis();
  mqtt.start();
  while (!mqtt.connected()) {
    if (!mqtt.mqtt_reconnect()) {
      delay(500);
      if (!mqtt.mqtt_reconnect() && mqtt_reconnects++ > 30) {
        log_e("mqtt retry count exceeded - rebooting");
        delay(100);
        ESP.restart();
      }
    }
  }

  app.on_network_connect = mqtt.mqtt_reconnect;

  mqtt.queue("tele/" HOSTNAME "/LWT", "Online", true);

  bool result;
  int attempts = 0;
  while (!(result = lr.client().connect_to_server(
               []() { get_all_scenes(lr.client()); }))) {
    delay(1000);
    if (attempts++ == 10) {
      log_e("unable to connect to BLE Device - restarting");
      ESP.restart();
    }
  }

  if (result) {
    auto initalscene = get_current_scene(lr.client());
    if (initalscene == 0) {
      lr.sync_powerstate(false);
      // if (lr.state().scene == 0)
      //   lr.set_scene(0xFF,false);
    } else {
      lr.sync_powerstate(true);
      ;
      lr.set_scene(initalscene);
    }
  }

#ifdef ROTARY
  ESP_ERROR_CHECK(
      rotary.init(ROTARY_PIN_A, ROTARY_PIN_B, ROTARY_PIN_BUTTON, false));
  auto buttonConfig = rotary.getButtonConfig();

  rotary.set_speedup_times(50, 25);
  rotary.on_rotary_event = [&](rotary_encoder_event_t event) {
    if (!get_powerstate())
      return;

    log_v("Rotary event %d  %d (%d) %ld ", event.state.direction,
          event.state.position, event.state.speed);
    int dimmerlevel = get_dimmer_value();
    int step = 5;
    if (event.state.direction == ROTARY_ENCODER_DIRECTION_CLOCKWISE) {
      step *= event.state.speed;
    } else if (event.state.direction ==
               ROTARY_ENCODER_DIRECTION_COUNTER_CLOCKWISE) {
      step *= -1 * event.state.speed;
    }
    set_dimmer_value(dimmerlevel + step);

  };
  rotary.on_button_event = [&](uint8_t eventType, uint8_t buttonState) {
    if (eventType == AceButton::kEventReleased) {
      set_powerstate(!get_powerstate());
    }
    if (eventType == AceButton::kEventRepeatPressed) {
      set_scene(get_scene() + 1);
    }
  };

  buttonConfig->setFeature(ButtonConfig::kFeatureRepeatPress);
  //  buttonConfig->setFeature(ButtonConfig::kFeatureSuppressAfterLongPress);
  buttonConfig->setRepeatPressDelay(1500);
  buttonConfig->setRepeatPressInterval(1500);
#endif

#if RELAY_PIN != 0
  pinMode(RELAY_PIN, OUTPUT);
#endif
  log_d("Inital State: Power = %d scene %d", get_powerstate(), get_scene());

#ifndef LR_BLEADDRESS
  mqtt.queue("tele/" HOSTNAME "/BLEADDRESS", device_addr.toString().c_str());
#endif
}

#define TAG "LRGateway"

void loop() {
  static unsigned long last_statemsg = 0;
  app_utils::AppUtils::loop();
  // mqtt.loop();
  lr.client().loop([&]() {
    mqtt.queue("stat/" HOSTNAME "/RESULT", lr.create_state_message(), true);
  });

  if (millis() - last_statemsg > 60000) {
    mqtt.queue("stat/" HOSTNAME "/RESULT", lr.create_state_message(), true);
    //   mqtt.queue("cmnd/" HOSTNAME "/mqttping", "ping", false);
    last_statemsg = millis();

    // usually the free heap is around 100k . If it is below 50k I must have a
    // memory leak somewhere. Reboot as a workaround
    if (ESP.getFreeHeap() < 50000) {
      log_e("POSSIBLE MEMORY LEAK detected. Free heap is %d k.  Rebooting",
            ESP.getFreeHeap() / 1024);
      app.fast_restart();
    }
  }
  // restablish mqtt after 10 mins without an incoming ping
  if (0 && millis() - last_mqttping > 1000 * 70) {
    log_e("missing mqtt ping. trying to reconnect");

    app.stop_network();
    mqtt.disconnect();
    delay(100);

    // unclear why reconnecting doesn't always work reliably - just reboot
    app.fast_restart();

    app.start_wifi();
    last_mqttping = millis();
    uint8_t mqtt_reconnects = 0;
    while (!mqtt.connected()) {
      if (!mqtt.mqtt_reconnect()) {
        delay(500);
        if (!mqtt.mqtt_reconnect() && mqtt_reconnects++ > 10) {
          log_e("mqtt retry count exceeded - rebooting");
          delay(100);
          app.fast_restart();
        }
      }
    }
  }
}