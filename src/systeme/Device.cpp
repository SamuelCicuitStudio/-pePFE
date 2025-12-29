#include "systeme/Device.h"
#include <math.h>

Device* Device::inst_ = nullptr;

void Device::Init(Relay* relay,
                  StatusLeds* leds,
                  Buzzer* buzzer,
                  Acs712Sensor* current,
                  Ds18b20Sensor* ds18,
                  Bme280Sensor* bme,
                  BusSampler* sampler,
                  RTCManager* rtc,
                  SessionHistory* sessions,
                  EventLog* events) {
    // Injection des dependances (creee une seule fois).
    // On garde le pattern Singleton pour simplifier l'acces global
    // dans un firmware Arduino (pas de container de DI).
    if (!inst_) {
        inst_ = new Device(relay, leds, buzzer, current, ds18, bme, sampler, rtc, sessions, events);
    }
}

Device* Device::Get() {
    return inst_;
}

Device::Device(Relay* relay,
               StatusLeds* leds,
               Buzzer* buzzer,
               Acs712Sensor* current,
               Ds18b20Sensor* ds18,
               Bme280Sensor* bme,
               BusSampler* sampler,
               RTCManager* rtc,
               SessionHistory* sessions,
               EventLog* events)
    : relay_(relay),
      leds_(leds),
      buzzer_(buzzer),
      current_(current),
      ds18_(ds18),
      bme_(bme),
      sampler_(sampler),
      rtc_(rtc),
      sessions_(sessions),
      events_(events) {
    // Mutex pour proteger snapshot_ et certaines variables partagees.
    mutex_ = xSemaphoreCreateMutex();

    // File de commandes asynchrones (bouton + HTTP).
    // Les commandes sont traitees dans la tache controlTask_().
    cmdQueue_ = xQueueCreate(10, sizeof(Command));
}

void Device::begin() {
    // Charge tous les parametres persistants (NVS -> cache runtime)
    loadConfig_();

    // Etat initial securise
    applyRelay_(false);
    setState_(DeviceState::Idle);

    // Le sampler tourne en tache dediee (historique et donnees alignees).
    if (sampler_) {
        sampler_->start();
    }

    // Tache principale "control" : gestion commandes + securites + energie.
    if (!controlTaskHandle_) {
        xTaskCreate(controlTaskThunk_, "DeviceCtrl", 4096, this, 2, &controlTaskHandle_);
    }

}

void Device::loadConfig_() {
    // Toutes les cles sont definies dans Config.h (NVS keys courtes <= 6).
    // Ici on ramene les valeurs en RAM pour eviter des Get() a chaque cycle.
    limitCurrentA_ = CONF->GetFloat(KEY_LIM_CUR, DEFAULT_LIMIT_CURRENT_A);
    ovcMode_ = static_cast<OvcMode>(CONF->GetInt(KEY_OVC_MODE, static_cast<int>(OvcMode::Latch)));
    ovcMinMs_ = CONF->GetUInt(KEY_OVC_MIN, DEFAULT_OVC_MIN_DURATION_MS);
    ovcRetryMs_ = CONF->GetUInt(KEY_OVC_RTRY, DEFAULT_OVC_RETRY_DELAY_MS);

    tempMotorC_ = CONF->GetFloat(KEY_TEMP_MOTOR, DEFAULT_TEMP_MOTOR_C);
    tempBoardC_ = CONF->GetFloat(KEY_TEMP_BOARD, DEFAULT_TEMP_BOARD_C);
    tempAmbientC_ = CONF->GetFloat(KEY_TEMP_AMB, DEFAULT_TEMP_AMBIENT_C);
    tempHystC_ = CONF->GetFloat(KEY_TEMP_HYST, DEFAULT_TEMP_HYST_C);
    latchOvertemp_ = CONF->GetBool(KEY_LATCH_TEMP, DEFAULT_LATCH_OVERTEMP);

    motorVcc_ = CONF->GetFloat(KEY_MOTOR_VCC, DEFAULT_MOTOR_VCC_V);
}

