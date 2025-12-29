#include "capteurs/TempSensor.h"
#include <string.h>

// -----------------------------------------------------------------------------
// DS18B20 (OneWire "pur") :
//  - Pas de DallasTemperature (compatibilite).
//  - Lecture periodique avec cache de la derniere valeur valide.
//  - Reconnexion automatique si deconnexion detectee.
// -----------------------------------------------------------------------------

namespace {
    static constexpr uint8_t kCmdConvertT = 0x44;
    static constexpr uint8_t kCmdReadScratch = 0xBE;
    static constexpr uint32_t kConvertDelayMs = 750; // 12-bit
}

Ds18b20Sensor::Ds18b20Sensor(OneWire* bus)
    : oneWire_(bus) {}

void Ds18b20Sensor::begin(uint32_t periodMs) {
    if (!oneWire_) return;
    if (!mutex_) {
        // Mutex protege: present_ + lastTempC_ + lastValid_ + adresse
        mutex_ = xSemaphoreCreateMutex();
    }

    periodMs_ = (periodMs == 0) ? 1000 : periodMs;

    // Premiere detection
    discoverSensor_();

    if (!task_) {
        // Tache de lecture periodique (non-bloquante pour le reste du systeme)
        xTaskCreate(taskThunk_, "Ds18b20Task", 4096, this, 1, &task_);
    }

    // Lancer une premiere lecture
    update();
}

void Ds18b20Sensor::update() {
    if (!oneWire_) return;

    float tempC = NAN;
    bool ok = readTempOnce_(tempC);

    if (lock_()) {
        if (ok) {
            lastTempC_ = tempC;
            lastValid_ = true;
            badReadStreak_ = 0;
            present_ = true;
        } else {
            // On conserve la derniere valeur valide
            lastValid_ = false;
            if (badReadStreak_ < 255) badReadStreak_++;
            if (badReadStreak_ >= kBadReadThreshold) {
                present_ = false;
                hasAddress_ = false;
            }
        }
        unlock_();
    }

    if (!ok) {
        // Tentative de recuperation plus tard (re-scan OneWire)
        tryReconnect_();
    }
}

float Ds18b20Sensor::getTempC(bool* valid) const {
    float t = lastTempC_;
    bool v = lastValid_;
    if (lock_()) {
        t = lastTempC_;
        v = lastValid_;
        unlock_();
    }
    if (valid) *valid = v;
    return t;
}

bool Ds18b20Sensor::isPresent() const {
    bool p = present_;
    if (lock_()) {
        p = present_;
        unlock_();
    }
    return p;
}

bool Ds18b20Sensor::discoverSensor_() {
    if (!oneWire_) return false;

    uint8_t addr[8] = {0};
    bool found = false;

    oneWire_->reset_search();
    while (oneWire_->search(addr)) {
        // Verifie CRC et famille DS18*
        if (OneWire::crc8(addr, 7) != addr[7]) continue;
        uint8_t family = addr[0];
        if (family != 0x28 && family != 0x22 && family != 0x10) continue;

        if (lock_()) {
            memcpy(address_, addr, sizeof(address_));
            hasAddress_ = true;
            present_ = true;
            unlock_();
        }
        found = true;
        break;
    }

    if (!found && lock_()) {
        hasAddress_ = false;
        present_ = false;
        unlock_();
    }

    return found;
}

bool Ds18b20Sensor::readTempOnce_(float& outTempC) {
    if (!oneWire_) return false;

    uint8_t addr[8] = {0};
    bool haveAddr = false;
    if (lock_()) {
        haveAddr = hasAddress_;
        if (haveAddr) memcpy(addr, address_, sizeof(address_));
        unlock_();
    }
    if (!haveAddr) return false;

    // Conversion temperature (blocant, mais uniquement dans la tache capteur).
    if (!oneWire_->reset()) return false;
    oneWire_->select(addr);
    oneWire_->write(kCmdConvertT, 0);
    vTaskDelay(pdMS_TO_TICKS(kConvertDelayMs));

    // Lecture scratchpad (9 octets)
    if (!oneWire_->reset()) return false;
    oneWire_->select(addr);
    oneWire_->write(kCmdReadScratch);

    uint8_t data[9] = {0};
    for (uint8_t i = 0; i < 9; ++i) {
        data[i] = oneWire_->read();
    }
    if (OneWire::crc8(data, 8) != data[8]) {
        return false;
    }

    int16_t raw = (int16_t)((data[1] << 8) | data[0]);
    outTempC = static_cast<float>(raw) / 16.0f;
    return isTempValid_(outTempC);
}

void Ds18b20Sensor::tryReconnect_() {
    const uint32_t now = millis();
    if (now - lastReconnectMs_ < kReconnectIntervalMs) return;
    lastReconnectMs_ = now;

    // Re-scan du bus OneWire
    discoverSensor_();
}

bool Ds18b20Sensor::isTempValid_(float tempC) const {
    if (!isfinite(tempC)) return false;
    return (tempC >= -55.0f && tempC <= 125.0f);
}

void Ds18b20Sensor::taskThunk_(void* param) {
    auto* self = static_cast<Ds18b20Sensor*>(param);
    self->taskLoop_();
    vTaskDelete(nullptr);
}

void Ds18b20Sensor::taskLoop_() {
    for (;;) {
        const uint32_t startMs = millis();
        update();

        // Respecter la periode globale (conversion incluse).
        const uint32_t elapsedMs = millis() - startMs;
        uint32_t waitMs = (periodMs_ > elapsedMs) ? (periodMs_ - elapsedMs) : 10;
        vTaskDelay(pdMS_TO_TICKS(waitMs));
    }
}

bool Ds18b20Sensor::lock_() const {
    if (!mutex_) return false;
    return xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) == pdTRUE;
}

void Ds18b20Sensor::unlock_() const {
    if (mutex_) xSemaphoreGive(mutex_);
}
