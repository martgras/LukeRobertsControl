#pragma once
#ifndef __LR_H__
#define __LR_H__

///////////////////////////////////////////////
// Luke Roberts BLE Device
///////////////////////////////////////////////

#include <string>
#include <SPIFFS.h>
#include <WiFi.h>
#include "BleGattClient.h"

extern boolean ble_connected;
extern uint8_t max_scenes;
extern WiFiClient network_client;

// The remote service we wish to connect to.
static const BLEUUID serviceUUID("44092840-0567-11E6-B862-0002A5D5C51B");
// The characteristic of the remote service we are interested in.
static const BLEUUID charUUID("44092842-0567-11E6-B862-0002A5D5C51B");
// static BLEUUID descUUID("00002902-0000-1000-8000-00805f9b34fb");
static const BLEUUID charUUID_Scene("44092844-0567-11E6-B862-0002A5D5C51B");

const uint8_t default_qos = 0;
class LR_Ble_Device {
public:
  using mqtt_publish_topic_t =
      std::function<void(const char *, const char *, bool, uint8_t)>;
  enum slots { kScene = 0, kBrightness = 1, kColortemp = 2 };

  struct State {
    bool power;
    uint8_t scene;
    uint8_t brightness;
    uint16_t mired;
    uint16_t kelvin;
  };

  // The remote service we wish to connect to.
  const BLEUUID serviceUUID = ::serviceUUID;
  // The characteristic of the remote service we are interested in.
  static BLEUUID charUUID() { return ::charUUID; }
  static BLEUUID charUUID_Scene() { return ::charUUID_Scene; }

  const State &state() { return state_; }
  BleGattClient &client() { return gatt_client_; }

  mqtt_publish_topic_t mqtt_publisher;

  LR_Ble_Device(mqtt_publish_topic_t mqttpublisher)
      : mqtt_publisher(mqttpublisher) {

    // Setup the cached commands
    //
    BleGattClient::BleCommand cmd;
    // Set brightness
    cmd = {{0xA0, 0x01, 0x03, 0x00},
           4,
           false,
           [&](int newval) {
             char json[32];
             snprintf(json, sizeof(json), "%d", state().brightness);
             mqtt_publisher("stat/" HOSTNAME "/DIMMER", json, false,
                            default_qos);
             mqtt_publisher("stat/" HOSTNAME "/RESULT",
                            this->create_state_message(), true, default_qos);
           }};
    gatt_client_.queue_cmd(cmd, (uint8_t)kBrightness, false);

    // Set colortemperature
    cmd = {{0xA0, 0x01, 0x04, 0x00, 0x00},
           5,
           false,
           [&](int) {
             char json[32];
             snprintf(json, sizeof(json), "%d", state().mired);
             mqtt_publisher("stat/" HOSTNAME "/CT", json, false, default_qos);
             mqtt_publisher("stat/" HOSTNAME "/RESULT",
                            this->create_state_message(), true, default_qos);
           }};
    gatt_client_.queue_cmd(cmd, (uint8_t)kColortemp, false);

    // Set scene
    cmd = {{0xA0, 0x02, 0x05, 0x00},
           4,
           false,
           [&](int) {
             /*if (true || state().power)*/ {
               char json[32];
               snprintf(json, sizeof(json), "%d", state_.scene);
               mqtt_publisher("stat/" HOSTNAME "/SCENE", json, false,
                              default_qos);
               // request the current downlight settings to syn the state
               BleGattClient::BleCommand cmd = {{0x09}, 1, true, nullptr};
               this->client().queue_cmd(cmd);
               // state message will be sent fro response handler
             }
           }};
    gatt_client_.queue_cmd(cmd, (uint8_t)kScene, false);
  }

