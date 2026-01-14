/**************************************************************
 *  EventLog - warnings/erreurs persistants
 *
 *  Objectif :
 *  - Conserver un journal des avertissements / erreurs dans SPIFFS
 *    (fichier JSON), separatement de l'historique de sessions.
 *  - L'UI peut "poll" /api/events?since=... pour recuperer uniquement
 *    les nouveaux evenements.
 *
 *  Structure :
 *  - On garde un ring buffer en RAM (entries_) pour acces rapide.
 *  - On persiste regulierement en JSON dans SPIFFS.
 *
 *  Concurrence :
 *  - Un mutex protege le ring buffer (tache Device + HTTP).
 **************************************************************/
#ifndef EVENT_LOG_H
#define EVENT_LOG_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Config.hpp>
#include <NVSManager.hpp>

class EventLog {
public:
    struct Entry {
        // seq : identifiant monotone (permet /api/events?since=seq)
        uint32_t seq = 0;
        // ts_ms : horodatage relatif (millis()). Pour un horodatage absolu,
        // l'UI peut aussi utiliser RTCManager si besoin.
        uint32_t ts_ms = 0;
        EventLevel level = EventLevel::Warning;
        uint16_t code = 0;
        char message[64] = {0};
        char source[16] = {0};
    };

    // Initialise la RAM (ring buffer) et charge depuis SPIFFS.
    void begin();

    // Ajoute un evenement et persiste en SPIFFS.
    void append(EventLevel level, uint16_t code, const char* message, const char* source);

    // Lecture "liste" (du plus recent au plus ancien).
    uint16_t getCount() const;
    bool getEntry(uint16_t indexFromNewest, Entry& out) const;

    // Lecture "stream" : renvoie les evenements dont seq > sinceSeq.
    // newSeq = dernier seq renvoye (a reutiliser au prochain appel).
    size_t getSince(uint32_t sinceSeq, Entry* out, size_t maxOut, uint32_t& newSeq) const;

private:
    void loadFromFile_();
    void saveToFile_() const;

    // Mutex interne (thread-safe).
    bool lock_() const;
    void unlock_() const;

    Entry* entries_ = nullptr; // Ring buffer RAM
    uint16_t maxEntries_ = DEFAULT_EVENTLOG_MAX_ENTRIES;
    uint16_t count_ = 0;
    uint16_t head_ = 0;
    uint32_t seq_ = 0; // seq global monotone

    String filePath_ = DEFAULT_SPIFFS_EVT_FILE;
    mutable SemaphoreHandle_t mutex_ = nullptr;
};

#endif // EVENT_LOG_H
