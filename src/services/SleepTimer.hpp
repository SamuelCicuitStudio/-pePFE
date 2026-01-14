/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 **************************************************************/
#ifndef SLEEPTIMER_H
#define SLEEPTIMER_H

#include <Arduino.h>

#ifndef SLEEP_TIMER_MS
#define SLEEP_TIMER_MS (10UL * 60UL * 1000UL) // 10 minutes par defaut
#endif

/**
 * @brief Superviseur de mise en sommeil par inactivite.
 *
 * Usage :
 *   SleepTimer::Init();
 *   SLEEP->timerLoop();        // macro plus bas
 *   SLEEP->reset();
 *
 * Comportement :
 * - Si aucune activite pendant SLEEP_TIMER_MS, le systeme peut entrer en deep sleep.
 */
class SleepTimer {
public:
    // ------------------ Acces singleton ------------------
    static void Init();          // Assure la construction une fois (optionnel, recommande)
    static SleepTimer* Get();    // Acces global paresseux

    // ------------------------ API publique ---------------------------
    void begin();                // Conserve pour compatibilite (ne fait rien)
    void reset();
    void checkInactivity();
    void timerLoop();
    void goToSleep();

    // ------------------ Champs historiques (conserves) ----------------
    unsigned long inactivityTimeout = 0;  ///< Placeholder inutilise (legacy).
    unsigned long lastActivityTime  = 0;  ///< Dernier timestamp d'activite (ms).
    bool          isSleepMode       = false;

private:
    // Coeur singleton
    SleepTimer();
    SleepTimer(const SleepTimer&) = delete;
    SleepTimer& operator=(const SleepTimer&) = delete;
    static SleepTimer* s_instance;

    // ------------------------- Internes ------------------------------
    struct MutexGuard {
        explicit MutexGuard(SemaphoreHandle_t mtx,
                            TickType_t timeout = portMAX_DELAY)
            : _mtx(mtx), _ok(false)
        {
            if (_mtx) _ok = (xSemaphoreTake(_mtx, timeout) == pdTRUE);
        }
        ~MutexGuard() { if (_ok && _mtx) xSemaphoreGive(_mtx); }
        bool ok() const { return _ok; }
    private:
        SemaphoreHandle_t _mtx;
        bool              _ok;
    };

    SemaphoreHandle_t _mutex = nullptr;   // protege l'etat partage / sequence sommeil
    TaskHandle_t _timerTask   = nullptr;  // evite plusieurs taches timerLoop
    bool _sleepInProgress     = false;    // evite goToSleep() concurrent
    bool timerTaskRunning() const;
};

// ------------- Acces global simple (comme CONF) -------------
#ifndef SLEEP
#define SLEEP (SleepTimer::Get())
#endif

#endif // SLEEPTIMER_H
