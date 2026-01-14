/**************************************************************
 *  Capteur BME280
 *  - Lecture temperature/pression
 *  - Cache derniere valeur valide
 *
 *  Raison d'etre :
 *  - Le BME280 est sur le bus I2C (SDA/SCL).
 *  - Les lectures peuvent echouer (capteur absent, bus bloque, etc.).
 *  - On conserve donc la derniere valeur valide pour l'UI (mode degrade).
 *
 *  Robustesse :
 *  - isPresent() indique si le capteur est actuellement detecte.
 *  - update() tente une re-init si le capteur a disparu.
 *  - Un mutex protege les variables partagees (tache sampler + HTTP).
 **************************************************************/
#ifndef BME280_SENSOR_H
#define BME280_SENSOR_H

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_BME280.h>
#include <Config.hpp>

class Bme280Sensor {
public:
    // wire : bus I2C utilise (par defaut Wire).
    explicit Bme280Sensor(TwoWire* wire = &Wire);

    // Initialise le bus I2C et detecte le capteur.
    void begin();

    // Met a jour les valeurs (temperature + pression) et le flag de presence.
    void update();

    // Accesseurs (retournent la derniere valeur en cache).
    // valid (optionnel) : true si la derniere lecture etait valide.
    float getTempC(bool* valid = nullptr) const;
    float getPressurePa(bool* valid = nullptr) const;
    bool  isPresent() const;

private:
    // Essaye d'initialiser le capteur (adresse 0x76 puis 0x77).
    bool tryBegin_();

    // Mutex interne (thread-safe).
    bool lock_() const;
    void unlock_() const;

    TwoWire* wire_ = &Wire;
    Adafruit_BME280 bme_;

    mutable SemaphoreHandle_t mutex_ = nullptr;

    bool present_ = false;
    float lastTempC_ = NAN;
    float lastPressurePa_ = NAN;
    bool lastValid_ = false;
};

#endif // BME280_SENSOR_H
