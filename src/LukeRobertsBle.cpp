#include <string>
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <map>
#include <SPIFFS.h>
#include "LukeRobertsBle.h"

// The remote service we wish to connect to.
static BLEUUID serviceUUID("44092840-0567-11E6-B862-0002A5D5C51B");
// The characteristic of the remote service we are interested in.
static BLEUUID charUUID("44092842-0567-11E6-B862-0002A5D5C51B");
static BLEUUID descUUID("00002902-0000-1000-8000-00805f9b34fb");
static BLEUUID charUUID_Scene("44092844-0567-11E6-B862-0002A5D5C51B");
static BLEAddress deviceaddr = BLEAddress(LR_BLEADDRESS, 1);

NimBLERemoteService *pSvc = nullptr;
NimBLERemoteCharacteristic *pChr = nullptr;
NimBLERemoteDescriptor *pDsc = nullptr;

void scanEndedCB(NimBLEScanResults results);

static NimBLEAdvertisedDevice *ble_device;

std::vector<int> SceneBrightnessMapper::brightness_map_ = {
    0, 65, 70, 50, 40, 45, 10, 98, 45}; // Load the default values

bool ble_connected = false;
#ifdef USE_BLE_SCAN
static uint32_t scanTime = 1; /** 0 = scan forever */
#endif

std::map<uint8_t, std::string> scenes = {{0, "OFF"}};
bool got_new_scene = false;
uint8_t last_reported_scene = 0;
uint8_t max_scenes = 1;

int request_all_scenes() {
  uint8_t blecmd[] = {0xA0, 0x1, 0x1, 0x0};
  if (!ble_connected) {
    int retry = 3;
    while (retry-- && !connect_to_server()) {
      delay(10);
    }
  }
  if (!ble_connected) {
    return false;
  }
  auto start = millis();
  last_reported_scene = 0;
  while (last_reported_scene != 0xFF && millis() - start < 5000) {
    got_new_scene = false;
    blecmd[3] = last_reported_scene;
    ble_write(blecmd, 4);
    while (got_new_scene == false) {
      vTaskDelay(10);
    }
    max_scenes = scenes.size();
  }
  log_i("Got %d scenes", scenes.size());
  return scenes.size();
}

void get_all_scenes() {
  if (scenes.size() < 2) {
    request_all_scenes();
    for (const auto &s : scenes) {
      log_i("Scene %d name=%s", s.first, s.second.c_str());
    }
  }
}

/**  None of these are required as they will be handled by the library with
 *defaults. **
 **                       Remove as you see fit for your needs */
class ClientCallbacks : public NimBLEClientCallbacks {
  void onConnect(NimBLEClient *pClient) {
    log_i("BLE Connected");
    /** After connection we should change the parameters if we don't need fast
     * response times.
     *  These settings are 150ms interval, 0 latency, 450ms timout.
     *  Timeout should be a multiple of the interval, minimum is 100ms.
     *  I find a multiple of 3-5 * the interval works best for quick
     * response/reconnect.
     *  Min interval: 120 * 1.25ms = 150, Max interval: 120 * 1.25ms = 150, 0
     * latency, 60 * 10ms = 600ms timeout
     */
    pClient->updateConnParams(120, 120, 0, 60);
    ble_connected = true;
  };

  void onDisconnect(NimBLEClient *pClient) {

    log_i("%s Disconnected", pClient->getPeerAddress().toString().c_str());
    ble_connected = false;
    //   NimBLEDevice::getScan()->start(scanTime, scanEndedCB);
  };

  /** Called when the peripheral requests a change to the connection parameters.
   *  Return true to accept and apply them or false to reject and keep
   *  the currently used parameters. Default will return true.
   */
  bool onConnParamsUpdateRequest(NimBLEClient *pClient,
                                 const ble_gap_upd_params *params) {
    if (params->itvl_min < 12) { /** 1.25ms units */
      return false;
    } else if (params->itvl_max > 40) { /** 1.25ms units */
      return false;
    } else if (params->latency > 2) { /** Number of intervals allowed to skip */
      return false;
    } else if (params->supervision_timeout > 100) { /** 10ms units */
      return false;
    }

    return true;
  };

  /********************* Security handled here **********************
  ****** Note: these are the same return values as defaults ********/
  uint32_t onPassKeyRequest() {
    log_i("Client Passkey Request");
    /** return the passkey to send to the server */
    return 123456;
  };

