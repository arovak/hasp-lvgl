#include "Arduino.h"
#include "ArduinoLog.h"

#include "AceButton.h"
#include "lv_conf.h" // For timing defines

#include "hasp_conf.h"
#include "hasp_gpio.h"
#include "hasp_config.h"
#include "hasp_dispatch.h"
#include "hasp.h"

uint8_t gpioUsedInputCount = 0;

using namespace ace_button;
static AceButton * button[HASP_NUM_INPUTS];

hasp_gpio_config_t gpioConfig[HASP_NUM_GPIO_CONFIG];

#if defined(ARDUINO_ARCH_ESP32)
class TouchConfig : public ButtonConfig {
  public:
    TouchConfig();

  protected:
    // Number of iterations to sample the capacitive switch. Higher number
    // provides better smoothing but increases the time taken for a single read.
    static const uint8_t kSamples = 10;

    // The threshold value which is considered to be a "touch" on the switch.
    static const long kTouchThreshold = 70;

    int readButton(uint8_t pin) override
    {
        // long total =  mSensor.capacitiveSensor(kSamples);
        return (touchRead(pin) > kTouchThreshold) ? LOW : HIGH;
    }
};

TouchConfig touchConfig();
#endif

static void gpio_event_handler(AceButton * button, uint8_t eventType, uint8_t buttonState)
{
    uint8_t eventid;
    char buffer[16];
    switch(eventType) {
        case AceButton::kEventPressed:
            eventid = HASP_EVENT_DOWN;
            memcpy_P(buffer, PSTR("DOWN"), sizeof(buffer));
            break;
        case 2: // AceButton::kEventClicked:
            eventid = HASP_EVENT_SHORT;
            memcpy_P(buffer, PSTR("SHORT"), sizeof(buffer));
            break;
        case AceButton::kEventDoubleClicked:
            eventid = HASP_EVENT_DOUBLE;
            memcpy_P(buffer, PSTR("DOUBLE"), sizeof(buffer));
            break;
        case AceButton::kEventLongPressed:
            eventid = HASP_EVENT_LONG;
            memcpy_P(buffer, PSTR("LONG"), sizeof(buffer));
            break;
        case AceButton::kEventRepeatPressed:
            // return; // Fix needed for switches
            eventid = HASP_EVENT_HOLD;
            memcpy_P(buffer, PSTR("HOLD"), sizeof(buffer));
            break;
        case AceButton::kEventReleased:
            eventid = HASP_EVENT_UP;
            memcpy_P(buffer, PSTR("UP"), sizeof(buffer));
            break;
        default:
            eventid = HASP_EVENT_LOST;
            memcpy_P(buffer, PSTR("UNKNOWN"), sizeof(buffer));
    }
    dispatch_button(button->getId(), buffer);
    dispatch_send_group_event(gpioConfig[button->getId()].group, eventid, true);
}

void aceButtonSetup(void)
{
    ButtonConfig * buttonConfig = ButtonConfig::getSystemButtonConfig();
    buttonConfig->setEventHandler(gpio_event_handler);

    // Features
    buttonConfig->setFeature(ButtonConfig::kFeatureClick);
    buttonConfig->setFeature(ButtonConfig::kFeatureLongPress);
    buttonConfig->setFeature(ButtonConfig::kFeatureRepeatPress);
    // buttonConfig->setFeature(ButtonConfig::kFeatureDoubleClick);
    // buttonConfig->setFeature(ButtonConfig::kFeatureSuppressClickBeforeDoubleClick);

    // Delays
    buttonConfig->setClickDelay(LV_INDEV_DEF_LONG_PRESS_TIME);
    buttonConfig->setDoubleClickDelay(LV_INDEV_DEF_LONG_PRESS_TIME);
    buttonConfig->setLongPressDelay(LV_INDEV_DEF_LONG_PRESS_TIME);
    buttonConfig->setRepeatPressDelay(LV_INDEV_DEF_LONG_PRESS_TIME);
    buttonConfig->setRepeatPressInterval(LV_INDEV_DEF_LONG_PRESS_REP_TIME);
}

void IRAM_ATTR gpioLoop(void)
{
    // Should be called every 4-5ms or faster, for the default debouncing time of ~20ms.
    for(uint8_t i = 0; i < gpioUsedInputCount; i++) {
        if(button[i]) button[i]->check();
    }
}