  // Not sure why this doesn't work from constustructor
  void init() {
    static const uint8_t id = 0xF2;

    // register a callback to handle response from command 0x9  (request current
    // values for brightness / ct)
    // Response: 5 Byte Magic, 16 Bit Color temperature, 8 Bit Brightness, 8 Bit
    // Flags.
    //
    gatt_client_.register_callback_notification(
        id,
        [](NimBLERemoteCharacteristic *pRemoteCharacteristic, uint8_t *pData,
           size_t length, bool is_notify) {
          return (BleGattClient::matches_api_characteristics(
                      pRemoteCharacteristic->getUUID()) &&
                  length == 9 && pData[0] == 0 && pData[1] == 0x88 &&
                  pData[2] == 0xF4 && pData[3] == 0x18 && pData[4] == 0x71);
        },
        [&](NimBLERemoteCharacteristic *pRemoteCharacteristic, uint8_t *pData,
            size_t length, bool is_notify) {
          uint8_t brightness = pData[7];
          uint16_t kelvin = pData[5] | pData[6] << 8;
          // update state and publish new values
          state_.brightness = brightness;
          state_.kelvin = kelvin;
          state_.mired = switch_kelvin_mired(state_.kelvin);
          char json[32];
          snprintf(json, sizeof(json), "%d", state().mired);
          mqtt_publisher("stat/" HOSTNAME "/CT", json, false, default_qos);
          snprintf(json, sizeof(json), "%d", state().brightness);
          mqtt_publisher("stat/" HOSTNAME "/DIMMER", json, false, default_qos);
          mqtt_publisher("stat/" HOSTNAME "/RESULT",
                         this->create_state_message(), true, default_qos);
          return true;
        });
    gatt_client_.start_ble_loop();
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

  void restore_state(const State &state) { state_ = state; }
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
      if (duration == 0) {                             // permanent ?
        state_.brightness = (brightness / 2.56) + 0.5; // Range is  0 .. 255
        state_.kelvin = kelvin;
        state_.mired = switch_kelvin_mired(kelvin);
        cmd.on_send = [&](int) {
          char json[32];
          snprintf(json, sizeof(json), "%d", state().brightness);
          mqtt_publisher("stat/" HOSTNAME "/DIMMER", json, false, default_qos);
          snprintf(json, sizeof(json), "%d", state().mired);
          mqtt_publisher("stat/" HOSTNAME "/CT", json, false, default_qos);
          mqtt_publisher("stat/" HOSTNAME "/RESULT",
                         this->create_state_message(), true, default_qos);
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
          mqtt_publisher("stat/" HOSTNAME "/SATURATION", json, false,
                         default_qos);
          snprintf(json, sizeof(json), "%d", hue);
          mqtt_publisher("stat/" HOSTNAME "/HUE", json, false, default_qos);
          snprintf(json, sizeof(json), "%d", brightness);
          mqtt_publisher("stat/" HOSTNAME "/BRIGHTNESS_UP", json, false,
                         default_qos);
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
#ifdef OLD
    //    if (state_.brightness == 0) {
    state_.brightness = SceneMapper::map_brightness(state_.scene);
    //    }
    //    if (state_.kelvin == 0) {
    state_.kelvin = SceneMapper::map_colortemperature(state_.scene);
    state_.mired = switch_kelvin_mired(state_.kelvin);
    //    }
    gatt_client_.cached_commands[kBrightness].on_send(state_.brightness);
    gatt_client_.cached_commands[kColortemp].on_send(state_.mired);
#endif
    return state_.scene;
  }

  bool sync_powerstate(bool powerstate) {
    if (state_.power != powerstate) {
      mqtt_publisher("stat/" HOSTNAME "/POWER", powerstate ? "ON" : "OFF",
                     false, default_qos);
    }
    state_.power = powerstate;
    return powerstate;
  }

  bool sync_scene(unsigned int new_scene) {
    state_.scene = new_scene;
    return new_scene;
  }
  bool get_powerstate() { return state_.power; }

  bool set_powerstate(bool new_power_state, bool force_dirty = false) {
    if (new_power_state == true) {

      restore_state(saved_state_);
      if (state_.scene == 0) {
        // Should only happen during startup
        state_.scene = 0xFF;
      }
    }
    if (new_power_state == false) {
      // Backup state so that we can restore it at power on
      saved_state_ = state_;
      state_.kelvin = state_.mired = 0;
      state_.brightness = 0;
    }
    gatt_client_.cached_commands[kScene].is_dirty |=
        force_dirty || (new_power_state != state_.power);
    state_.power = new_power_state;

    gatt_client_.cached_commands[kScene].data[3] =
        (new_power_state ? state_.scene : 0);

    if (state_.power && gatt_client_.cached_commands[kScene].is_dirty) {
      if (state_.brightness != 0xFF) {
        set_dimmer(state_.brightness, true);
      }
      if (state_.mired != 0) {
        set_colortemperature_mired(state_.mired, true);
      }
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

  const char *
  create_state_message(const char *additional_information = nullptr) {
    static char statemsg[512];
    time_t now = time(0);

    // Convert now to tm struct for local timezone
    tm *t = localtime(&now);

    snprintf(
        statemsg, sizeof(statemsg),
        additional_information
            ? "{\"Time\":"
              "\"%04d-%02d-%02dT%02d:%02d:%02d\","
              "\"Heap\":%u,\"IPAddress\":\"%s\",\"POWER\":\"%s\",\"CT\":"
              "%d,\"KELVIN\":%d,\"DIMMER\":%d,\"SCENE\":%d, \"INFO\":\"%s\" }"
            : "{\"Time\":"
              "\"%04d-%02d-%02dT%02d:%02d:%02d\","
              "\"Heap\":%u,\"IPAddress\":\"%s\",\"POWER\":\"%s\",\"CT\":"
              "%d,\"KELVIN\":%d,\"DIMMER\":%d,\"SCENE\":%d}",
        t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min,
        t->tm_sec, ESP.getFreeHeap() / 1024, WiFi.localIP().toString().c_str(),
        state_.power ? "ON" : "OFF", state_.mired, state_.kelvin,
        state_.brightness, state_.scene, additional_information);
    return statemsg;
  }

  void loop() {
    gatt_client_.loop([&]() {
      mqtt_publisher("stat/" HOSTNAME "/RESULT", this->create_state_message(),
                     true, default_qos);
    });
  }

  /*
    requests all scenes from the lamp
  */
  static int request_all_scenes(BleGattClient &client) {

    // a random id used to reference the cb
    static const uint8_t kSceneId = 0x1F;
    // ble command Select Scene
    // Structure: A0 02 05 II
    //  II Scene id to select
    //  Send 0xFF as II to select the default scene.
    // Response
    //   00 Status OK
    uint8_t blecmd[] = {0xA0, 0x1, 0x1, 0x0};

    if (!client.connected()) {
      int retry = 3;
      while (retry-- && !client.connect_to_server(charUUID())) {
        delay(10);
      }
    }
    if (!client.connected()) {
      return false;
    }

    SemaphoreHandle_t new_scene_received = xSemaphoreCreateBinary();
    auto start = millis();
    uint8_t last_reported_scene = 0;

    // register a callback handler for the expected scene data
    if (!client.enable_callback_notification(kSceneId, true)) {
      client.register_callback_notification(
          kSceneId,
          [&new_scene_received, &last_reported_scene](
              NimBLERemoteCharacteristic *pRemoteCharacteristic, uint8_t *pData,
              size_t length, bool isNotify) {
            // is the request in the expected format?
            return (length > 4 &&
                    pRemoteCharacteristic->getUUID() == charUUID() &&
                    pData[0] == 0 && pData[1] == 1);
          },
          [&new_scene_received, &last_reported_scene](
              NimBLERemoteCharacteristic *pRemoteCharacteristic, uint8_t *pData,
              size_t length, bool isNotify) {
            char tmp[32];
            if (length > 32)
              length = 32;
            if (pData[1] == 1) {
              strncpy(tmp, (const char *)&pData[3], length - 3);
              tmp[length - 3] = '\0';
              log_d("Scene notification : %d : %s", pData[2], tmp);
              scenes[last_reported_scene] = tmp;
              last_reported_scene = pData[2];
              xSemaphoreGive(new_scene_received);
            }
          });
    }
    last_reported_scene = 0;
    // loop through the scenes
    while (last_reported_scene != 0xFF && millis() - start < 5000) {
      blecmd[3] = last_reported_scene;
      client.write(blecmd, 4);
      // Wait for the notification callback signal that a response arrived
      // request the next scene
      if (!xSemaphoreTake(new_scene_received, 5000 / portTICK_PERIOD_MS)) {
        break;
      }
    }
    log_d("Got %d scenes", scenes.size());

    vSemaphoreDelete(new_scene_received);
    new_scene_received = nullptr;
    client.enable_callback_notification(kSceneId, false);
    return scenes.size();
  }

  static void request_downlight_settings(BleGattClient &client) {
    BleGattClient::BleCommand cmd = {{0x09}, 1, true, nullptr};
    client.queue_cmd(cmd);
  }

  static void get_all_scenes(BleGattClient &client) {
    if (scenes.size() < 2) {
      request_all_scenes(client);
#if CORE_DEBUG_LEVEL > 3
      for (const auto &s : scenes) {
        log_d("Scene %d name=%s", s.first, s.second.c_str());
      }
#endif
    }
  }

  static uint8_t get_current_scene(BleGattClient &client) {
    NimBLERemoteCharacteristic *pRemoteCharacteristic2 =
        client.service->getCharacteristic(charUUID_Scene());
    if (pRemoteCharacteristic2->canRead()) {
      int v = pRemoteCharacteristic2->readValue<uint8_t>();
      log_i("Current Scene: %d\r\n ", v);
      return v;
    }
    return 0xFF;
  }
  static std::map<uint8_t, std::string> scenes; // = {{0, "OFF"}};

private:
  volatile bool sending = false;
  BleGattClient gatt_client_;
  State state_;
  RTC_DATA_ATTR static State saved_state_;
};

RTC_DATA_ATTR LR_Ble_Device::State
    LR_Ble_Device::saved_state_; // store settings before power-off
std::map<uint8_t, std::string> LR_Ble_Device::scenes = {{0, "OFF"}};

#ifdef USE_SCENE_MAPPER
// Probably not neded anymore since we now have an API to request the current
// values for brightness and color temperature

// Helper call. Reads/Writes  bightnesses for scenes from SPIFFS storage

template <typename T> class SPIFFSHelper {
public:
  File file_;
  File open(const char *filename, const char *mode) {
    if (!SPIFFS.begin(true)) {
      log_e("An Error has occurred while mounting SPIFFS");
      return File();
    }
    file_ = SPIFFS.open(filename, mode);
    return file_;
  }

  void close() {
    file_.close();
    SPIFFS.end();
  }
  SPIFFSHelper() {}

  ~SPIFFSHelper() { close(); }
  size_t read(File &file, T &element) {
    return file.read((uint8_t *)&element, sizeof(T));
  }
  size_t read(T &element) { return read(file_, element); }

  size_t write(File &file, const T &element) {
    return file.write((const uint8_t *)&element, sizeof(T));
  }
  size_t write(const T &element) { return write(file_, element); }

  bool write(const char *filename, T &element) {
    if (!SPIFFS.begin(true)) {
      log_e("An Error has occurred while mounting SPIFFS");
      return false;
    }
    File mapfile = SPIFFS.open(filename, "w+");
    if (mapfile) {
      write(mapfile, &element);
      mapfile.close();
      SPIFFS.end();
      return true;
    }
    return false;
  }

  bool read(const char *filename, T &element) {
    if (!SPIFFS.begin(true)) {
      log_e("An Error has occurred while mounting SPIFFS");
      return false;
    }
    File mapfile = SPIFFS.open(filename, "r");
    if (mapfile) {
      read(mapfile, &element);
      mapfile.close();
      SPIFFS.end();
      return true;
    }
    return false;
  }
};

class SceneMapper {
public:
  SceneMapper() {}
  static int save() {

    SPIFFSHelper<uint16_t> spiff;
    if (spiff.open("/scenemap.bin", "w+")) {
      uint16_t number_of_elements = brightness_map_.size();
      number_of_elements = colortemperature_map_.size();
      spiff.write(number_of_elements);
      for (auto i : brightness_map_) {
        spiff.write(i);
      }
      for (auto i : colortemperature_map_) {
        spiff.write(i);
      }

      spiff.close();
      return number_of_elements;
    } else {
      log_e("Can't create brightness mapfile");
    }
    return 0;
  }
  static int load() {

    SPIFFSHelper<uint16_t> spiff;
    File mapfile = spiff.open("/scenemap.bin", "r");
    if (!mapfile) {
      log_w("Failed to open file for reading");
      return save();
    }

    uint16_t number_of_brightness_elements = 0;
    uint16_t number_of_colortemperature_elements = 0;
    if (mapfile) {
      brightness_map_.clear();
      colortemperature_map_.clear();
      spiff.read(number_of_brightness_elements);
      spiff.read(number_of_colortemperature_elements);
      uint16_t item = 0;
      for (int i = 0; i < number_of_brightness_elements; i++) {
        spiff.read(item);
        brightness_map_.push_back(item);
      }
      item = 0;
      for (int i = 0; i < number_of_colortemperature_elements; i++) {
        spiff.read(item);
        colortemperature_map_.push_back(item);
      }

      spiff.close();
    }
    return number_of_brightness_elements;
  }

  static int size() { return brightness_map_.size(); }

  static int map_brightness(int scene) {
    if (scene >= 0 && scene < brightness_map_.size()) {
      return brightness_map_[scene];
    }
    // just add a default value ;
    return 50;
  }

  static int map_colortemperature(int scene) {
    if (scene >= 0 && scene < colortemperature_map_.size()) {
      return colortemperature_map_[scene];
    }
    // just add a default value ;
    return 50;
  }

  static bool set_brightness(int scene, int brightness_level) {
    if (scene >= 0 && scene < brightness_map_.size()) {
      brightness_map_[scene] = brightness_level;
      save();
      return true;
    } else if (scene == brightness_map_.size()) {
      brightness_map_.push_back(brightness_level);
      save();
      return true;
    }
    return false;
  }

  static bool set_colortemperature(int scene, int kelvin) {
    if (scene >= 0 && scene < brightness_map_.size()) {
      colortemperature_map_[scene] = kelvin;
      save();
      return true;
    } else if (scene == colortemperature_map_.size()) {
      colortemperature_map_.push_back(kelvin);
      save();
      return true;
    }
    return false;
  }

private:
  static std::vector<int> brightness_map_;
  static std::vector<int> colortemperature_map_;
};
#endif

#endif
