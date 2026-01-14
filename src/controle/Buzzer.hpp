/**************************************************************
 *  Buzzer - patterns simples
 **************************************************************/
#ifndef BUZZER_H
#define BUZZER_H

#include <Config.hpp>
#include <NVSManager.hpp>

class Buzzer {
public:
    enum class Pattern : uint8_t {
        // Commande acceptee (bip court)
        Command,
        // Succes
        Success,
        // Echec
        Failed,
        // Wi-Fi connecte (STA)
        WifiConnected,
        // Wi-Fi coupe (STA)
        WifiOff,
        // Surchauffe
        OverTemperature,
        // Sequence de demarrage
        Startup,
        // Systeme pret
        SystemReady,
        // Avertissement general (2 bips courts)
        Warn,
        // Erreur non verrouillee (3 bips longs)
        Error,
        // Defaut verrouille (1 long + 2 courts)
        Latch,
        // Connexion d'un client (AP)
        ClientConnect,
        // Deconnexion d'un client (AP)
        ClientDisconnect,
        // Echec authentification HTTP
        AuthFail
    };

    static Buzzer* Get();

    // begin():
    // - configure GPIO du buzzer
    // - charge l'etat enabled_ depuis NVS (KEY_BUZZ_EN)
    // - demarre une tache de playback non-bloquante
    void begin();
    void bip();
    // Active/desactive le buzzer et persiste dans NVS.
    void setEnabled(bool on);
    bool isEnabled() const;

    // Helpers (API lisible)
    void playSuccess();
    void playFailed();
    void playWiFiConnected();
    void playWiFiOff();
    void playOverTemperature();
    void playStartupSequence();
    void playSystemReady();
    void playCommand();
    void playWarn();
    void playError();
    void playLatch();
    void playClientConnect();
    void playClientDisconnect();
    void playAuthFail();

    // Ajoute un pattern dans la file (non bloquant).
    void enqueue(Pattern p);

private:
    Buzzer() = default;
    static void taskThunk_(void* param);
    void taskLoop_();
    void play_(Pattern p);

    void playTone_(uint16_t freqHz, uint32_t durationMs);
    void silence_(uint32_t ms);

    static Buzzer* inst_;

    QueueHandle_t queue_ = nullptr;
    TaskHandle_t task_ = nullptr;
    SemaphoreHandle_t mutex_ = nullptr;

    bool enabled_ = true;
};

#define BUZZ Buzzer::Get()

#endif // BUZZER_H
