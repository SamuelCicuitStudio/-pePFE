#include "capteurs/BusSampler.h"

BusSampler* BusSampler::Get() {
    // Singleton local (stack static) : pas d'allocation dynamique.
    static BusSampler inst;
    return &inst;
}

void BusSampler::begin(Acs712Sensor* current,
                       Ds18b20Sensor* ds18,
                       Bme280Sensor* bme,
                       uint32_t samplingHz) {
    // Injection dependances capteurs
    current_ = current;
    ds18_ = ds18;
    bme_ = bme;

    // Conversion Hz -> periode en ms (limitation a 1ms mini).
    if (samplingHz == 0) samplingHz = DEFAULT_SAMPLING_HZ;
    periodMs_ = 1000U / samplingHz;
    if (periodMs_ == 0) periodMs_ = 20;

    if (!mutex_) {
        // Mutex pour proteger history_/head_/seq_
        mutex_ = xSemaphoreCreateMutex();
    }
}

void BusSampler::start() {
    // running_ permet de pauser sans detruire la tache.
    running_ = true;
    if (!task_) {
        xTaskCreate(taskThunk_, "BusSamplerTask", 4096, this, 1, &task_);
    }
}

void BusSampler::stop() {
    // On garde la tache vivante mais inactive.
    running_ = false;
}

bool BusSampler::sampleNow() {
    if (!current_) return false;

    Sample s{};
    s.ts_ms = millis();

    // Courant (lecture fraiche)
    s.current_a = current_->readCurrent();

    // DS18B20 et BME280 utilisent le cache interne
    if (ds18_) {
        s.motor_c = ds18_->getTempC();
    } else {
        s.motor_c = NAN;
    }

    if (bme_) {
        // Mise a jour BME a frequence basse
        // (capteur plus lent + evite de surcharger le bus I2C).
        const uint32_t now = millis();
        if (now - lastBmeUpdateMs_ > 1000) {
            bme_->update();
            lastBmeUpdateMs_ = now;
        }
        s.bme_c = bme_->getTempC();
        s.bme_pa = bme_->getPressurePa();
    } else {
        s.bme_c = NAN;
        s.bme_pa = NAN;
    }

    pushSample_(s);
    return true;
}

void BusSampler::taskThunk_(void* param) {
    static_cast<BusSampler*>(param)->taskLoop_();
    vTaskDelete(nullptr);
}

void BusSampler::taskLoop_() {
    // Boucle d'echantillonnage a periode fixe.
    for (;;) {
        if (running_) {
            sampleNow();
        }
        vTaskDelay(pdMS_TO_TICKS(periodMs_));
    }
}

void BusSampler::pushSample_(const Sample& s) {
    // Ecriture dans un ring buffer :
    // - idx = head % taille
    // - head++ ; seq++
    if (!lock_()) return;
    const uint32_t idx = head_ % BUS_SAMPLER_HISTORY_SIZE;
    history_[idx] = s;
    head_++;
    seq_++;
    unlock_();
}

size_t BusSampler::getHistorySince(uint32_t lastSeq,
                                   Sample* out,
                                   size_t maxOut,
                                   uint32_t& newSeq) const {
    // API "pull" : l'UI donne le dernier seq recu, on renvoie les suivants.
    if (!out || maxOut == 0) {
        newSeq = lastSeq;
        return 0;
    }

    if (!lock_()) {
        newSeq = lastSeq;
        return 0;
    }

    const uint32_t seqNow = seq_;
    if (seqNow == 0) {
        unlock_();
        newSeq = 0;
        return 0;
    }

    // On ne peut pas remonter plus loin que la taille du ring buffer.
    const uint32_t maxSpan = (seqNow > BUS_SAMPLER_HISTORY_SIZE)
                           ? BUS_SAMPLER_HISTORY_SIZE
                           : seqNow;
    const uint32_t minSeq = seqNow - maxSpan;

    if (lastSeq < minSeq) lastSeq = minSeq;
    if (lastSeq > seqNow) lastSeq = seqNow;

    uint32_t available = seqNow - lastSeq;
    if (available > maxOut) available = maxOut;

    for (uint32_t i = 0; i < available; ++i) {
        uint32_t sSeq = lastSeq + i;
        uint32_t idx = sSeq % BUS_SAMPLER_HISTORY_SIZE;
        out[i] = history_[idx];
    }

    // newSeq pointe sur le prochain element attendu par le client.
    newSeq = lastSeq + available;
    unlock_();
    return static_cast<size_t>(available);
}

bool BusSampler::lock_() const {
    if (!mutex_) return false;
    return xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) == pdTRUE;
}

void BusSampler::unlock_() const {
    if (mutex_) xSemaphoreGive(mutex_);
}
