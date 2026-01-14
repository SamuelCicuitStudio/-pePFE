#include <CurrentSensor.hpp>

Acs712Sensor::Acs712Sensor() = default;

void Acs712Sensor::begin() {
    // Pin ADC en entree (ADC1/ADC2 selon mapping ESP32-S3)
    pinMode(PIN_CURRENT_ADC, INPUT);
    if (!mutex_) {
        // Mutex protege: calibration + cache courant
        mutex_ = xSemaphoreCreateMutex();
    }

    // Charger calibration depuis NVS
    zeroMv_ = CONF->GetFloat(KEY_CUR_ZERO, DEFAULT_CURRENT_ZERO_MV);
    sensMvPerA_ = CONF->GetFloat(KEY_CUR_SENS, DEFAULT_CURRENT_SENS_MV_A);
    inputScale_ = CONF->GetFloat(KEY_CUR_SCALE, DEFAULT_CURRENT_INPUT_SCALE);
    adcRefV_ = CONF->GetFloat(KEY_ADC_REF, DEFAULT_ADC_REF_V);
    adcMax_ = CONF->GetInt(KEY_ADC_MAX, DEFAULT_ADC_MAX);

    // Securites simples
    if (inputScale_ <= 0.0f) inputScale_ = 1.0f;
    if (adcMax_ <= 0) adcMax_ = DEFAULT_ADC_MAX;
}

float Acs712Sensor::readCurrent() {
    // Moyenne pour reduire le bruit (au prix d'un peu de latence)
    const int adc = readAdcAverage_(20);
    const float mv = adcToMillivolts_(adc);
    // Formule:
    //  - mv : tension capteur (mV) reconstituee
    //  - zeroMv_ : milieu a 0A
    //  - sensMvPerA_ : pente (mV/A)
    const float currentA = (mv - zeroMv_) / sensMvPerA_;

    // Detection saturation ADC:
    // si la mesure est collee a 0 ou au max, le cablage/adaptation est suspect.
    bool adcOk = true;
    if (adc <= 2 || adc >= (adcMax_ - 2)) {
        adcOk = false;
    }

    if (lock_()) {
        lastCurrentA_ = currentA;
        lastValid_ = true;
        adcOk_ = adcOk;
        unlock_();
    }

    return currentA;
}

void Acs712Sensor::calibrateZero(uint16_t samples) {
    if (samples == 0) samples = 200;
    // On borne volontairement le nombre de samples pour eviter une calibration trop longue.
    uint16_t count = samples;
    if (count > 50) count = 50;
    const int adc = readAdcAverage_(static_cast<uint8_t>(count));
    const float mv = adcToMillivolts_(adc);

    if (lock_()) {
        zeroMv_ = mv;
        unlock_();
    }

    // Persist
    CONF->PutFloat(KEY_CUR_ZERO, mv);
}

void Acs712Sensor::setCalibration(float zeroMv, float sensMvPerA, float inputScale) {
    if (lock_()) {
        if (zeroMv > 0.0f) zeroMv_ = zeroMv;
        if (sensMvPerA > 0.0f) sensMvPerA_ = sensMvPerA;
        if (inputScale > 0.0f) inputScale_ = inputScale;
        unlock_();
    }

    CONF->PutFloat(KEY_CUR_ZERO, zeroMv_);
    CONF->PutFloat(KEY_CUR_SENS, sensMvPerA_);
    CONF->PutFloat(KEY_CUR_SCALE, inputScale_);
}

float Acs712Sensor::getLastCurrent(bool* valid) const {
    float v = lastCurrentA_;
    bool ok = lastValid_;
    if (lock_()) {
        v = lastCurrentA_;
        ok = lastValid_;
        unlock_();
    }
    if (valid) *valid = ok;
    return v;
}

bool Acs712Sensor::isAdcOk() const {
    bool ok = adcOk_;
    if (lock_()) {
        ok = adcOk_;
        unlock_();
    }
    return ok;
}

float Acs712Sensor::adcToMillivolts_(int adc) const {
    if (adc < 0) adc = 0;
    if (adc > adcMax_) adc = adcMax_;

    // Conversion code ADC -> volts ADC
    const float vAdc = (static_cast<float>(adc) / static_cast<float>(adcMax_)) * adcRefV_;
    // Correction adaptation (diviseur / ampli) => tension reelle capteur
    const float vSensor = vAdc / inputScale_;
    return vSensor * 1000.0f;
}

int Acs712Sensor::readAdcAverage_(uint8_t samples) const {
    if (samples == 0) samples = 1;
    uint32_t sum = 0;
    for (uint8_t i = 0; i < samples; ++i) {
        sum += static_cast<uint32_t>(analogRead(PIN_CURRENT_ADC));
        // Petit delai pour decorreler les conversions
        delayMicroseconds(100);
    }
    return static_cast<int>(sum / samples);
}

bool Acs712Sensor::lock_() const {
    if (!mutex_) return false;
    return xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) == pdTRUE;
}

void Acs712Sensor::unlock_() const {
    if (mutex_) xSemaphoreGive(mutex_);
}