void gpioAddButton(uint8_t pin, uint8_t input_mode, uint8_t default_state, uint8_t index)
{
    uint8_t i;
    for(i = 0; i < HASP_NUM_INPUTS; i++) {

        if(!button[i]) {
            button[i] = new AceButton(pin, default_state, index);
            // button[i]->init(pin, default_state, index);

            if(button[i]) {
                pinMode(pin, input_mode);

                ButtonConfig * buttonConfig = button[i]->getButtonConfig();
                buttonConfig->setEventHandler(gpio_event_handler);
                buttonConfig->setFeature(ButtonConfig::kFeatureClick);
                buttonConfig->clearFeature(ButtonConfig::kFeatureDoubleClick);
                buttonConfig->setFeature(ButtonConfig::kFeatureLongPress);
                buttonConfig->clearFeature(ButtonConfig::kFeatureRepeatPress);
                buttonConfig->clearFeature(
                    ButtonConfig::kFeatureSuppressClickBeforeDoubleClick); // Causes annoying pauses

                Log.verbose(F("GPIO: Button%d created on pin %d (index %d) mode %d default %d"), i, pin, index,
                            input_mode, default_state);
                gpioUsedInputCount = i + 1;
                return;
            }
        }
    }
    Log.error(F("GPIO: Failed to create Button%d pin %d (index %d). All %d slots available are in use!"), i, pin, index,
              HASP_NUM_INPUTS);
}

void gpioAddTouchButton(uint8_t pin, uint8_t input_mode, uint8_t default_state, uint8_t index)
{
    uint8_t i;
    for(i = 0; i < HASP_NUM_INPUTS; i++) {

        if(!button[i]) {
            button[i] = new AceButton(pin, default_state, index);

            if(button[i]) {
                pinMode(pin, input_mode);

                ButtonConfig * buttonConfig = button[i]->getButtonConfig();
                buttonConfig->setEventHandler(gpio_event_handler);
                buttonConfig->setFeature(ButtonConfig::kFeatureClick);
                buttonConfig->clearFeature(ButtonConfig::kFeatureDoubleClick);
                buttonConfig->setFeature(ButtonConfig::kFeatureLongPress);
                buttonConfig->clearFeature(ButtonConfig::kFeatureRepeatPress);
                buttonConfig->clearFeature(
                    ButtonConfig::kFeatureSuppressClickBeforeDoubleClick); // Causes annoying pauses

                Log.verbose(F("GPIO: Button%d created on pin %d (index %d) mode %d default %d"), i, pin, index,
                            input_mode, default_state);
                gpioUsedInputCount = i + 1;
                return;
            }
        }
    }
    Log.error(F("GPIO: Failed to create Button%d pin %d (index %d). All %d slots available are in use!"), i, pin, index,
              HASP_NUM_INPUTS);
}

