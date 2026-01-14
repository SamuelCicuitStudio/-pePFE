/**************************************************************
 *  RTCManager - gestion du temps systeme
 *
 *  Ce module fournit :
 *  - Lecture/criture de l'heure systeme (epoch UNIX)
 *  - Cache de chaines "date" et "heure" pour l'UI
 *  - Application d'une timezone (nom ou offset) stockee en NVS
 *
 *  Notes :
 *  - Sur ESP32 (Arduino), on peut utiliser settimeofday()/time() pour
 *    manipuler l'horloge systeme.
 *  - La synchro NTP (quand Wi-Fi STA) peut etre ajoutee au-dessus de ce module
 *    (ou dans un futur RTCManager::beginNtp()) - ici on garde un coeur simple.
 **************************************************************/
#ifndef RTCMANAGER_H
#define RTCMANAGER_H

#include <NVSManager.hpp>
#include <Config.hpp>

class RTCManager {
public:
    static void Init();
    static RTCManager* Get();
    static RTCManager* TryGet();

    // Reglage direct en epoch UNIX (secondes).
    void setUnixTime(uint64_t epoch);

    // Lecture epoch UNIX (secondes).
    uint64_t getUnixTime();

    // Valeurs formattees (cache), utiles pour l'UI.
    String getTime();
    String getDate();

    // Met a jour le cache (a appeler periodiquement si besoin).
    void update();

    // Regler la date/heure manuellement
    void setRTCTime(int year, int month, int day,
                    int hour, int minute, int second);

private:
    RTCManager();

    static RTCManager* s_instance;

    // Mutex : protege timeStr_/dateStr_ et les appels set/get.
    SemaphoreHandle_t mutex_ = nullptr;

    // Cache (evite d'appeler strftime/localtime a chaque requete HTTP).
    String timeStr_;
    String dateStr_;
};

#define RTC RTCManager::Get()

#endif // RTCMANAGER_H
