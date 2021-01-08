#pragma once
//
// Based on
// https://github.com/h2zero/NimBLE-Arduino/tree/master/examples/NimBLE_Client
//
//
//
#include <queue>
#include <NimBLEDevice.h>

class BleGattClient {

public:
  friend uint8_t get_current_scene(BleGattClient &client);

  using on_complete_callback = std::function<void()>;
  using on_downlight_callback =
      std::function<void(uint8_t brightness, uint16_t kelvin)>;

  static void init(BLEAddress device_addr, BLEUUID service_uuid);
  bool connect_to_server(BLEUUID charUUID,
                         on_complete_callback on_complete = nullptr);

  static notify_callback set_on_notify(notify_callback on_notify) {
    auto tmp = on_notify_;
    on_notify_ = on_notify;
    return tmp;
  }

  void start_ble_loop();

  bool connected() { return client->isConnected(); }

  bool write(const uint8_t *data, size_t length) {
    log_i("WritBLE");
    return characteristic->writeValue(const_cast<uint8_t *>(data), length,
                                      true);
  }

  bool send(const uint8_t *data, size_t length) {
    log_d("BLE GATT SEND");
    int attempts = 5;
    while (connected_ != true && attempts-- > 0) {
      connected_ = connect_to_server(charUUID);
    }
    if (connected_) {
      log_i("Connected to the BLE Server.");
      return characteristic->writeValue(const_cast<uint8_t *>(data), length,
                                        true);
    } else {
      log_e("Failed to connect to the ble server");
      NimBLEDevice::deleteClient(client);
      log_e("BLE Connect %s", "Failed to connect, deleted client");
      client = nullptr;
      return false;
    }
  }

  struct BleCommand {
    uint8_t data[16];
    size_t size;
    bool is_dirty;
    std::function<void(int)> on_send;
  };

  static const int CACHE_SIZE_ = 4;
  // std::vector<BleData> cached_commands;
  std::array<BleCommand, CACHE_SIZE_> cached_commands;
  std::queue<BleCommand> pending_commands;
  uint8_t number_of_cached_commands_ = CACHE_SIZE_;

  void set_cache_size(uint8_t number_of_cached_commands) {
    // cached_commands.resize(number_of_cached_commands);
    number_of_cached_commands_ = number_of_cached_commands;
  }

  // ble_data *cached_commands;
  void set_cache_array(BleCommand cache[], size_t n_items) {
    for (auto i = 0; i < n_items; i++) {
      cached_commands[i] = cache[i];
    }
  }

  void queue_cmd(BleCommand &item, uint8_t index, bool is_dirty = true) {
    item.is_dirty = is_dirty;
    cached_commands[index] = item;
  }

  void queue_cmd(BleCommand &item) {
    item.is_dirty = true;
    pending_commands.push(item);
  }

  bool send_queued();
  void loop(on_complete_callback on_send = nullptr);

  static BLEAddress device_addr_;

  static BLEUUID serviceUUID;
  static BLEUUID charUUID;

  NimBLERemoteService *service;
  NimBLERemoteCharacteristic *characteristic;
  NimBLERemoteDescriptor *remote_descriptor;
  NimBLEClient *client;

private:
  static notify_callback on_notify_;
  static bool initialized_;
  volatile bool connected_;

  static on_complete_callback on_connect_;
  static on_complete_callback on_disconnect_;