void gpioSetup()
{
    aceButtonSetup();

    // return;

#if defined(ARDUINO_ARCH_ESP8266)
    gpioConfig[0] = {D2, 7, HASP_GPIO_BUTTON, INPUT_PULLUP};
    gpioConfig[1] = {D1, 7, HASP_GPIO_LED, OUTPUT};

// gpioAddButton(D2, INPUT_PULLUP, HIGH, 1);
// pinMode(D1, OUTPUT);
#endif

#if defined(ARDUINO_ARCH_ESP32)
    // gpioConfig[0] = {D2, 0, HASP_GPIO_SWITCH, INPUT};
    // gpioConfig[1] = {D1, 1, HASP_GPIO_RELAY, OUTPUT};

// gpioAddButton(D2, INPUT, HIGH, 1);
// pinMode(D1, OUTPUT);
#endif

    /*
    #if defined(ARDUINO_ARCH_ESP8266)
        pinMode(D1, OUTPUT);
        pinMode(D2, INPUT_PULLUP);
    #endif
    #if defined(STM32F4xx)
        pinMode(HASP_OUTPUT_PIN, OUTPUT);
        pinMode(HASP_INPUT_PIN, INPUT);
    #endif
    */

    for(uint8_t i = 0; i < HASP_NUM_GPIO_CONFIG; i++) {
        uint8_t input_mode;
        switch(gpioConfig[i].gpio_function) {
            case OUTPUT:
                input_mode = OUTPUT;
                break;
            case INPUT_PULLUP:
                input_mode = INPUT_PULLUP;
                break;
#ifndef ARDUINO_ARCH_ESP8266
            case INPUT_PULLDOWN:
                input_mode = INPUT_PULLDOWN;
                break;
#endif
            default:
                input_mode = INPUT;
        }

        switch(gpioConfig[i].type) {
            case HASP_GPIO_SWITCH:
            case HASP_GPIO_BUTTON:
                gpioAddButton(gpioConfig[i].pin, input_mode, HIGH, i);
                break;
            case HASP_GPIO_SWITCH_INVERTED:
            case HASP_GPIO_BUTTON_INVERTED:
                gpioAddButton(gpioConfig[i].pin, input_mode, LOW, i);
                break;

            case HASP_GPIO_RELAY:
            case HASP_GPIO_RELAY_INVERTED:
            case HASP_GPIO_LED:
            case HASP_GPIO_LED_INVERTED:
                pinMode(gpioConfig[i].pin, OUTPUT);
                break;

            case HASP_GPIO_PWM:
            case HASP_GPIO_PWM_INVERTED:
                // case HASP_GPIO_BACKLIGHT:
                pinMode(gpioConfig[i].pin, OUTPUT);
#if defined(ARDUINO_ARCH_ESP32)
                // configure LED PWM functionalitites
                ledcSetup(gpioConfig[i].group, 20000, 10);
                // attach the channel to the GPIO to be controlled
                ledcAttachPin(gpioConfig[i].pin, gpioConfig[i].group);
#endif
                break;
        }
    }
}

void gpio_set_group_outputs(uint8_t groupid, uint8_t eventid)
{
    bool state = dispatch_get_event_state(eventid);
    for(uint8_t i = 0; i < HASP_NUM_GPIO_CONFIG; i++) {
        if(gpioConfig[i].group == groupid) {
            switch(gpioConfig[i].type) {
                case HASP_GPIO_RELAY:
                case HASP_GPIO_LED:
                    digitalWrite(gpioConfig[i].pin, state ? HIGH : LOW);
                    break;
                case HASP_GPIO_RELAY_INVERTED:
                case HASP_GPIO_LED_INVERTED:
                    digitalWrite(gpioConfig[i].pin, state ? LOW : HIGH);
                    break;
#if defined(ARDUINO_ARCH_ESP32)
                case HASP_GPIO_PWM:
                    ledcWrite(groupid, map(state, 0, 1, 0, 1023)); // ledChannel and value
                    break;
                case HASP_GPIO_PWM_INVERTED:
                    ledcWrite(groupid, map(!state, 0, 1, 0, 1023)); // ledChannel and value
                    break;
#else
                case HASP_GPIO_PWM:
                    analogWrite(gpioConfig[i].pin, map(state, 0, 1, 0, 1023));
                    break;
                case HASP_GPIO_PWM_INVERTED:
                    analogWrite(gpioConfig[i].pin, map(!state, 0, 1, 0, 1023));
                    break;
#endif
                default:;
            }
        }
    }
}

