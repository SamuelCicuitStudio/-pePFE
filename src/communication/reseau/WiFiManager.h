/**************************************************************
 *  WiFiManager - STA puis fallback AP, API HTTP
 *
 *  Fonction :
 *  - Demarre le Wi-Fi en mode Station (STA) en premier.
 *  - Si la connexion STA echoue, bascule en Access Point (AP).
 *  - Si wifi_mode = AP, on demarre directement en AP.
 *  - Publie un serveur HTTP (ESPAsyncWebServer) pour l'UI / API JSON.
 *  - Active mDNS avec hostname fixe : contro.local
 *
 *  Securite :
 *  - Les endpoints de config/controle/calibration sont proteges par
 *    authentification (basic ou token, selon NVS).
 *  - En cas d'echec auth : log + bip buzzer.
 **************************************************************/
#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <WiFi.h>
#include <ESPmDNS.h>
#include <ESPAsyncWebServer.h>
#include "systeme/Config.h"
#include "systeme/DeviceTransport.h"
#include "capteurs/BusSampler.h"
#include "services/SessionHistory.h"
#include "services/EventLog.h"
#include "services/RTCManager.h"
#include "controle/Buzzer.h"

class WiFiManager {
public:
    static void Init(Device* device,
                     DeviceTransport* transport,
                     BusSampler* sampler,
                     SessionHistory* sessions,
                     EventLog* events,
                     RTCManager* rtc,
                     Buzzer* buzzer);
    static WiFiManager* Get();

    // Lance Wi-Fi + mDNS + serveur HTTP (a appeler une seule fois au setup).
    void begin();

private:
    WiFiManager(Device* device,
                DeviceTransport* transport,
                BusSampler* sampler,
                SessionHistory* sessions,
                EventLog* events,
                RTCManager* rtc,
                Buzzer* buzzer);

    // Essay de connexion STA (SSID/PASS depuis NVS). Retourne true si connecte.
    bool startSta_();

    // Demarrage du mode AP (SSID/PASS depuis NVS).
    void startAp_();

    // Declaration des routes HTTP (/api/*).
    void setupRoutes_();

    // Tache unique "worker" (housekeeping leger : RTC update, etc.).
    void startWorker_();
    static void workerTaskThunk_(void* param);
    void workerTask_();

    // Auth helper : verifie sans envoyer de reponse.
    bool checkAuth_(AsyncWebServerRequest* request);

    // Auth helper : force l'auth (401 + requestAuthentication()) si invalide.
    bool requireAuth_(AsyncWebServerRequest* request);

    // -------------------- Handlers API --------------------

    void handleApiInfo_(AsyncWebServerRequest* request);
    void handleApiStatus_(AsyncWebServerRequest* request);
    void handleApiHistory_(AsyncWebServerRequest* request);
    void handleApiEvents_(AsyncWebServerRequest* request);
    void handleApiConfigGet_(AsyncWebServerRequest* request);
    void handleApiConfigPost_(AsyncWebServerRequest* request, JsonVariant& json);
    void handleApiControl_(AsyncWebServerRequest* request, JsonVariant& json);
    void handleApiCalibrate_(AsyncWebServerRequest* request, JsonVariant& json);
    void handleApiRtc_(AsyncWebServerRequest* request, JsonVariant& json);
    void handleApiRunTimer_(AsyncWebServerRequest* request, JsonVariant& json);
    void handleApiSessions_(AsyncWebServerRequest* request);

    // Dependances (non possedees)
    Device* device_ = nullptr;
    DeviceTransport* transport_ = nullptr;
    BusSampler* sampler_ = nullptr;
    SessionHistory* sessions_ = nullptr;
    EventLog* events_ = nullptr;
    RTCManager* rtc_ = nullptr;
    Buzzer* buzzer_ = nullptr;

    AsyncWebServer server_{80};
    TaskHandle_t workerTaskHandle_ = nullptr;

    static WiFiManager* inst_;
};

#define WIFI WiFiManager::Get()

#endif // WIFI_MANAGER_H
