#define ETH_CLK_MODE ETH_CLOCK_GPIO17_OUT
#define ETH_PHY_POWER 12

#include <string>
#include <Arduino.h>
//#include <ETH.h>
//#include <WiFi.h>
#include <AsyncTCP.h>

#include <ESPAsyncWebServer.h>
#include <PubSubClient.h>

#include "app_utils.h"
#include <SPIFFS.h>
#include "LukeRobertsBle.h"
#include "esp_wifi.h"
#include "WiFiGeneric.h"
#include <SPIFFS.h>

#include "webpages.h"

#include "mqtt_handler.h"

using namespace app_utils;

WiFiClient network_client;
AppUtils app;

extern boolean ble_connected;
extern uint8_t max_scenes;
extern std::map<uint8_t, std::string> scenes;

// RTC_DATA_ATTR bool powerstate = false;
RTC_DATA_ATTR int16_t dimlevel = 50;
RTC_DATA_ATTR int16_t colortemperature = 3000;
RTC_DATA_ATTR int16_t scene = 01;

// publishing mqtt message is a blocking operation
// use a task instead that sends queues messages

MqttPublish mqtt;

class LR_Ble_Device {
public:
  using Mqtt_Report_Function = std::function<void(int)>;
  using Task_function = std::function<void(void *)>;

  enum slots { kBrightness = 0, kColortemp = 1, kScene = 2 };

  static void report_mqtt() {}

  LR_Ble_Device() {
    ble_default_commands_[kBrightness] = {{0xA0, 0x01, 0x03, 0x00}, 4, false};
    ble_default_commands_[kBrightness].report_to_mqtt = [this](int newval) {
      char json[32];
      //      snprintf(json, sizeof(json), "{\"DIMMER\": \"%d\"}",
      //      this->brightness);
      //      mqtt.queue("stat/" HOSTNAME "/RESULT", json);
      snprintf(json, sizeof(json), "%d", this->brightness);
      mqtt.queue("stat/" HOSTNAME "/DIMMER", json);

    };

    ble_default_commands_[kColortemp] = {
        {0xA0, 0x01, 0x04, 0x00, 0x00}, 5, false};
    ble_default_commands_[kColortemp].report_to_mqtt = [this](int) {
      char json[32];
      //      snprintf(json, sizeof(json), "{\"CT\": \"%d\"}",
      //      this->colortemperature);
      //      mqtt.queue("stat/" HOSTNAME "/RESULT", json);
      snprintf(json, sizeof(json), "%d", this->colortemperature);
      mqtt.queue("stat/" HOSTNAME "/CT", json);
    };
    ble_default_commands_[kScene] = {{0xA0, 0x02, 0x05, 0x00}, 4, false};
    ble_default_commands_[kScene].report_to_mqtt = [this](int) {

      if (this->power_state) {
        char json[32];
        //        snprintf(json, sizeof(json), "{\"SCENE\": \"%d\"}",
        //                 this->current_scene);
        //        mqtt.queue("stat/" HOSTNAME "/RESULT", json);
        snprintf(json, sizeof(json), "%d", this->current_scene);
        mqtt.queue("stat/" HOSTNAME "/SCENE", json);
      }
    };
  }
  void set_dimmer(unsigned int new_dim_level) {
    if (new_dim_level > 99)
      new_dim_level = 99;
    if (new_dim_level < 0)
      new_dim_level = 0;
    ble_default_commands_[kBrightness].is_dirty = brightness != new_dim_level;
    brightness = new_dim_level;
    ble_default_commands_[kBrightness].data[3] = brightness;
  }

  unsigned int switch_kelvin_mired(unsigned int value) {
    return (1000000 / value);
  }

  void set_colortemperature_mired(unsigned new_colortemperature) {
    if (new_colortemperature < 250)
      new_colortemperature = 250;
    if (new_colortemperature > 416)
      new_colortemperature = 416;
    auto kelvin = switch_kelvin_mired(new_colortemperature);
    ble_default_commands_[kColortemp].data[3] = kelvin >> 8;
    ble_default_commands_[kColortemp].data[4] = kelvin & 0xFF;
    ble_default_commands_[kColortemp].is_dirty =
        colortemperature != new_colortemperature;
    log_i("Color temperature to %d from %d (%d)", new_colortemperature,
          colortemperature, kelvin);
    colortemperature = new_colortemperature;
  }

  void set_colortemperature_kelvin(unsigned new_colortemperature) {
    if (new_colortemperature > 4000)
      new_colortemperature = 4000;
    if (new_colortemperature < 2700)
      new_colortemperature = 2700;
    ble_default_commands_[kColortemp].data[3] = new_colortemperature >> 8;
    ble_default_commands_[kColortemp].data[4] = new_colortemperature & 0xFF;
    ble_default_commands_[kColortemp].is_dirty =
        colortemperature != new_colortemperature;
    colortemperature = new_colortemperature;
  }