  /**  None of these are required as they will be handled by the library with
   *defaults. **
   **                       Remove as you see fit for your needs */
  class ClientCallbacks : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient *pClient) {
      if (on_connect_) {
        on_connect_();
      }

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
      // gatt_client_-
      // connected_ = true;
    };

    void onDisconnect(NimBLEClient *pClient) {

      log_i("%s Disconnected", pClient->getPeerAddress().toString().c_str());
      // connected_ = false;
      //   NimBLEDevice::getScan()->start(scanTime, scanEndedCB);
      if (on_disconnect_) {
        on_disconnect_();
      }
    };

    /** Called when the peripheral requests a change to the connection
     * parameters.
     *  Return true to accept and apply them or false to reject and keep
     *  the currently used parameters. Default will return true.
     */
    bool onConnParamsUpdateRequest(NimBLEClient *pClient,
                                   const ble_gap_upd_params *params) {
      if (params->itvl_min < 12) { /** 1.25ms units */
        return false;
      } else if (params->itvl_max > 40) { /** 1.25ms units */
        return false;
      } else if (params->latency >
                 2) { /** Number of intervals allowed to skip */
        return false;
      } else if (params->supervision_timeout > 100) { /** 10ms units */
        return false;
      }
      return true;
    };

  public:
    void set_on_connect(BleGattClient &client, on_complete_callback notify) {
      client.on_connect_ = notify;
    }

    void set_on_disconnect(BleGattClient &client, on_complete_callback notify) {
      client.on_disconnect_ = notify;
    }

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

    /** Pairing process complete, we can check the results in ble_gap_conn_desc
     */
    void onAuthenticationComplete(ble_gap_conn_desc *desc) {
      if (!desc->sec_state.encrypted) {
        log_i("Encrypt connection failed - disconnecting");
        /** Find the client with the connection handle provided in desc */
        NimBLEDevice::getClientByID(desc->conn_handle)->disconnect();
        return;
      }
    };
  };

  using match_func_t = std::function<bool(NimBLERemoteCharacteristic *,
                                          uint8_t *, size_t, bool)>;

  struct ble_notify_callback_t {
    uint8_t id;
    bool is_enabled;
    match_func_t match;
    notify_callback notify;
  };

  static std::list<ble_notify_callback_t> callbacks_;

public:
  static bool matches_api_characteristics(NimBLEUUID uuid) {
    return uuid.equals(charUUID);
  }

  void register_callback_notification(uint8_t id, match_func_t match_func,
                                      notify_callback notify_func,
                                      bool enable = true) {
    ble_notify_callback_t new_cb = {id, enable, match_func, notify_func};
    for (auto &c : callbacks_) {
      if (c.id == id) {
        c = new_cb;
        return;
      }
    }
    callbacks_.push_back(new_cb);
  }

  bool enable_callback_notification(uint8_t id, bool enable) {
    for (auto &c : callbacks_) {
      if (c.id == id) {
        c.is_enabled = enable;
        return true; // entry found
      }
    }
    return false; // entry not found
  }

  bool unregister_callback_notification(uint8_t id) {
    for (auto it = callbacks_.begin(); it != callbacks_.end();) {
      if (it->id == id) {
        it = callbacks_.erase(it);
        return true;
      } else {
        ++it;
      }
    }
    return false;
  }

private:
  /** Notification / Indication receiving handler callback */
  static void notifyCB(NimBLERemoteCharacteristic *pRemoteCharacteristic,
                       uint8_t *pData, size_t length, bool isNotify) {
    log_i(
        "%s from %s : Service = %s, Characteristic = %s, len=%d, Value=<%.*s> "
        "(%d)",
        (isNotify == true) ? "Notification" : "Indication",
        pRemoteCharacteristic->getRemoteService()
            ->getClient()
            ->getPeerAddress()
            .toString()
            .c_str(),
        pRemoteCharacteristic->getRemoteService()->getUUID().toString().c_str(),
        pRemoteCharacteristic->getUUID().toString().c_str(), length, length,
        (char *)pData, (int)pData[0]);

    log_v("Client data received len: %d\n", length);
    for (auto i = 0; i < length; i++) {
      log_v("Response byte[%d]: %d (0x%X)", i, pData[i], pData[i]);
    }
    for (auto cb : callbacks_) {
      if (cb.is_enabled &&
          cb.match(pRemoteCharacteristic, pData, length, isNotify)) {
        cb.notify(pRemoteCharacteristic, pData, length, isNotify);
      }
    }
    /*
        if (on_downlight_notification_ != nullptr &&
            pRemoteCharacteristic->getUUID() == charUUID && length == 9 &&
            pData[0] == 0 && pData[1] == 0x88 && pData[2] == 0xF4 &&
            pData[3] == 0x18 && pData[4] == 0x71) {
          on_downlight_notification_(pData[7], pData[5] | pData[6] << 8);
        }
    */
    if (on_notify_ != nullptr) {
      on_notify_(pRemoteCharacteristic, pData, length, isNotify);
    }
  }

  ClientCallbacks client_cb_;
};

// void get_all_scenes(BleGattClient &client);
// uint8_t get_current_scene(BleGattClient &client);
// void request_downlight_settings(BleGattClient &client);
NimBLEAddress scan_for_device();