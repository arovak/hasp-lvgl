#include "ArduinoJson.h"
#include "ArduinoLog.h"
#include "lvgl.h"
//#include "time.h"

#include "hasp_conf.h"

#if defined(ARDUINO_ARCH_ESP8266)
#include <sntp.h> // sntp_servermode_dhcp()
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#elif defined(ARDUINO_ARCH_ESP32)
#include <Wifi.h>
#include <WiFiUdp.h>
#endif

#include "hasp_hal.h"
#if HASP_USE_MQTT > 0
#include "hasp_mqtt.h"
#endif

#include "hasp_conf.h"
#include "hasp_debug.h"
#include "hasp_config.h"
#include "hasp_dispatch.h"

#ifdef USE_CONFIG_OVERRIDE
#include "user_config_override.h"
#endif

#ifndef SERIAL_SPEED
#define SERIAL_SPEED 115200
#endif

#if HASP_USE_TELNET > 0
#include "hasp_telnet.h"
#endif

#if HASP_USE_SYSLOG > 0
#include "Syslog.h"
#include <WiFiUdp.h>

#ifndef SYSLOG_SERVER
#define SYSLOG_SERVER ""
#endif

#ifndef SYSLOG_PORT
#define SYSLOG_PORT 514
#endif

#ifndef APP_NAME
#define APP_NAME "HASP"
#endif

// variables for debug stream writer
// static String debugOutput((char *)0);
// static StringStream debugStream((String &)debugOutput);

extern char mqttNodeName[16];
const char * syslogAppName  = APP_NAME;
char debugSyslogHost[32]    = SYSLOG_SERVER;
uint16_t debugSyslogPort    = SYSLOG_PORT;
uint8_t debugSyslogFacility = 0;
uint8_t debugSyslogProtocol = 0;

// A UDP instance to let us send and receive packets over UDP
WiFiUDP syslogClient;

// Create a new syslog instance with LOG_KERN facility
// Syslog syslog(syslogClient, SYSLOG_SERVER, SYSLOG_PORT, MQTT_CLIENT, APP_NAME, LOG_KERN);
// Create a new empty syslog instance
Syslog * syslog;
#endif // USE_SYSLOG

// Serial Settings
uint8_t serialInputIndex = 0; // Empty buffer
char serialInputBuffer[1024];
uint16_t debugSerialBaud = SERIAL_SPEED / 10; // Multiplied by 10
bool debugSerialStarted  = false;
bool debugAnsiCodes      = true;

//#define TERM_COLOR_Black "\u001b[30m"
#define TERM_COLOR_GRAY "\e[37m"
#define TERM_COLOR_RED "\e[91m"
#define TERM_COLOR_GREEN "\e[92m"
#define TERM_COLOR_ORANGE "\e[38;5;214m"
#define TERM_COLOR_YELLOW "\e[93m"
#define TERM_COLOR_BLUE "\e[94m"
#define TERM_COLOR_MAGENTA "\e[35m"
#define TERM_COLOR_CYAN "\e[96m"
#define TERM_COLOR_WHITE "\e[97m"
#define TERM_COLOR_RESET "\e[0m"

unsigned long debugLastMillis = 0;
uint16_t debugTelePeriod      = 300;

String debugHaspHeader()
{
    String header((char *)0);
    header.reserve(256);
    if(debugAnsiCodes) header += TERM_COLOR_YELLOW;
    header += F("\r\n"
                "           _____ _____ _____ _____\r\n"
                "          |  |  |  _  |   __|  _  |\r\n"
                "          |     |     |__   |   __|\r\n"
                "          |__|__|__|__|_____|__|\r\n"
                "        Home Automation Switch Plate\r\n");
    char buffer[128];
    snprintf(buffer, sizeof(buffer), PSTR("        Open Hardware edition v%u.%u.%u\r\n"), HASP_VERSION_MAJOR,
             HASP_VERSION_MINOR, HASP_VERSION_REVISION);
    header += buffer;
    return header;
}

void debugStart()
{
    if(debugSerialStarted) {
        Serial.flush();
        Serial.println();
        Serial.println(debugHaspHeader());
        Serial.flush();
    }

    // prepare syslog configuration here (can be anywhere before first call of
    // log/logf method)
}

#if HASP_USE_SYSLOG > 0
void syslogSend(uint8_t priority, const char * debugText)
{
    if(strlen(debugSyslogHost) != 0 && WiFi.isConnected()) {
        syslog->log(priority, debugText);
    }
}
#endif

