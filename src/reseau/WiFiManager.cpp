#include <WiFiManager.hpp>
#include <Utils.hpp>
#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <SPIFFS.h>
#include <WiFiEndpoints.hpp>

WiFiManager* WiFiManager::inst_ = nullptr;

void WiFiManager::Init(SessionHistory* sessions,
                       EventLog* events,
                       RTCManager* rtc) {
    // Singleton : injection des dependances une seule fois.
    if (!inst_) {
        inst_ = new WiFiManager(sessions, events, rtc);
    }
}

WiFiManager* WiFiManager::Get() {
    return inst_;
}

WiFiManager::WiFiManager(SessionHistory* sessions,
                         EventLog* events,
                         RTCManager* rtc)
    : sessions_(sessions),
      events_(events),
      rtc_(rtc) {}

// Convertit l'etat enum en string stable pour l'UI.
static const char* stateName_(DeviceState s) {
    switch (s) {
        case DeviceState::Off: return "Off";
        case DeviceState::Idle: return "Idle";
        case DeviceState::Running: return "Running";
        case DeviceState::Fault: return "Fault";
        case DeviceState::Shutdown: return "Shutdown";
        default: return "Unknown";
    }
}

void WiFiManager::begin() {
    // Demarrage Wi-Fi :
    // - Tentative STA en premier
    // - Fallback AP si echec de connexion
    const WiFiModeSetting mode = static_cast<WiFiModeSetting>(
        CONF->GetInt(KEY_WIFI_MODE, static_cast<int>(WiFiModeSetting::Sta)));

    bool staOk = false;
    if (mode == WiFiModeSetting::Ap) {
        // Mode force AP (utile quand on veut rester en point d'acces).
        startAp_();
    } else {
        // Mode STA -> fallback AP si echec.
        staOk = startSta_();
        if (!staOk) {
            startAp_();
        }
    }

    // mDNS : permet d'acceder a http://contro.local
    if (MDNS.begin(MDNS_HOSTNAME)) {
        MDNS.addService("http", "tcp", 80);
    }

    // Routes / API HTTP
    setupRoutes_();
    server_.begin();

    // Worker unique (housekeeping).
    startWorker_();

}

bool WiFiManager::startSta_() {
    // Lecture identifiants depuis NVS
    String ssid = CONF->GetString(KEY_STA_SSID, DEFAULT_STA_SSID);
    String pass = CONF->GetString(KEY_STA_PASS, DEFAULT_STA_PASS);

    if (ssid.length() == 0) {
        return false;
    }

    // Connexion STA avec timeout (evite de rester bloque si SSID absent).
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());

    const uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > 12000) {
            return false;
        }
        delay(200);
    }

    return true;
}

void WiFiManager::startAp_() {
    // Lecture identifiants AP depuis NVS.
    String apSsid = CONF->GetString(KEY_AP_SSID, DEFAULT_AP_SSID);
    String apPass = CONF->GetString(KEY_AP_PASS, DEFAULT_AP_PASS);

    WiFi.mode(WIFI_AP);
    WiFi.softAP(apSsid.c_str(), apPass.c_str());
}

bool WiFiManager::checkAuth_(AsyncWebServerRequest* request) {
    // Mode auth configurable :
    // - "basic" : user/pass (HTTP basic auth)
    // - "token" : header X-Auth-Token
    String mode = CONF->GetString(KEY_AUTH_MODE, DEFAULT_AUTH_MODE);
    mode.toLowerCase();

    if (mode == "basic") {
        String user = CONF->GetString(KEY_AUTH_USER, DEFAULT_AUTH_USER);
        String pass = CONF->GetString(KEY_AUTH_PASS, DEFAULT_AUTH_PASS);
        return request->authenticate(user.c_str(), pass.c_str());
    }

    if (mode == "token") {
        String token = CONF->GetString(KEY_AUTH_TOKEN, "");
        String hdr = request->getHeader(HDR_AUTH_TOKEN)
                       ? request->getHeader(HDR_AUTH_TOKEN)->value()
                       : "";
        return token.length() > 0 && hdr == token;
    }

    // Mode inconnu -> refuse
    return false;
}

