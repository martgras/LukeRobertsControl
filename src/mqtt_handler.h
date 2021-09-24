#ifndef __MQTT_HANDLER_H__
#define __MQTT_HANDLERH__

#define HA_ID HOSTNAME "_device"
const int kConnectThreadIndex = 0;

#ifndef MQTTHOST
// Dummy class to work without mqtt server

class MqttPublish {
public:
  MqttPublish() {}
  void recreate_client() {}
  using mqtt_command_handler = std::function<bool(String, String)>;
  void init(Client &network_client,
            mqtt_command_handler command_handler = nullptr) {}
  void start() {}
  void queue(const char *topic, const char *message, bool retained = false,
             uint8_t qos = 0) {}
  bool connected() { return true; }
  void disconnect() {}
  bool mqtt_connect() { return true; }
  static void loop() {}
};

#else

#include <MQTT.h>

// clang-format off
/*
const char MQTT_DISCOVERY2[] =
    "{"
    "\"name\":\"" HOSTNAME "\""
    ",\"brightness\": true"
    ",\"color_temp\": true"
    ",\"stat_t\":\"stat/" HOSTNAME "/RESULT\""
    ",\"avty_t\":\"tele/" HOSTNAME "/LWT\""
    ",\"pl_avail\":\"Online\""
    ",\"pl_not_avail\":\"Offline\""
    ",\"val_tpl\":\"{{value_json.POWER}}\""
    ",\"frc_upd\":true"
    ",\"cmd_t\":\"cmnd/" HOSTNAME "/POWER\""
    ",\"pl_off\":\"OFF\""
    ",\"pl_on\":\"ON\""
    ",\"bri_cmd_t\":\"cmnd/" HOSTNAME "/DIMMER\""
    ",\"bri_stat_t\":\"stat/" HOSTNAME "/RESULT\""
    ",\"bri_scl\":100"
    ",\"on_cmd_type\":\"%s\","
    ",\"bri_val_tpl\":\"{{value_json.DIMMER}}\""
    ",\"clr_temp_cmd_t\":\"cmnd/" HOSTNAME "/CT\""
    ",\"clr_temp_stat_t\":\"stat / " HOSTNAME "/RESULT\""
    ",\"max_mirs\":416"
    ",\"min_mirs\":250"
    ",\"clr_temp_val_tpl\":\"{{value_json.CT}}\""
    ",\"fx_cmd_t\":\"cmnd/" HOSTNAME "/SCENE\""
    ",\"fx_stat_t\":\"stat / " HOSTNAME "/RESULT\""
    ",\"fx_val_tpl\":\"{{value_json.SCENE}}\""
    ",\"fx_list\":[\"0\",\"1\",\"2\",\"3\",\"4\"]"
    ",\"uniq_id\":\"" HOSTNAME "_L_1\",\"dev\":{\"ids\":[\"" HOSTNAME "\"] "
   "\"name\": \"lrdimmer\""
    ",\"sw_version\": \"BLE Bridge\""
    ",\"model\": \"ESP32\""
    ",\"manufacturer\": \"gnet\""
      "}"
    "}";
*/
const char MQTT_DISCOVERY[] = 
"{"
    "\"name\": \"" HOSTNAME "\""
    ",\"avty_t\": \"tele/" HOSTNAME "/LWT\""
    ",\"pl_avail\": \"Online\""
    ",\"pl_not_avail\": \"Offline\""
    ",\"stat_t\": \"stat/" HOSTNAME "/POWER\""    
    ",\"cmd_t\": \"cmnd/" HOSTNAME "/POWER\""
//    ",\"val_tpl\": \"{{value_json.POWER}}\""
    ",\"pl_off\": \"OFF\""
    ",\"pl_on\": \"ON\""
   ", \"bri_cmd_t\": \"cmnd/" HOSTNAME "/DIMMER\""
   ", \"bri_stat_t\": \"stat/" HOSTNAME "/RESULT\""
   ", \"bri_val_tpl\": \"{{value_json.DIMMER}}\""
   ", \"bri_scl\": 100"
