#include "controle/StatusLeds.h"

void StatusLeds::begin() {
    // Configuration des deux LEDs
    pinMode(PIN_LED_OVERTEMP, OUTPUT);
    pinMode(PIN_LED_CMD, OUTPUT);
    digitalWrite(PIN_LED_OVERTEMP, LOW);
    digitalWrite(PIN_LED_CMD, LOW);

    if (!mutex_) {
        // Mutex protege: busy_ + acces GPIO si besoin
        mutex_ = xSemaphoreCreateMutex();
    }

    if (!queue_) {
        // File des codes a emettre (warnings/erreurs).
        // Si la file est pleine, on perd potentiellement un code (choix volontaire).
        queue_ = xQueueCreate(10, sizeof(AlertItem));
    }

    if (!task_) {
        // Tache de clignotement (ne bloque pas la logique Device)
        xTaskCreate(taskThunk_, "StatusLedTask", 4096, this, 1, &task_);
    }
}

void StatusLeds::setOvertemp(bool on) {
    if (lock_()) {
        overtempOn_ = on;
        // LED fixe: indique un etat de surchauffe/defaut temperature
        digitalWrite(PIN_LED_OVERTEMP, on ? HIGH : LOW);
        unlock_();
    }
}

void StatusLeds::notifyCommand() {
    // Si on emet un code, on ignore le blink commande
    bool busy = false;
    if (lock_()) {
        busy = busy_;
        unlock_();
    }
    if (busy) return;

    blinkOnce_(60, 60);
}

void StatusLeds::enqueueAlert(EventLevel level, uint16_t code) {
    if (!queue_) return;
    AlertItem item{level, code};
    // Enqueue non-bloquant: si echec, on abandonne.
    xQueueSendToBack(queue_, &item, 0);
}

void StatusLeds::taskThunk_(void* param) {
    static_cast<StatusLeds*>(param)->taskLoop_();
    vTaskDelete(nullptr);
}

void StatusLeds::taskLoop_() {
    for (;;) {
        AlertItem item{};
        if (xQueueReceive(queue_, &item, portMAX_DELAY) == pdTRUE) {
            if (lock_()) {
                busy_ = true;
                unlock_();
            }

            // Emission du code sous forme de rafales:
            // - prefixe (1 ou 2 flashes)
            // - pause
            // - N flashes correspondant au numero de code (borne a 9)
            // Prefixe : warning = 1 flash, error = 2 flashes
            uint8_t prefix = (item.level == EventLevel::Error) ? 2 : 1;
            blinkCount_(prefix);
            vTaskDelay(pdMS_TO_TICKS(CMD_LED_PAUSE_GROUP_MS));

            // Code : nombre de flashes (1..9). On borne pour rester lisible.
            uint8_t code = (item.code > 9) ? 9 : static_cast<uint8_t>(item.code);
            blinkCount_(code);
            vTaskDelay(pdMS_TO_TICKS(CMD_LED_PAUSE_CODE_MS));

            if (lock_()) {
                busy_ = false;
                unlock_();
            }
        }
    }
}

void StatusLeds::blinkOnce_(uint32_t onMs, uint32_t offMs) {
    digitalWrite(PIN_LED_CMD, HIGH);
    vTaskDelay(pdMS_TO_TICKS(onMs));
    digitalWrite(PIN_LED_CMD, LOW);
    vTaskDelay(pdMS_TO_TICKS(offMs));
}

void StatusLeds::blinkCount_(uint8_t count) {
    for (uint8_t i = 0; i < count; ++i) {
        blinkOnce_(CMD_LED_FLASH_ON_MS, CMD_LED_FLASH_OFF_MS);
    }
}

bool StatusLeds::lock_() const {
    if (!mutex_) return false;
    return xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) == pdTRUE;
}

void StatusLeds::unlock_() const {
    if (mutex_) xSemaphoreGive(mutex_);
}
