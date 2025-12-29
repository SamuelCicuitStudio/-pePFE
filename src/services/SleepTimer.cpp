/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 **************************************************************/

#include "services/SleepTimer.h"
#include "systeme/Config.h"
#include "systeme/DeviceTransport.h"
#include "systeme/Utils.h"
#include <esp_sleep.h>
#include <WiFi.h>

// ---------------------- Singleton core (like NVS) ----------------------
SleepTimer* SleepTimer::s_instance = nullptr;

void SleepTimer::Init() {
    (void)SleepTimer::Get();
}

SleepTimer* SleepTimer::Get() {
    if (!s_instance) {
        s_instance = new SleepTimer();
    }
    return s_instance;
}

// --------------------------- Implementation ----------------------------
SleepTimer::SleepTimer() {
    lastActivityTime = millis();          // Initialise avec le temps courant
    isSleepMode      = false;
    _mutex           = xSemaphoreCreateMutex();
}

void SleepTimer::begin() {
    // Intentionnellement vide (compatibilite API)
}

void SleepTimer::reset() {
    MutexGuard g(_mutex, pdMS_TO_TICKS(100));
    if (!g.ok()) return;
    lastActivityTime = millis();
}

bool SleepTimer::timerTaskRunning() const {
    if (!_timerTask) return false;
    eTaskState s = eTaskGetState(_timerTask);
    return (s != eDeleted && s != eInvalid);
}

void SleepTimer::checkInactivity() {
    bool shouldSleep = false;

    {
        MutexGuard g(_mutex, pdMS_TO_TICKS(50));
        if (!g.ok()) return;

        const unsigned long now = millis();
        const bool timeoutReached =
            ((now - lastActivityTime) >= SLEEP_TIMER_MS);  // securise au rollover

        // Decide sous mutex, execution sleep en dehors du mutex
        if (timeoutReached && !isSleepMode && !_sleepInProgress) {
            _sleepInProgress = true;   // marquer l'intention
            shouldSleep      = true;
        }
    }

    if (shouldSleep) {
        goToSleep();  // reset _sleepInProgress en interne
    }
}

void SleepTimer::timerLoop() {
    if (timerTaskRunning()) return;

    xTaskCreate(
        [](void* parameter) {
            auto* self = static_cast<SleepTimer*>(parameter);
            for (;;) {
                self->checkInactivity();
                vTaskDelay(pdMS_TO_TICKS(1000)); // verif toutes les 1s
            }
        },
        "SleepTimerLoop",
        2048,
        this,
        1,
        &_timerTask
    );
}

void SleepTimer::goToSleep() {
    // Phase 1: marquer l'etat sous mutex
    {
        MutexGuard g(_mutex, pdMS_TO_TICKS(250));
        if (!g.ok()) return;

        // Un autre contexte a pu deja entrer en sommeil
        if (isSleepMode) {
            _sleepInProgress = false;
            return;
        }

        isSleepMode = true;
        DEBUG_PRINTLN("[SLEEP] Inactivity timeout reached. Preparing to sleep...");
    }

    // Couper le relais/moteur si possible (via DeviceTransport)
    if (DEVTRAN) {
        DEVTRAN->stop();
    }

    // Desactiver WiFi pour economiser l'energie
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);

    // Configurer la source de reveil : bouton utilisateur (GPIO0)
    const uint64_t wakeMask = (1ULL << PIN_BUTTON);
    esp_sleep_enable_ext1_wakeup(wakeMask, ESP_EXT1_WAKEUP_ANY_LOW);

    DEBUG_PRINTLN("[SLEEP] Entering deep sleep (wake on button)...");
    esp_deep_sleep_start();
}
