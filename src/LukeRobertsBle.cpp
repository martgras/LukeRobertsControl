#include <string>
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <map>
#include <SPIFFS.h>
#include "LukeRobertsBle.h"

bool BleGattClient::connect_to_server(on_complete_callback on_complete) {
  log_d("Connect client");

  if (!initialized_) {
    NimBLEDevice::init("");
    NimBLEDevice::getScan()->setActiveScan(false);
    NimBLEDevice::getScan()->stop();
    initialized_ = true;
  }
  connected_ = false;
  /** No client to reuse? Create a new one. */
  if (!client) {
    if (NimBLEDevice::getClientListSize() >= NIMBLE_MAX_CONNECTIONS) {
      log_e("BLE Connect Max clients reached - no more connections available");
      return false;
    }

    client = NimBLEDevice::createClient(deviceaddr);

    log_i("New BLE client created");

    client->setClientCallbacks(&client_cb_, false);

    client_cb_.set_on_connect(*this, [&]() {
      this->connected_ = true;
      log_i("BLE Connect ON CONNECT LAMBDA ==========================");
    });
    client_cb_.set_on_diconnect(*this, [&]() {
      this->connected_ = false;
      log_i("BLE Connect ON DISCONNECT LAMBDA ==========================");
    });
    /** Set initial connection parameters: These settings are 15ms interval, 0
     * latency, 120ms timout.
     *  These settings are safe for 3 clients to connect reliably, can go
     * faster
     * if you have less
     *  connections. Timeout should be a multiple of the interval, minimum is
     * 100ms.
     *  Min interval: 12 * 1.25ms = 15, Max interval: 12 * 1.25ms = 15, 0
     * latency, 51 * 10ms = 510ms timeout
     */
    client->setConnectionParams(12, 12, 0, 120);
    /** Set how long we are willing to wait for the connection to complete
     * (seconds), default is 30. */
    client->setConnectTimeout(5);

    if (!client->connect(deviceaddr, true)) {
      /** Created a client but failed to connect, don't need to keep it as it
       * has no data */
      NimBLEDevice::deleteClient(client);
      log_e("BLE Connect %s", "Failed to connect, deleted client");
      client = nullptr;
      connected_ = false;
      return false;
    }
  }

  if (!client->isConnected()) {
    if (!client->connect(deviceaddr, true)) {
      log_e("BLE Connect %s", "Failed to connect");
      connected_ = false;
      return false;
    }
  }

  log_i("Connected to: %s", client->getPeerAddress().toString().c_str());
  log_i("RSSI: %d", client->getRssi());

  /** Now we can read/write/subscribe the charateristics of the services we
   * are
   * interested in */

  service = client->getService(serviceUUID);
  if (service) { /** make sure it's not null */
    characteristic = service->getCharacteristic(charUUID);
  }

  if (characteristic) { /** make sure it's not null */
    if (characteristic->canRead()) {
      log_i("%s Value %s ", characteristic->getUUID().toString().c_str(),
            characteristic->readValue().c_str());
    }

    /** registerForNotify() has been deprecated and replaced with subscribe()
     * /
     * unsubscribe().
     *  Subscribe parameter defaults are: notifications=true,
     * notifyCallback=nullptr, response=false.
     *  Unsubscribe parameter defaults are: response=false.
     */
    if (characteristic->canNotify()) {
      // if(!pChr->registerForNotify(notifyCB)) {
      if (!characteristic->subscribe(true, notifyCB)) {
        /** Disconnect if subscribe failed */
        client->disconnect();
        connected_ = false;
        return false;
      }
    } else if (characteristic->canIndicate()) {
      /** Send false as first argument to subscribe to indications instead of
       * notifications */
      // if(!pChr->registerForNotify(notifyCB, false)) {
      if (!characteristic->subscribe(false, notifyCB)) {
        /** Disconnect if subscribe failed */
        client->disconnect();
        connected_ = false;
        return false;
      }
    }
  } else {
    log_e("BLE Connect %s", "service not found.");
  }
  log_d("Freeheap = %lu", ESP.getFreeHeap());
  connected_ = true;
  if (on_complete != nullptr) {
    on_complete();
  }
  return client->isConnected();
  // return true;
}

bool BleGattClient::send_queued() {
  bool needs_result = false;
  static bool sending = false;

  if (!sending) {
    sending = true;
    for (auto &command : cached_commands) {
      if (command.is_dirty) {
        send(command.data, command.size);
        command.is_dirty = false;
        // b.report_to_mqtt(0);
        if (command.on_send) {
          command.on_send(0);
        }
        needs_result = true;
        delay(50);
      }
    }
    // Other pending commands
    while (!pending_commands.empty()) {
      auto cmd = pending_commands.front();
      send(cmd.data, cmd.size);
      needs_result = true;
      delay(50);
      pending_commands.pop();
      if (cmd.on_send) {
        cmd.on_send(0);
      }
    }
    sending = false;
  }
  return needs_result;
}

