#include <Relay.hpp>

// -----------------------------------------------------------------------------
// Implementation Relay
// -----------------------------------------------------------------------------

Relay::Relay(int pin, bool activeHigh)
    : pin_(pin),
      activeHigh_(activeHigh),
      state_(false),
      mutex_(nullptr) {}

void Relay::begin() {
    // Mutex pour proteger les acces multi-taches (web, bouton, device loop, etc.)
    mutex_ = xSemaphoreCreateMutex();

    // Configuration du GPIO
    pinMode(pin_, OUTPUT);

    // Etat securise: OFF
    writePin_(false);
}

void Relay::turnOn() {
    set(true);
}

void Relay::turnOff() {
    set(false);
}

void Relay::set(bool on) {
    // Si on ne peut pas prendre le mutex, on abandonne pour eviter blocage.
    if (!lock_()) return;

    writePin_(on);
    state_ = on;

    unlock_();
}

bool Relay::isOn() const {
    bool v = state_;
    if (lock_()) {
        v = state_;
        unlock_();
    }
    return v;
}

void Relay::writePin_(bool on) {
    // Polarite configurable
    const int level = (on == activeHigh_) ? HIGH : LOW;
    digitalWrite(pin_, level);
}

bool Relay::lock_() const {
    if (!mutex_) return false;
    // Timeout court: on prefere rater une commande plutot que bloquer une tache.
    return xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) == pdTRUE;
}

void Relay::unlock_() const {
    if (mutex_) xSemaphoreGive(mutex_);
}