bool Device::applyConfig(const ConfigUpdate& cfg) {
    // IMPORTANT :
    // - Device est le seul ecrivain NVS.
    // - On met a jour le cache runtime (variables *_ ) ET on persiste en NVS.
    // - Certaines MAJ declenchent des actions (ex: samplingHz -> reinit sampler).

    // MAJ locale + NVS (courant/OVC)
    if (cfg.hasLimitCurrent) {
        limitCurrentA_ = cfg.limitCurrentA;
        CONF->PutFloat(KEY_LIM_CUR, limitCurrentA_);
    }
    if (cfg.hasOvcMode) {
        ovcMode_ = cfg.ovcMode;
        CONF->PutInt(KEY_OVC_MODE, static_cast<int>(ovcMode_));
    }
    if (cfg.hasOvcMinMs) {
        ovcMinMs_ = cfg.ovcMinMs;
        CONF->PutUInt(KEY_OVC_MIN, ovcMinMs_);
    }
    if (cfg.hasOvcRetryMs) {
        ovcRetryMs_ = cfg.ovcRetryMs;
        CONF->PutUInt(KEY_OVC_RTRY, ovcRetryMs_);
    }

    // MAJ temperatures / surchauffe
    if (cfg.hasTempMotor) {
        tempMotorC_ = cfg.tempMotorC;
        CONF->PutFloat(KEY_TEMP_MOTOR, tempMotorC_);
    }
    if (cfg.hasTempBoard) {
        tempBoardC_ = cfg.tempBoardC;
        CONF->PutFloat(KEY_TEMP_BOARD, tempBoardC_);
    }
    if (cfg.hasTempAmbient) {
        tempAmbientC_ = cfg.tempAmbientC;
        CONF->PutFloat(KEY_TEMP_AMB, tempAmbientC_);
    }
    if (cfg.hasTempHyst) {
        tempHystC_ = cfg.tempHystC;
        CONF->PutFloat(KEY_TEMP_HYST, tempHystC_);
    }
    if (cfg.hasLatchOvertemp) {
        latchOvertemp_ = cfg.latchOvertemp;
        CONF->PutBool(KEY_LATCH_TEMP, latchOvertemp_);
    }

    // Parametres puissance
    if (cfg.hasMotorVcc) {
        motorVcc_ = cfg.motorVcc;
        CONF->PutFloat(KEY_MOTOR_VCC, motorVcc_);
    }

    // Sampler : si la frequence change, on recalcule la periode.
    if (cfg.hasSamplingHz) {
        CONF->PutUInt(KEY_SAMPLING_HZ, cfg.samplingHz);
        if (sampler_) {
            sampler_->begin(current_, ds18_, bme_, cfg.samplingHz);
            sampler_->start();
        }
    }

    // Activation buzzer (persistante en NVS deja geree par Buzzer::setEnabled)
    if (cfg.hasBuzzerEnabled && buzzer_) {
        buzzer_->setEnabled(cfg.buzzerEnabled);
    }

    // Wi-Fi : SSID/PASS et mode (STA/AP). Le demarrage/restart Wi-Fi est gere
    // par WiFiManager (ici on ne redemarre pas automatiquement le Wi-Fi).
    if (cfg.hasWifiSta) {
        CONF->PutString(KEY_STA_SSID, cfg.staSsid);
        CONF->PutString(KEY_STA_PASS, cfg.staPass);
    }
    if (cfg.hasWifiAp) {
        CONF->PutString(KEY_AP_SSID, cfg.apSsid);
        CONF->PutString(KEY_AP_PASS, cfg.apPass);
    }
    if (cfg.hasWifiMode) {
        CONF->PutInt(KEY_WIFI_MODE, static_cast<int>(cfg.wifiMode));
    }

    return true;
}

void Device::calibrateCurrentZero() {
    // Calibration "zero" : mesure le capteur ACS712 moteur arrete.
    if (current_) current_->calibrateZero();
}

void Device::setCurrentCalibration(float zeroMv, float sensMvPerA, float inputScale) {
    // Calibration avancee : ajuste offset + sensibilite + echelle analogique.
    // Les valeurs sont stockees par la classe capteur (NVS).
    if (current_) current_->setCalibration(zeroMv, sensMvPerA, inputScale);
}

void Device::notifyCommand() {
    // Petit blink qui indique qu'une commande a ete acceptee (UI/bouton).
    if (leds_) leds_->notifyCommand();
}

bool Device::submitCommand(const Command& cmd) {
    // Non-bloquant : si la file est pleine, on retourne false.
    if (!cmdQueue_) return false;
    return xQueueSendToBack(cmdQueue_, &cmd, 0) == pdTRUE;
}

bool Device::getSnapshot(SystemSnapshot& out) const {
    if (!lock_()) return false;

    // Copie "atomique" du snapshot cache.
    out = snapshot_;
    unlock_();

    // Age calcule a la demande
    out.age_ms = millis() - out.ts_ms;
    return true;
}

