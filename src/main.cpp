#define ETH_CLK_MODE ETH_CLOCK_GPIO17_OUT
#define ETH_PHY_POWER 12

#include <string>
#include <Arduino.h>
//#include <ETH.h>
//#include <WiFi.h>
#include <AsyncTCP.h>
#include <SPIFFS.h>
#include <ESPAsyncWebServer.h>
#include <esp_wifi.h>
#include <WiFiGeneric.h>

#include "app_utils.h"
#include "BleGattClient.h"
#include "lukeroberts.h"
#include "mqtt_handler.h"
#include "webpages.h"

#if defined(ROTARY_PIN_A)
#if !defined(ROTARY_PIN_B)
#error "ROTARY configuration error - PIN A and B must be defined"
#endif
#include "rotaryencoder.h"

#if !defined(ROTARY_STEP_VALUE)
#define ROTARY_STEP_VALUE 5
#endif
#endif

#include <AceButton.h>

using namespace app_utils;
using namespace rotary_encoder;
using namespace ace_button;
WiFiClient network_client;
AppUtils app;

extern boolean ble_connected;
extern uint8_t max_scenes;

// RTC_DATA_ATTR bool powerstate = false;
// RTC_DATA_ATTR int16_t dimlevel = 50;
// RTC_DATA_ATTR int16_t colortemperature = 3000;
// RTC_DATA_ATTR int16_t scene = 01;

// publishing mqtt message is a blocking operation
// use a task instead that sends queues messages

MqttPublish mqtt;

// provide a lamdba to call the used mqtt client (decouples mqtt library used)
RTC_DATA_ATTR LR_Ble_Device lr([](const char *topic, const char *data,
                                  bool retained, uint8_t qos) {
  mqtt.queue(topic, data, retained, qos);
});

AsyncWebServer server(80);

void notFound(AsyncWebServerRequest *request) {
  request->send(404, "text/plain", "Not found");
}

///// Command parsing //////////////
bool get_powerstate() { return lr.get_powerstate(); }

bool set_powerstate(bool value) {

#ifndef RELAY_PIN
  lr.set_powerstate(value, true);
#else
  digitalWrite(RELAY_PIN, value ? HIGH : LOW);
  //  log_i("PORT %d = %d",RELAY_PIN,value);
  lr.sync_powerstate(value);
#endif
  if (mqtt.connected() && value == false) {
    char json[32];
    mqtt.queue("stat/" HOSTNAME "/RESULT", lr.create_state_message());
    snprintf(json, sizeof(json), "%d", lr.state().brightness);
    snprintf(json, sizeof(json), "%d", lr.state().mired);
    mqtt.queue("stat/" HOSTNAME "/CT", json);
    mqtt.queue("stat/" HOSTNAME "/POWER", value ? "ON" : "OFF");
  }

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
  if (scene > lr.scenes.size() && scene != 0xFF)
    scene = 1;
  if (scene < 1 && scene != 0xFF)
    scene = lr.scenes.size();

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

int get_colortemperature_kelvin() { return lr.state().kelvin; }

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
#ifdef USE_SCENE_MAPPER
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
      SceneMapper::set_brightness(scene, dim_level);
    }
  }
  return SceneMapper::size();
}

