#include <Arduino.h>
#include <SPIFFS.h>
#include <OneWire.h>

#include <Config.hpp>
#include <Utils.hpp>
#include <NVSManager.hpp>
#include <RTCManager.hpp>
#include <SessionHistory.hpp>
#include <EventLog.hpp>

#include <Relay.hpp>
#include <StatusLeds.hpp>
#include <Buzzer.hpp>

#include <TempSensor.hpp>
#include <Bme280Sensor.hpp>
#include <CurrentSensor.hpp>
#include <BusSampler.hpp>

#include <Device.hpp>
#include <DeviceTransport.hpp>
#include <SwitchManager.hpp>
#include <WiFiManager.hpp>

// -----------------------------------------------------------------------------
// main.cpp
//
// Point d'entree Arduino :
// - setup() : initialise la plateforme, le stockage (SPIFFS/NVS), puis toutes les
//            classes (peripheriques -> services -> Device -> transport -> UI).
// - loop()  : vide, car toute la logique tourne dans des taches FreeRTOS.
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// Instances principales (scope global)
//
// Note : pointeurs initialises a nullptr et instancies dans setup().
// -----------------------------------------------------------------------------
// OneWire bus (for digital temperature sensors like DS18B20)
OneWire oneWire(PIN_DS18B20);
Relay* gRelay = nullptr;
 StatusLeds* gLeds = nullptr;
 Acs712Sensor* gCurrent = nullptr;
 Ds18b20Sensor* gDs18 = nullptr;
 Bme280Sensor* gBme = nullptr;
 SessionHistory* gSessions = nullptr;
 EventLog* gEvents = nullptr;
SwitchManager* gSwitch = nullptr;

void WiFiEvent(WiFiEvent_t event) {
    if (event == ARDUINO_EVENT_WIFI_STA_CONNECTED ||
        event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
        if (BUZZ) BUZZ->playWiFiConnected();
    } else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
        if (BUZZ) BUZZ->playWiFiOff();
    } else if (event == ARDUINO_EVENT_WIFI_AP_STACONNECTED) {
        if (BUZZ) BUZZ->playClientConnect();
    } else if (event == ARDUINO_EVENT_WIFI_AP_STADISCONNECTED) {
        if (BUZZ) BUZZ->playClientDisconnect();
        if (gEvents) {
            gEvents->append(EventLevel::Warning,
                            static_cast<uint16_t>(WarnCode::W09_ClientGone),
                            "Client disconnect",
                            "wifi");
        }
    }
}