//   ", \"on_cmd_type\": \"brightness\""
//   ", \"bri_val_tpl\": \"{{value_json.DIMMER}}\""
   ", \"clr_temp_cmd_t\": \"cmnd/" HOSTNAME "/CT\""
   ", \"clr_temp_stat_t\": \"stat/" HOSTNAME "/RESULT\""
   ", \"clr_temp_val_tpl\": \"{{value_json.CT}}\""
    ",\"max_mirs\":370"
    ",\"min_mirs\":250"
    ",\"uniq_id\": \""  HA_ID  "\""
    ",\"dev\": {"
    "    \"ids\": ["
    "        \"" HA_ID "\""
    "       ],"
"        \"name\": \"" HOSTNAME "\","
"        \"mdl\": \"Luke Roberts Lamp\","
"        \"sw\": \"1.1.0.2\","
"        \"mf\": \"msoft\""    
    "}"
"}";
/*
const char MQTT_DISCOVERY_SENSOR[] = 
"{"
"    \"name\": \"" HOSTNAME " status\","
"    \"stat_t\": \"tele/" HOSTNAME "/HASS_STATE\","
"    \"avty_t\": \"tele/" HOSTNAME "/LWT\","
"    \"pl_avail\": \"Online\","
"    \"pl_not_avail\": \"Offline\","
"    \"json_attr_t\": \"tele/" HOSTNAME "/HASS_STATE\","
"    \"unit_of_meas\": \"%\","
"    \"val_tpl\": \"{{value_json['RSSI']}}\","
"    \"ic\": \"mdi:information-outline\","
"    \"uniq_id\": \"" HA_ID "_status\","
"    \"dev\": {"
"        \"ids\": ["
"        \"" HA_ID "\""
"        ],"
"        \"name\": \"" HOSTNAME "\","
"        \"mdl\": \"Luke Roberts Lamp\","
"        \"sw\": \"1.1.0.2\","
"        \"mf\": \"msoft\""
"    }"
"}";
*/
// clang-format on

class MqttPublish {
public:
  MqttPublish() { mqtt_client = new MQTTClient(4096); }
  ~MqttPublish() {
    if (mqtt_client) {
      delete mqtt_client;
    }
    if (mutex_) {
      vSemaphoreDelete(mutex_);
      mutex_ = nullptr;
    }
  }
  void recreate_client() {
    /*
        if (mqtt_client) {
          delete mqtt_client;
        }
    */
    while (!mqtt_messages.empty()) {
      mqtt_messages.pop();
    }
    /*
        mqtt_client = new AsyncMqttClient();
        init(*netclient);
    */
  }

  using mqtt_command_handler = std::function<bool(const char *, const char *)>;

  void init(Client &network_client,
            mqtt_command_handler command_handler = nullptr) {

    netclient = &network_client;
    mqtt_client->begin(network_client);
    mqtt_client->setHost(MQTTHOST, MQTTPORT);
    if (command_handler) {
      command_handler_ = std::move(command_handler);
      mqtt_client->onMessageAdvanced(
          [&](MQTTClient *client, char *topic, char *payload, size_t length) {
            mqtt_callback(client, topic, payload, length);
          });

      if (connect_mux_ == nullptr) {
        connect_mux_ = xSemaphoreCreateBinary();
      }
    }
    connection_state_ = MQTT_CLIENT_DISCONNECTED;
    xSemaphoreGive(connect_mux_);
  }

  void start() {

    mutex_ = xSemaphoreCreateMutex();

    xTaskCreatePinnedToCore([](void *this_ptr) {
      while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        log_i("Reconnect task started");
        reinterpret_cast<MqttPublish *>(this_ptr)->mqtt_reconnect();
      }
    }, "mqtt_connect", 2048, this, 2, &connect_task_, 1);