int set_scene_colortemperature(const String &value) {
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
      int kelvin = 3000;
      param_value = value.substring(param + 1);
      cmd = value.substring(0, param);
      scene = atol(cmd.c_str());
      kelvin = atol(param_value.c_str());
      SceneMapper::set_colortemperature(scene, kelvin);
    }
  }
  return SceneMapper::size();
}
#endif

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
#ifndef RELAY_PIN
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
#ifdef USE_SCENE_MAPPER
  } else if (cmd.equals("mapscene")) {
    if (has_value) {
      set_scene_brightness(value);
    } else { /* return state */
    }
    return true;
#endif
  } else if (cmd.equals("ota")) {
    AppUtils::setupOta();
    mqtt.queue("tele/" HOSTNAME "/ota", "waiting for ota start on port 3232");
    return true;
  } else if (cmd.equals("result") || cmd.equals("mqttping")) {
    last_mqttping = millis();
    log_d("got mqtt ping");
    mqtt.queue("tele/" HOSTNAME "/state",lr.create_state_message(app.ota_started() ? "waiting for ota start on port 3232" : nullptr));
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

  if (var == "POWER") {
    return lr.state().power ? "On" : "Off";
  }
  if (var == "SCENES") {

    String scene_html = "<br><select id=\"sceneselect\" name=\"scenes\" "
                        "onchange=\"updateScene(this)\"  size=\"" +
                        String(lr.scenes.size() - 1) + String("\" >");
    for (const auto &s : lr.scenes) {
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
#if defined(ROTARY_PIN_A)
RotaryEncoderButton rotary;
#endif

// odd place for an include but the button settings depend on the functions
// defined above
// until proper prototypes are created the include has to stay here
// the button code was moved out because it is pretty repeptive, simple but long
#include "buttons.h"

#include <esp_panic.h>

void setup() {

/* Used to help narrowing down a heap corruption */
/*
xTaskCreatePinnedToCore( [](void*){
  esp_set_watchpoint(0, (void *)0xfffba5c4, 4, ESP_WATCHPOINT_STORE);
  vTaskDelete(0);
},"wp",4096,nullptr,1,nullptr,1);
*/

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
  lr.client().init(NimBLEAddress(LR_BLEADDRESS, 1), serviceUUID);
#else
#pragma message(                                                               \
    "NO BLE Device Address provided. Scanning for a Luke Roberts Lamp during startup")
  auto device_addr = scan_for_device();
  log_i("DEVICE : %s", device_addr.toString().c_str());
  lr.client().init(device_addr);
#endif
  lr.init();
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
      request->send(200, "application/json",
                    lr.create_state_message(
                        app.ota_started() ? "waiting for ota start on port 3232"
                                          : nullptr));
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
        app.fast_restart();
      }
    }
  }

  app.on_network_connect = std::bind(&MqttPublish::mqtt_reconnect, &mqtt);
  mqtt.queue("tele/" HOSTNAME "/LWT", "Online", true);

  bool result;
  int attempts = 0;
  while (!(result = lr.client().connect_to_server(
               lr.charUUID(), []() { lr.get_all_scenes(lr.client()); }))) {
    delay(1000);
    if (attempts++ == 60) {
      log_e("unable to connect to BLE Device - restarting");
      app.fast_restart();
      ;
    }
  }

  if (result) {
    auto initalscene = lr.get_current_scene(lr.client());
    if (initalscene == 0) {
      lr.sync_powerstate(false);
      // if (lr.state().scene == 0)
      //   lr.set_scene(0xFF,false);
    } else {
      lr.sync_powerstate(true);
      ;
      lr.sync_scene(initalscene);
    }
    lr.request_downlight_settings(lr.client());
    // returns immediatly - values will be set async in lr.client when we have a
    // BLE response
  }

#if defined(ROTARY_PIN_A)
  ESP_ERROR_CHECK(rotary.init(ROTARY_PIN_A, ROTARY_PIN_B, false));
  rotary.set_speedup_times(50, 25);
  rotary.on_rotary_event = [&](rotary_encoder_event_t event) {
    if (!get_powerstate())
      return;

    log_v("Rotary event %d  %d (%d) %ld ", event.state.direction,
          event.state.position, event.state.speed);
    int dimmerlevel = get_dimmer_value();
    int step = ROTARY_STEP_VALUE;
    if (event.state.direction == ROTARY_ENCODER_DIRECTION_CLOCKWISE) {
      step *= event.state.speed;
    } else if (event.state.direction ==
               ROTARY_ENCODER_DIRECTION_COUNTER_CLOCKWISE) {
      step *= -1 * event.state.speed;
    }
    set_dimmer_value(dimmerlevel + step);

  };

#endif

#ifdef RELAY_PIN
  pinMode(RELAY_PIN, OUTPUT);
#endif

  button_handler::setup_buttons();

  log_d("Inital State: Power = %d scene %d", get_powerstate(), get_scene());

#ifndef LR_BLEADDRESS
  mqtt.queue("tele/" HOSTNAME "/BLEADDRESS", device_addr.toString().c_str());
#endif
}

void loop() {
  static unsigned long last_statemsg = 0;
  app_utils::AppUtils::loop();
  mqtt.loop();
  /*
    lr.client().loop([&]() {
      // mqtt.queue("stat/" HOSTNAME "/RESULT", lr.create_state_message(),
    true);
    });
  */
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
}