/**************************************************************
 *  Status LEDs
 *  - LED surchauffe (etat fixe)
 *  - LED CMD (clignotements rapides pour codes)
 **************************************************************/
#ifndef STATUS_LEDS_H
#define STATUS_LEDS_H

#include "systeme/Config.h"

class StatusLeds {
public:
    // Initialisation:
    // - configure les deux GPIO
    // - demarre une tache qui emet les codes (queue)
    void begin();

    // LED surchauffe
    // true => LED allumee fixe
    void setOvertemp(bool on);

    // Clignotement court pour commande acceptee
    // (ignore si un code d'alerte est en cours)
    void notifyCommand();

    // Code d'alerte (warning/error)
    // Le code est typiquement 1..9 (Wxx/Exx).
    // Le pattern exact est defini dans Config.h:
    // - Warning: 1 flash prefixe + pause + N flashes
    // - Error  : 2 flashes prefixe + pause + N flashes
    void enqueueAlert(EventLevel level, uint16_t code);

private:
    struct AlertItem {
        EventLevel level;
        uint16_t code;
    };

    static void taskThunk_(void* param);
    void taskLoop_();
    void blinkOnce_(uint32_t onMs, uint32_t offMs);
    void blinkCount_(uint8_t count);

    bool lock_() const;
    void unlock_() const;

    QueueHandle_t queue_ = nullptr;
    TaskHandle_t task_ = nullptr;
    mutable SemaphoreHandle_t mutex_ = nullptr;

    bool overtempOn_ = false;
    bool busy_ = false;
};

#endif // STATUS_LEDS_H