void BleGattClient::loop(on_complete_callback on_send) {

  static uint8_t failed_connects = 0;
  bool is_dirty = false;

  if (!pending_commands.empty()) {
    is_dirty = true;
  } else {
    for (auto i : cached_commands) {
      if (i.is_dirty) {
        is_dirty = true;
        break;
      }
    }
  }
  if (is_dirty) {
    static bool connecting = false;
    if (!connecting) {
      connecting = true;
      log_i("trying to connect");
      if (connected() || connect_to_server()) {
        log_i("Connected to the BLE Server.");
      } else {
        failed_connects++;
        log_i("Failed to connect to the BLE server (%d)", failed_connects);
        if (failed_connects > 20) {
          log_e("Failed to connect to the BLE server more than %d times. "
                "Reboooting..",
                failed_connects);
          ESP.restart();
        }
      }
      connecting = false;
    }
    static unsigned long last_sent = 0;
    //    if (millis() - last_sent <  200) // throttle a bit:  wait 200 ms
    //    between commands
    {
      if (connected()) {
        if (send_queued()) {
          if (on_send) {
            on_send();
          }
        }
        last_sent = millis();
        log_i("%ld Command sent", last_sent);
      }
    }
  }
}

/*
NimBLEClient *BleGattClient::client = nullptr;
NimBLERemoteService *BleGattClient::service = nullptr;
NimBLERemoteCharacteristic *BleGattClient::characteristic = nullptr;
NimBLERemoteDescriptor *BleGattClient::remote_descriptor = nullptr;
*/
BleGattClient::on_complete_callback BleGattClient::on_connect_ = nullptr;
BleGattClient::on_complete_callback BleGattClient::on_disconnect_ = nullptr;
notify_callback BleGattClient::on_notify_ = nullptr;
// BleGattClient::ClientCallbacks BleGattClient::client_cb_;

bool BleGattClient::initialized_ = false;
// volatile bool BleGattClient::connected_ = false;

std::vector<int> SceneBrightnessMapper::brightness_map_ = {
    0, 65, 70, 50, 40, 45, 10, 98, 45}; // Load the default values

std::map<uint8_t, std::string> scenes = {{0, "OFF"}};

/*
  requests all scenes from the lamp

*/
int request_all_scenes(BleGattClient &client) {
  uint8_t blecmd[] = {0xA0, 0x1, 0x1, 0x0};

  if (!client.connected()) {
    int retry = 3;
    while (retry-- && !client.connect_to_server()) {
      delay(10);
    }
  }
  if (!client.connected()) {
    return false;
  }

  SemaphoreHandle_t new_scene_received = xSemaphoreCreateBinary();

  auto start = millis();

  uint8_t last_reported_scene = 0;

  // create a notification callback directly (lambda avoids global vars)
  auto prev = client.set_on_notify([&new_scene_received, &last_reported_scene](
      NimBLERemoteCharacteristic *pRemoteCharacteristic, uint8_t *pData,
      size_t length, bool isNotify) {

    for (auto i = 0; i < length; i++) {
      log_i("Response byte: %d (0x%X)", pData[i], pData[i]);
    }

    // There is nothing in the response to identify if the response is from the
    // query scene request. Therefore looking at messages longer than 4 bytes
    // and
    // where the second response byte is 1 as a best guess that it must be a
    // scene response
    if (length > 4 && pRemoteCharacteristic->getUUID() == charUUID) {
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
    }
  });

  last_reported_scene = 0;
  while (last_reported_scene != 0xFF && millis() - start < 5000) {
    blecmd[3] = last_reported_scene;
    client.write(blecmd, 4);
    if (!xSemaphoreTake(new_scene_received, 5000 / portTICK_PERIOD_MS)) {
      break;
    }
    /*
    while (got_new_scene == false millis() - start < 5000) {
      vTaskDelay(10);
    }
    */
  }
  log_i("Got %d scenes", scenes.size());

  vSemaphoreDelete(new_scene_received);
  new_scene_received = nullptr;
  client.set_on_notify(prev);
  return scenes.size();
}

void get_all_scenes(BleGattClient &client) {
  if (scenes.size() < 2) {
    request_all_scenes(client);
    for (const auto &s : scenes) {
      log_i("Scene %d name=%s", s.first, s.second.c_str());
    }
  }
}

uint8_t get_current_scene(BleGattClient &client) {
  NimBLERemoteCharacteristic *pRemoteCharacteristic2 =
      client.service->getCharacteristic(charUUID_Scene);
  if (pRemoteCharacteristic2->canRead()) {
    int v = pRemoteCharacteristic2->readValue<uint8_t>();
    log_i("Scene: %d\r\n ", v);
    return v;
  }
  return 0xFF;
}
