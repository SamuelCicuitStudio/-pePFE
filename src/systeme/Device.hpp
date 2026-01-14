/**************************************************************
 *  Device - coeur du systeme
 *
 *  Device est le "bloc central" de l'architecture.
 *
 *  Responsabilites :
 *  - Machine d'etats (Off/Idle/Running/Fault/...)
 *  - Application des protections :
 *      - OVC (surintensite) avec mode configurable (Latch / AutoRetry)
 *      - Overtemp (surchauffe) moteur/carte
 *  - Calcul puissance/energie (integration Wh)
 *  - Construction d'un snapshot coherant (SystemSnapshot) pour l'UI Web
 *  - Ecriture unique dans la NVS (tous les parametres persistants)
 *  - Publication des warnings/erreurs vers EventLog + LED + Buzzer
 *
 *  Concurrence :
 *  - Une file de commandes (Queue FreeRTOS) recoit les actions externes
 *    via DeviceTransport.
 *  - Un mutex (semaphore) protege le snapshot et certaines variables
 *    partagees afin d'eviter les incoherences.
 **************************************************************/
#ifndef DEVICE_H
#define DEVICE_H

#include <Config.hpp>
#include <StatusSnapshot.hpp>
#include <Relay.hpp>
#include <StatusLeds.hpp>
#include <Buzzer.hpp>
#include <CurrentSensor.hpp>
#include <TempSensor.hpp>
#include <Bme280Sensor.hpp>
#include <BusSampler.hpp>
#include <RTCManager.hpp>
#include <SessionHistory.hpp>
#include <EventLog.hpp>

class Device {
public:
    // ---------------------------------------------------------------------
    // Commandes asynchrones (envoyees via DeviceTransport)
    // ---------------------------------------------------------------------
    struct Command {
        enum class Type : uint8_t {
            Start,       // Demarrer (equivalent ON)
            Stop,        // Arreter (equivalent OFF)
            Toggle,      // Inverser l'etat (bouton)
            ClearFault,  // Acquitter / rearmement (si possible)
            TimedRun,    // Demarrer pendant N secondes
            SetRelay,    // Forcer relais ON/OFF (si pas en defaut latch)
            Reset        // Redemarrage systeme (ESP.restart)
        } type;

        // Champs generiques de "payload" (selon cmd.type)
        uint32_t u32 = 0;
        bool b = false;
    };

    // Singleton : on injecte les dependances une seule fois pendant setup().
    static void Init(Relay* relay,
                     StatusLeds* leds,
                     Acs712Sensor* current,
                     Ds18b20Sensor* ds18,
                     Bme280Sensor* bme,
                     RTCManager* rtc,
                     SessionHistory* sessions,
                     EventLog* events);

    static Device* Get();

    // Lance la logique (charge config, demarre sampler + taches).
    void begin();

    // Commande externe (DeviceTransport). Non bloquant : push dans une queue.
    bool submitCommand(const Command& cmd);

    // ---------------------------------------------------------------------
    // Mise a jour config (Device seul ecrivain NVS)
    // ---------------------------------------------------------------------
    struct ConfigUpdate {
        // Courant / OVC
        bool hasLimitCurrent = false;
        float limitCurrentA = 0.0f;

        bool hasOvcMode = false;
        OvcMode ovcMode = OvcMode::Latch;
        bool hasOvcMinMs = false;
        uint32_t ovcMinMs = 0;
        bool hasOvcRetryMs = false;
        uint32_t ovcRetryMs = 0;

        // Temperatures / surchauffe
        bool hasTempMotor = false;
        float tempMotorC = 0.0f;
        bool hasTempBoard = false;
        float tempBoardC = 0.0f;
        bool hasTempAmbient = false;
        float tempAmbientC = 0.0f;
        bool hasTempHyst = false;
        float tempHystC = 0.0f;
        bool hasLatchOvertemp = false;
        bool latchOvertemp = false;

        // Parametres calcul puissance et echantillonnage
        bool hasMotorVcc = false;
        float motorVcc = 0.0f;
        bool hasSamplingHz = false;
        uint32_t samplingHz = 0;
        bool hasBuzzerEnabled = false;
        bool buzzerEnabled = true;

        // Wi-Fi (identifiants et mode)
        bool hasWifiSta = false;
        String staSsid;
        String staPass;
        bool hasWifiAp = false;
        String apSsid;
        String apPass;
        bool hasWifiMode = false;
        WiFiModeSetting wifiMode = WiFiModeSetting::Sta;
    };

    // Applique une mise a jour de configuration (et ecrit en NVS).
    bool applyConfig(const ConfigUpdate& cfg);

    // ---------------------------------------------------------------------
    // Calibration courant (ACS712)
    // ---------------------------------------------------------------------

    // Calibre l'offset (zero) en mesurant le courant "a vide".
    void calibrateCurrentZero();

    // Fixe la calibration (offset + sensibilite + echelle analogique).
    void setCurrentCalibration(float zeroMv, float sensMvPerA, float inputScale);

    // Feedback commande (LED CMD) : blink bref "commande recu".
    void notifyCommand();

