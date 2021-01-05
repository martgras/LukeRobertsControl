//#pragma once

namespace button_handler {
enum button_function_codes {
  kNoop = 0,
  kPowerOn,
  kPowerOff,
  kPowerToggle,
  kChangeBrightness,
  kChangeColorTemperature,
  kNextScene
};

/*******  Map the button definitions or set defaults  *****
 For each button 3 actions can be defined
 CLICK_ACTION is called when the button is pressed
 LONG_PRESS_ACTION is called when the button is pressed for more than
LONG_PRESS_DELAY milliseconds
 DOUBLE_CLICK_ACTION is called when the button is clicked twice within
DDOUBLE_CLICK_INTERVAL milliseconds

Every action can be mapped to differents functions  (enum button_function_codes)

STEP_VALUE:  To allow modifiying the step value for brightness or
colortemperature change a step value is also assigned to every button
Although STEP_VALUE is ignored for some function codes it must be defined for
every button. Default is 10 for up and -10 for down

************************************************************/
#ifndef LONG_PRESS_DELAY
#define LONG_PRESS_DELAY 1500
#endif
#ifndef LONG_PRESS_INTERVAL
#define LONG_PRESS_INTERVAL 1500
#endif
#ifndef DOUBLE_CLICK_INTERVAL
#define DOUBLE_CLICK_INTERVAL 400
#endif


#if defined(ROTARY_BUTTON_PIN)
static const uint8_t rotary_button_pin = ROTARY_BUTTON_PIN;

static const button_function_codes rotary_button_pin_click_action =
#if defined(ROTARY_BUTTON_PIN_CLICK_ACTION)
    static_cast<button_function_codes>(ROTARY_BUTTON_PIN_CLICK_ACTION);
#else
    kPowerToggle;
#endif

static const button_function_codes rotary_button_pin_double_click_action =
#if defined(ROTARY_BUTTON_PIN_DOUBLE_CLICK_ACTION)
    static_cast<button_function_codes>(ROTARY_BUTTON_PIN_DOUBLE_CLICK_ACTION);
#else
    kNoop;
#endif

static const button_function_codes rotary_button_pin_long_press_action =
#if defined(ROTARY_BUTTON_PIN_LONG_PRESS_ACTION)
    static_cast<button_function_codes>(ROTARY_BUTTON_PIN_LONG_PRESS_ACTION);
#else
    kNextScene;
#endif

static const int rotary_button_pin_step_value =
#if defined(ROTARY_BUTTON_PIN_STEP_VALUE)
    ROTARY_BUTTON_PIN_STEP_VALUE;
#else
    10;
#endif

#else
static const uint8_t rotary_button_pin = GPIO_NUM_0;
#endif

#ifdef SINGLE_BUTTON_PIN
static const uint8_t single_button_pin = SINGLE_BUTTON_PIN;

static const button_function_codes single_button_click_action =
#if defined(SINGLE_BUTTON_CLICK_ACTION)
    static_cast<button_function_codes>(SINGLE_BUTTON_CLICK_ACTION);
#else
    kPowerToggle;
#endif

static const button_function_codes single_button_double_click_action =
#if defined(SINGLE_BUTTON_DOUBLE_CLICK_ACTION)
    static_cast<button_function_codes>(SINGLE_BUTTON_DOUBLE_CLICK_ACTION);
#else
    kNextScene;
#endif

static const button_function_codes single_button_long_press_action =
#if defined(SINGLE_BUTTON_LONG_PRESS_ACTION)
    static_cast<button_function_codes>(SINGLE_BUTTON_LONG_PRESS_ACTION);
#else
    kChangeBrightness;
#endif

static const int single_button_step_value =
#if defined(SINGLE_BUTTON_STEP_VALUE)
    SINGLE_BUTTON_STEP_VALUE;
#else
    10;
#endif
#else
static const uint8_t single_button_pin = GPIO_NUM_0;
#endif // #ifdef SINGLE_BUTTON_PIN

#ifdef BUTTON_UP_PIN
static const uint8_t button_up_pin = BUTTON_UP_PIN;

static const button_function_codes button_up_pin_click_action =
#if defined(BUTTON_UP_PIN_CLICK_ACTION)
    static_cast<button_function_codes>(BUTTON_UP_PIN_CLICK_ACTION);
#else
    kPowerToggle;
#endif

static const button_function_codes button_up_pin_double_click_action =
#if defined(BUTTON_UP_PIN_DOUBLE_CLICK_ACTION)
    static_cast<button_function_codes>(BUTTON_UP_PIN_DOUBLE_CLICK_ACTION);
#else
    kNoop;
#endif

static const button_function_codes button_up_pin_long_press_action =
#if defined(BUTTON_UP_PIN_LONG_PRESS_ACTION)
    static_cast<button_function_codes>(BUTTON_UP_PIN_LONG_PRESS_ACTION);
#else
    kChangeBrightness;
#endif
static const int button_up_pin_step_value =
#if defined(BUTTON_UP_PIN_STEP_VALUE)
    BUTTON_UP_PIN_STEP_VALUE;
#else
    10;
#endif
#else
static const uint8_t button_up_pin = GPIO_NUM_0;
#endif

#ifdef BUTTON_DOWN_PIN
static const uint8_t button_down_pin = BUTTON_DOWN_PIN;

static const button_function_codes button_down_pin_click_action =
#if defined(BUTTON_DOWN_PIN_CLICK_ACTION)
    static_cast<button_function_codes>(BUTTON_UP_PIN_CLICK_ACTION);
#else
    kPowerToggle;
#endif

static const button_function_codes button_down_pin_double_click_action =
#if defined(BUTTON_DOWN_PIN_DOUBLE_CLICK_ACTION)
    static_cast<button_function_codes>(BUTTON_DOWN_PIN_DOUBLE_CLICK_ACTION);
#else
    kNoop;
#endif

static const button_function_codes button_down_pin_long_press_action =
#if defined(BUTTON_DOWN_PIN_LONG_PRESS_ACTION)
    static_cast<button_function_codes>(BUTTON_DOWN_PIN_LONG_PRESS_ACTION);
#else
    kChangeBrightness;
#endif

static const int button_down_pin_step_value =
#if defined(BUTTON_DOWN_PIN_STEP_VALUE)
    BUTTON_DOWN_PIN_STEP_VALUE;
#else
    -10;
#endif
#else
static const uint8_t button_down_pin = GPIO_NUM_0;
#endif

#if defined(RESISTOR_BUTTON_PIN)
static const uint8_t resistor_pin = RESISTOR_BUTTON_PIN;

#if defined(RESISTOR_BUTTON_SWITCH)

static const uint16_t resistor_switch = RESISTOR_BUTTON_SWITCH;

static const button_function_codes resistor_switch_click_action =
#if defined(RESISTOR_BUTTON_SWITCH_CLICK_ACTION)
    static_cast<button_function_codes>(RESISTOR_BUTTON_SWITCH_CLICK_ACTION);
#else
    kPowerToggle;
#endif // if defined(RESISTOR_BUTTON_SWITCH_CLICK_ACTION)

static const button_function_codes resistor_switch_double_click_action =
#if defined(RESISTOR_BUTTON_SWITCH_DOUBLE_CLICK_ACTION)
    static_cast<button_function_codes>(RESISTOR_BUTTON_SWITCH_DOUBLE_CLICK_ACTION);
#else
    kNoop;
#endif // if defined(RESISTOR_BUTTON_SWITCH_DOUBLE_CLICK_ACTION)

static const button_function_codes resistor_switch_long_press_action =
#if defined(RESISTOR_BUTTON_SWITCH_LONG_PRESS_ACTION)
    static_cast<button_function_codes>(RESISTOR_BUTTON_SWITCH_LONG_PRESS_ACTION);
#else
    kNextScene;
#endif // if defined(RESISTOR_BUTTON_SWITCH_LONG_PRESS_ACTION)

static const int resistor_switch_step_value =
#if defined(RESISTOR_BUTTON_SWITCH_STEP_VALUE)
    RESISTOR_BUTTON_SWITCH_STEP_VALUE;
#else
    10;
#endif // if defined(RESISTOR_BUTTON_SWITCH_STEP_VALUE)

#else
static const uint16_t resistor_switch = 0;
#endif // if defined(RESISTOR_BUTTON_SWITCH)

#ifdef RESISTOR_BUTTON_UP
static const uint16_t resistor_up = RESISTOR_BUTTON_UP;

static const button_function_codes resistor_up_click_action =
#if defined(RESISTOR_BUTTON_UP_CLICK_ACTION)
    static_cast<button_function_codes>(RESISTOR_BUTTON_UP_CLICK_ACTION);
#else
    kChangeBrightness;
#endif

static const button_function_codes resistor_up_double_click_action =
#if defined(RESISTOR_BUTTON_UP_DOUBLE_CLICK_ACTION)
    static_cast<button_function_codes>(RESISTOR_BUTTON_UP_DOUBLE_CLICK_ACTION);
#else
    kNoop;
#endif

static const button_function_codes resistor_up_long_press_action =
#if defined(RESISTOR_BUTTON_UP_LONG_PRESS_ACTION)
    static_cast<button_function_codes>(RESISTOR_BUTTON_UP_LONG_PRESS_ACTION);
#else
    kChangeBrightness;
#endif
static const int resistor_up_step_value =
#if defined(RESISTOR_BUTTON_UP_STEP_VALUE)
    RESISTOR_BUTTON_UP_STEP_VALUE;
#else
    10;
#endif
#else
static const uint16_t resistor_up = 0;
#endif

#ifdef RESISTOR_BUTTON_DOWN
static const uint16_t resistor_down = RESISTOR_BUTTON_DOWN;

static const button_function_codes resistor_down_click_action =
#if defined(RESISTOR_BUTTON_DOWN_CLICK_ACTION)
    static_cast<button_function_codes>(RESISTOR_BUTTON_UP_CLICK_ACTION);
#else
    kChangeBrightness;
#endif

static const button_function_codes resistor_down_double_click_action =
#if defined(RESISTOR_BUTTON_DOWN_DOUBLE_CLICK_ACTION)
    static_cast<button_function_codes>(RESISTOR_BUTTON_DOWN_DOUBLE_CLICK_ACTION);
#else
    kNoop;
#endif

static const button_function_codes resistor_down_long_press_action =
#if defined(RESISTOR_BUTTON_DOWN_LONG_PRESS_ACTION)
    static_cast<button_function_codes>(RESISTOR_BUTTON_DOWN_LONG_PRESS_ACTION);
#else
    kChangeBrightness;
#endif

static const int resistor_down_step_value =
#if defined(RESISTOR_BUTTON_DOWN_STEP_VALUE)
    RESISTOR_BUTTON_DOWN_STEP_VALUE;
#else
    -10;
#endif
#else
static const uint16_t resistor_down = 0;
#endif

#else
static const uint8_t resistor_pin = GPIO_NUM_0;
#endif

void handle_button_event(AceButton *button, uint8_t eventType,
                         uint8_t buttonState);

using button_func_t = std::function<void(int value)>;

struct button_handler {
  uint8_t id;
  int8_t step_value;
  button_func_t event_handlers[7] = {nullptr, nullptr, nullptr, nullptr,
                                     nullptr, nullptr, nullptr};
};

button_handler buttons[10];

button_func_t button_functions[6];

#ifdef SWITCH_PIN
// Every change of the switch toggles power
// no need to read the value: toggle if the last change is older than 500ms
void IRAM_ATTR isr_extraswitch(void *) {
  static long lastevent;
  if (millis() - lastevent > 500) {
    set_powerstate(!get_powerstate());
    lastevent = millis();
  }
}
#endif
uint8_t single_button_idx;
void setup_buttons() {
  uint8_t button_idx = 0;

  button_functions[kNoop] = [](int) {};

  button_functions[kPowerOn] = [](int) { set_powerstate(true); };

  button_functions[kPowerOff] = [](int) { set_powerstate(false); };

  button_functions[kPowerToggle] = [](int) {
    set_powerstate(!get_powerstate());
  };
  button_functions[kChangeBrightness] = [](int step) {
    set_dimmer_value(get_dimmer_value() + step);
  };

  button_functions[kChangeColorTemperature] = [](int step) {
    set_colortemperature_kelvin(get_colortemperature_kelvin() + step);
  };

  button_functions[kNextScene] = [](int) { set_scene(get_scene() + 1); };

#if defined(ROTARY_BUTTON_PIN) || defined(BUTTON_DOWN_PIN)

  static ace_button::ButtonConfig button_cfg;

  button_cfg.setFeature(ButtonConfig::kFeatureRepeatPress);
  button_cfg.setRepeatPressDelay(LONG_PRESS_DELAY);
  button_cfg.setRepeatPressInterval(LONG_PRESS_INTERVAL);
  button_cfg.setFeature(ButtonConfig::kFeatureSuppressAfterRepeatPress);
  button_cfg.setEventHandler(handle_button_event);

#endif
#if defined(ROTARY_BUTTON_PIN)
  pinMode(ROTARY_BUTTON_PIN, INPUT_PULLUP);
  static ace_button::AceButton rotary_button;
  rotary_button.setButtonConfig(&button_cfg);
  rotary_button.init(ROTARY_BUTTON_PIN, HIGH, ROTARY_BUTTON_PIN);

  button_handler *b;

  b = &buttons[button_idx++];
  b->id = rotary_button.getId();
  b->event_handlers[AceButton::kEventReleased] =
      button_functions[rotary_button_pin_click_action];
  b->event_handlers[AceButton::kEventRepeatPressed] =
      button_functions[rotary_button_pin_long_press_action];
  b->event_handlers[AceButton::kEventDoubleClicked] =
      button_functions[rotary_button_pin_double_click_action];
  b->step_value = rotary_button_pin_step_value;
#endif

// If BUTTON_DOWN_PIN is defined use regular buttons instead of Ladder Buttons
#if defined(BUTTON_DOWN_PIN)

#define BUTTON_DOWN_ACTION_PRESS kChangeBrightness
#define BUTTON_DOWN_STEP -5

#define BUTTON_UP_ACTION_LONG_PRESS kNextScene
#define BUTTON_UP_STEP 5

  pinMode(BUTTON_DOWN_PIN, INPUT_PULLUP);
  static ace_button::AceButton down_button;
  pinMode(BUTTON_UP_PIN, INPUT_PULLUP);
  static ace_button::AceButton up_button;
  down_button.setButtonConfig(&button_cfg);
  up_button.setButtonConfig(&button_cfg);
  down_button.init(BUTTON_DOWN_PIN, HIGH, BUTTON_DOWN_PIN);
  up_button.init(BUTTON_UP_PIN, HIGH, BUTTON_UP_PIN);

  b = &buttons[button_idx++];
  b->id = up_button.getId();
  b->event_handlers[AceButton::kEventReleased] =
      button_functions[button_up_pin_click_action];
  b->event_handlers[AceButton::kEventRepeatPressed] =
      button_functions[button_up_pin_long_press_action];
  b->event_handlers[AceButton::kEventDoubleClicked] =
      button_functions[button_up_pin_double_click_action];
  b->step_value = button_up_pin_step_value;

  b = &buttons[button_idx++];
  b->id = down_button.getId();
  b->event_handlers[AceButton::kEventReleased] =
      button_functions[button_down_pin_click_action];
  b->event_handlers[AceButton::kEventRepeatPressed] =
      button_functions[button_down_pin_long_press_action];
  b->event_handlers[AceButton::kEventDoubleClicked] =
      button_functions[button_down_pin_double_click_action];
  b->step_value = button_down_pin_step_value;

#endif

#if defined(SINGLE_BUTTON_PIN)
  pinMode(SINGLE_BUTTON_PIN, INPUT_PULLUP);
  log_i("SINGLE %d", SINGLE_BUTTON_PIN);
  static ace_button::AceButton single_button;
  static ace_button::ButtonConfig single_cfg;

  single_cfg.setFeature(ButtonConfig::kFeatureRepeatPress);
  single_cfg.setFeature(ButtonConfig::kFeatureSuppressAfterLongPress);
  single_cfg.setFeature(ButtonConfig::kFeatureDoubleClick);
  // single_cfg.setFeature(ButtonConfig::kFeatureSuppressAfterRepeatPress);
  single_cfg.setFeature(ButtonConfig::kFeatureSuppressAfterDoubleClick);
  single_cfg.setRepeatPressDelay(LONG_PRESS_DELAY );
  single_cfg.setRepeatPressInterval(LONG_PRESS_INTERVAL );
  single_cfg.setDoubleClickDelay(DOUBLE_CLICK_INTERVAL);

  single_button_idx = button_idx;
  b = &buttons[button_idx++];

  // needs a separate event handler to allow chaning the direction for dimming
  // (detect 2 long press events within 10s )
  single_cfg.setEventHandler([](ace_button::AceButton *button,
                                uint8_t eventType, uint8_t buttonState) {
    static uint8_t last_event = AceButton::kEventRepeatPressed;
    static long last_press;

    log_i("Single button %d last = %d %d", eventType, last_event,
          millis() - last_press);

    if (eventType == AceButton::kEventRepeatPressed) {
      if (last_event != AceButton::kEventRepeatPressed &&
          millis() - last_press < 10000) {
        log_i("Direction switch");
        buttons[single_button_idx].step_value *= -1;
      }
      button_functions[single_button_long_press_action](
          buttons[single_button_idx].step_value);
      last_press = millis();
    }
    if (eventType == AceButton::kEventClicked && millis() - last_press > 500) {
      button_functions[single_button_click_action](single_button_step_value);
      last_press = millis();
    }
    if (eventType == AceButton::kEventDoubleClicked) {
      button_functions[single_button_double_click_action](
          single_button_step_value);
    }
    last_event = eventType;
  });

  // single_cfg.setEventHandler(handle_button_event);
  single_button.setButtonConfig(&single_cfg);
  single_button.init(SINGLE_BUTTON_PIN, HIGH, SINGLE_BUTTON_PIN);

  b->id = single_button.getId();
  b->event_handlers[AceButton::kEventReleased] =
      button_functions[single_button_click_action];
  b->event_handlers[AceButton::kEventRepeatPressed] =
      button_functions[single_button_long_press_action];
  b->event_handlers[AceButton::kEventDoubleClicked] =
      button_functions[single_button_double_click_action];
  b->step_value = single_button_step_value;
#endif

#if defined(RESISTOR_BUTTON_PIN)
  pinMode(RESISTOR_BUTTON_PIN, INPUT_PULLUP);
#define RESISTOR_BUTTON_UP_VPIN 0
#define RESISTOR_BUTTON_DOWN_VPIN 1

  static AceButton resistor_up((uint8_t)RESISTOR_BUTTON_UP_VPIN, HIGH,
                             RESISTOR_BUTTON_UP_VPIN | 0x80);
  static AceButton resistor_down(RESISTOR_BUTTON_DOWN_VPIN, HIGH, RESISTOR_BUTTON_DOWN_VPIN | 0x80);
#ifdef RESISTOR_BUTTON_SWITCH
#define RESISTOR_BUTTON_SWITCH_VPIN 2
  static AceButton resistor_sw(RESISTOR_BUTTON_SWITCH_VPIN, HIGH,
                             RESISTOR_BUTTON_SWITCH_VPIN | 0x80);
#endif
  static AceButton *const resistor_buttons[] = {&resistor_up, &resistor_down
#ifdef RESISTOR_BUTTON_SWITCH
                                              ,
                                              &resistor_sw
#endif
  };
  static const uint16_t levels[] = {RESISTOR_BUTTON_UP, RESISTOR_BUTTON_DOWN,
#ifdef RESISTOR_BUTTON_SWITCH
                                    RESISTOR_BUTTON_SWITCH,
#endif
                                    3900};

  static LadderButtonConfig resistor_button_config(
      RESISTOR_BUTTON_PIN, sizeof(levels) / sizeof(levels[0]), levels,
      sizeof(resistor_buttons) / sizeof(resistor_buttons[0]), resistor_buttons);

  resistor_button_config.setFeature(ButtonConfig::kFeatureRepeatPress);
  resistor_button_config.setFeature(
      ButtonConfig::kFeatureSuppressAfterRepeatPress);
  resistor_button_config.setFeature(
      ButtonConfig::kFeatureSuppressAfterDoubleClick);
  resistor_button_config.setRepeatPressDelay(LONG_PRESS_DELAY );
  resistor_button_config.setRepeatPressInterval(LONG_PRESS_INTERVAL );
  resistor_button_config.setEventHandler(handle_button_event);

  b = &buttons[button_idx++];
  b->id = resistor_up.getId();
  b->event_handlers[AceButton::kEventReleased] =
      button_functions[resistor_up_click_action];
  b->event_handlers[AceButton::kEventRepeatPressed] =
      button_functions[resistor_up_long_press_action];
  b->event_handlers[AceButton::kEventDoubleClicked] =
      button_functions[resistor_up_double_click_action];
  b->step_value = resistor_up_step_value;

  b = &buttons[button_idx++];
  b->id = resistor_down.getId();
  b->event_handlers[AceButton::kEventReleased] =
      button_functions[resistor_down_click_action];
  b->event_handlers[AceButton::kEventRepeatPressed] =
      button_functions[resistor_down_long_press_action];
  b->event_handlers[AceButton::kEventDoubleClicked] =
      button_functions[resistor_down_long_press_action];
  b->step_value = resistor_down_step_value;

  b = &buttons[button_idx++];
  b->id = resistor_sw.getId();
  b->event_handlers[AceButton::kEventReleased] =
      button_functions[resistor_switch_click_action];
  b->event_handlers[AceButton::kEventRepeatPressed] =
      button_functions[resistor_switch_long_press_action];
  b->event_handlers[AceButton::kEventDoubleClicked] =
      button_functions[resistor_switch_long_press_action];
  b->step_value = resistor_switch_step_value;
#endif

#ifdef SWITCH_PIN
  pinMode(SWITCH_PIN, INPUT_PULLUP);
  attachInterruptArg(SWITCH_PIN, isr_extraswitch, (void *)&lr, CHANGE);
#endif

#if defined(ROTARY_BUTTON_PIN) || defined(SINGLE_BUTTON_PIN) ||                \
    defined(RESISTOR_BUTTON_PIN) || defined(BUTTON_DOWN_PIN)
  xTaskCreate([&](void *) {
    while (1) {
#ifdef ROTARY_BUTTON_PIN
      rotary_button.check();
#endif
#ifdef SINGLE_BUTTON_PIN
      single_button.check();
#endif
#ifdef RESISTOR_BUTTON_PIN
      resistor_button_config.checkButtons();
#endif
#ifdef BUTTON_DOWN_PIN
      down_button.check();
      up_button.check();
#endif
      vTaskDelay(5 / portTICK_PERIOD_MS);
    }
  }, "acebuttoncheck", 4000, nullptr, 2, nullptr);
#endif
}

portMUX_TYPE button_mux = portMUX_INITIALIZER_UNLOCKED;
void handle_button_event(AceButton *button, uint8_t eventType,
                         uint8_t buttonState) {
  auto pin = button->getPin();
  auto id = button->getId();
  log_i("Button event PIN=%d, ID: %d event %d", pin, id, eventType);
  if (pin == GPIO_NUM_0 && id == 0) {
    return;
  }
  portENTER_CRITICAL(&button_mux);
  for (auto b : buttons) {
    if (b.id == button->getId()) {
      if (b.event_handlers[eventType]) {
        b.event_handlers[eventType](b.step_value);
      }
      portEXIT_CRITICAL(&button_mux);
      return;
    }
  }
  portEXIT_CRITICAL(&button_mux);
}
}