DeviceState Device::getState() const {
    DeviceState s = state_;
    if (lock_()) {
        s = state_;
        unlock_();
    }
    return s;
}

void Device::applyRelay_(bool on) {
    if (!relay_) return;

    // Action physique (GPIO) + persistance "last state" (utile au reboot).
    relay_->set(on);
    CONF->PutBool(KEY_RELAY_LAST, on);
}

void Device::setState_(DeviceState s) {
    if (lock_()) {
        state_ = s;
        unlock_();
    }
}

void Device::processCommands_() {
    // Consomme toutes les commandes en attente (boucle non-bloquante).
    if (!cmdQueue_) return;

    Command cmd;
    while (xQueueReceive(cmdQueue_, &cmd, 0) == pdTRUE) {
        switch (cmd.type) {
            case Command::Type::Start:
            case Command::Type::Toggle: {
                // Toggle : ON <-> OFF (comportement "bouton").
                if (state_ == DeviceState::Running) {
                    // Stop
                    applyRelay_(false);
                    endSession_(true);
                    setState_(DeviceState::Idle);
                } else {
                    // Start : si defaut latch, on l'acquitte ici (comportement
                    // demande : "lock until ON is pressed again").
                    if (faultLatched_) {
                        // Clear fault et rearmement
                        faultLatched_ = false;
                    }
                    applyRelay_(true);
                    startSession_();
                    setState_(DeviceState::Running);
                    if (cmd.type == Command::Type::Toggle && cmd.u32 > 0) {
                        // Option : toggle peut embarquer un timer (secondes).
                        runUntilMs_ = millis() + (cmd.u32 * 1000U);
                    }
                }
                break;
            }
            case Command::Type::Stop:
                // Arret immediat + fermeture de session.
                applyRelay_(false);
                endSession_(true);
                setState_(DeviceState::Idle);
                runUntilMs_ = 0;
                break;
            case Command::Type::ClearFault:
                // Re-armement manuel (sans demarrer le relais).
                faultLatched_ = false;
                setState_(DeviceState::Idle);
                break;
            case Command::Type::TimedRun:
                // Marche temporisee : ON pendant cmd.u32 secondes.
                if (faultLatched_) {
                    faultLatched_ = false;
                }
                applyRelay_(true);
                startSession_();
                setState_(DeviceState::Running);
                runUntilMs_ = millis() + (cmd.u32 * 1000U);
                break;
            case Command::Type::SetRelay:
                // Forcage direct (seulement si pas de defaut latch).
                if (!faultLatched_) {
                    applyRelay_(cmd.b);
                }
                break;
            case Command::Type::Reset:
                // Reset systeme demande par bouton long ou API.
                ESP.restart();
                break;
        }
    }
}