  bool onConfirmPIN(uint32_t pass_key) {
    log_i("The passkey YES/NO number: pass key %lu", pass_key);
    /** Return false if passkeys don't match. */
    return true;
  };

  /** Pairing process complete, we can check the results in ble_gap_conn_desc */
  void onAuthenticationComplete(ble_gap_conn_desc *desc) {
    if (!desc->sec_state.encrypted) {
      log_i("Encrypt connection failed - disconnecting");
      /** Find the client with the connection handle provided in desc */
      NimBLEDevice::getClientByID(desc->conn_handle)->disconnect();
      return;
    }
  };
};

/** Define a class to handle the callbacks when advertisments are received */
class AdvertisedDeviceCallbacks : public NimBLEAdvertisedDeviceCallbacks {

  void onResult(NimBLEAdvertisedDevice *advertisedDevice) {
    log_i("Advertised Device found: %s", advertisedDevice->toString().c_str());
    if (advertisedDevice->isAdvertisingService(NimBLEUUID("DEAD"))) {
      log_i("Found Our Service");
      /** stop scan before connecting */
      NimBLEDevice::getScan()->stop();
      /** Save the device reference in a global for the client to use*/
      ble_device = advertisedDevice;
      /** Ready to connect now */
    }
  };
};

/** Notification / Indication receiving handler callback */
void notifyCB(NimBLERemoteCharacteristic *pRemoteCharacteristic, uint8_t *pData,
              size_t length, bool isNotify) {
  log_i(
      "%s from %s : Service = %s, Characteristic = %s, len=%d, Value=<%s> (%d)",
      (isNotify == true) ? "Notification" : "Indication",
      pRemoteCharacteristic->getRemoteService()
          ->getClient()
          ->getPeerAddress()
          .toString()
          .c_str(),
      pRemoteCharacteristic->getRemoteService()->getUUID().toString().c_str(),
      pRemoteCharacteristic->getUUID().toString().c_str(), length,
      (char *)pData, (int)pData[0]);
  got_new_scene = false;

  for (auto i = 0; i < length; i++) {
    log_i("Response byte: %d (0x%X)", pData[i], pData[i]);
  }
  // Scene names
  if (length > 4) {
    char tmp[32];
    if (length > 32)
      length = 32;
    strncpy(tmp, (const char *)&pData[3], length - 3);
    tmp[length - 3] = '\0';
    log_d("Scene notification : %d : %s", pData[2], tmp);
    scenes[last_reported_scene] = tmp;
    last_reported_scene = pData[2];

    got_new_scene = true;
  }
}

bool scan_complete = false;
/** Callback to process the results of the last scan or restart it */
void scanEndedCB(NimBLEScanResults results) {
  log_d("BLE Scan Ended");
  scan_complete = true;
}

/** Create a single global instance of the callback class to be used by all
 * clients */
static ClientCallbacks clientCB;

/** Handles the provisioning of clients and connects / interfaces with the
 * server */
