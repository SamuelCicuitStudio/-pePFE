#include <EventLog.hpp>
#include <FS.h>
#include <SPIFFS.h>

void EventLog::begin() {
    if (!mutex_) {
        // Mutex pour proteger entries_ / head_ / count_ / seq_ (tache + HTTP).
        mutex_ = xSemaphoreCreateMutex();
    }

    // Parametres persistants (NVS)
    maxEntries_ = static_cast<uint16_t>(CONF->GetUInt(KEY_EVENT_MAX, DEFAULT_EVENTLOG_MAX_ENTRIES));
    if (maxEntries_ == 0) maxEntries_ = DEFAULT_EVENTLOG_MAX_ENTRIES;

    filePath_ = CONF->GetString(KEY_SPIFFS_EVT, DEFAULT_SPIFFS_EVT_FILE);
    if (filePath_.length() == 0) filePath_ = DEFAULT_SPIFFS_EVT_FILE;

    if (!entries_) {
        // Allocation RAM du ring buffer (taille maxEntries_).
        entries_ = new Entry[maxEntries_];
    }

    // Recharge depuis SPIFFS (si le fichier existe).
    loadFromFile_();
}

void EventLog::append(EventLevel level, uint16_t code, const char* message, const char* source) {
    if (!entries_) return;

    // Construit une entree complete (copies dans des buffers fixes).
    Entry e;
    e.seq = ++seq_;
    e.ts_ms = millis();
    e.level = level;
    e.code = code;
    snprintf(e.message, sizeof(e.message), "%s", message ? message : "");
    snprintf(e.source, sizeof(e.source), "%s", source ? source : "");

    if (lock_()) {
        // Ecriture dans le ring buffer (head_ pointe la prochaine case a ecrire).
        entries_[head_] = e;
        head_ = (head_ + 1) % maxEntries_;
        if (count_ < maxEntries_) count_++;
        unlock_();
    }

    // Persistence "simple" : on reecrit le JSON entier.
    // Pour un petit log (200-500 entries), c'est acceptable et robuste.
    saveToFile_();
}

uint16_t EventLog::getCount() const {
    uint16_t v = count_;
    if (lock_()) {
        v = count_;
        unlock_();
    }
    return v;
}

bool EventLog::getEntry(uint16_t indexFromNewest, Entry& out) const {
    if (!entries_) return false;

    bool ok = false;
    if (lock_()) {
        if (indexFromNewest < count_) {
            // indexFromNewest=0 -> dernier element ecrit (head_-1).
            int idx = static_cast<int>(head_) - 1 - indexFromNewest;
            if (idx < 0) idx += maxEntries_;
            out = entries_[idx];
            ok = true;
        }
        unlock_();
    }
    return ok;
}

size_t EventLog::getSince(uint32_t sinceSeq, Entry* out, size_t maxOut, uint32_t& newSeq) const {
    // Renvoie les events dont e.seq > sinceSeq (polling UI).
    if (!out || maxOut == 0) {
        newSeq = sinceSeq;
        return 0;
    }

    if (!lock_()) {
        newSeq = sinceSeq;
        return 0;
    }

    size_t written = 0;
    uint32_t lastSeq = sinceSeq;

    // Parcours du plus ancien au plus recent
    uint16_t total = count_;
    uint16_t start = (count_ == maxEntries_) ? head_ : 0;

    for (uint16_t i = 0; i < total && written < maxOut; ++i) {
        uint16_t idx = (start + i) % maxEntries_;
        const Entry& e = entries_[idx];
        if (e.seq > sinceSeq) {
            out[written++] = e;
            lastSeq = e.seq;
        }
    }

    newSeq = lastSeq;
    unlock_();
    return written;
}

void EventLog::loadFromFile_() {
    // Format JSON :
    // {"events":[{"seq":..,"ts_ms":..,"level":..,"code":..,"message":"..","source":".."}, ...]}
    if (!SPIFFS.exists(filePath_)) return;

    File f = SPIFFS.open(filePath_, "r");
    if (!f) return;

    // Document JSON dynamique (ArduinoJson v7).
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err) return;

    JsonArray arr = doc["events"].as<JsonArray>();
    if (arr.isNull()) return;

    // Recharge dans le ring buffer RAM.
    for (JsonObject obj : arr) {
        Entry e;
        e.seq = obj["seq"] | 0;
        e.ts_ms = obj["ts_ms"] | 0;
        e.level = static_cast<EventLevel>((int)(obj["level"] | (int)EventLevel::Warning));
        e.code = obj["code"] | 0;
        const char* msg = obj["message"] | "";
        const char* src = obj["source"] | "";
        snprintf(e.message, sizeof(e.message), "%s", msg);
        snprintf(e.source, sizeof(e.source), "%s", src);

        entries_[head_] = e;
        head_ = (head_ + 1) % maxEntries_;
        if (count_ < maxEntries_) count_++;
        if (e.seq > seq_) seq_ = e.seq;
    }
}

void EventLog::saveToFile_() const {
    if (!entries_) return;

    // Reecriture complete du fichier JSON (simple et robuste).
    File f = SPIFFS.open(filePath_, "w");
    if (!f) return;

    JsonDocument doc;
    JsonArray arr = doc["events"].to<JsonArray>();

    // Ecriture du plus ancien au plus recent (ordre chronologique).
    uint16_t total = count_;
    uint16_t start = (count_ == maxEntries_) ? head_ : 0;

    for (uint16_t i = 0; i < total; ++i) {
        uint16_t idx = (start + i) % maxEntries_;
        const Entry& e = entries_[idx];
        JsonObject obj = arr.add<JsonObject>();
        obj["seq"] = e.seq;
        obj["ts_ms"] = e.ts_ms;
        obj["level"] = (int)e.level;
        obj["code"] = e.code;
        obj["message"] = e.message;
        obj["source"] = e.source;
    }

    serializeJson(doc, f);
    f.close();
}

bool EventLog::lock_() const {
    if (!mutex_) return false;
    return xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) == pdTRUE;
}

void EventLog::unlock_() const {
    if (mutex_) xSemaphoreGive(mutex_);
}