    // ---------------------------------------------------------------------
    // Acces snapshot / etat
    // ---------------------------------------------------------------------

    // Copie coherente du snapshot (sous mutex interne).
    bool getSnapshot(SystemSnapshot& out) const;

    // Lecture rapide de l'etat (tentative mutex, sinon valeur courante).
    DeviceState getState() const;

private:
    Device(Relay* relay,
           StatusLeds* leds,
           Acs712Sensor* current,
           Ds18b20Sensor* ds18,
           Bme280Sensor* bme,
           RTCManager* rtc,
           SessionHistory* sessions,
           EventLog* events);

    void loadConfig_();
    void applyRelay_(bool on);
    void setState_(DeviceState s);

    // Traitement de la file de commandes (start/stop/clearFault/...)
    void processCommands_();

    // Protections (OVC + surchauffe + diagnostics capteurs)
    void updateProtection_();

    // Integration energie (Wh) a partir de la puissance instantanee
    void updateEnergy_();

    // Construit un SystemSnapshot coherant pour l'UI
    void updateSnapshot_();

    // Sessions : demarre / termine une session, stocke dans SessionHistory
    void startSession_();
    void endSession_(bool success);

    // Publication des warnings/erreurs vers EventLog + LED + buzzer.
    void raiseWarning_(WarnCode code, const char* msg, const char* src);
    void raiseError_(ErrorCode code, const char* msg, const char* src);

    // Tache "control" : boucle temps reel (process commands + protections + snapshot).
    static void controlTaskThunk_(void* param);
    void controlTask_();

    // Mutex interne (semaphore) pour proteger snapshot_ et etat partage.
    bool lock_() const;
    void unlock_() const;

    // ---------------------------------------------------------------------
    // Dependances (peripheriques / services)
    // ---------------------------------------------------------------------

    // Pointeurs non-possede : les objets sont crees dans main.cpp et vivent
    // pendant toute la duree du firmware.
    Relay* relay_ = nullptr;
    StatusLeds* leds_ = nullptr;
    Acs712Sensor* current_ = nullptr;
    Ds18b20Sensor* ds18_ = nullptr;
    Bme280Sensor* bme_ = nullptr;
    RTCManager* rtc_ = nullptr;
    SessionHistory* sessions_ = nullptr;
    EventLog* events_ = nullptr;

    // ---------------------------------------------------------------------
    // Configuration (cache runtime - chargee depuis NVS)
    // ---------------------------------------------------------------------
    float limitCurrentA_ = DEFAULT_LIMIT_CURRENT_A;
    OvcMode ovcMode_ = OvcMode::Latch;
    uint32_t ovcMinMs_ = DEFAULT_OVC_MIN_DURATION_MS;
    uint32_t ovcRetryMs_ = DEFAULT_OVC_RETRY_DELAY_MS;

    float tempMotorC_ = DEFAULT_TEMP_MOTOR_C;
    float tempBoardC_ = DEFAULT_TEMP_BOARD_C;
    float tempAmbientC_ = DEFAULT_TEMP_AMBIENT_C;
    float tempHystC_ = DEFAULT_TEMP_HYST_C;
    bool latchOvertemp_ = DEFAULT_LATCH_OVERTEMP;

    float motorVcc_ = DEFAULT_MOTOR_VCC_V;

    // ---------------------------------------------------------------------
    // Etat runtime / securites
    // ---------------------------------------------------------------------
    DeviceState state_ = DeviceState::Off;
    bool faultLatched_ = false;
    uint32_t runUntilMs_ = 0;

    // OVC (surintensite)
    uint32_t ovcStartMs_ = 0;
    uint32_t ovcRetryAtMs_ = 0;

    // Surchauffe
    bool overtempActive_ = false;

    // Energie / session
    bool sessionActive_ = false;
    uint32_t sessionStartMs_ = 0;
    uint64_t sessionStartEpoch_ = 0;
    float energyWh_ = 0.0f;
    float peakPowerW_ = 0.0f;
    float peakCurrentA_ = 0.0f;
    uint32_t lastEnergyMs_ = 0;

    float lastCurrentA_ = 0.0f;
    float lastPowerW_ = 0.0f;

    // Derniers codes publies (anti-spam sur l'UI / buzzer)
    uint16_t lastWarningCode_ = 0;
    uint16_t lastErrorCode_ = 0;
    uint32_t lastWarnMs_ = 0;
    uint32_t lastErrMs_ = 0;

    // Cadence snapshot (integree dans la tache control)
    uint32_t lastSnapshotMs_ = 0;

    // ---------------------------------------------------------------------
    // Infrastructure RTOS
    // ---------------------------------------------------------------------

    // Tache interne unique (control + snapshot)
    TaskHandle_t controlTaskHandle_ = nullptr;

    // Queue commandes asynchrones
    QueueHandle_t cmdQueue_ = nullptr;

    // Mutex snapshot/etat
    mutable SemaphoreHandle_t mutex_ = nullptr;

    // Dernier snapshot publie
    SystemSnapshot snapshot_{};

    static Device* inst_;
};

#define DEVICE Device::Get()

#endif // DEVICE_H