void debugSetup()
{
#if HASP_USE_SYSLOG > 0
    syslog = new Syslog(syslogClient, debugSyslogProtocol == 0 ? SYSLOG_PROTO_IETF : SYSLOG_PROTO_BSD);
    syslog->server(debugSyslogHost, debugSyslogPort);
    syslog->deviceHostname(mqttNodeName);
    syslog->appName(syslogAppName);
    uint16_t priority = (uint16_t)(debugSyslogFacility + 16) << 3; // localx facility, x = 0-7
    syslog->defaultPriority(priority);
#endif
}

void debugStop()
{
    if(debugSerialStarted) Serial.flush();
}

bool debugGetConfig(const JsonObject & settings)
{
    bool changed = false;

    if(debugSerialBaud != settings[FPSTR(F_CONFIG_BAUD)].as<uint16_t>()) changed = true;
    settings[FPSTR(F_CONFIG_BAUD)] = debugSerialBaud;

    if(debugTelePeriod != settings[FPSTR(F_DEBUG_TELEPERIOD)].as<uint16_t>()) changed = true;
    settings[FPSTR(F_DEBUG_TELEPERIOD)] = debugTelePeriod;

#if HASP_USE_SYSLOG > 0
    if(strcmp(debugSyslogHost, settings[FPSTR(F_CONFIG_HOST)].as<String>().c_str()) != 0) changed = true;
    settings[FPSTR(F_CONFIG_HOST)] = debugSyslogHost;

    if(debugSyslogPort != settings[FPSTR(F_CONFIG_PORT)].as<uint16_t>()) changed = true;
    settings[FPSTR(F_CONFIG_PORT)] = debugSyslogPort;

    if(debugSyslogProtocol != settings[FPSTR(F_CONFIG_PROTOCOL)].as<uint8_t>()) changed = true;
    settings[FPSTR(F_CONFIG_PROTOCOL)] = debugSyslogProtocol;

    if(debugSyslogFacility != settings[FPSTR(F_CONFIG_LOG)].as<uint8_t>()) changed = true;
    settings[FPSTR(F_CONFIG_LOG)] = debugSyslogFacility;
#endif

    if(changed) configOutput(settings);
    return changed;
}

/** Set DEBUG Configuration.
 *
 * Read the settings from json and sets the application variables.
 *
 * @note: data pixel should be formated to uint32_t RGBA. Imagemagick requirements.
 *
 * @param[in] settings    JsonObject with the config settings.
 **/
bool debugSetConfig(const JsonObject & settings)
{
    configOutput(settings);
    bool changed = false;

    /* Serial Settings*/
    changed |= configSet(debugSerialBaud, settings[FPSTR(F_CONFIG_BAUD)], PSTR("debugSerialBaud"));

    /* Teleperiod Settings*/
    changed |= configSet(debugTelePeriod, settings[FPSTR(F_DEBUG_TELEPERIOD)], PSTR("debugTelePeriod"));

    /* Syslog Settings*/
#if HASP_USE_SYSLOG > 0
    if(!settings[FPSTR(F_CONFIG_HOST)].isNull()) {
        changed |= strcmp(debugSyslogHost, settings[FPSTR(F_CONFIG_HOST)]) != 0;
        strncpy(debugSyslogHost, settings[FPSTR(F_CONFIG_HOST)], sizeof(debugSyslogHost));
    }
    changed |= configSet(debugSyslogPort, settings[FPSTR(F_CONFIG_PORT)], PSTR("debugSyslogPort"));
    changed |= configSet(debugSyslogProtocol, settings[FPSTR(F_CONFIG_PROTOCOL)], PSTR("debugSyslogProtocol"));
    changed |= configSet(debugSyslogFacility, settings[FPSTR(F_CONFIG_LOG)], PSTR("debugSyslogFacility"));
#endif

    return changed;
}

inline void debugSendAnsiCode(const __FlashStringHelper * code, Print * _logOutput)
{
    if(debugAnsiCodes) _logOutput->print(code);
}