    xTaskCreatePinnedToCore(send_pump, "mqttsend", 8192, (void *)this, 1,
                            &pump_task_, 1);
  }

  void queue(const char *topic, const char *message, bool retained = false,
             uint8_t qos = 0) {
    mqtt_messages.emplace(topic, message, retained, qos);
    xSemaphoreGive(mutex_);
  }

  bool connected() { return mqtt_client->connected(); }

  void disconnect() {
    log_i("disonnecting from  MQTT...");
    connection_state_ = MQTT_CLIENT_DISCONNECTED;
    mqtt_client->disconnect();
  }

  bool mqtt_connect() {
    // xSemaphoreGive(connect_mux_);
    return mqtt_reconnect();
    return true;
  }

  void loop() {
    // MQTTClientState connection_state_;
    mqtt_client->loop();
    if (connection_state_ == MQTT_CLIENT_CONNECTED &&
        !mqtt_client->connected()) {
      on_mqtt_disconnect(mqtt_client->lastError());
    }
  }

private:
  enum MQTTClientState {
    MQTT_CLIENT_DISCONNECTED = 0,
    MQTT_CLIENT_RESOLVING_ADDRESS,
    MQTT_CLIENT_CONNECTING,
    MQTT_CLIENT_CONNECTED,
  };

  MQTTClient *mqtt_client;
  Client *netclient = nullptr;
  void on_mqtt_connect(bool sessionPresent) {

    log_i("mqtt connected");
    char info[128];
    mqtt_client->setWill("tele/" HOSTNAME "/LWT", "Offline", true, 0);

    mqtt_client->subscribe("cmnd/" HOSTNAME "/#", 2);
    log_i("mqtt subscribed to %s", "cmnd/" HOSTNAME);
    //    mqtt_client->subscribe("stat/" HOSTNAME "/RESULT", 0);
    //  log_i("mqtt subscribed to %s", "stat/" HOSTNAME "/RESULT");
    log_i("mqtt %d", strlen(MQTT_DISCOVERY));
    mqtt_client->publish("homeassistant/light/" HA_ID "/light/config",
                         MQTT_DISCOVERY, true, 0);
    mqtt_client->publish("tele/" HOSTNAME "/LWT", "Online", true, 0);
    auto local_ip = WiFi.localIP();
    snprintf(info, sizeof(info),
             "{\"Hostname\":\"" HOSTNAME "\",\"IPAddress\":\"%u.%u.%u.%u\"}",
             local_ip[0], local_ip[1], local_ip[2], local_ip[3]);
    mqtt_client->publish("tele/" HOSTNAME "/INFO", info, false, 0);
    connection_state_ = MQTT_CLIENT_CONNECTED;
  }

  void on_mqtt_disconnect(lwmqtt_err_t reason) {
    connection_state_ = MQTT_CLIENT_DISCONNECTED;
    log_w("Mqtt was disconnected %d", reason);
    delay(100);
    xTaskNotifyGive(connect_task_);
  }

  void mqtt_callback(MQTTClient *client, char *topic, char *payload,
                     size_t length) {

    // String cmd;
    char message[kMaxMessageSize];
    char cmd[kMaxTopicSize];
    // the last segment of all subscribed topics/uris is the command
    char *pch = strrchr(topic, '/');
    if (pch && pch[1]) {
      pch++;
    } else {
      pch = topic;
    }
    snprintf(cmd, sizeof(cmd), "%s", pch);
    // safe assumption that cmd is 0 terminated
    pch = cmd;
    // lopwercase string and rtim space
    while (*pch != '\0' && *pch != ' ') {
      *pch = std::tolower(*pch);
      pch++;
    }
    // pch either points to a space or the terminating 0
    *pch = '\0';

    log_i("Message arrived in topic: %s", topic);

    if (length > kMaxMessageSize - 1) {
      length = kMaxMessageSize - 1;
    }

    for (int i = 0; i < length; i++) {
      message[i] = std::tolower(payload[i]);
    }
    message[length] = '\0';
    log_i("mqtt message: %s %s", cmd, message);
    if (command_handler_) {
      command_handler_(cmd, message);
    }
  }

  void send_pump_() {
    static mqtt_msg item;
    while (true) {
      xSemaphoreTake(mutex_, portMAX_DELAY);
      // thread_notification = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

      if (connected() && connection_state_ == MQTT_CLIENT_CONNECTED) {
        while (!mqtt_messages.empty()) {
          item = mqtt_messages.front();
          log_d("Publish: %s %s %d", item.topic, item.message, item.retained);
          if (mqtt_client->publish(item.topic, item.message, item.retained,
                                   0)) {
            mqtt_messages.pop();
            log_d("mqtt publish complete");
          } else {
            log_e("mqtt publish failed");
          }
          vTaskDelay(10);
        }
      }
      vTaskDelay(100);
    }
    vTaskDelete(nullptr); // should we every exit
  }

  // dispatch to member function
  static void send_pump(void *this_ptr) {
    auto *this_ = reinterpret_cast<MqttPublish *>(this_ptr);
    if (this_ != nullptr) {
      this_->send_pump_();
    }
  }

  bool mqtt_reconnect() {

    if (!app_utils::AppUtils::network_connected()) {
      log_e("MQTT Connect: network not connected");
      return false;
    }

    log_i("Connecting to MQTT...");
    if (connection_state_ == MQTT_CLIENT_CONNECTED) {
      log_i("already connected to MQTT ");
      return true;
    }

    if (connection_state_ == MQTT_CLIENT_CONNECTING) {
      log_i("already connecting to MQTT ...");
      vTaskDelay(1);
      return false;
    }
    connection_state_ = MQTT_CLIENT_CONNECTING;

    mqtt_client->setKeepAlive(90);

    bool result = false;
    // retry 10 times start with 250 ms delay (doubles for every retry)
    result = app_utils::retry(10, 250, [this]() {
      if (MQTTUSER != nullptr) {
        return mqtt_client->connect(HOSTNAME, MQTTUSER, MQTTPASSWORD);
      } else {
        return mqtt_client->connect(HOSTNAME);
      }
    }, true);
    if (result) {
      on_mqtt_connect(true);
      log_i("Connected to mqtt broker " MQTTHOST);
      connection_state_ = MQTT_CLIENT_CONNECTED;
      return true;
    } else {
      log_e("Unable to connect to mqtt");
      connection_state_ = MQTT_CLIENT_DISCONNECTED;
      return false;
    }
  }

  static const int kMaxTopicSize = 64;
  static const size_t kMaxMessageSize = 256;
  struct mqtt_msg {

    char topic[kMaxTopicSize];
    char message[kMaxMessageSize];
    bool retained = false;
    uint8_t qos = 0;

    mqtt_msg() = default;
    mqtt_msg(const mqtt_msg &msg)
        : mqtt_msg(msg.topic, msg.message, msg.retained, msg.qos) {}

    mqtt_msg(const char *topic, const char *message, bool retained = false,
             bool qos = 0) {
      strncpy(this->topic, topic, kMaxTopicSize);
      this->topic[kMaxTopicSize - 1] = '\0';
      strncpy(this->message, message, kMaxMessageSize);
      this->message[kMaxMessageSize - 1] = '\0';
      this->retained = retained;
      this->qos = qos;
    }
  };
  std::queue<mqtt_msg> mqtt_messages;
  SemaphoreHandle_t mutex_ = nullptr;
  static SemaphoreHandle_t connect_mux_;
  TaskHandle_t pump_task_;
  TaskHandle_t connect_task_;

  mqtt_command_handler command_handler_;
  volatile MQTTClientState connection_state_;
};
SemaphoreHandle_t MqttPublish::connect_mux_ = nullptr;
#endif

#endif