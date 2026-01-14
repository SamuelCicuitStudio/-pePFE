#include <Buzzer.hpp>

// -----------------------------------------------------------------------------
// Buzzer: implementation simple basee sur une file + une tache.
// Objectif:
//  - ne jamais bloquer la logique principale (Device, WiFi)
//  - serialiser les patterns audio
// -----------------------------------------------------------------------------

namespace {
constexpr uint8_t kBuzzerPwmChannel = 6;
constexpr uint32_t kBuzzerPwmBaseFreq = 4000;
constexpr uint8_t kBuzzerPwmResolution = 8;
}

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

    ledcSetup(kBuzzerPwmChannel, kBuzzerPwmBaseFreq, kBuzzerPwmResolution);
    ledcAttachPin(PIN_BUZZER, kBuzzerPwmChannel);
    ledcWriteTone(kBuzzerPwmChannel, 0);

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
    if (!on) {
        ledcWriteTone(kBuzzerPwmChannel, 0);
        digitalWrite(PIN_BUZZER, LOW);
    }
}

bool Buzzer::isEnabled() const {
    return enabled_;
}

void Buzzer::bip() { enqueue(Pattern::Command); }

void Buzzer::playWarn() { enqueue(Pattern::Warn); }
void Buzzer::playError() { enqueue(Pattern::Error); }
void Buzzer::playLatch() { enqueue(Pattern::Latch); }
void Buzzer::playClientConnect() { enqueue(Pattern::ClientConnect); }
void Buzzer::playClientDisconnect() { enqueue(Pattern::ClientDisconnect); }
void Buzzer::playAuthFail() { enqueue(Pattern::AuthFail); }
void Buzzer::playCommand() { enqueue(Pattern::Command); }

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
            silence_(10);
        }
    }
}

void Buzzer::play_(Pattern p) {
    if (!enabled_) return;

    switch (p) {
        case Pattern::Command:
            playTone_(1000, 80); silence_(80);
            break;
        case Pattern::Warn:
            // 2 bips courts
            playTone_(1000, 100); silence_(100);
            playTone_(1000, 100); silence_(100);
            break;
        case Pattern::Error:
            // 3 bips longs
            for (int i = 0; i < 3; ++i) {
                playTone_(400, 400); silence_(200);
            }
            break;
        case Pattern::Latch:
            // 1 long + 2 courts
            playTone_(400, 400); silence_(150);
            playTone_(1000, 100); silence_(150);
            playTone_(1000, 100); silence_(150);
            break;
        case Pattern::ClientConnect:
            playTone_(1200, 120); silence_(60);
            break;
        case Pattern::ClientDisconnect:
            playTone_(900, 80); silence_(80);
            playTone_(900, 80); silence_(80);
            break;
        case Pattern::AuthFail:
            playTone_(400, 400); silence_(200);
            break;
    }
}

void Buzzer::playTone_(uint16_t freqHz, uint32_t durationMs) {
    if (!enabled_) return;
    ledcWriteTone(kBuzzerPwmChannel, freqHz);
    vTaskDelay(pdMS_TO_TICKS(durationMs));
    ledcWriteTone(kBuzzerPwmChannel, 0);
}

void Buzzer::silence_(uint32_t ms) {
    ledcWriteTone(kBuzzerPwmChannel, 0);
    vTaskDelay(pdMS_TO_TICKS(ms));
}
