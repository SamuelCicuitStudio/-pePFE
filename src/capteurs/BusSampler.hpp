/**************************************************************
 *  BusSampler
 *  - Echantillonnage synchronise (courant + temperature + pression)
 *  - Buffer circulaire de taille fixe
 *
 *  Pourquoi un "bus sampler" ?
 *  - L'UI veut tracer des graphes "courant/temps" et "temp/temps".
 *  - Il faut que les mesures aient la meme reference de temps.
 *  - On stocke donc des echantillons (Sample) avec un timestamp commun.
 *
 *  Points importants :
 *  - Historique fixe (BUS_SAMPLER_HISTORY_SIZE = 800) en RAM.
 *  - getHistorySince() renvoie une fenetre a partir d'un numero de sequence.
 *  - Thread-safe via mutex (semaphore) car acces depuis tache sampler + HTTP.
 **************************************************************/
#ifndef BUS_SAMPLER_H
#define BUS_SAMPLER_H

#include <Config.hpp>
#include <CurrentSensor.hpp>
#include <TempSensor.hpp>
#include <Bme280Sensor.hpp>

class BusSampler {
public:
    struct Sample {
        // Timestamp commun (millis()) pour aligner toutes les mesures.
        uint32_t ts_ms;

        // Courant instantane (A) (lecture "fraiche" du capteur).
        float current_a;

        // Temperature moteur (DS18) (valeur cache si lecture fail).
        float motor_c;

        // Temperature carte (BME) (valeur cache si lecture fail).
        float bme_c;

        // Pression (Pa) (valeur cache si lecture fail).
        float bme_pa;
    };

    static BusSampler* Get();

    // Injecte les capteurs et fixe la frequence d'echantillonnage (Hz).
    void begin(Acs712Sensor* current,
               Ds18b20Sensor* ds18,
               Bme280Sensor* bme,
               uint32_t samplingHz = DEFAULT_SAMPLING_HZ);

    // Demarre/arrete la tache de sampling.
    void start();
    void stop();

    // Force un echantillonnage immediat (utile debug / tests).
    bool sampleNow();

    // Recupere un morceau d'historique depuis lastSeq (ex: depuis l'UI).
    // newSeq = lastSeq + nb_samples_reellement_retournes.
    size_t getHistorySince(uint32_t lastSeq,
                           Sample* out,
                           size_t maxOut,
                           uint32_t& newSeq) const;

private:
    BusSampler() = default;
    static void taskThunk_(void* param);
    void taskLoop_();

    // Ajoute un sample dans le ring buffer (sous mutex).
    void pushSample_(const Sample& s);

    // Mutex interne (historique + index)
    bool lock_() const;
    void unlock_() const;

    Acs712Sensor* current_ = nullptr;
    Ds18b20Sensor* ds18_ = nullptr;
    Bme280Sensor* bme_ = nullptr;

    uint32_t periodMs_ = 20;
    uint32_t lastBmeUpdateMs_ = 0;

    // Ring buffer fixe
    Sample history_[BUS_SAMPLER_HISTORY_SIZE]{};
    uint32_t head_ = 0;
    uint32_t seq_ = 0; // Numero de sequence global (monotone)

    TaskHandle_t task_ = nullptr;
    mutable SemaphoreHandle_t mutex_ = nullptr;
    bool running_ = false;
};

#define BUS_SAMPLER BusSampler::Get()

#endif // BUS_SAMPLER_H