void Device::updateProtection_() {
    // Courant
    bool curValid = false;
    float currentA = current_ ? current_->getLastCurrent(&curValid) : 0.0f;
    lastCurrentA_ = currentA;

    // Puissance instantanee (on suppose Vcc moteur connu, stocke en NVS).
    lastPowerW_ = motorVcc_ * currentA;

    if (current_ && !current_->isAdcOk()) {
        // Diagnostic : saturation ADC (cablage, offset, echelle analogique, etc.)
        raiseWarning_(WarnCode::W03_AdcSat, "ADC saturation", "current");
    }

    // OVC
    // Strategie :
    // - Si |I| >= seuil, on demarre un timer.
    // - Si le depassement dure au moins ovcMinMs_, on declenche le defaut.
    // - En mode Latch : defaut memorise jusqu'a "ON" ou clearFault.
    // - En mode AutoRetry : on relache le latch apres un delai.
    if (fabsf(currentA) >= limitCurrentA_) {
        if (ovcStartMs_ == 0) {
            ovcStartMs_ = millis();
        } else if (millis() - ovcStartMs_ >= ovcMinMs_) {
            faultLatched_ = true;
            applyRelay_(false);
            setState_(DeviceState::Fault);
            raiseError_(ErrorCode::E01_OvcLatched, "OVC latch", "current");
            if (ovcMode_ == OvcMode::AutoRetry) {
                ovcRetryAtMs_ = millis() + ovcRetryMs_;
            }
        }
    } else {
        ovcStartMs_ = 0;
    }

    if (faultLatched_ && ovcMode_ == OvcMode::AutoRetry && ovcRetryAtMs_ > 0) {
        // Auto-reprise : on repasse Idle (relais reste OFF tant qu'une commande ON
        // n'est pas envoyee, selon l'usage).
        if (millis() >= ovcRetryAtMs_) {
            faultLatched_ = false;
            ovcRetryAtMs_ = 0;
            setState_(DeviceState::Idle);
        }
    }

    // Temperatures
    bool motorOk = false;
    bool bmeOk = false;

    float motorC = ds18_ ? ds18_->getTempC(&motorOk) : NAN;
    float boardC = bme_ ? bme_->getTempC(&bmeOk) : NAN;

    // Overtemp :
    // - DS18 -> temperature moteur
    // - BME  -> temperature carte (et eventuellement ambiante si on n'a qu'une sonde)
    bool over = false;
    if (motorOk && motorC >= tempMotorC_) over = true;
    if (bmeOk && (boardC >= tempBoardC_ || boardC >= tempAmbientC_)) over = true;

    if (over) {
        if (!overtempActive_) {
            overtempActive_ = true;
            if (leds_) leds_->setOvertemp(true);
        }
        if (latchOvertemp_) {
            // Surchauffe avec latch : defaut memorise jusqu'a acquittement.
            faultLatched_ = true;
            applyRelay_(false);
            setState_(DeviceState::Fault);
            raiseError_(ErrorCode::E02_OverTemp, "Overtemp", "temp");
        } else {
            // Mode "non latch" : on coupe le relais mais on ne memorise pas.
            applyRelay_(false);
        }
    } else if (overtempActive_) {
        // Hysteresis simple
        if ((motorOk && motorC <= (tempMotorC_ - tempHystC_)) ||
            (bmeOk && boardC <= (tempBoardC_ - tempHystC_))) {
            overtempActive_ = false;
            if (leds_) leds_->setOvertemp(false);
        }
    }

    // Warnings capteurs
    // Le but ici est de notifier l'UI qu'on est en "mode degrade" :
    // - capteur absent (missing)
    // - valeur invalide -> cache utilise
    if (ds18_ && !ds18_->isPresent()) {
        raiseWarning_(WarnCode::W01_Ds18Missing, "DS18 absent", "ds18");
    } else if (ds18_ && !motorOk) {
        raiseWarning_(WarnCode::W04_CacheUsed, "DS18 cache", "ds18");
    }

    if (bme_ && !bme_->isPresent()) {
        raiseWarning_(WarnCode::W02_BmeMissing, "BME absent", "bme");
    } else if (bme_ && !bmeOk) {
        raiseWarning_(WarnCode::W04_CacheUsed, "BME cache", "bme");
    }
}

void Device::updateEnergy_() {
    // Integration de l'energie uniquement quand le moteur tourne.
    if (state_ != DeviceState::Running) {
        lastEnergyMs_ = millis();
        return;
    }

    const uint32_t now = millis();
    const uint32_t dtMs = (lastEnergyMs_ == 0) ? 0 : (now - lastEnergyMs_);
    lastEnergyMs_ = now;

    if (dtMs == 0) return;

    const float powerW = lastPowerW_;
    const float dtS = dtMs / 1000.0f;

    // Wh = W * s / 3600
    energyWh_ += (powerW * dtS) / 3600.0f;

    // Pics (utiles pour l'historique sessions)
    if (fabsf(lastCurrentA_) > peakCurrentA_) peakCurrentA_ = fabsf(lastCurrentA_);
    if (fabsf(powerW) > peakPowerW_) peakPowerW_ = fabsf(powerW);
}

void Device::updateSnapshot_() {
    // Construit un snapshot local, puis on le copie sous mutex dans snapshot_.
    // Cela evite de bloquer le mutex pendant la lecture des capteurs.
    SystemSnapshot s;
    s.seq = snapshot_.seq + 1;
    s.ts_ms = millis();
    s.state = state_;
    s.fault_latched = faultLatched_;

    s.relay_on = relay_ ? relay_->isOn() : false;
    s.current_a = lastCurrentA_;
    s.power_w = lastPowerW_;
    s.energy_wh = energyWh_;

    bool motorOk = false;
    bool bmeOk = false;
    s.motor_c = ds18_ ? ds18_->getTempC(&motorOk) : NAN;
    s.board_c = bme_ ? bme_->getTempC(&bmeOk) : NAN;
    s.ambient_c = s.board_c;

    s.ds18_ok = (ds18_ && ds18_->isPresent() && motorOk);
    s.bme_ok = (bme_ && bme_->isPresent() && bmeOk);
    s.adc_ok = current_ ? current_->isAdcOk() : true;

    s.last_warning = lastWarningCode_;
    s.last_error = lastErrorCode_;

    if (lock_()) {
        snapshot_ = s;
        unlock_();
    }
}