bool gpioIsSystemPin(uint8_t gpio)
{
    if((gpio >= NUM_DIGITAL_PINS) // invalid pins

// Use individual checks instead of switch statement, as some case labels could be duplicated
#ifdef TOUCH_CS
       || (gpio == TOUCH_CS)
#endif
#ifdef TFT_MOSI
       || (gpio == TFT_MOSI)
#endif
#ifdef TFT_MISO
       || (gpio == TFT_MISO)
#endif
#ifdef TFT_SCLK
       || (gpio == TFT_SCLK)
#endif
#ifdef TFT_CS
       || (gpio == TFT_CS)
#endif
#ifdef TFT_DC
       || (gpio == TFT_DC)
#endif
#ifdef TFT_BL
       || (gpio == TFT_BL)
#endif
#ifdef TFT_RST
       || (gpio == TFT_RST)
#endif
#ifdef TFT_WR
       || (gpio == TFT_WR)
#endif
#ifdef TFT_RD
       || (gpio == TFT_RD)
#endif
#ifdef TFT_D0
       || (gpio == TFT_D0)
#endif
#ifdef TFT_D1
       || (gpio == TFT_D1)
#endif
#ifdef TFT_D2
       || (gpio == TFT_D2)
#endif
#ifdef TFT_D3
       || (gpio == TFT_D3)
#endif
#ifdef TFT_D4
       || (gpio == TFT_D4)
#endif
#ifdef TFT_D5
       || (gpio == TFT_D5)
#endif
#ifdef TFT_D6
       || (gpio == TFT_D6)
#endif
#ifdef TFT_D7
       || (gpio == TFT_D7)
#endif
#ifdef TFT_D8
       || (gpio == TFT_D8)
#endif
#ifdef TFT_D9
       || (gpio == TFT_D9)
#endif
#ifdef TFT_D10
       || (gpio == TFT_D10)
#endif
#ifdef TFT_D11
       || (gpio == TFT_D11)
#endif
#ifdef TFT_D12
       || (gpio == TFT_D12)
#endif
#ifdef TFT_D13
       || (gpio == TFT_D13)
#endif
#ifdef TFT_D14
       || (gpio == TFT_D14)
#endif
#ifdef TFT_D15
       || (gpio == TFT_D15)
#endif
    ) {
        return true;
    } // if tft_espi pins

    // To-do:
    // Network GPIOs
    // Serial GPIOs
    // Tasmota GPIOs

#ifdef ARDUINO_ARCH_ESP8266
    if((gpio >= 6) && (gpio <= 11)) return true; // VSPI
#ifndef TFT_SPI_OVERLAP
    if((gpio >= 12) && (gpio <= 14)) return true; // HSPI
#endif
#endif

    return false;
}

bool gpioInUse(uint8_t gpio)
{
    for(uint8_t i = 0; i < HASP_NUM_GPIO_CONFIG; i++) {
        if(gpioConfigInUse(i) && (gpioConfig[i].pin == gpio)) {
            return true; // pin matches and is in use
        }
    }

    return false;
}

bool gpioSavePinConfig(uint8_t config_num, uint8_t pin, uint8_t type, uint8_t group, uint8_t pinfunc)
{
    // Input validation

    // ESP8266: Only Pullups except on gpio16

    // ESP32: Pullup or Pulldown except on 34-39

    if(config_num < HASP_NUM_GPIO_CONFIG && !gpioIsSystemPin(pin)) {
        gpioConfig[config_num].pin           = pin;
        gpioConfig[config_num].type          = type;
        gpioConfig[config_num].group         = group;
        gpioConfig[config_num].gpio_function = pinfunc;
        Log.notice(F("GPIO: Saving Pin config #%d pin %d - type %d - group %d - func %d"), config_num, pin, type, group,
                   pinfunc);
        return true;
    }

    return false;
}

bool gpioConfigInUse(uint8_t num)
{
    if(num >= HASP_NUM_GPIO_CONFIG) return false;
    return gpioConfig[num].type != HASP_GPIO_FREE;
}

int8_t gpioGetFreeConfigId()
{
    uint8_t id = 0;
    while(id < HASP_NUM_GPIO_CONFIG) {
        if(!gpioConfigInUse(id)) return id;
        id++;
    }
    return -1;
}

hasp_gpio_config_t gpioGetPinConfig(uint8_t num)
{
    return gpioConfig[num];
}

