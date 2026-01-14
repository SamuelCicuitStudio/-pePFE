#include <SwitchManager.hpp>

void SwitchManager::begin() {
    // GPIO0 avec pull-up interne -> lecture stable sans resistance externe.
    pinMode(PIN_BUTTON, INPUT_PULLUP);

    if (!task_) {
        xTaskCreate(taskThunk_, "SwitchTask", 4096, this, 1, &task_);
    }
}

bool SwitchManager::readButton_() const {
    // Bouton actif a l'etat bas (pull-up interne)
    return digitalRead(PIN_BUTTON) == LOW;
}

void SwitchManager::taskThunk_(void* param) {
    static_cast<SwitchManager*>(param)->taskLoop_();
    vTaskDelete(nullptr);
}

void SwitchManager::taskLoop_() {
    // Boucle de polling simple : suffisante pour un bouton utilisateur.
    // Avantage : pas d'interruptions (plus simple, moins de risques).
    for (;;) {
        const uint32_t now = millis();
        bool pressed = readButton_();

        if (pressed && !lastPressed_) {
            // Debut appui
            pressStartMs_ = now;
            longTriggered_ = false;
        }

        if (pressed && !longTriggered_ && (now - pressStartMs_ >= BUTTON_LONG_RESET_MS)) {
            // Reset force
            if (DEVTRAN) DEVTRAN->reset();
            longTriggered_ = true;
        }

        if (!pressed && lastPressed_) {
            // Relache
            const uint32_t heldMs = now - pressStartMs_;
            if (heldMs < BUTTON_LONG_RESET_MS) {
                // Appui court : toggle marche/arret.
                if (DEVTRAN) DEVTRAN->toggle();
            }
        }

        lastPressed_ = pressed;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