static void debugPrintTimestamp(int level, Print * _logOutput)
{ /* Print Current Time */
    time_t rawtime;
    struct tm * timeinfo;

    // time(&rawtime);
    // timeinfo = localtime(&rawtime);

    // strftime(buffer, sizeof(buffer), "%b %d %H:%M:%S.", timeinfo);
    // Serial.println(buffer);

    debugSendAnsiCode(F(TERM_COLOR_CYAN), _logOutput);

    /* if(timeinfo->tm_year >= 120) {
         char buffer[64];
         strftime(buffer, sizeof(buffer), "[%b %d %H:%M:%S.", timeinfo); // Literal String
         _logOutput->print(buffer);
         _logOutput->printf(PSTR("%03lu]"), millis() % 1000);
     } else */
    {
        uint32_t msecs = millis();
        _logOutput->printf(PSTR("[%16d.%03d]"), msecs / 1000, msecs % 1000);
    }
}

static void debugPrintHaspMemory(int level, Print * _logOutput)
{
    size_t maxfree     = halGetMaxFreeBlock();
    uint32_t totalfree = halGetFreeHeap();
    uint8_t frag       = halGetHeapFragmentation();

    /* Print HASP Memory Info */
    if(debugAnsiCodes) {
        if(maxfree > (1024u * 5) && (totalfree > 1024u * 6) && (frag <= 10))
            debugSendAnsiCode(F(TERM_COLOR_GREEN), _logOutput);
        else if(maxfree > (1024u * 3) && (totalfree > 1024u * 5) && (frag <= 20))
            debugSendAnsiCode(F(TERM_COLOR_ORANGE), _logOutput);
        else
            debugSendAnsiCode(F(TERM_COLOR_RED), _logOutput);
    }
    _logOutput->printf(PSTR("[%5u/%5u%3u]"), maxfree, totalfree, frag);
}

#if LV_MEM_CUSTOM == 0
static void debugPrintLvglMemory(int level, Print * _logOutput)
{
    lv_mem_monitor_t mem_mon;
    lv_mem_monitor(&mem_mon);

    /* Print LVGL Memory Info */
    if(debugAnsiCodes) {
        if(mem_mon.free_biggest_size > (1024u * 2) && (mem_mon.free_size > 1024u * 2.5) && (mem_mon.frag_pct <= 10))
            debugSendAnsiCode(F(TERM_COLOR_GREEN), _logOutput);
        else if(mem_mon.free_biggest_size > (1024u * 1) && (mem_mon.free_size > 1024u * 1.5) &&
                (mem_mon.frag_pct <= 25))
            debugSendAnsiCode(F(TERM_COLOR_ORANGE), _logOutput);
        else
            debugSendAnsiCode(F(TERM_COLOR_RED), _logOutput);
    }
    _logOutput->printf(PSTR("[%5u/%5u%3u]"), mem_mon.free_biggest_size, mem_mon.free_size, mem_mon.frag_pct);
}
#endif

static void debugPrintPriority(int level, Print * _logOutput)
{
    if(_logOutput == &syslogClient) {
    }

    switch(level) {
        case LOG_LEVEL_FATAL:
        case LOG_LEVEL_ERROR:
            debugSendAnsiCode(F(TERM_COLOR_RED), _logOutput);
            break;
        case LOG_LEVEL_WARNING:
            debugSendAnsiCode(F(TERM_COLOR_YELLOW), _logOutput);
            break;
        case LOG_LEVEL_NOTICE:
            debugSendAnsiCode(F(TERM_COLOR_WHITE), _logOutput);
            break;
        case LOG_LEVEL_VERBOSE:
            debugSendAnsiCode(F(TERM_COLOR_CYAN), _logOutput);
            break;
        case LOG_LEVEL_TRACE:
            debugSendAnsiCode(F(TERM_COLOR_GRAY), _logOutput);
            break;
        default:
            debugSendAnsiCode(F(TERM_COLOR_RESET), _logOutput);
    }
}

void debugPrintPrefix(int level, Print * _logOutput)
{
    debugPrintTimestamp(level, _logOutput);
    debugPrintHaspMemory(level, _logOutput);
#if LV_MEM_CUSTOM == 0
    debugPrintLvglMemory(level, _logOutput);
#endif
    debugPrintPriority(level, _logOutput);
}

void debugPrintSuffix(int level, Print * _logOutput)
{
    if(debugAnsiCodes)
        _logOutput->println(F(TERM_COLOR_RESET));
    else
        _logOutput->println();
    if(debugAnsiCodes) _logOutput->print(F(TERM_COLOR_MAGENTA));

    // syslogSend(level, debugOutput);
}

