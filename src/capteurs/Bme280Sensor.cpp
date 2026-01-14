#include <Bme280Sensor.hpp>

Bme280Sensor::Bme280Sensor(TwoWire* wire)
    : wire_(wire) {}

void Bme280Sensor::begin() {
    if (!mutex_) {
        // Mutex pour proteger lastTempC_/lastPressurePa_/present_
        mutex_ = xSemaphoreCreateMutex();
    }

    // Demarrage bus I2C avec les broches definies dans Config.h
    wire_->begin(PIN_I2C_SDA, PIN_I2C_SCL);
    present_ = tryBegin_();

    // Lecture initiale
    update();
}

bool Bme280Sensor::tryBegin_() {
    // Adresse la plus commune
    if (bme_.begin(0x76, wire_)) return true;
    // Adresse alternative
    return bme_.begin(0x77, wire_);
}

void Bme280Sensor::update() {
    // Si absent, on tente une re-detection (recuperation).
    if (!present_) {
        present_ = tryBegin_();
        if (!present_) return;
    }

    float t = bme_.readTemperature();
    float p = bme_.readPressure();

    // Validation simple (temperature/pression finies, pression > 0).
    bool ok = isfinite(t) && isfinite(p) && p > 0.0f;

    if (lock_()) {
        if (ok) {
            // Mise a jour du cache uniquement si valide.
            lastTempC_ = t;
            lastPressurePa_ = p;
            lastValid_ = true;
        } else {
            // Lecture invalide -> on garde les anciennes valeurs, mais on note invalid.
            lastValid_ = false;
        }
        unlock_();
    }

    if (!ok) {
        // On marque "absent" pour forcer une re-init au prochain update().
        present_ = false;
    }
}

float Bme280Sensor::getTempC(bool* valid) const {
    float v = lastTempC_;
    bool ok = lastValid_;
    if (lock_()) {
        v = lastTempC_;
        ok = lastValid_;
        unlock_();
    }
    if (valid) *valid = ok;
    return v;
}

float Bme280Sensor::getPressurePa(bool* valid) const {
    float v = lastPressurePa_;
    bool ok = lastValid_;
    if (lock_()) {
        v = lastPressurePa_;
        ok = lastValid_;
        unlock_();
    }
    if (valid) *valid = ok;
    return v;
}

bool Bme280Sensor::isPresent() const {
    bool p = present_;
    if (lock_()) {
        p = present_;
        unlock_();
    }
    return p;
}

bool Bme280Sensor::lock_() const {
    if (!mutex_) return false;
    return xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) == pdTRUE;
}

void Bme280Sensor::unlock_() const {
    if (mutex_) xSemaphoreGive(mutex_);
}
