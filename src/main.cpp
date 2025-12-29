#include <Arduino.h>
#include <SPIFFS.h>
#include <OneWire.h>

#include "systeme/Config.h"
#include "systeme/Utils.h"
#include "services/NVSManager.h"
#include "services/RTCManager.h"
#include "services/SessionHistory.h"
#include "services/EventLog.h"

#include "actionneurs/Relay.h"
#include "controle/StatusLeds.h"
#include "controle/Buzzer.h"

#include "capteurs/TempSensor.h"
#include "capteurs/Bme280Sensor.h"
#include "capteurs/CurrentSensor.h"
#include "capteurs/BusSampler.h"

#include "systeme/Device.h"
#include "systeme/DeviceTransport.h"
#include "communication/entrees/SwitchManager.h"
#include "communication/reseau/WiFiManager.h"

// -----------------------------------------------------------------------------
// main.cpp
//
// Point d'entree Arduino :
// - setup() : initialise la plateforme, le stockage (SPIFFS/NVS), puis toutes les
//            classes (peripheriques -> services -> Device -> transport -> UI).
// - loop()  : vide, car toute la logique tourne dans des taches FreeRTOS.
// -----------------------------------------------------------------------------

// OneWire bus (DS18B20) : bus physique sur PIN_DS18B20.
static OneWire gOneWire(PIN_DS18B20);

// -----------------------------------------------------------------------------
// Instances principales (scope global)
//
// Note : la plupart des classes sont des objets statiques pour simplifier la
// gestion de vie (pas de delete) dans un firmware Arduino.
// -----------------------------------------------------------------------------
static Relay gRelay;
static StatusLeds gLeds;
static Buzzer* gBuzzer = nullptr;
static Acs712Sensor gCurrent;
static Ds18b20Sensor gDs18(&gOneWire);
static Bme280Sensor gBme;
static BusSampler* gSampler = nullptr;
static RTCManager* gRtc = nullptr;
static SessionHistory gSessions;
static EventLog gEvents;
static Device* gDevice = nullptr;
static DeviceTransport* gTransport = nullptr;
static SwitchManager* gSwitch = nullptr;
static WiFiManager* gWifi = nullptr;

void setup() {
    // Debug serie (thread-safe via Utils.*)
    Debug::begin(250000);
    DEBUG_PRINTLN("[Setup] Boot...");

    // Systeme de fichiers (SPIFFS) : sessions + events + UI (si utilise).
    if (!SPIFFS.begin(true)) {
        DEBUG_PRINTLN("[Setup] SPIFFS init failed");
    }

    // ---------------------------------------------------------------------
    // Etape 1 : Configuration / NVS (Preferences)
    // ---------------------------------------------------------------------
    NVS::Init();
    CONF->begin();

    // ---------------------------------------------------------------------
    // Etape 2 : Peripheriques (GPIO + capteurs)
    // ---------------------------------------------------------------------
    gRelay.begin();
    gLeds.begin();

    gBuzzer = Buzzer::Get();
    gBuzzer->begin();

    gCurrent.begin();
    gDs18.begin();
    gBme.begin();

    // ---------------------------------------------------------------------
    // Etape 3 : BusSampler (historique des mesures alignees)
    // ---------------------------------------------------------------------
    gSampler = BusSampler::Get();
    gSampler->begin(&gCurrent, &gDs18, &gBme, CONF->GetUInt(KEY_SAMPLING_HZ, DEFAULT_SAMPLING_HZ));
    gSampler->start();

    // ---------------------------------------------------------------------
    // Etape 4 : Services (RTC + logs persistants)
    // ---------------------------------------------------------------------
    RTCManager::Init();
    gRtc = RTC;
    gSessions.begin();
    gEvents.begin();

    // ---------------------------------------------------------------------
    // Etape 5 : Device (coeur) + transport (API commandes)
    // ---------------------------------------------------------------------
    Device::Init(&gRelay, &gLeds, gBuzzer, &gCurrent, &gDs18, &gBme, gSampler, gRtc, &gSessions, &gEvents);
    gDevice = Device::Get();
    gDevice->begin();

    gTransport = DeviceTransport::Get();
    gTransport->attach(gDevice);

    // ---------------------------------------------------------------------
    // Etape 6 : SwitchManager (bouton local)
    // ---------------------------------------------------------------------
    gSwitch = new SwitchManager(gTransport);
    gSwitch->begin();

    // ---------------------------------------------------------------------
    // Etape 7 : WiFiManager + HTTP API (STA -> fallback AP) + mDNS
    // ---------------------------------------------------------------------
    WiFiManager::Init(gDevice, gTransport, gSampler, &gSessions, &gEvents, gRtc, gBuzzer);
    gWifi = WiFiManager::Get();
    gWifi->begin();

    DEBUG_PRINTLN("[Setup] Ready");
}

void loop() {
    // Tout est gere par les tasks RTOS.
    // On laisse loop() en "idle" pour ne pas monopoliser le CPU.
    vTaskDelay(pdMS_TO_TICKS(1000));
}