void debugPreSetup(JsonObject settings)
{
    // Link stream to debugOutput
    // debugOutput.reserve(512);

    Log.begin(LOG_LEVEL_WARNING, true);
    Log.setPrefix(debugPrintPrefix); // Uncomment to get timestamps as prefix
    Log.setSuffix(debugPrintSuffix); // Uncomment to get newline as suffix

    uint32_t baudrate = settings[FPSTR(F_CONFIG_BAUD)].as<uint32_t>() * 10;
    if(baudrate == 0) baudrate = SERIAL_SPEED;
    if(baudrate >= 9600u) { /* the baudrates are stored divided by 10 */

#if defined(STM32F4xx)
#ifndef STM32_SERIAL1      // Define what Serial port to use for log output
        Serial.setRx(PA3); // User Serial2
        Serial.setTx(PA2);
#endif
#endif
        Serial.begin(baudrate); /* prepare for possible serial debug */
        delay(10);
        Log.registerOutput(0, &Serial, LOG_LEVEL_VERBOSE, true);
        debugSerialStarted = true;
        Serial.println();
        Log.trace(("Serial started at %u baud"), baudrate);
    }
}

#if LV_USE_LOG != 0
static uint32_t lastDbgLine;
static uint16_t lastDbgFree;
void debugLvgl(lv_log_level_t level, const char * file, uint32_t line, const char * funcname, const char * descr)
{
    lv_mem_monitor_t mem_mon;
    lv_mem_monitor(&mem_mon);

    /* Reduce the number of reepeated debug message */
    if(line != lastDbgLine || mem_mon.free_biggest_size != lastDbgFree) {
        switch(level) {
            case LV_LOG_LEVEL_TRACE:
                Log.trace(descr);
                break;
            case LV_LOG_LEVEL_WARN:
                Log.warning(descr);
                break;
            case LV_LOG_LEVEL_ERROR:
                Log.error(descr);
                break;
            default:
                Log.notice(descr);
        }
        lastDbgLine = line;
        lastDbgFree = mem_mon.free_biggest_size;
    }
}
#endif

void debugLoop()
{
    while(Serial.available()) {
        char ch = Serial.read();
        Serial.print(ch);
        if(ch == 13 || ch == 10) {
            serialInputBuffer[serialInputIndex] = 0;
            if(serialInputIndex > 0) dispatchCommand(serialInputBuffer);
            serialInputIndex = 0;
        } else {
            if(serialInputIndex < sizeof(serialInputBuffer) - 1) {
                serialInputBuffer[serialInputIndex++] = tolower(ch);
            }
            serialInputBuffer[serialInputIndex] = 0;
            if(strcmp(serialInputBuffer, "jsonl=") == 0) {
                dispatchJsonl(Serial);
                serialInputIndex = 0;
            }
        }
    }
}

/*void printLocalTime()
{
    char buffer[128];
    time_t rawtime;
    struct tm * timeinfo;

    // if(!time(nullptr)) return;

    time(&rawtime);
    timeinfo = localtime(&rawtime);

    strftime(buffer, sizeof(buffer), "%b %d %H:%M:%S.", timeinfo);
    Serial.println(buffer);
    // struct tm timeinfo;
    // time_t now = time(nullptr);

    // Serial-.print(ctime(&now));
    // Serial.print(&timeinfo, " %d %B %Y %H:%M:%S ");

#if LWIP_VERSION_MAJOR > 1

    // LwIP v2 is able to list more details about the currently configured SNTP servers
    for(int i = 0; i < SNTP_MAX_SERVERS; i++) {
        IPAddress sntp    = *sntp_getserver(i);
        const char * name = sntp_getservername(i);
        if(sntp.isSet()) {
            Serial.printf("sntp%d:     ", i);
            if(name) {
                Serial.printf("%s (%s) ", name, sntp.toString().c_str());
            } else {
                Serial.printf("%s ", sntp.toString().c_str());
            }
            Serial.printf("IPv6: %s Reachability: %o\n", sntp.isV6() ? "Yes" : "No", sntp_getreachability(i));
        }
    }
#endif
}*/

void debugEverySecond()
{
    if(debugTelePeriod > 0 && (millis() - debugLastMillis) >= debugTelePeriod * 1000) {
        dispatchStatusUpdate();
        debugLastMillis = millis();
    }
    // printLocalTime();
}
