#include "controle/Buzzer.h"

// -----------------------------------------------------------------------------
// Buzzer: implementation simple basee sur une file + une tache.
// Objectif:
//  - ne jamais bloquer la logique principale (Device, WiFi)
//  - serialiser les patterns audio
// -----------------------------------------------------------------------------

Buzzer* Buzzer::inst_ = nullptr;

Buzzer* Buzzer::Get() {
    if (!inst_) inst_ = new Buzzer();
    return inst_;
}

void Buzzer::begin() {
    // GPIO en sortie, etat OFF par defaut
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW);

    // Lecture de l'activation depuis NVS
    enabled_ = CONF->GetBool(KEY_BUZZ_EN, DEFAULT_BUZZER_ENABLED);

    if (!mutex_) mutex_ = xSemaphoreCreateMutex();
    if (!queue_) queue_ = xQueueCreate(10, sizeof(Pattern));

    if (!task_) {
        // Tache bloquante sur queue (playback)
        xTaskCreate(taskThunk_, "BuzzerTask", 4096, this, 1, &task_);
    }
}

void Buzzer::setEnabled(bool on) {
    enabled_ = on;
    CONF->PutBool(KEY_BUZZ_EN, on);
}

bool Buzzer::isEnabled() const {
    return enabled_;
}

void Buzzer::playWarn() { enqueue(Pattern::Warn); }
void Buzzer::playError() { enqueue(Pattern::Error); }
void Buzzer::playLatch() { enqueue(Pattern::Latch); }
void Buzzer::playClientConnect() { enqueue(Pattern::ClientConnect); }
void Buzzer::playClientDisconnect() { enqueue(Pattern::ClientDisconnect); }
void Buzzer::playAuthFail() { enqueue(Pattern::AuthFail); }

void Buzzer::enqueue(Pattern p) {
    if (!enabled_ || !queue_) return;

    if (mutex_) xSemaphoreTake(mutex_, portMAX_DELAY);
    if (xQueueSendToBack(queue_, &p, 0) != pdTRUE) {
        // Si la queue est pleine, on retire le plus ancien pour garder le plus recent.
        Pattern dump;
        xQueueReceive(queue_, &dump, 0);
        xQueueSendToBack(queue_, &p, 0);
    }
    if (mutex_) xSemaphoreGive(mutex_);
}

void Buzzer::taskThunk_(void* param) {
    static_cast<Buzzer*>(param)->taskLoop_();
    vTaskDelete(nullptr);
}

void Buzzer::taskLoop_() {
    for (;;) {
        Pattern p;
        // Attend un pattern puis le joue.
        if (xQueueReceive(queue_, &p, portMAX_DELAY) == pdTRUE) {
            play_(p);
            // Petite pause pour eviter un "clic" entre patterns.
            buzzOff_(10);
        }
    }
}

void Buzzer::play_(Pattern p) {
    if (!enabled_) return;

    switch (p) {
        case Pattern::Warn:
            // 2 bips courts
            buzzOn_(100); buzzOff_(100);
            buzzOn_(100); buzzOff_(100);
            break;
        case Pattern::Error:
            // 3 bips longs
            for (int i = 0; i < 3; ++i) {
                buzzOn_(400); buzzOff_(200);
            }
            break;
        case Pattern::Latch:
            // 1 long + 2 courts
            buzzOn_(400); buzzOff_(150);
            buzzOn_(100); buzzOff_(150);
            buzzOn_(100); buzzOff_(150);
            break;
        case Pattern::ClientConnect:
            buzzOn_(120); buzzOff_(60);
            break;
        case Pattern::ClientDisconnect:
            buzzOn_(80); buzzOff_(80);
            buzzOn_(80); buzzOff_(80);
            break;
        case Pattern::AuthFail:
            buzzOn_(400); buzzOff_(200);
            break;
    }
}

void Buzzer::buzzOn_(uint32_t ms) {
    // NOTE: version PWM/LEDC possible, ici on reste en digitalWrite simple.
    digitalWrite(PIN_BUZZER, HIGH);
    vTaskDelay(pdMS_TO_TICKS(ms));
}

void Buzzer::buzzOff_(uint32_t ms) {
    digitalWrite(PIN_BUZZER, LOW);
    vTaskDelay(pdMS_TO_TICKS(ms));
}
