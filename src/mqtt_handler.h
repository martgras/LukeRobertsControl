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
  using mqtt_command_handler = std::function<bool(String, String)>;

  static void init(Client &network_client,
                   mqtt_command_handler command_handler = nullptr) {

    mqtt_client.setServer(MQTTHOST, MQTTPORT);
    if (command_handler) {
      command_handler_ = std::move(command_handler);
      mqtt_client.onMessage(mqtt_callback);
    }
    mqtt_client.onConnect(on_mqtt_connect);
    // mqtt_reconnect_timer = xTimerCreate("mqttTimer", pdMS_TO_TICKS(2000),
    // pdFALSE, (void*)0,
    // reinterpret_cast<TimerCallbackFunction_t>(connectToMqtt));
  }
  static void start() {
    pending_data = false;
    mutex_ = xSemaphoreCreateMutex();
    xTaskCreate(send_pump, "mqttsend", 8192, (void *)&mqtt_client, 1,
                &pump_task_);
  }

  static void queue(const char *topic, const char *message,
                    bool retained = false) {
    mqtt_messages.emplace(topic, message, retained);
    xSemaphoreGive(mutex_);
    // xTaskNotifyGive (pump_task_);
  }

  static bool connected() { return mqtt_client.connected(); }

  static void disconnect() {
    log_i("disonnecting from  MQTT...");
    mqtt_client.disconnect();
  }

  static bool mqtt_reconnect() {
    is_connected_ = true;
    log_i("Connecting to MQTT...");
    mqtt_client.connect();
    while (!is_connected_) {
      delay(50);
    }
    return mqtt_client.connected();
  }

  static void loop() {
    // mqtt_client.loop();
  }

private:
  static AsyncMqttClient mqtt_client;

  static void on_mqtt_connect(bool sessionPresent) {

    //      mqtt_client.onMessage(mqtt_callback);
    log_i("mqtt connected");
    mqtt_client.setWill("tele/" HOSTNAME "/LWT", 0, true, "Offline");
    mqtt_client.subscribe("cmnd/" HOSTNAME "/#", 0);
    log_i("mqtt subscribed to %s", "cmnd/" HOSTNAME);
    mqtt_client.subscribe("stat/" HOSTNAME "/RESULT", 0);
    //  log_i("mqtt subscribed to %s", "stat/" HOSTNAME "/RESULT");

    mqtt_client.publish("homeassistant/light/" HA_ID "/light/config", 0, true,
                        MQTT_DISCOVERY);
    mqtt_client.publish("tele/" HOSTNAME "/LWT", 1, true, "Online");
    mqtt_client.publish(
        "tele/" HOSTNAME "/INFO", 0, false,
        (String("{ \"Hostname\":\"" HOSTNAME "\" \"IPAddress\":\"") +
         WiFi.localIP().toString() + ("\"}"))
            .c_str());
    is_connected_ = true;
  }

  static void on_mqtt_disconnect(AsyncMqttClientDisconnectReason reason) {
    /*
      Serial.println("Disconnected from MQTT.");

      if (WiFi.isConnected()) {
        xTimerStart(mqttReconnectTimer, 0);
      }
    */
    is_connected_ = false;
  }
  static void mqtt_callback(char *topic, char *payload,
                            AsyncMqttClientMessageProperties properties,
                            size_t length, size_t index, size_t total) {

    String cmd;
    char message[kMaxMessageSize];

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

  static void send_pump(void *mqtt_client) {
    auto *client = (AsyncMqttClient *)mqtt_client;
    while (true) {
      xSemaphoreTake(mutex_, portMAX_DELAY);
      // thread_notification = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

      if (client != nullptr && client->connected()) {
        // reset reconnect counter after a minute
        while (!mqtt_messages.empty()) {
          auto item = mqtt_messages.front();
          char topic[kMaxTopicSize], msg[kMaxMessageSize];
          strcpy(topic, item.topic);
          strcpy(msg, item.message);
          log_v(" Pub: %s %s %d", topic, msg, item.retained);
          client->publish(topic, 0, item.retained, msg);
          vTaskDelay(1);
          mqtt_messages.pop();
          delay(100);
        }
        delay(200);
      }
    }
    vTaskDelete(nullptr); // shoud we every exit
  }

  static const int kMaxTopicSize = 64;
  static const size_t kMaxMessageSize = 256;
  struct mqtt_msg {

    char topic[kMaxTopicSize];
    char message[kMaxMessageSize];
    bool retained = false;

    mqtt_msg() = default;
    mqtt_msg(const char *topic, const char *message, bool retained = false) {
      strncpy(this->topic, topic, kMaxTopicSize);
      this->topic[kMaxTopicSize - 1] = '\0';
      strncpy(this->message, message, kMaxMessageSize);
      this->message[kMaxMessageSize - 1] = '\0';
      this->retained = retained;
    }
  };
  static std::queue<mqtt_msg> mqtt_messages;
  volatile static bool pending_data;
  static SemaphoreHandle_t mutex_;
  static TaskHandle_t pump_task_;
  static mqtt_command_handler command_handler_;
  volatile static bool is_connected_;
};

std::queue<MqttPublish::mqtt_msg> MqttPublish::mqtt_messages;
volatile bool MqttPublish::pending_data;
volatile bool MqttPublish::is_connected_;
SemaphoreHandle_t MqttPublish::mutex_;
TaskHandle_t MqttPublish::pump_task_;
MqttPublish::mqtt_command_handler MqttPublish::command_handler_ = nullptr;
AsyncMqttClient MqttPublish::mqtt_client;
// TimerHandle_t MqttPublish::mqtt_reconnect_timer;
#endif