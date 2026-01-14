/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 **************************************************************/
#ifndef POWER_TRACKER_H
#define POWER_TRACKER_H

// -----------------------------------------------------------------------------
// PowerTracker
//
// Objectif :
// - Suivre la puissance/energie en RAM (aucune persistence NVS).
// - Chaque demarrage de session genere une entree persistante dans SPIFFS
//   (historique JSON).
//
// Note :
// - Les donnees "temps reel" restent en RAM et repartent a zero apres reboot.
// -----------------------------------------------------------------------------

#include <stdint.h>
#include <Utils.hpp>
#include <CurrentSensor.hpp>

// ----------------------------------------------------------------------------
// Historique sessions (SPIFFS)
// ----------------------------------------------------------------------------

#define POWERTRACKER_HISTORY_MAX   800
#define POWERTRACKER_HISTORY_FILE  "/History.json"

class PowerTracker {
public:
    struct SessionStats {
        bool      valid          = false;
        float     energy_Wh      = 0.0f;
        uint32_t  duration_s     = 0;      // secondes arrondies
        float     peakPower_W    = 0.0f;
        float     peakCurrent_A  = 0.0f;
    };

    struct HistoryEntry {
        bool        valid   = false;
        uint32_t    startMs = 0;   // millis() au debut de la session
        SessionStats stats;        // snapshot final des stats
    };

    // Acces singleton
    static PowerTracker* Get() {
        static PowerTracker instance;
        return &instance;
    }

    // Charge l'historique depuis SPIFFS (si present).
    void begin();

    // Demarre une nouvelle session.
    void startSession(float nominalBusV, float idleCurrentA);

    // Met a jour l'integration (energie) a partir du courant.
    void update(CurrentSensor& cs);

    // Termine la session et persiste les stats dans SPIFFS.
    //  success = true  => fin normale
    //          = false => interrompue/defaut (toujours loggee)
    void endSession(bool success);

    bool isSessionActive() const { return _active; }

    // Totaux en RAM (reset au reboot)
    float    getTotalEnergy_Wh()  const { return _totalEnergy_Wh; }
    uint32_t getTotalSessions()   const { return _totalSessions; }
    uint32_t getTotalSuccessful() const { return _totalSessionsOk; }

    // Derniere session terminee (RAM)
    const SessionStats& getLastSession() const { return _lastSession; }

    // Snapshot de la session en cours (non persiste)
    SessionStats getCurrentSessionSnapshot() const {
        SessionStats s;
        if (_active) {
            uint32_t now = millis();
            s.valid         = true;
            s.energy_Wh     = _sessionEnergy_Wh;
            s.duration_s    = (now - _startMs) / 1000U;
            s.peakPower_W   = _sessionPeakPower_W;
            s.peakCurrent_A = _sessionPeakCurrent_A;
        }
        return s;
    }

    // ------------------------------------------------------------------------
    // API historique (utilisee par WiFiManager /session_history, etc.)
    // indexFromNewest: 0 = plus recente, 1 = precedente, ...
    // ------------------------------------------------------------------------

    uint16_t getHistoryCount() const { return _historyCount; }

    bool getHistoryEntry(uint16_t indexFromNewest, HistoryEntry& out) const;

    // Optionnel : efface tout l'historique + supprime le fichier
    void clearHistory();

private:
    PowerTracker() = default;

    // Helpers historique (SPIFFS)
    void loadHistoryFromFile();
    bool saveHistoryToFile() const;
    void appendHistoryEntry(const HistoryEntry& e);

    // Etat session
    bool      _active            = false;
    uint32_t  _startMs           = 0;
    uint32_t  _lastSampleTsMs    = 0;
    uint32_t  _lastHistorySeq    = 0;
    uint32_t  _lastBusSeq        = 0;

    float     _nominalBusV       = 0.0f;
    float     _idleCurrentA      = 0.0f;

    float     _sessionEnergy_Wh  = 0.0f;
    float     _sessionPeakPower_W= 0.0f;
    float     _sessionPeakCurrent_A = 0.0f;

    // Totaux en RAM (non persistants)
    float     _totalEnergy_Wh    = 0.0f;
    uint32_t  _totalSessions     = 0;
    uint32_t  _totalSessionsOk   = 0;

    // Buffer circulaire des POWERTRACKER_HISTORY_MAX dernieres sessions
    HistoryEntry _history[POWERTRACKER_HISTORY_MAX];
    uint16_t     _historyHead  = 0;   // prochain index d'ecriture
    uint16_t     _historyCount = 0;   // nombre d'entrees valides

    // Dernier snapshot session (acces rapide)
    SessionStats _lastSession;
};

#define POWER_TRACKER PowerTracker::Get()

#endif // POWER_TRACKER_H


