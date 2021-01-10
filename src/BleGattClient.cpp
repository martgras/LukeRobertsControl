#include <string>
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <map>

#include "BleGattClient.h"

BLEUUID BleGattClient::serviceUUID;
BLEUUID BleGattClient::charUUID;

void BleGattClient::init(BLEAddress device_addr, BLEUUID service_UUID) {
  NimBLEDevice::init("");
  //  device_addr_ = BLEAddress(LR_BLEADDRESS, 1);
  device_addr_ = device_addr;
  serviceUUID = service_UUID;
  NimBLEDevice::getScan()->setActiveScan(false);
  NimBLEDevice::getScan()->stop();
  initialized_ = true;
}
bool BleGattClient::connect_to_server(BLEUUID characteristicsUUID,
                                      on_complete_callback on_complete) {
  log_d("Connect client");
  charUUID = characteristicsUUID;
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
      log_d("BLE DISCONNECT");
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

   auto rssi = client->getRssi();
  log_i("Connected to: %s", client->getPeerAddress().toString().c_str());
  log_i("RSSI: %d",rssi);
  if (rssi == 0) {
      NimBLEDevice::deleteClient(client);
      log_e("BLE Connect %s", "Failed to connect rssi = 0, deleted client");
      client = nullptr;
      connected_ = false;
      return false ; 
  }
  /** Now we can read/write/subscribe the charateristics of the services we
   * are
   * interested in */

  service = client->getService(serviceUUID);
  if (service) { /** make sure it's not null */
    characteristic = service->getCharacteristic(charUUID);
  } else {
    log_e("BLE Connect failed: service not found.");    
    return false ;
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
    log_e("BLE Connect failed: characteristic not found.");
    return false ;
  }
  log_i("Freeheap = %lu", ESP.getFreeHeap());
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
      delay(100);
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
      if (connected() || connect_to_server(charUUID)) {
        log_i("Connected to the BLE Server.");
      } else {
        failed_connects++;
        log_w("Failed to connect to the BLE server (%d)", failed_connects);
        if (failed_connects > 20) {
          log_e("Failed to connect to the BLE server more than %d times. "
                "Reboooting..",
                failed_connects);
          esp_sleep_enable_timer_wakeup(10);
          delay(100);
          esp_deep_sleep_start();
        }
      }
      connecting = false;
    }
    #if CORE_DEBUG_LEVEL > 3
    long start = millis();
    #endif

    if (connected()) {
      if (send_queued()) {
        if (on_send) {
          on_send();
        }
      }
      log_d("Command sent in %ld ms", millis() - start);
    }
  }
}

static void ble_loop_(void *this_ptr) {
    auto *this_ = static_cast<BleGattClient *>(this_ptr);
      if (this_ != nullptr) {
        this_->loop();
      }
}

void BleGattClient::start_ble_loop()
{
    xTaskCreatePinnedToCore([](void* this_ptr){
      while(1) {
      ble_loop_(this_ptr);
      vTaskDelay(50 / portTICK_PERIOD_MS);
      }
    }, "blesend", 8192, this, 1,
                            nullptr,1);

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
std::list<BleGattClient::ble_notify_callback_t> BleGattClient::callbacks_;

// BleGattClient::ClientCallbacks BleGattClient::client_cb_;

bool BleGattClient::initialized_ = false;
// volatile bool BleGattClient::connected_ = false;
#ifdef USE_SCENE_MAPPER
std::vector<int> SceneMapper::brightness_map_ = {
    0, 63, 71, 39, 42, 48, 10, 100}; // Load the default values

std::vector<int> SceneMapper::colortemperature_map_ = {
    0, 2700, 4000, 3800, 2800, 4000, 2700, 4000}; // Load the default values
#endif

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