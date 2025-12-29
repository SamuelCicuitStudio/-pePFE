/**************************************************************
 *  Relay simple, thread-safe
 **************************************************************/
#ifndef RELAY_H
#define RELAY_H

#include "systeme/Config.h"

class Relay {
public:
    // Par defaut: pin et polarite depuis Config.h
    explicit Relay(int pin = PIN_RELAY, bool activeHigh = RELAY_ACTIVE_HIGH);

    // Initialisation hardware:
    // - creation mutex
    // - configuration GPIO
    // - force OFF (etat securise)
    void begin();

    // API simplifiee
    // turnOn/turnOff: wrappers pratiques (thread-safe)
    void turnOn();
    void turnOff();

    // set(): commande principale (thread-safe)
    // On met a jour l'etat logiciel + ecriture GPIO.
    void set(bool on);

    // Lecture de l'etat (thread-safe). Ne lit pas le GPIO, mais l'etat cache.
    bool isOn() const;

private:
    // Convertit l'etat logique "on/off" vers le niveau GPIO selon la polarite.
    void writePin_(bool on);
    bool lock_() const;
    void unlock_() const;

    // Configuration
    int  pin_ = PIN_RELAY;
    bool activeHigh_ = RELAY_ACTIVE_HIGH;

    // Etat cache
    bool state_ = false;

    // Protection concurrence
    mutable SemaphoreHandle_t mutex_ = nullptr;
};

#endif // RELAY_H
