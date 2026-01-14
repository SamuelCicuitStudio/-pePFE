/**************************************************************
 *  SwitchManager - gestion du bouton Boot/User
 *
 *  Comportement :
 *  - Appui court : Toggle (ON<->OFF)
 *  - Appui long (>= 10 s) : Reset force (ESP.restart)
 *
 *  Note :
 *  - Bouton connecte sur GPIO0 (Boot), actif a l'etat bas.
 *  - On utilise une tache RTOS pour eviter de bloquer loop().
 **************************************************************/
#ifndef SWITCH_MANAGER_H
#define SWITCH_MANAGER_H

#include <Config.hpp>
#include <DeviceTransport.hpp>

class SwitchManager {
public:
    SwitchManager() = default;

    // Configure GPIO + demarre la tache.
    void begin();

private:
    // Thunk statique FreeRTOS -> methode membre.
    static void taskThunk_(void* param);
    void taskLoop_();

    // Lecture brute du bouton (actif bas).
    bool readButton_() const;

    TaskHandle_t task_ = nullptr;

    // Etat interne (anti-rebond simple par echantillonnage periodique)
    bool lastPressed_ = false;
    uint32_t pressStartMs_ = 0;
    bool longTriggered_ = false;
};

#endif // SWITCH_MANAGER_H
