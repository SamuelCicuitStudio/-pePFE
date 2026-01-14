/**************************************************************
 *  Capteur de courant ACS712ELCTR-20A-T
 *  - Calibration zero / sensibilite
 *  - Cache de la derniere valeur valide
 *  - Detection saturation ADC
 **************************************************************/
#ifndef CURRENT_SENSOR_H
#define CURRENT_SENSOR_H

#include <Config.hpp>
#include <NVSManager.hpp>

class Acs712Sensor {
public:
    Acs712Sensor();

    // begin():
    // - configure le pin ADC
    // - charge les parametres de calibration depuis NVS
    //   (zero_mv, sens_mv_a, input_scale, adc_ref_v, adc_max)
    void begin();

    // Lecture instantanee (avec moyenne)
    // Retourne le courant en amperes.
    // Met a jour:
    //  - lastCurrentA_ / lastValid_
    //  - adcOk_ (false si saturation ADC detectee)
    float readCurrent();

    // Calibration
    // calibrateZero():
    // - doit etre appele a 0A (aucune charge)
    // - mesure le point milieu (mV) et persiste dans NVS
    void calibrateZero(uint16_t samples = 200);

    // setCalibration():
    // - permet d'ecrire manuellement les parametres
    // - inputScale: ratio Vadc/Vsensor (voir Config.h)
    void setCalibration(float zeroMv, float sensMvPerA, float inputScale = 1.0f);

    // Acces cache
    // valid=false signifie "la derniere lecture etait invalide",
    // mais la valeur numerique reste la derniere valeur connue.
    float getLastCurrent(bool* valid = nullptr) const;
    // true si l'ADC n'est pas sature (valeur pas collee a 0 ou max)
    bool  isAdcOk() const;

private:
    float adcToMillivolts_(int adc) const;
    int   readAdcAverage_(uint8_t samples) const;

    bool lock_() const;
    void unlock_() const;

    mutable SemaphoreHandle_t mutex_ = nullptr;

    float zeroMv_ = DEFAULT_CURRENT_ZERO_MV;
    float sensMvPerA_ = DEFAULT_CURRENT_SENS_MV_A;
    float inputScale_ = DEFAULT_CURRENT_INPUT_SCALE;
    float adcRefV_ = DEFAULT_ADC_REF_V;
    int   adcMax_ = DEFAULT_ADC_MAX;

    // Cache courant
    float lastCurrentA_ = 0.0f;
    bool  lastValid_ = false;
    // Etat ADC: false => probablement saturations (cablage/diviseur a verifier)
    bool  adcOk_ = true;
};

// Alias pour compatibilite
using CurrentSensor = Acs712Sensor;

#endif // CURRENT_SENSOR_H
