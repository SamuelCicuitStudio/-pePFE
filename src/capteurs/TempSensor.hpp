/**************************************************************
 *  Capteur DS18B20 (OneWire) - capteur unique
 *  - Lecture periodique avec cache de la derniere valeur valide
 *  - Reconnexion automatique si capteur debranche
 *  - Implementation OneWire "directe" (sans DallasTemperature)
 **************************************************************/
#ifndef TEMP_SENSOR_H
#define TEMP_SENSOR_H

#include <Arduino.h>
#include <OneWire.h>
#include <Config.hpp>

class Ds18b20Sensor {
public:
    explicit Ds18b20Sensor(OneWire* bus);

    // Demarrage de la lecture periodique
    // periodMs: periode de lecture en millisecondes.
    // La lecture DS18B20 est relativement lente; une periode 1s est classique.
    void begin(uint32_t periodMs = 1000);

    // Forcer une mise a jour (utile si on ne lance pas de task)
    // NOTE: update() met a jour le cache si la lecture est valide.
    void update();

    // Derniere valeur connue
    // valid = true si la derniere lecture etait valide.
    // Si la lecture echoue, on conserve lastTempC_ mais valid=false.
    float getTempC(bool* valid = nullptr) const;

    // true si au moins un capteur est detecte sur le bus.
    bool isPresent() const;

private:
    static void taskThunk_(void* param);
    void taskLoop_();

    // Bus OneWire (DS18B20) : scan + lecture scratchpad
    bool discoverSensor_();
    bool readTempOnce_(float& outTempC);
    void tryReconnect_();

    bool isTempValid_(float tempC) const;

    bool lock_() const;
    void unlock_() const;

    OneWire* oneWire_ = nullptr;

    mutable SemaphoreHandle_t mutex_ = nullptr;
    TaskHandle_t task_ = nullptr;
    uint32_t periodMs_ = 1000;

    bool present_ = false;
    bool hasAddress_ = false;
    uint8_t address_[8] = {0};
    uint8_t badReadStreak_ = 0;

    // Derniere valeur valide (ou derniere valeur connue)
    float lastTempC_ = NAN;
    // Indique si lastTempC_ provient d'une lecture valide recente
    bool lastValid_ = false;
    uint32_t lastReconnectMs_ = 0;

    static constexpr uint32_t kReconnectIntervalMs = 5000;
    static constexpr uint8_t kBadReadThreshold = 2;
};

// Alias pour compatibilite si d'anciens fichiers incluent TempSensor
using TempSensor = Ds18b20Sensor;

#endif // TEMP_SENSOR_H