  void set_scene(unsigned int new_scene) {
    new_scene &= 0xFF;
    ble_default_commands_[kScene].data[3] = new_scene;
    ble_default_commands_[kScene].is_dirty = true; // current_scene !=
                                                   // new_scene;
    current_scene = new_scene;

    brightness = SceneBrightnessMapper::map(current_scene);
    ble_default_commands_[kBrightness].report_to_mqtt(brightness);
  }

  bool set_powerstate(bool new_power_state) {
    ble_default_commands_[kScene].is_dirty = new_power_state != power_state;
    power_state = new_power_state;
    ble_default_commands_[kScene].data[3] =
        (new_power_state ? current_scene : 0);
    return power_state;
  }

  bool is_dirty() {
    for (auto &b : ble_default_commands_) {
      if (b.is_dirty) {
        return true;
      }
    }
    return !ble_send_queue_.empty();
  }

  void send_custom(const uint8_t *data, size_t length) {
    ble_data custom;
    custom.is_dirty = true;
    custom.size = length;
    for (int i = 0; i < length && i < sizeof(custom.data); i++) {
      custom.data[i] = data[i];
    }
    ble_send_queue_.push(custom);
  }

  void send_queued() {
    bool needs_mqtt_result = false;
    if (!sending) {
      sending = true;
      for (auto &b : ble_default_commands_) {
        if (b.is_dirty) {
          ble_send(b.data, b.size);
          b.is_dirty = false;
          b.report_to_mqtt(0);
          needs_mqtt_result = true;
          delay(50);
        }
      }
      // Other pending commands
      while (!ble_send_queue_.empty()) {
        auto cmd = ble_send_queue_.front();
        ble_send(cmd.data, cmd.size);
        needs_mqtt_result = true;
        delay(50);
        ble_send_queue_.pop();
      }
      if (needs_mqtt_result) {
        mqtt.queue("stat/" HOSTNAME "/RESULT", create_state_message(), true);
      }
      sending = false;
    }
  }

  const char *create_state_message() {
    static char statemsg[128];
    time_t now = time(0);

    // Convert now to tm struct for local timezone
    tm *t = localtime(&now);

    snprintf(statemsg, sizeof(statemsg),
             "{\"Time\":"
             "\"%04d-%02d-%02dT%02d:%02d:%02d\","
             "\"Heap\":%u,\"IPAddress\":\"%s\",\"POWER\":\"%s\",\"CT\":"
             "%d,\"DIMMER\":%d,\"SCENE\":%d}",
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour,
             t->tm_min, t->tm_sec, ESP.getFreeHeap() / 1024,
             WiFi.localIP().toString().c_str(), power_state ? "ON" : "OFF",
             colortemperature, brightness, current_scene);
    return statemsg;
  }

  void loop() {
    if (is_dirty()) {
      static bool connecting = false;
      if (!connecting) {
        connecting = true;
        log_i("trying to connect");
        if (ble_connected || connect_to_server()) {
          log_i("Connected to the BLE Server.");
        } else {
          log_i("Failed to connect to the BLE server"
                "we will do.");
        }
        connecting = false;
      }
      static unsigned long last_sent = 0;
      //    if (millis() - last_sent <  200) // throttle a bit:  wait 200 ms
      //    between commands
      {
        if (ble_connected) {
          send_queued();
          last_sent = millis();
          log_i("%ld Command sent", last_sent);
        }
      }
    }
  }

  unsigned int colortemperature;
  unsigned int brightness;
  unsigned int current_scene;
  bool power_state;

private:
  volatile bool sending = false;

  portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;

  struct ble_data {
    uint8_t data[16];
    size_t size;
    bool is_dirty;
    Mqtt_Report_Function report_to_mqtt;
  } ble_default_commands_[3];

  std::queue<ble_data> ble_send_queue_;
};

LR_Ble_Device lr;

AsyncWebServer server(80);

void notFound(AsyncWebServerRequest *request) {
  request->send(404, "text/plain", "Not found");
}

static bool pending_command = false;

int get_power_state() { return lr.power_state ? 1 : 0; }

bool set_powerstate(bool value) {

  if (mqtt.connected()) {
    char json[32];
    mqtt.queue("stat/" HOSTNAME "/RESULT", lr.create_state_message());
    snprintf(json, sizeof(json), "%d", dimlevel);
    mqtt.queue("stat/" HOSTNAME "/POWER", value ? "ON" : "OFF");
  }

  return lr.set_powerstate(value);
}
bool set_powerstate(const String &value) {
  if (value.equals("0") || value.equals("off") || value.equals("false") ||
      value.equals("0")) {
    set_powerstate(false);
  } else if (value.equals("1") || value.equals("on") || value.equals("true") ||
             value.equals("1")) {
    set_powerstate(true);
  } else if (value.equals("toggle")) {
    set_powerstate(!lr.power_state);
  }
  return lr.power_state;
}