bool WiFiManager::requireAuth_(AsyncWebServerRequest* request) {
    // Si auth OK, on autorise l'operation.
    if (checkAuth_(request)) return true;

    // Sinon : log + bip, puis on declenche la demande d'authentification.
    if (events_) {
        events_->append(EventLevel::Warning,
                        static_cast<uint16_t>(WarnCode::W07_AuthFail),
                        "Auth fail",
                        "http");
    }
    BUZZ->playAuthFail();

    request->requestAuthentication();
    return false;
}

void WiFiManager::setupRoutes_() {
    // UI statique (SPIFFS) : index.html + app.css + app.js + assets
    server_.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");

    // API "open" (pas d'auth) : infos et statut live.
    server_.on(EP_API_INFO, HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleApiInfo_(request);
    });

    server_.on(EP_API_STATUS, HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleApiStatus_(request);
    });

    server_.on(EP_API_HISTORY, HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleApiHistory_(request);
    });

    server_.on(EP_API_EVENTS, HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleApiEvents_(request);
    });

    server_.on(EP_API_CONFIG, HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleApiConfigGet_(request);
    });

    // API protegee (auth obligatoire)
    auto* configHandler = new AsyncCallbackJsonWebHandler(EP_API_CONFIG,
        [this](AsyncWebServerRequest* request, JsonVariant& json) {
            if (!requireAuth_(request)) return;
            handleApiConfigPost_(request, json);
        });
    server_.addHandler(configHandler);

    auto* controlHandler = new AsyncCallbackJsonWebHandler(EP_API_CONTROL,
        [this](AsyncWebServerRequest* request, JsonVariant& json) {
            if (!requireAuth_(request)) return;
            handleApiControl_(request, json);
        });
    server_.addHandler(controlHandler);

    auto* calibHandler = new AsyncCallbackJsonWebHandler(EP_API_CALIBRATE,
        [this](AsyncWebServerRequest* request, JsonVariant& json) {
            if (!requireAuth_(request)) return;
            handleApiCalibrate_(request, json);
        });
    server_.addHandler(calibHandler);

    auto* rtcHandler = new AsyncCallbackJsonWebHandler(EP_API_RTC,
        [this](AsyncWebServerRequest* request, JsonVariant& json) {
            if (!requireAuth_(request)) return;
            handleApiRtc_(request, json);
        });
    server_.addHandler(rtcHandler);

    auto* runHandler = new AsyncCallbackJsonWebHandler(EP_API_RUN_TIMER,
        [this](AsyncWebServerRequest* request, JsonVariant& json) {
            if (!requireAuth_(request)) return;
            handleApiRunTimer_(request, json);
        });
    server_.addHandler(runHandler);

    server_.on(EP_API_SESSIONS, HTTP_GET, [this](AsyncWebServerRequest* request) {
        handleApiSessions_(request);
    });
}

void WiFiManager::handleApiInfo_(AsyncWebServerRequest* request) {
    // Endpoint simple : infos device, version, ip, mdns.
    DynamicJsonDocument doc(512);
    doc["device_id"] = CONF->GetString(KEY_DEV_ID, "");
    doc["device_name"] = CONF->GetString(KEY_DEV_NAME, "");
    doc["sw"] = CONF->GetString(KEY_DEV_SW, DEVICE_SW_VERSION);
    doc["hw"] = CONF->GetString(KEY_DEV_HW, DEVICE_HW_VERSION);

    doc["mdns"] = String(MDNS_HOSTNAME) + ".local";
    doc["ip"] = WiFi.isConnected() ? WiFi.localIP().toString() : WiFi.softAPIP().toString();

    String out;
    serializeJson(doc, out);
    request->send(200, CT_APP_JSON, out);
}

void WiFiManager::startWorker_() {
    if (workerTaskHandle_) return;
    xTaskCreate(workerTaskThunk_, "WiFiWorker", 4096, this, 1, &workerTaskHandle_);
}

void WiFiManager::workerTaskThunk_(void* param) {
    static_cast<WiFiManager*>(param)->workerTask_();
    vTaskDelete(nullptr);
}

