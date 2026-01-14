/**************************************************************
 *  DeviceTransport - pont de commandes vers Device
 *
 *  Objectif :
 *  - Isoler la "surface" de commandes (start/stop/clearFault/...) dans
 *    une petite classe, pour que les modules externes (WiFiManager,
 *    SwitchManager, etc.) ne dependent pas directement de l'implementation
 *    interne de Device.
 *
 *  Philosophie :
 *  - Device reste le point de verite (etat + securites + NVS).
 *  - DeviceTransport ne fait que pousser des commandes dans la file
 *    de Device (thread-safe via FreeRTOS queue).
 **************************************************************/
#ifndef DEVICE_TRANSPORT_H
#define DEVICE_TRANSPORT_H

#include <Device.hpp>

class DeviceTransport {
public:
    // Singleton simple (pas de destruction a l'arret).
    static DeviceTransport* Get();

    // API de commandes (retourne false si Device non initialise ou file pleine).
    bool start();
    bool stop();
    bool toggle();
    bool clearFault();
    bool timedRun(uint32_t seconds);
    bool setRelay(bool on);
    bool reset();

    // Lecture snapshot / etat (utilise les accesseurs thread-safe de Device).
    bool getSnapshot(SystemSnapshot& out) const;
    DeviceState getState() const;

private:
    DeviceTransport() = default;

    static DeviceTransport* inst_;
};

#define DEVTRAN DeviceTransport::Get()

#endif // DEVICE_TRANSPORT_H