void setup() {
    delay(2000);
    // --------------------------------------------------
    // 0) Serial / Debug FIRST
    // --------------------------------------------------
    Debug::begin(SERIAL_BAUD_RATE);
    DEBUG_PRINTLN();
    DEBUG_PRINTLN("==================================================");
    DEBUG_PRINTLN("[BOOT] System startup");
    DEBUG_PRINTLN("==================================================");
    delay(2000);

    // --------------------------------------------------
    // 1) SPIFFS
    // --------------------------------------------------
    DEBUG_PRINTLN("[BOOT] Initializing SPIFFS...");
    if (!SPIFFS.begin(true)) {
        DEBUG_PRINTLN("[FATAL] SPIFFS init FAILED");
        while (true) {
            delay(500);
        }
    }
    DEBUG_PRINTLN("[BOOT] SPIFFS OK");

    Debug::enableMemoryLog(1024 * 1024);
    DEBUG_PRINTLN("[BOOT] Debug memory log enabled");

    // --------------------------------------------------
    // 2) NVS + Config
    // --------------------------------------------------
    DEBUG_PRINTLN("[BOOT] Initializing NVS...");
    NVS::Init();
    DEBUG_PRINTLN("[BOOT] NVS OK");

    DEBUG_PRINTLN("[BOOT] Loading configuration...");
    CONF->begin();
    DEBUG_PRINTLN("[BOOT] Config OK");

    // --------------------------------------------------
    // 3) RTC (EARLY)
    // --------------------------------------------------
    DEBUG_PRINTLN("[BOOT] Initializing RTC (early)...");
    RTC->Init();
    RTC->setUnixTime(1768396343);
    DEBUG_PRINTLN("[BOOT] RTC OK");

    // --------------------------------------------------
    // 4) Hardware peripherals
    // --------------------------------------------------
    DEBUG_PRINTLN("[BOOT] Initializing Buzzer...");
    (void)BUZZ;
    BUZZ->begin();
    BUZZ->playStartupSequence();
    DEBUG_PRINTLN("[BOOT] Buzzer OK");

    DEBUG_PRINTLN("[BOOT] Initializing Relay...");
    gRelay = new Relay();
    gRelay->begin();
    DEBUG_PRINTLN("[BOOT] Relay OK");

    DEBUG_PRINTLN("[BOOT] Initializing Status LEDs...");
    gLeds = new StatusLeds();
    gLeds->begin();
    gLeds->bootAnimation();
    DEBUG_PRINTLN("[BOOT] LEDs OK");

    DEBUG_PRINTLN("[BOOT] Initializing Current Sensor...");
    gCurrent = new Acs712Sensor();
    gCurrent->begin();
    DEBUG_PRINTLN("[BOOT] Current Sensor OK");
    // --------------------------------------------------
    // 8) Switch manager
    // --------------------------------------------------
    DEBUG_PRINTLN("[BOOT] Initializing SwitchManager...");
    gSwitch = new SwitchManager();
    gSwitch->begin();
    DEBUG_PRINTLN("[BOOT] SwitchManager OK");

    DEBUG_PRINTLN("[BOOT] Initializing DS18B20...");
    gDs18 = new Ds18b20Sensor(&oneWire);
    gDs18->begin();
    DEBUG_PRINTLN("[BOOT] DS18B20 OK");

    DEBUG_PRINTLN("[BOOT] Initializing BME280...");
    gBme = new Bme280Sensor();
    gBme->begin();
    DEBUG_PRINTLN("[BOOT] BME280 OK");

    // --------------------------------------------------
    // 5) BusSampler
    // --------------------------------------------------
    uint32_t samplingHz = CONF->GetUInt(KEY_SAMPLING_HZ, DEFAULT_SAMPLING_HZ);
    DEBUG_PRINT("[BOOT] Initializing BusSampler @ ");
    DEBUG_PRINT(samplingHz);
    DEBUG_PRINTLN(" Hz");

    (void)BUS_SAMPLER;
    BUS_SAMPLER->begin(gCurrent, gDs18, gBme, samplingHz);
    DEBUG_PRINTLN("[BOOT] BusSampler started");

    // --------------------------------------------------
    // 6) Persistent logs
    // --------------------------------------------------
    DEBUG_PRINTLN("[BOOT] Initializing SessionHistory...");
    gSessions = new SessionHistory();
    gSessions->begin();
    DEBUG_PRINTLN("[BOOT] SessionHistory OK");

    DEBUG_PRINTLN("[BOOT] Initializing EventLog...");
    gEvents = new EventLog();
    gEvents->begin();
    DEBUG_PRINTLN("[BOOT] EventLog OK");

    // --------------------------------------------------
    // 7) Device core + transport
    // --------------------------------------------------
    DEBUG_PRINTLN("[BOOT] Initializing Device core...");
    Device::Init(
        gRelay,
        gLeds,
        gCurrent,
        gDs18,
        gBme,
        RTC,
        gSessions,
        gEvents
    );
    DEVICE->begin();
    DEBUG_PRINTLN("[BOOT] Device core OK");

    DEBUG_PRINTLN("[BOOT] Initializing DeviceTransport...");
    (void)DEVTRAN;
    DEBUG_PRINTLN("[BOOT] DeviceTransport OK");



    // --------------------------------------------------
    // 9) WiFi + API
    // --------------------------------------------------
    DEBUG_PRINTLN("[BOOT] Initializing WiFiManager...");
    WiFi.onEvent(WiFiEvent);
    WiFiManager::Init(
        gSessions,
        gEvents,
        RTC
    );
    WIF->begin();
    DEBUG_PRINTLN("[BOOT] WiFiManager OK");

    // --------------------------------------------------
    // DONE
    // --------------------------------------------------
    DEBUG_PRINTLN("==================================================");
    DEBUG_PRINTLN("[BOOT] SETUP COMPLETE - SYSTEM READY");
    DEBUG_PRINTLN("==================================================");
    BUZZ->playSystemReady();
}

void loop() {
    // Tout est gere par les tasks RTOS.
    // On laisse loop() en "idle" pour ne pas monopoliser le CPU.
    vTaskDelay(pdMS_TO_TICKS(2000));
    //DEBUG_PRINTLN("MainLoop!");
}