void WiFiManager::workerTask_() {
    // Tache unique: mise a jour RTC (string cache) + autres petites actions.
    for (;;) {
        if (rtc_) rtc_->update();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void WiFiManager::handleApiStatus_(AsyncWebServerRequest* request) {
    // Snapshot centralise (coherent) -> JSON pour l'UI.
    SystemSnapshot snap{};
    DeviceTransport* transport = DEVTRAN;
    if (!transport || !transport->getSnapshot(snap)) {
        request->send(503, CT_APP_JSON, "{\"error\":\"no_snapshot\"}");
        return;
    }

    DynamicJsonDocument doc(512);
    doc["seq"] = snap.seq;
    doc["ts_ms"] = snap.ts_ms;
    doc["age_ms"] = snap.age_ms;
    doc["state"] = stateName_(snap.state);
    doc["fault_latched"] = snap.fault_latched;

    doc["relay_on"] = snap.relay_on;
    doc["current_a"] = snap.current_a;
    doc["power_w"] = snap.power_w;
    doc["energy_wh"] = snap.energy_wh;

    doc["motor_c"] = snap.motor_c;
    doc["board_c"] = snap.board_c;
    doc["ambient_c"] = snap.ambient_c;

    doc["ds18_ok"] = snap.ds18_ok;
    doc["bme_ok"] = snap.bme_ok;
    doc["adc_ok"] = snap.adc_ok;

    doc["last_warning"] = snap.last_warning;
    doc["last_error"] = snap.last_error;

    String out;
    serializeJson(doc, out);
    request->send(200, CT_APP_JSON, out);
}

void WiFiManager::handleApiHistory_(AsyncWebServerRequest* request) {
    // Historique de mesures (800 max en RAM, on renvoie une fenetre).
    if (!BUS_SAMPLER) {
        request->send(503, CT_APP_JSON, "{\"error\":\"no_sampler\"}");
        return;
    }

    uint32_t since = 0;
    uint32_t maxN = 50;
    if (request->hasParam("since")) since = request->getParam("since")->value().toInt();
    if (request->hasParam("max")) maxN = request->getParam("max")->value().toInt();
    if (maxN > 200) maxN = 200;

    // Note memoire : allocation temporaire (max 200 samples).
    BusSampler::Sample* buf = new BusSampler::Sample[maxN];
    uint32_t newSeq = since;
    size_t n = BUS_SAMPLER->getHistorySince(since, buf, maxN, newSeq);

    // Estimation capacity JSON (evite un doc trop petit).
    const size_t cap = 512 + (maxN * 64);
    DynamicJsonDocument doc(cap);
    JsonArray arr = doc.createNestedArray("samples");
    for (size_t i = 0; i < n; ++i) {
        JsonObject o = arr.createNestedObject();
        o["ts_ms"] = buf[i].ts_ms;
        o["current_a"] = buf[i].current_a;
        o["motor_c"] = buf[i].motor_c;
        o["bme_c"] = buf[i].bme_c;
        o["bme_pa"] = buf[i].bme_pa;
    }
    doc["seq_end"] = newSeq;

    String out;
    serializeJson(doc, out);
    request->send(200, CT_APP_JSON, out);
    delete[] buf;
}

void WiFiManager::handleApiEvents_(AsyncWebServerRequest* request) {
    // Flux d'evenements (warnings/erreurs) pour l'UI.
    if (!events_) {
        request->send(503, CT_APP_JSON, "{\"error\":\"no_events\"}");
        return;
    }

    uint32_t since = 0;
    uint32_t maxN = 50;
    if (request->hasParam("since")) since = request->getParam("since")->value().toInt();
    if (request->hasParam("max")) maxN = request->getParam("max")->value().toInt();
    if (maxN > 200) maxN = 200;

    // Allocation temporaire (max 200 events).
    EventLog::Entry* buf = new EventLog::Entry[maxN];
    uint32_t newSeq = since;
    size_t n = events_->getSince(since, buf, maxN, newSeq);

    const size_t cap = 512 + (maxN * 96);
    DynamicJsonDocument doc(cap);
    JsonArray arr = doc.createNestedArray("events");
    for (size_t i = 0; i < n; ++i) {
        JsonObject o = arr.createNestedObject();
        o["seq"] = buf[i].seq;
        o["ts_ms"] = buf[i].ts_ms;
        o["level"] = (int)buf[i].level;
        o["code"] = buf[i].code;
        o["message"] = buf[i].message;
        o["source"] = buf[i].source;
    }
    doc["seq_end"] = newSeq;

    String out;
    serializeJson(doc, out);
    request->send(200, CT_APP_JSON, out);
    delete[] buf;
}

void WiFiManager::handleApiConfigGet_(AsyncWebServerRequest* request) {
    // Retourne un miroir de la config persistante (NVS) utile pour UI.
    DynamicJsonDocument doc(512);

    doc["limit_current_a"] = CONF->GetFloat(KEY_LIM_CUR, DEFAULT_LIMIT_CURRENT_A);
    doc["ovc_mode"] = CONF->GetInt(KEY_OVC_MODE, 0);
    doc["ovc_min_ms"] = CONF->GetUInt(KEY_OVC_MIN, DEFAULT_OVC_MIN_DURATION_MS);
    doc["ovc_retry_ms"] = CONF->GetUInt(KEY_OVC_RTRY, DEFAULT_OVC_RETRY_DELAY_MS);

    doc["temp_motor_c"] = CONF->GetFloat(KEY_TEMP_MOTOR, DEFAULT_TEMP_MOTOR_C);
    doc["temp_board_c"] = CONF->GetFloat(KEY_TEMP_BOARD, DEFAULT_TEMP_BOARD_C);
    doc["temp_ambient_c"] = CONF->GetFloat(KEY_TEMP_AMB, DEFAULT_TEMP_AMBIENT_C);
    doc["temp_hyst_c"] = CONF->GetFloat(KEY_TEMP_HYST, DEFAULT_TEMP_HYST_C);
    doc["latch_overtemp"] = CONF->GetBool(KEY_LATCH_TEMP, DEFAULT_LATCH_OVERTEMP);

    doc["motor_vcc_v"] = CONF->GetFloat(KEY_MOTOR_VCC, DEFAULT_MOTOR_VCC_V);
    doc["sampling_hz"] = CONF->GetUInt(KEY_SAMPLING_HZ, DEFAULT_SAMPLING_HZ);
    doc["buzzer_enabled"] = CONF->GetBool(KEY_BUZZ_EN, DEFAULT_BUZZER_ENABLED);
    doc["current_zero_mv"] = CONF->GetFloat(KEY_CUR_ZERO, DEFAULT_CURRENT_ZERO_MV);
    doc["current_sens_mv_a"] = CONF->GetFloat(KEY_CUR_SENS, DEFAULT_CURRENT_SENS_MV_A);
    doc["current_input_scale"] = CONF->GetFloat(KEY_CUR_SCALE, DEFAULT_CURRENT_INPUT_SCALE);

    doc["wifi_mode"] = CONF->GetInt(KEY_WIFI_MODE, 0);
    doc["sta_ssid"] = CONF->GetString(KEY_STA_SSID, "");
    doc["ap_ssid"] = CONF->GetString(KEY_AP_SSID, DEFAULT_AP_SSID);

    String out;
    serializeJson(doc, out);
    request->send(200, CT_APP_JSON, out);
}

void WiFiManager::handleApiConfigPost_(AsyncWebServerRequest* request, JsonVariant& json) {
    // Applique une mise a jour partielle de config.
    // Seul Device ecrit la NVS : on appelle device->applyConfig().
    Device* device = DEVICE;
    if (!device) {
        request->send(500, CT_APP_JSON, "{\"error\":\"no_device\"}");
        return;
    }

    Device::ConfigUpdate cfg;
    JsonObject obj = json.as<JsonObject>();

    if (obj.containsKey("limit_current_a")) {
        cfg.hasLimitCurrent = true;
        cfg.limitCurrentA = obj["limit_current_a"].as<float>();
    }
    if (obj.containsKey("ovc_mode")) {
        cfg.hasOvcMode = true;
        String mode = obj["ovc_mode"].as<String>();
        mode.toLowerCase();
        cfg.ovcMode = (mode == "auto") ? OvcMode::AutoRetry : OvcMode::Latch;
    }
    if (obj.containsKey("ovc_min_ms")) {
        cfg.hasOvcMinMs = true;
        cfg.ovcMinMs = obj["ovc_min_ms"].as<uint32_t>();
    }
    if (obj.containsKey("ovc_retry_ms")) {
        cfg.hasOvcRetryMs = true;
        cfg.ovcRetryMs = obj["ovc_retry_ms"].as<uint32_t>();
    }

    if (obj.containsKey("temp_motor_c")) {
        cfg.hasTempMotor = true;
        cfg.tempMotorC = obj["temp_motor_c"].as<float>();
    }
    if (obj.containsKey("temp_board_c")) {
        cfg.hasTempBoard = true;
        cfg.tempBoardC = obj["temp_board_c"].as<float>();
    }
    if (obj.containsKey("temp_ambient_c")) {
        cfg.hasTempAmbient = true;
        cfg.tempAmbientC = obj["temp_ambient_c"].as<float>();
    }
    if (obj.containsKey("temp_hyst_c")) {
        cfg.hasTempHyst = true;
        cfg.tempHystC = obj["temp_hyst_c"].as<float>();
    }
    if (obj.containsKey("latch_overtemp")) {
        cfg.hasLatchOvertemp = true;
        cfg.latchOvertemp = obj["latch_overtemp"].as<bool>();
    }

    if (obj.containsKey("motor_vcc_v")) {
        cfg.hasMotorVcc = true;
        cfg.motorVcc = obj["motor_vcc_v"].as<float>();
    }
    if (obj.containsKey("sampling_hz")) {
        cfg.hasSamplingHz = true;
        cfg.samplingHz = obj["sampling_hz"].as<uint32_t>();
    }
    if (obj.containsKey("buzzer_enabled")) {
        cfg.hasBuzzerEnabled = true;
        cfg.buzzerEnabled = obj["buzzer_enabled"].as<bool>();
    }

    if (obj.containsKey("sta_ssid") || obj.containsKey("sta_pass")) {
        cfg.hasWifiSta = true;
        cfg.staSsid = obj.containsKey("sta_ssid")
                        ? obj["sta_ssid"].as<String>()
                        : CONF->GetString(KEY_STA_SSID, DEFAULT_STA_SSID);
        cfg.staPass = obj.containsKey("sta_pass")
                        ? obj["sta_pass"].as<String>()
                        : CONF->GetString(KEY_STA_PASS, DEFAULT_STA_PASS);
    }
    if (obj.containsKey("ap_ssid") || obj.containsKey("ap_pass")) {
        cfg.hasWifiAp = true;
        cfg.apSsid = obj.containsKey("ap_ssid")
                        ? obj["ap_ssid"].as<String>()
                        : CONF->GetString(KEY_AP_SSID, DEFAULT_AP_SSID);
        cfg.apPass = obj.containsKey("ap_pass")
                        ? obj["ap_pass"].as<String>()
                        : CONF->GetString(KEY_AP_PASS, DEFAULT_AP_PASS);
    }
    if (obj.containsKey("wifi_mode")) {
        cfg.hasWifiMode = true;
        String mode = obj["wifi_mode"].as<String>();
        mode.toLowerCase();
        cfg.wifiMode = (mode == "ap") ? WiFiModeSetting::Ap : WiFiModeSetting::Sta;
    }

    device->applyConfig(cfg);
    device->notifyCommand();

    request->send(200, CT_APP_JSON, "{\"ok\":true}");
}

void WiFiManager::handleApiControl_(AsyncWebServerRequest* request, JsonVariant& json) {
    // Controle runtime (start/stop/relay/clear_fault).
    JsonObject obj = json.as<JsonObject>();
    String action = obj["action"] | "";
    action.toLowerCase();
    DEBUG_PRINTLN(String("[HTTP] /api/control action: ") + action);

    bool ok = false;
    const bool isNoop = (action == "noop");
    DeviceTransport* transport = DEVTRAN;
    if (transport) {
        if (action == "relay_on") ok = transport->setRelay(true);
        else if (action == "relay_off") ok = transport->setRelay(false);
        else if (action == "start") ok = transport->start();
        else if (action == "stop") ok = transport->stop();
        else if (action == "clear_fault") ok = transport->clearFault();
        else if (action == "reset") ok = true;
    }
    if (isNoop) ok = true;

    if (ok && DEVICE && !isNoop && action != "reset") DEVICE->notifyCommand();
    request->send(200, CT_APP_JSON, ok ? "{\"ok\":true}" : "{\"ok\":false}");

    if (ok && action == "reset") {
        CONF->RestartSysDelay(1000);
    }
}

void WiFiManager::handleApiCalibrate_(AsyncWebServerRequest* request, JsonVariant& json) {
    // Calibration capteur courant (zero + parametres).
    JsonObject obj = json.as<JsonObject>();
    String action = obj["action"] | "";
    action.toLowerCase();

    if (action == "current_zero") {
        if (DEVICE) DEVICE->calibrateCurrentZero();
        if (DEVICE) DEVICE->notifyCommand();
        request->send(200, CT_APP_JSON, "{\"ok\":true}");
        return;
    }

    if (action == "current_sensitivity") {
        float zeroMv = obj["zero_mv"] | DEFAULT_CURRENT_ZERO_MV;
        float sensMv = obj["sens_mv_a"] | DEFAULT_CURRENT_SENS_MV_A;
        float scale = obj["input_scale"] | DEFAULT_CURRENT_INPUT_SCALE;
        if (DEVICE) DEVICE->setCurrentCalibration(zeroMv, sensMv, scale);
        if (DEVICE) DEVICE->notifyCommand();
        request->send(200, CT_APP_JSON, "{\"ok\":true}");
        return;
    }

    request->send(400, CT_APP_JSON, "{\"error\":\"invalid_action\"}");
}

void WiFiManager::handleApiRtc_(AsyncWebServerRequest* request, JsonVariant& json) {
    // Reglage RTC : soit epoch direct, soit date/heure (Y/M/D H:M:S).
    if (!rtc_) {
        request->send(500, CT_APP_JSON, "{\"error\":\"no_rtc\"}");
        return;
    }

    JsonObject obj = json.as<JsonObject>();
    if (obj.containsKey("epoch")) {
        uint64_t epoch = obj["epoch"].as<uint64_t>();
        rtc_->setUnixTime(epoch);
        if (DEVICE) DEVICE->notifyCommand();
        request->send(200, CT_APP_JSON, "{\"ok\":true}");
        return;
    }

    int year = obj["year"] | 0;
    int month = obj["month"] | 0;
    int day = obj["day"] | 0;
    int hour = obj["hour"] | 0;
    int minute = obj["minute"] | 0;
    int second = obj["second"] | 0;

    if (year > 0) {
        rtc_->setRTCTime(year, month, day, hour, minute, second);
        if (DEVICE) DEVICE->notifyCommand();
        request->send(200, CT_APP_JSON, "{\"ok\":true}");
        return;
    }

    request->send(400, CT_APP_JSON, "{\"error\":\"invalid_rtc\"}");
}

void WiFiManager::handleApiRunTimer_(AsyncWebServerRequest* request, JsonVariant& json) {
    // Marche temporisee (secondes).
    uint32_t seconds = json["seconds"] | 0;
    DeviceTransport* transport = DEVTRAN;
    bool ok = transport ? transport->timedRun(seconds) : false;
    if (ok && DEVICE) DEVICE->notifyCommand();
    request->send(200, CT_APP_JSON, ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

void WiFiManager::handleApiSessions_(AsyncWebServerRequest* request) {
    // Historique des sessions (SPIFFS -> RAM -> JSON).
    if (!sessions_) {
        request->send(503, CT_APP_JSON, "{\"error\":\"no_sessions\"}");
        return;
    }

    const uint16_t count = sessions_->getCount();
    const size_t cap = 512 + (count * 96);
    DynamicJsonDocument doc(cap);
    JsonArray arr = doc.createNestedArray("sessions");

    for (uint16_t i = 0; i < count; ++i) {
        SessionHistory::Entry e;
        if (!sessions_->getEntry(i, e)) continue;
        JsonObject o = arr.createNestedObject();
        o["start_epoch"] = e.start_epoch;
        o["end_epoch"] = e.end_epoch;
        o["duration_s"] = e.duration_s;
        o["energy_wh"] = e.energy_wh;
        o["peak_power_w"] = e.peak_power_w;
        o["peak_current_a"] = e.peak_current_a;
        o["success"] = e.success;
        o["last_error"] = e.last_error;
    }

    String out;
    serializeJson(doc, out);
    request->send(200, CT_APP_JSON, out);
}
