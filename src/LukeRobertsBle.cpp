#include <string>
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <map>
#include <SPIFFS.h>
#include "LukeRobertsBle.h"

void BleGattClient::init(BLEAddress device_addr) {
  //  device_addr_ =
  NimBLEDevice::init("");
  //  device_addr_ = BLEAddress(LR_BLEADDRESS, 1);
  device_addr_ = device_addr;
  NimBLEDevice::getScan()->setActiveScan(false);
  NimBLEDevice::getScan()->stop();
  initialized_ = true;
}
bool BleGattClient::connect_to_server(on_complete_callback on_complete) {
  log_d("Connect client");

  // device_addr_ = deviceaddr;
  if (!initialized_) {
    log_e("BleGattClient not initalized");
    return false;
  }
  connected_ = false;
  /** No client to reuse? Create a new one. */
  if (!client) {
    if (NimBLEDevice::getClientListSize() >= NIMBLE_MAX_CONNECTIONS) {
      log_e("BLE Connect Max clients reached - no more connections available");
      return false;
    }

    client = NimBLEDevice::createClient(device_addr_);

    log_d("New BLE client created");

    client->setClientCallbacks(&client_cb_, false);

    client_cb_.set_on_connect(*this, [&]() {
      this->connected_ = true;
      log_d("BLE CONNECT");
    });
    client_cb_.set_on_disconnect(*this, [&]() {
      this->connected_ = false;
      log_d("BLE DISCONNECT ");
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

    if (!client->connect(device_addr_, true)) {
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
    if (!client->connect(device_addr_, true)) {
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
      log_d("%s Value %s ", characteristic->getUUID().toString().c_str(),
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
        yield();
      }
    }
    // Other pending commands
    while (!pending_commands.empty()) {
      auto cmd = pending_commands.front();
      send(cmd.data, cmd.size);
      needs_result = true;
      yield();
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
      log_d("trying to connect");
      if (connected() || connect_to_server()) {
        log_i("Connected to the BLE Server.");
      } else {
        failed_connects++;
        log_w("Failed to connect to the BLE server (%d)", failed_connects);
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
        log_d("%ld Command sent", last_sent);
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

BLEAddress BleGattClient::device_addr_;
BleGattClient::on_complete_callback BleGattClient::on_connect_ = nullptr;
BleGattClient::on_complete_callback BleGattClient::on_disconnect_ = nullptr;
notify_callback BleGattClient::on_notify_ = nullptr;
BleGattClient::on_downlight_callback BleGattClient::on_downlight_notification_ =
    nullptr; // [](uint8_t,uint16_t){  log_i("=============== DEFAULT ON DOWN
             // ===============");} ;

// BleGattClient::ClientCallbacks BleGattClient::client_cb_;

bool BleGattClient::initialized_ = false;
// volatile bool BleGattClient::connected_ = false;
#ifdef USE_SCENE_MAPPER
std::vector<int> SceneMapper::brightness_map_ = {
    0, 63, 71, 39, 42, 48, 10, 100}; // Load the default values

std::vector<int> SceneMapper::colortemperature_map_ = {
    0, 2700, 4000, 3800, 2800, 4000, 2700, 4000}; // Load the default values
#endif 

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

    // There is nothing in the response to identify if the response is from the
    // query scene request. Therefore looking at messages longer than 4 bytes
    // and
    // where the second response byte is 1 as a best guess that it must be a
    // scene response
    if (length > 4 && pRemoteCharacteristic->getUUID() == charUUID &&
        pData[0] == 0 && pData[1] == 1) {
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
  log_d("Got %d scenes", scenes.size());

  vSemaphoreDelete(new_scene_received);
  new_scene_received = nullptr;
  client.set_on_notify(prev);
  return scenes.size();
}

void request_downlight_settings(BleGattClient &client) {

  BleGattClient::BleCommand cmd = {{0x09}, 1, true, nullptr};
  client.queue_cmd(cmd);
}

void get_all_scenes(BleGattClient &client) {
  if (scenes.size() < 2) {
    request_all_scenes(client);
    for (const auto &s : scenes) {
      log_d("Scene %d name=%s", s.first, s.second.c_str());
    }
  }
}

uint8_t get_current_scene(BleGattClient &client) {
  NimBLERemoteCharacteristic *pRemoteCharacteristic2 =
      client.service->getCharacteristic(charUUID_Scene);
  if (pRemoteCharacteristic2->canRead()) {
    int v = pRemoteCharacteristic2->readValue<uint8_t>();
    log_i("Current Scene: %d\r\n ", v);
    return v;
  }
  return 0xFF;
}

#ifndef LR_BLEADDRESS
/** Define a class to handle the callbacks when advertisments are received */
class BleScanner : public NimBLEAdvertisedDeviceCallbacks {

  static NimBLEAddress device_address;
  static BLEUUID searched_serviceUUID;
  void onResult(NimBLEAdvertisedDevice *advertisedDevice) {

    if (advertisedDevice->isAdvertisingService(searched_serviceUUID)) {
      log_i("Advertised Device found: ");

      log_i("Found Our Service");
      /** stop scan before connecting */

      log_i("Device found %s", advertisedDevice->toString().c_str());
      device_address = advertisedDevice->getAddress();
      NimBLEDevice::getScan()->stop();
    }
  };

public:
  static NimBLEAddress scan(BLEUUID my_serviceUUID) {
    NimBLEScan *pScan = NimBLEDevice::getScan();
    BleScanner cb;
    cb.searched_serviceUUID = my_serviceUUID;
    /** create a callback that gets called when advertisers are found */
    pScan->setAdvertisedDeviceCallbacks(&cb);

    /** Set scan interval (how often) and window (how long) in milliseconds */
    pScan->setInterval(45);
    pScan->setWindow(15);

    /** Active scan will gather scan response data from advertisers
     *  but will use more energy from both devices
     */
    pScan->setActiveScan(true);
    /** Start scanning for advertisers for the scan time specified (in
     * seconds)
     * 0 = forever
     *  Optional callback for when scanning stops.
     */

    auto result = pScan->start(5, false);

    log_i("Scan complete");
    return cb.device_address;
  }
};
NimBLEAddress BleScanner::device_address;
BLEUUID BleScanner::searched_serviceUUID;

NimBLEAddress scan_for_device() {

  NimBLEDevice::init("");

  static NimBLEAddress adr;
  adr = BleScanner::scan(serviceUUID);

  return adr;
}
#endif