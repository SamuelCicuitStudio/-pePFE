/**************************************************************
 *  SystemSnapshot - etat systeme centralise
 *
 *  But :
 *  - Regrouper dans une seule structure l'etat "instantane" du systeme :
 *    relais, mesures, et codes d'alerte.
 *  - Eviter que l'interface Web lise 10 variables differentes (risque
 *    d'incoherence) : on lit 1 snapshot, coherant, copie sous semaphore.
 *
 *  Note :
 *  - Le snapshot est produit par Device (tache periodique).
 *  - L'acces se fait via DeviceTransport -> Device::getSnapshot().
 **************************************************************/
#ifndef STATUS_SNAPSHOT_H
#define STATUS_SNAPSHOT_H

#include <Arduino.h>
#include "systeme/Config.h"

struct SystemSnapshot {
    // -------------------- Metadonnees --------------------

    uint32_t seq = 0;          // Compteur monotone (s'incremente a chaque snapshot)
    uint32_t ts_ms = 0;        // Instant de production (millis())
    uint32_t age_ms = 0;       // Age calcule au moment de la lecture (millis() - ts_ms)

    // -------------------- Etat global --------------------

    DeviceState state = DeviceState::Off; // Machine d'etat (Off/Idle/Running/Fault/...)
    bool fault_latched = false;           // Vrai si un defaut est memorise (OVC/Overtemp)

    // -------------------- Mesures "puissance" --------------------

    bool relay_on = false;     // Etat actuel de sortie (relais)
    float current_a = 0.0f;    // Dernier courant (A) (cache sensor si lecture fail)
    float power_w = 0.0f;      // Puissance calculee (W) = Vcc * I
    float energy_wh = 0.0f;    // Energie integree sur la session (Wh)

    // -------------------- Mesures "temperatures" --------------------

    float motor_c = NAN;       // Temperature moteur (DS18B20) en degre C
    float board_c = NAN;       // Temperature carte (BME280) en degre C
    float ambient_c = NAN;     // Temperature ambiante (si non disponible, peut dupliquer board_c)

    // -------------------- Sante capteurs --------------------

    bool ds18_ok = false;      // Vrai si DS18 present ET valeur valide
    bool bme_ok = false;       // Vrai si BME present ET valeur valide
    bool adc_ok = true;        // Vrai si l'ADC n'est pas sature (diagnostic ACS712)

    // -------------------- Derniere alerte --------------------

    uint16_t last_warning = 0; // Dernier code warning (WarnCode cast en uint16_t)
    uint16_t last_error = 0;   // Dernier code erreur (ErrorCode cast en uint16_t)
};

#endif // STATUS_SNAPSHOT_H
