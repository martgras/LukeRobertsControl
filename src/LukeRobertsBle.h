void lr_init();
using on_complete_callback = std::function<void(void)>;
uint8_t get_current_scene();
void get_all_scenes();
void ble_write(const uint8_t *data, size_t length);
bool connect_to_server(on_complete_callback on_complete = []() {});
bool ble_send(const uint8_t *data, size_t length);

class SceneBrightnessMapper {
public:
  SceneBrightnessMapper() {}
  static int save() {
    if (!SPIFFS.begin(true)) {
      log_e("An Error has occurred while mounting SPIFFS");
      return 0;
    }
    File mapfile = SPIFFS.open("/bmap.bin", "w+");
    if (mapfile) {
      uint16_t number_of_elements = brightness_map_.size();
      mapfile.write((uint8_t *)&number_of_elements, sizeof(number_of_elements));
      for (auto i : brightness_map_) {
        mapfile.write((uint8_t *)&i, sizeof(i));
      }
      mapfile.close();
      SPIFFS.end();
      return number_of_elements;
    } else {
      SPIFFS.end();
      log_e("Can't create brightness mapfile");
    }
    return 0;
  }
  static int load() {

    if (!SPIFFS.begin(true)) {
      log_e("An Error has occurred while mounting SPIFFS");
      return 0;
    }
    File mapfile = SPIFFS.open("/bmap.bin", "r");
    if (!mapfile) {
      log_w("Failed to open file for reading");
      return save();
    }

    uint16_t number_of_elements = 0;
    if (mapfile) {
      brightness_map_.clear();

      mapfile.read((uint8_t *)&number_of_elements, sizeof(number_of_elements));
      int item = 0;
      for (int i = 0; i < number_of_elements; i++) {
        mapfile.read((uint8_t *)&item, sizeof(item));
        brightness_map_.push_back(item);
      }
      mapfile.close();
    }
    SPIFFS.end();
    return number_of_elements;
  }

  static int size() { return brightness_map_.size(); }

  static int map(int scene) {
    if (scene >= 0 && scene < brightness_map_.size()) {
      return brightness_map_[scene];
    }
    // just add a default value ;
    return 50;
  }
  static bool set(int scene, int brightness_level) {
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

private:
  static std::vector<int> brightness_map_;
};