int get_dimmer_value() { return lr.brightness; }

int set_dimmer_value(int new_level) {
  pending_command = false;
  dimlevel = new_level;
  if (new_level > 100) {
    new_level = 100;
  }
  if (new_level < 0) {
    new_level = 0;
  }
  log_i("Set Dimmer Level to %d", new_level);
  lr.set_dimmer(new_level);

  pending_command = true;
  dimlevel = new_level;
  return new_level;
}

int set_dimmer_value(const String &value) {
  bool parsing_success = false;
  char *p;
  long numvalue = strtol(value.c_str(), &p, 10);
  if (*p) {
    log_i("Text Dimmer Level %s", value.c_str());
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
      dimlevel += step_value;
      log_i("Increase Dimmer Level to %d", dimlevel);
    } else {
      if (cmd.equals("-") || cmd.equals("down")) {
        parsing_success = true;
        dimlevel -= step_value;
      }
    }
  } else {
    parsing_success = true;
    dimlevel = (int)numvalue;
  }
  if (parsing_success) {
    set_dimmer_value(dimlevel);
  }
  return dimlevel;
}

int set_scene(int numvalue) {
  pending_command = false;
  scene = numvalue & 0xFF;
  if (scene > max_scenes && scene != 0xFF)
    scene = 1;
  if (scene < 1 && scene != 0xFF)
    scene = max_scenes;

  log_i("Set Scene Level to %d", scene);
  lr.set_scene(scene);
  pending_command = true;
  return scene;
}

int set_scene(const String &value) {
  bool parsing_success = false;
  char *p;
  long numvalue = strtol(value.c_str(), &p, 10);
  if (*p) {
    log_i("Scene %s", value.c_str());
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
    scene = (int)numvalue;
  }
  if (parsing_success) {
    set_scene(scene);
  }
  return scene;
}

int set_scene_brightness(const String &value) {
  char *p;
  strtol(value.c_str(), &p, 10);
  if (*p && *p != '.') {
    log_i("Map Scene %s", value.c_str());
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
      dimlevel = atol(param_value.c_str());
      SceneBrightnessMapper::set(scene, dim_level);
    }
  }
  return SceneBrightnessMapper::size();
}

int set_colortemperature(int numvalue) {
  pending_command = false;
  colortemperature = numvalue;
  if (colortemperature > 416) {
    colortemperature = 416;
  }
  if (colortemperature < 250) {
    colortemperature = 250;
  }
  log_i("Set Color temperature to %d", colortemperature);

  lr.set_colortemperature_mired(colortemperature);
  return colortemperature;
}

int set_colortemperature(const String &value) {
  bool parsing_success = false;
  char *p;
  long numvalue = strtol(value.c_str(), &p, 10);
  if (*p && *p != '.') {
    log_i("Text Color Temperature <%s>", value.c_str());
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
      colortemperature += step_value;
      log_i("Increase Color Temperature to %d", colortemperature);
    } else {
      if (cmd.equals("-") || cmd.equals("down")) {
        parsing_success = true;
        colortemperature -= step_value;
      }
    }
  } else {
    parsing_success = true;
    colortemperature = (int)numvalue;
  }
  if (parsing_success) {
    set_colortemperature(colortemperature);
  }
  return colortemperature;
}

void queue_ble_command(const String &value) {
  int len = value.length() / 2;
  char *p;
  union {
    unsigned long long numvalue;
    uint8_t data[8];
  } buff;

  buff.numvalue = strtoull(value.c_str(), &p, 16);
  uint8_t ble_data[8];
  if (buff.numvalue != 0) {

    for (int i = 0; i < len; i++) {
      ble_data[i] = buff.data[len - i - 1];
      //   log_i(" BLE custom %d : %d",i,ble_data[i]);
    }
  }
  lr.send_custom(ble_data, len);
}

unsigned long last_mqttping = millis();

