/**************************************************************
 *  SessionHistory - stockage JSON dans SPIFFS
 *
 *  Objectif :
 *  - Conserver l'historique des "sessions" (periodes moteur ON) :
 *    duree, energie, pics, statut, etc.
 *  - Le stockage est independant du EventLog (warnings/erreurs).
 *
 *  Implementation :
 *  - Ring buffer en RAM (entries_) pour acces rapide.
 *  - Persistence en JSON dans SPIFFS (fichier /sessions.json par defaut).
 *
 *  Concurrence :
 *  - Mutex interne car acces depuis Device + HTTP.
 **************************************************************/
#ifndef SESSION_HISTORY_H
#define SESSION_HISTORY_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Config.hpp>
#include <NVSManager.hpp>

class SessionHistory {
public:
    struct Entry {
        // Horodatage absolu (epoch UNIX) si RTC valide, sinon 0.
        uint32_t start_epoch = 0;
        uint32_t end_epoch = 0;

        // Duree logique (secondes).
        uint32_t duration_s = 0;

        // Energie integree sur la session (Wh) et pics observes.
        float energy_wh = 0.0f;
        float peak_power_w = 0.0f;
        float peak_current_a = 0.0f;

        // success : true si arret normal, false si defaut / interruption.
        bool success = false;

        // Dernier code erreur associe (si success=false typiquement).
        uint16_t last_error = 0;
    };

    // Initialise la RAM + charge depuis SPIFFS.
    void begin();

    // Ajoute une session et persiste en SPIFFS.
    void append(const Entry& e);

    // Lecture (du plus recent au plus ancien).
    uint16_t getCount() const;
    bool getEntry(uint16_t indexFromNewest, Entry& out) const;

private:
    void loadFromFile_();
    void saveToFile_() const;

    // Mutex interne (thread-safe).
    bool lock_() const;
    void unlock_() const;

    Entry* entries_ = nullptr; // Ring buffer RAM
    uint16_t maxEntries_ = DEFAULT_SESSION_MAX_ENTRIES;
    uint16_t count_ = 0;
    uint16_t head_ = 0; // index prochaine ecriture

    String filePath_ = DEFAULT_SPIFFS_SESS_FILE;
    mutable SemaphoreHandle_t mutex_ = nullptr;
};

#endif // SESSION_HISTORY_H