String gpioName(uint8_t gpio)
{
#if defined(STM32F4xx)
    String ioName;
    uint16_t name = digitalPin[gpio];
    uint8_t num   = name % 16;
    switch(name / 16) {
        case PortName::PortA:
            ioName = F("PA");
            break;
        case PortName::PortB:
            ioName = F("PB");
            break;
#if defined GPIOC_BASE
        case PortName::PortC:
            ioName = F("PC");
            break;
#endif
#if defined GPIOD_BASE
        case PortName::PortD:
            ioName = F("PD");
            break;
#endif
#if defined GPIOE_BASE
        case PortName::PortE:
            ioName = F("PE");
            break;
#endif
#if defined GPIOF_BASE
        case PortName::PortF:
            ioName = F("PF");
            break;
#endif
#if defined GPIOG_BASE
        case PortName::PortG:
            ioName = F("PG");
            break;
#endif
#if defined GPIOH_BASE
        case PortName::PortH:
            ioName = F("PH");
            break;
#endif
#if defined GPIOI_BASE
        case PortName::PortI:
            ioName = F("PI");
            break;
#endif
#if defined GPIOJ_BASE
        case PortName::PortJ:
            ioName = F("PJ");
            break;
#endif
#if defined GPIOK_BASE
        case PortName::PortK:
            ioName = F("PK");
            break;
#endif
#if defined GPIOZ_BASE
        case PortName::PortZ:
            ioName = F("PZ");
            break;
#endif
        default:
            ioName = F("P?");
    }
    ioName += num;
    ioName += F(" (io");
    ioName += gpio;
    ioName += F(")");
    return ioName;
#endif

// For ESP32 pin labels on boards use the GPIO number
#ifdef ARDUINO_ARCH_ESP32
    return String(F("gpio")) + gpio;
#endif

#ifdef ARDUINO_ARCH_ESP8266
    // For ESP8266 the pin labels are not the same as the GPIO number
    // These are for the NodeMCU pin definitions:
    //        GPIO       Dxx
    switch(gpio) {
        case 16:
            return F("D0");
        case 5:
            return F("D1");
        case 4:
            return F("D2");
        case 0:
            return F("D3");
        case 2:
            return F("D4");
        case 14:
            return F("D5");
        case 12:
            return F("D6");
        case 13:
            return F("D7");
        case 15:
            return F("D8");
        case 3:
            return F("TX");
        case 1:
            return F("RX");
        // case 9:
        //     return F("D11");
        // case 10:
        //     return F("D12");
        default:
            return F("D?"); // Invalid pin
    }
#endif
}

////////////////////////////////////////////////////////////////////////////////////////////////////
bool gpioGetConfig(const JsonObject & settings)
{
    bool changed = false;

    /* Check Gpio array has changed */
    JsonArray array = settings[FPSTR(F_GPIO_CONFIG)].as<JsonArray>();
    uint8_t i       = 0;
    for(JsonVariant v : array) {
        if(i < HASP_NUM_GPIO_CONFIG) {
            uint32_t cur_val = gpioConfig[i].pin | (gpioConfig[i].group << 8) | (gpioConfig[i].type << 16) |
                               (gpioConfig[i].gpio_function << 24);
            Log.verbose(F("GPIO CONF: %d: %d <=> %d"), i, cur_val, v.as<uint32_t>());

            if(cur_val != v.as<uint32_t>()) changed = true;
            v.set(cur_val);
        } else {
            changed = true;
        }
        i++;
    }

    /* Build new Gpio array if the count is not correct */
    if(i != HASP_NUM_GPIO_CONFIG) {
        array = settings[FPSTR(F_GPIO_CONFIG)].to<JsonArray>(); // Clear JsonArray
        for(uint8_t i = 0; i < HASP_NUM_GPIO_CONFIG; i++) {
            uint32_t cur_val = gpioConfig[i].pin | (gpioConfig[i].group << 8) | (gpioConfig[i].type << 16) |
                               (gpioConfig[i].gpio_function << 24);
            array.add(cur_val);
        }
        changed = true;
    }

    if(changed) configOutput(settings);
    return changed;
}

/** Set GPIO Configuration.
 *
 * Read the settings from json and sets the application variables.
 *
 * @note: data pixel should be formated to uint32_t RGBA. Imagemagick requirements.
 *
 * @param[in] settings    JsonObject with the config settings.
 **/
bool gpioSetConfig(const JsonObject & settings)
{
    configOutput(settings);
    bool changed = false;

    if(!settings[FPSTR(F_GPIO_CONFIG)].isNull()) {
        bool status = false;
        int i       = 0;

        JsonArray array = settings[FPSTR(F_GPIO_CONFIG)].as<JsonArray>();
        for(JsonVariant v : array) {
            uint32_t new_val = v.as<uint32_t>();

            if(i < HASP_NUM_GPIO_CONFIG) {
                uint32_t cur_val = gpioConfig[i].pin | (gpioConfig[i].group << 8) | (gpioConfig[i].type << 16) |
                                   (gpioConfig[i].gpio_function << 24);
                if(cur_val != new_val) status = true;

                gpioConfig[i].pin           = new_val & 0xFF;
                gpioConfig[i].group         = new_val >> 8 & 0xFF;
                gpioConfig[i].type          = new_val >> 16 & 0xFF;
                gpioConfig[i].gpio_function = new_val >> 24 & 0xFF;
            }
            i++;
        }
        changed |= status;
    }

    return changed;
}
