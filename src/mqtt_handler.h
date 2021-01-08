#ifndef __MQTT_HANDLER_H__
#define __MQTT_HANDLERH__

#define HA_ID HOSTNAME "_device"

#include <AsyncMqttClient.h>

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
  MqttPublish() { mqtt_client = new AsyncMqttClient(); }
  ~MqttPublish() {
    if (mqtt_client) {
      delete mqtt_client;
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

  using mqtt_command_handler = std::function<bool(String, String)>;

  void init(Client &network_client,
            mqtt_command_handler command_handler = nullptr) {

    netclient = &network_client;
    mqtt_client->setServer(MQTTHOST, MQTTPORT);
    if (command_handler) {
      command_handler_ = std::move(command_handler);
      mqtt_client->onMessage([&](char *topic, char *payload,
                                 AsyncMqttClientMessageProperties properties,
                                 size_t length, size_t index, size_t total) {
        mqtt_callback(topic, payload, properties, length, index, total);
      });
    }
    mqtt_client->onConnect(
        [&](bool has_session) { on_mqtt_connect(has_session); });
    mqtt_client->onDisconnect([&](AsyncMqttClientDisconnectReason reason) {
      on_mqtt_disconnect(reason);
    });
    // mqtt_reconnect_timer = xTimerCreate("mqttTimer", pdMS_TO_TICKS(2000),
    // pdFALSE, (void*)0,
    // reinterpret_cast<TimerCallbackFunction_t>(connectToMqtt));
  }

  void start() {
    pending_data = false;
    mutex_ = xSemaphoreCreateMutex();
    /*
    xTaskCreate(send_pump, "mqttsend", 8192, (void *)&mqtt_client, 1,
                &pump_task_);
    */
  }

  void queue(const char *topic, const char *message, bool retained = false,
             uint8_t qos = 0) {
    mqtt_messages.emplace(topic, message, retained, qos);
    xSemaphoreGive(mutex_);
    // xTaskNotifyGive (pump_task_);
  }

  bool connected() { return mqtt_client->connected(); }

  void disconnect() {
    log_i("disonnecting from  MQTT...");
    connection_state_ = MQTT_CLIENT_DISCONNECTED;
    mqtt_client->disconnect();
  }

  bool mqtt_connect() { return mqtt_reconnect(); }

  bool mqtt_reconnect() {

    if (connection_state_ == MQTT_CLIENT_CONNECTING) {
      log_i(" already connecting to MQTT ...");
      delay(500);
      return false;
    }
    log_i("Connecting to MQTT...");
    if (connection_state_ == MQTT_CLIENT_CONNECTED) {
      log_i(" already connected to MQTT ");
      return true;
    }
    mqtt_client->setKeepAlive(90);
    if (MQTTUSER != nullptr) {
      mqtt_client->setCredentials(MQTTUSER, MQTTPASSWORD);
    }
    mqtt_client->connect();
    connection_state_ = MQTT_CLIENT_CONNECTING;
    auto start_time = millis();
    while (connection_state_ != MQTT_CLIENT_CONNECTED) {
      delay(50);
      if (millis() - start_time > 30000) {
        esp_sleep_enable_timer_wakeup(10);
        delay(100);
        esp_deep_sleep_start();
      }
    }
    // return mqtt_client->connected();
    return true;
  }

  static void loop() {
    // mqtt_client->loop();
  }

private:
  enum MQTTClientState {
    MQTT_CLIENT_DISCONNECTED = 0,
    MQTT_CLIENT_RESOLVING_ADDRESS,
    MQTT_CLIENT_CONNECTING,
    MQTT_CLIENT_CONNECTED,
  };

  AsyncMqttClient *mqtt_client;
  Client *netclient;
  void on_mqtt_connect(bool sessionPresent) {

    //      mqtt_client->onMessage(mqtt_callback);
    log_i("mqtt connected");
    mqtt_client->setWill("tele/" HOSTNAME "/LWT", 0, true, "Offline");

    mqtt_client->subscribe("cmnd/" HOSTNAME "/#", 2);
    log_i("mqtt subscribed to %s", "cmnd/" HOSTNAME);
    //    mqtt_client->subscribe("stat/" HOSTNAME "/RESULT", 0);
    //  log_i("mqtt subscribed to %s", "stat/" HOSTNAME "/RESULT");

    mqtt_client->publish("homeassistant/light/" HA_ID "/light/config", 0, true,
                         MQTT_DISCOVERY);
    mqtt_client->publish("tele/" HOSTNAME "/LWT", 1, true, "Online");
    mqtt_client->publish(
        "tele/" HOSTNAME "/INFO", 0, false,
        (String("{ \"Hostname\":\"" HOSTNAME "\" \"IPAddress\":\"") +
         WiFi.localIP().toString() + ("\"}"))
            .c_str());
    connection_state_ = MQTT_CLIENT_CONNECTED;
    xTaskCreatePinnedToCore(send_pump, "mqttsend", 8192, (void *)this, 1,
                            &pump_task_, 1);
  }

  void on_mqtt_disconnect(AsyncMqttClientDisconnectReason reason) {

    connection_state_ = MQTT_CLIENT_DISCONNECTED;
    log_w("Mqtt was disconnected %d", reason);
    vTaskDelete(pump_task_);
    vSemaphoreDelete(mutex_);

    recreate_client();
    delay(100);
    mutex_ = xSemaphoreCreateMutex();
    xTaskCreatePinnedToCore([](void *this_ptr) {
      reinterpret_cast<MqttPublish *>(this_ptr)->mqtt_reconnect();
      vTaskDelete(nullptr);
    }, "mqtt_connect", 4096, this, 2, nullptr, 1);
  }

  void mqtt_callback(char *topic, char *payload,
                     AsyncMqttClientMessageProperties properties, size_t length,
                     size_t index, size_t total) {

    String cmd;
    char message[kMaxMessageSize];
    // the last segment of all subscribed topics/uris is the command
    char *pch = strrchr(topic, '/');
    if (pch && pch[1]) {
      cmd = pch + 1;
    } else {
      cmd = topic;
    }

    log_i("Message arrived in topic: %s", topic);

    if (length > kMaxMessageSize - 1) {
      length = kMaxMessageSize - 1;
    }

    for (int i = 0; i < length; i++) {
      message[i] = std::tolower(payload[i]);
    }
    message[length] = '\0';
    cmd.trim();
    cmd.toLowerCase();
    log_i("mqtt message: %s %s", cmd.c_str(), message);
    if (command_handler_) {
      command_handler_(cmd, message);
    }
  }

  void send_pump_() {
    while (true) {
      xSemaphoreTake(mutex_, portMAX_DELAY);
      // thread_notification = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

      if (connected() && connection_state_ == MQTT_CLIENT_CONNECTED) {
        while (!mqtt_messages.empty()) {
          mqtt_msg &item = mqtt_messages.front();
          log_d("Publish: %s %s %d", item.topic, item.message, item.retained);
          if (mqtt_client->publish(item.topic, 0, item.retained,
                                   item.message)) {
            vTaskDelay(10);
            mqtt_messages.pop();
            log_d("mqtt publish complete");
          } else {
            log_e("mqtt publish failed");
          }
        }
      } else {
        if (connection_state_ != MQTT_CLIENT_CONNECTING) {
          //      mqtt_reconnect();
        }
      }
      delay(200);
    }
    vTaskDelete(nullptr); // shoud we every exit
  }

  // dispatch to member function
  static void send_pump(void *this_ptr) {
    auto *this_ = reinterpret_cast<MqttPublish *>(this_ptr);
    if (this_ != nullptr) {
      this_->send_pump_();
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
  volatile bool pending_data;
  SemaphoreHandle_t mutex_;
  TaskHandle_t pump_task_;
  mqtt_command_handler command_handler_;
  volatile MQTTClientState connection_state_;
};
#endif