bool parse_command(const char *command) {
  log_i("%ld Start Parsing %s", millis(), command);
  auto position_space = strchr(command, ' ');
  String value;
  String cmd;
  if (position_space != nullptr) {
    // ensure that there is a string after the space
    if (position_space[1]) {
      value = (position_space + 1);
      value.trim();
      value.toLowerCase();
      auto tmp = *position_space;
      // terminate the string at the position of the blank
      *position_space = '\0';
      cmd = command;
      // be nice and restore the original
      *position_space = tmp;
    } else {
      position_space = nullptr;
    }
  } else {
    cmd = command;
    value.clear();
  }
  cmd.trim();
  cmd.toLowerCase();
  if (cmd.equals("power")) {
    if (position_space != nullptr) {
      set_powerstate(value);
    }
    return true;
  } else if (cmd.equals("dimmer")) {
    if (position_space != nullptr) {
      set_dimmer_value(value);
    } else { /* return state */
    }
    return true;
  } else if (cmd.equals("scene")) {
    if (position_space != nullptr) {
      set_scene(value);
    } else { /* return state */
    }
    return true;
  } else if (cmd.equals("ct")) {
    if (position_space != nullptr) {
      set_colortemperature(value);
    } else { /* return state */
    }
    return true;
  } else if (cmd.equals("mapscene")) {
    if (position_space != nullptr) {
      set_scene_brightness(value);
    } else { /* return state */
    }
    return true;
  } else if (cmd.equals("ota")) {
    AppUtils::setupOta();
    return true;
  } else if (cmd.equals("mqttping")) {
    last_mqttping = millis();
    log_i("got mqtt ping");
    return true;
  } else if (cmd.equals("reboot") || cmd.equals("restart")) {
    log_i("------- REBOOT -------");
    yield();
    delay(500);
    ESP.restart();
  } else if (cmd.equals("blecustom") && position_space > 0) {
    queue_ble_command(value);
  }
  return false;
}

// Replaces placeholder with button section in your web page
String processor(const String &var) {
  // Serial.println(var);
  if (var == "DIMVALUE") {
    return String(lr.brightness);
  }
  if (var == "CTVALUE") {
    return String(lr.colortemperature);
  }

  if (var == "CHECKED") {
    return lr.power_state ? "checked" : "";
  }
  if (var == "ANAUS") {
    return lr.power_state ? "An" : "Aus";
  }

  if (var == "SCENES") {

    String scene_html = "<br><select id=\"sceneselect\" name=\"scenes\" "
                        "onchange=\"updateScene(this)\"  size=\"" +
                        String(scenes.size() - 1) + String("\" >");
    for (const auto &s : scenes) {
      if (s.first != 0) {
        scene_html += "<option" +
                      String(lr.current_scene == s.first ? " selected" : "") +
                      " value=\"" + String(s.first) + "\">" + s.second.c_str() +
                      "</option>";
      }
    }
    scene_html += "</select>";
    log_i("HTML: %s", scene_html.c_str());
    return scene_html;
  }

  return String();
}

static const char *PARAM_CMD = "cmnd";
void setup() {

  app.on_network_connect = mqtt.mqtt_reconnect;
  //  esp_wifi_stop();
  app.set_hostname(HOSTNAME).set_ssid(WIFISID).set_password(WIFIPASSWORD);
  mqtt.init(network_client, parse_command);

  SceneBrightnessMapper::load();

  // app.start_eth(true);
  app.start_wifi();
  app.start_network_keepalive();

  while (!app.network_connected()) {
    delay(100);
  }

  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", index_html, processor);
  });

  // Send a GET request to <IP>/get?message=<message>
  server.on("/cm", HTTP_GET, [](AsyncWebServerRequest *request) {
    String message = "";
    if (request->hasParam(PARAM_CMD)) {
      message = request->getParam(PARAM_CMD)->value();
      parse_command(message.c_str());
    } else {
      message = "No message sent";
    }
    request->send(200, "text/plain", "GET: " + message);
  });

  server.onNotFound(notFound);
  server.begin();
  int mqtt_reconnects = 0;
  last_mqttping = millis();
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
  mqtt.start();

  lr_init();
  if (connect_to_server(get_all_scenes)) {
    auto initalscene = get_current_scene();

    if (initalscene == 0) {
      lr.power_state = false;
      if (lr.current_scene == 0)
        lr.current_scene = 0xFF;
    } else {
      lr.power_state = true;
      lr.current_scene = initalscene;
    }
    lr.brightness = SceneBrightnessMapper::map(lr.current_scene);
  }
}

void loop() {
  static unsigned long last_statemsg = 0;
  app_utils::AppUtils::loop();
  mqtt.loop();
  lr.loop();
  if (millis() - last_statemsg > 60000 * 5) {
    mqtt.queue("stat/" HOSTNAME "/RESULT", lr.create_state_message(), true);
    mqtt.queue("cmnd/" HOSTNAME "/mqttping", "ping", false);
    last_statemsg = millis();
  }
  // restablish mqtt after 10 mins without an incoming ping
  if (millis() - last_mqttping > 60000 * 10) {
    mqtt.disconnect();
    delay(1000);
    mqtt.mqtt_reconnect();
  }
}