bool connect_to_server(on_complete_callback on_complete) {
  log_d("Connect client");
  NimBLEClient *pClient = nullptr;

  /** Check if we have a client we should reuse first **/
  if (NimBLEDevice::getClientListSize()) {
    /** Special case when we already know this device, we send false as the
     *  second argument in connect() to prevent refreshing the service database.
     *  This saves considerable time and power.
     */
    pClient = NimBLEDevice::getClientByPeerAddress(deviceaddr);
    log_v("pClient = %lx", pClient);
    if (pClient) {
      log_i("Tyring to reconnect client");
      //  if (!pClient->connect(ble_device, false)) {
      if (!pClient->connect(false)) {
        log_e("%s", "Reconnect failed");
        return false;
      }
      log_i("%s", "Reconnected client");
    }
    /** We don't already have a client that knows this device,
     *  we will check for a client that is disconnected that we can use.
     */
    else {
      pClient = NimBLEDevice::getDisconnectedClient();
    }
  }

  /** No client to reuse? Create a new one. */
  if (!pClient) {
    if (NimBLEDevice::getClientListSize() >= NIMBLE_MAX_CONNECTIONS) {
      log_e("BLE Connect Max clients reached - no more connections available");
      return false;
    }

    pClient = NimBLEDevice::createClient(deviceaddr);

    log_i("New BLE client created");

    pClient->setClientCallbacks(&clientCB, false);
    /** Set initial connection parameters: These settings are 15ms interval, 0
     * latency, 120ms timout.
     *  These settings are safe for 3 clients to connect reliably, can go faster
     * if you have less
     *  connections. Timeout should be a multiple of the interval, minimum is
     * 100ms.
     *  Min interval: 12 * 1.25ms = 15, Max interval: 12 * 1.25ms = 15, 0
     * latency, 51 * 10ms = 510ms timeout
     */
    pClient->setConnectionParams(12, 12, 0, 120);
    /** Set how long we are willing to wait for the connection to complete
     * (seconds), default is 30. */
    pClient->setConnectTimeout(5);

    if (!pClient->connect()) {
      /** Created a client but failed to connect, don't need to keep it as it
       * has no data */
      NimBLEDevice::deleteClient(pClient);
      log_e("BLE Connect %s", "Failed to connect, deleted client");
      return false;
    }
  }

  if (!pClient->isConnected()) {
    if (!pClient->connect()) {
      log_e("BLE Connect %s", "Failed to connect");
      return false;
    }
  }

  log_i("Connected to: %s", pClient->getPeerAddress().toString().c_str());
  log_i("RSSI: %d", pClient->getRssi());

  /** Now we can read/write/subscribe the charateristics of the services we are
   * interested in */

  pSvc = pClient->getService(serviceUUID);
  if (pSvc) { /** make sure it's not null */
    pChr = pSvc->getCharacteristic(charUUID);
  }

  if (pChr) { /** make sure it's not null */
    if (pChr->canRead()) {
      log_i("%s Value %s ", pChr->getUUID().toString().c_str(),
            pChr->readValue().c_str());
    }

    /** registerForNotify() has been deprecated and replaced with subscribe() /
     * unsubscribe().
     *  Subscribe parameter defaults are: notifications=true,
     * notifyCallback=nullptr, response=false.
     *  Unsubscribe parameter defaults are: response=false.
     */
    if (pChr->canNotify()) {
      // if(!pChr->registerForNotify(notifyCB)) {
      if (!pChr->subscribe(true, notifyCB)) {
        /** Disconnect if subscribe failed */
        pClient->disconnect();
        return false;
      }
    } else if (pChr->canIndicate()) {
      /** Send false as first argument to subscribe to indications instead of
       * notifications */
      // if(!pChr->registerForNotify(notifyCB, false)) {
      if (!pChr->subscribe(false, notifyCB)) {
        /** Disconnect if subscribe failed */
        pClient->disconnect();
        return false;
      }
    }
  } else {
    log_e("BLE Connect %s", "service not found.");
  }
  log_d("Freeheap = %lu", ESP.getFreeHeap());

  on_complete();
  return true;
}

void lr_init() {

  ble_connected = false;
  NimBLEDevice::init("");
// Retrieve a Scanner and set the callback we want to use to be informed when
// we
// have detected a new device.  Specify that we want active scanning and start
// the
// scan to run for 5 seconds.
/** create new scan */
#ifdef USE_BLE_SCAN
  NimBLEScan *pScan = NimBLEDevice::getScan();

  /** create a callback that gets called when advertisers are found */
  pScan->setAdvertisedDeviceCallbacks(new AdvertisedDeviceCallbacks());

  /** Set scan interval (how often) and window (how long) in milliseconds */
  pScan->setInterval(45);
  pScan->setWindow(15);

  pScan->setActiveScan(true);
  /** Start scanning for advertisers for the scan time specified (in seconds) 0
   * = forever
   *  Optional callback for when scanning stops.
   */
  pScan->start(scanTime, false);
#endif
}
uint8_t get_current_scene() {

  NimBLERemoteCharacteristic *pRemoteCharacteristic2;

  pRemoteCharacteristic2 = pSvc->getCharacteristic(charUUID_Scene);
  if (pRemoteCharacteristic2->canRead()) {
    int v = pRemoteCharacteristic2->readValue<uint8_t>();
    log_i("Scene: %d\r\n ", v);
    return v;
  }
  return 0xFF;
}

void ble_write(const uint8_t *data, size_t length) {
  pChr->writeValue(const_cast<uint8_t *>(data), length, true);
}

bool ble_send(const uint8_t *data, size_t length) {
  int attempts = 5;
  while (ble_connected != true && attempts-- > 0) {
    ble_connected = connect_to_server();
  }
  if (ble_connected) {
    log_i("We are now connected to the BLE Server.");

    pChr->writeValue(const_cast<uint8_t *>(data), length, true);
    return true;
  } else {
    log_e("Failed to connect to the ble server");
    return false;
  }
  delay(50);
}