void Device::startSession_() {
    // Debut d'une session (moteur ON).
    sessionActive_ = true;
    sessionStartMs_ = millis();
    sessionStartEpoch_ = rtc_ ? rtc_->getUnixTime() : 0;
    energyWh_ = 0.0f;
    peakPowerW_ = 0.0f;
    peakCurrentA_ = 0.0f;
    lastEnergyMs_ = millis();
}

void Device::endSession_(bool success) {
    // Termine la session et l'ajoute a l'historique SPIFFS.
    // success peut etre false si arret force / defaut (option future).
    if (!sessionActive_ || !sessions_) {
        sessionActive_ = false;
        return;
    }

    SessionHistory::Entry e;
    e.start_epoch = static_cast<uint32_t>(sessionStartEpoch_);
    e.end_epoch = static_cast<uint32_t>(rtc_ ? rtc_->getUnixTime() : 0);
    e.duration_s = (millis() - sessionStartMs_) / 1000U;
    e.energy_wh = energyWh_;
    e.peak_power_w = peakPowerW_;
    e.peak_current_a = peakCurrentA_;
    e.success = success;
    e.last_error = lastErrorCode_;

    sessions_->append(e);
    sessionActive_ = false;
}

void Device::raiseWarning_(WarnCode code, const char* msg, const char* src) {
    // Anti-spam : on ne repete pas le meme warning trop rapidement.
    const uint32_t now = millis();
    const uint16_t c = static_cast<uint16_t>(code);
    if (lastWarningCode_ == c && (now - lastWarnMs_) < 5000) {
        return;
    }
    lastWarningCode_ = c;
    lastWarnMs_ = now;
    if (events_) {
        events_->append(EventLevel::Warning, lastWarningCode_, msg, src);
    }
    if (leds_) {
        // La LED CMD affiche les alertes en "rafales rapides" (voir StatusLeds).
        leds_->enqueueAlert(EventLevel::Warning, lastWarningCode_);
    }
    if (buzzer_) {
        // Certains warnings ont des sons specifiques (web / auth / client).
        if (code == WarnCode::W07_AuthFail) {
            buzzer_->playAuthFail();
        } else if (code == WarnCode::W09_ClientGone) {
            buzzer_->playClientDisconnect();
        } else {
            buzzer_->playWarn();
        }
    }
}

void Device::raiseError_(ErrorCode code, const char* msg, const char* src) {
    // Anti-spam : idem erreurs (plus agressif car critique).
    const uint32_t now = millis();
    const uint16_t c = static_cast<uint16_t>(code);
    if (lastErrorCode_ == c && (now - lastErrMs_) < 2000) {
        return;
    }
    lastErrorCode_ = c;
    lastErrMs_ = now;
    if (events_) {
        events_->append(EventLevel::Error, lastErrorCode_, msg, src);
    }
    if (leds_) {
        leds_->enqueueAlert(EventLevel::Error, lastErrorCode_);
    }
    if (buzzer_) {
        // OVC + Overtemp sont consideres "latch" : pattern sonore specifique.
        if (code == ErrorCode::E01_OvcLatched || code == ErrorCode::E02_OverTemp) {
            buzzer_->playLatch();
        } else {
            buzzer_->playError();
        }
    }
}

void Device::controlTaskThunk_(void* param) {
    static_cast<Device*>(param)->controlTask_();
    vTaskDelete(nullptr);
}

void Device::controlTask_() {
    // Boucle temps reel "soft" : toutes les 50ms.
    // On garde ce cycle court pour reagir vite aux defauts.
    for (;;) {
        processCommands_();

        if (state_ == DeviceState::Running) {
            updateProtection_();
            updateEnergy_();

            if (runUntilMs_ > 0 && millis() >= runUntilMs_) {
                applyRelay_(false);
                endSession_(true);
                setState_(DeviceState::Idle);
                runUntilMs_ = 0;
            }
        } else {
            lastEnergyMs_ = millis();
        }

        // Snapshot integre dans la meme tache (pas de tache dediee).
        const uint32_t now = millis();
        if (lastSnapshotMs_ == 0 || (now - lastSnapshotMs_) >= DEFAULT_SNAPSHOT_PERIOD_MS) {
            updateSnapshot_();
            lastSnapshotMs_ = now;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

bool Device::lock_() const {
    if (!mutex_) return false;
    return xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) == pdTRUE;
}

void Device::unlock_() const {
    if (mutex_) xSemaphoreGive(mutex_);
}
