#include <SessionHistory.hpp>
#include <FS.h>
#include <SPIFFS.h>

void SessionHistory::begin() {
    if (!mutex_) {
        // Mutex pour proteger entries_ / head_ / count_.
        mutex_ = xSemaphoreCreateMutex();
    }

    // Taille max et chemin fichier depuis NVS (avec fallback par defaut).
    maxEntries_ = static_cast<uint16_t>(CONF->GetUInt(KEY_SESS_MAX, DEFAULT_SESSION_MAX_ENTRIES));
    if (maxEntries_ == 0) maxEntries_ = DEFAULT_SESSION_MAX_ENTRIES;

    filePath_ = CONF->GetString(KEY_SPIFFS_SESS, DEFAULT_SPIFFS_SESS_FILE);
    if (filePath_.length() == 0) filePath_ = DEFAULT_SPIFFS_SESS_FILE;

    if (!entries_) {
        // Allocation RAM du ring buffer.
        entries_ = new Entry[maxEntries_];
    }

    // Recharge depuis SPIFFS (si present).
    loadFromFile_();
}

void SessionHistory::append(const Entry& e) {
    if (!entries_) return;

    if (lock_()) {
        // Ajout ring buffer
        entries_[head_] = e;
        head_ = (head_ + 1) % maxEntries_;
        if (count_ < maxEntries_) count_++;
        unlock_();
    }

    // Persistence complete du fichier JSON.
    saveToFile_();
}

uint16_t SessionHistory::getCount() const {
    uint16_t v = count_;
    if (lock_()) {
        v = count_;
        unlock_();
    }
    return v;
}

bool SessionHistory::getEntry(uint16_t indexFromNewest, Entry& out) const {
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

void SessionHistory::loadFromFile_() {
    // Format JSON :
    // {"sessions":[{"start_epoch":..,"end_epoch":..,"duration_s":..,"energy_wh":.., ...}, ...]}
    if (!SPIFFS.exists(filePath_)) return;

    File f = SPIFFS.open(filePath_, "r");
    if (!f) return;

    // Taille raisonnable pour ~200 sessions (depend du contenu).
    DynamicJsonDocument doc(16384);
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err) return;

    JsonArray arr = doc["sessions"].as<JsonArray>();
    if (arr.isNull()) return;

    // Recharge dans le ring buffer RAM.
    for (JsonObject obj : arr) {
        Entry e;
        e.start_epoch = obj["start_epoch"] | 0;
        e.end_epoch = obj["end_epoch"] | 0;
        e.duration_s = obj["duration_s"] | 0;
        e.energy_wh = obj["energy_wh"] | 0.0f;
        e.peak_power_w = obj["peak_power_w"] | 0.0f;
        e.peak_current_a = obj["peak_current_a"] | 0.0f;
        e.success = obj["success"] | false;
        e.last_error = obj["last_error"] | 0;

        // Ajout sans sauvegarde immediate (on sauvera uniquement lors de append()).
        entries_[head_] = e;
        head_ = (head_ + 1) % maxEntries_;
        if (count_ < maxEntries_) count_++;
    }
}

void SessionHistory::saveToFile_() const {
    if (!entries_) return;

    // Reecriture complete du fichier JSON (simple et robuste).
    File f = SPIFFS.open(filePath_, "w");
    if (!f) return;

    DynamicJsonDocument doc(16384);
    JsonArray arr = doc.createNestedArray("sessions");

    // Ecriture du plus ancien au plus recent
    uint16_t total = count_;
    uint16_t start = (count_ == maxEntries_) ? head_ : 0;

    for (uint16_t i = 0; i < total; ++i) {
        uint16_t idx = (start + i) % maxEntries_;
        const Entry& e = entries_[idx];
        JsonObject obj = arr.createNestedObject();
        obj["start_epoch"] = e.start_epoch;
        obj["end_epoch"] = e.end_epoch;
        obj["duration_s"] = e.duration_s;
        obj["energy_wh"] = e.energy_wh;
        obj["peak_power_w"] = e.peak_power_w;
        obj["peak_current_a"] = e.peak_current_a;
        obj["success"] = e.success;
        obj["last_error"] = e.last_error;
    }

    serializeJson(doc, f);
    f.close();
}

bool SessionHistory::lock_() const {
    if (!mutex_) return false;
    return xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) == pdTRUE;
}

void SessionHistory::unlock_() const {
    if (mutex_) xSemaphoreGive(mutex_);
}
