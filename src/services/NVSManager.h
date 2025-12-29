/**************************************************************
 *  Gestion NVS (Preferences) - thread-safe
 *
 *  Commentaires en francais, ASCII uniquement.
 **************************************************************/
#ifndef NVS_MANAGER_H
#define NVS_MANAGER_H

#include <Preferences.h>
#include "systeme/Config.h"
#include "systeme/Utils.h"

class NVS {
public:
    // Singleton
    static void Init();
    static NVS* Get();

    // Cycle de vie
    // begin():
    // - ouvre Preferences en lecture
    // - cree les cles manquantes avec valeurs par defaut
    // IMPORTANT: begin() doit etre appele au boot AVANT d'utiliser CONF.
    void begin();

    // end(): ferme Preferences (optionnel, rarement necessaire)
    void end();

    // Ecriture
    // NOTE: dans l'architecture cible, Device est le seul ecrivain logique.
    // Ici on fournit l'API generique, mais la convention d'usage est importante.
    void PutBool   (const char* key, bool value);
    void PutInt    (const char* key, int value);
    void PutUInt   (const char* key, unsigned int value);
    void PutULong64(const char* key, uint64_t value);
    void PutFloat  (const char* key, float value);
    void PutString (const char* key, const String& value);

    // Lecture
    // API thread-safe: toute lecture est protegee par mutex + ouverture RO.
    bool     GetBool   (const char* key, bool defaultValue);
    int      GetInt    (const char* key, int defaultValue);
    uint32_t GetUInt   (const char* key, uint32_t defaultValue);
    uint64_t GetULong64(const char* key, uint64_t defaultValue);
    float    GetFloat  (const char* key, float defaultValue);
    String   GetString (const char* key, const String& defaultValue);

    // Maintenance
    // RemoveKey: supprime une cle (attention: perte de persistance).
    void RemoveKey(const char* key);
    // ClearAll: efface toute la partition (danger).
    void ClearAll();

private:
    NVS();
    ~NVS();
    NVS(const NVS&) = delete;
    NVS& operator=(const NVS&) = delete;

    void ensureDefaults_();
    void ensureOpenRO_();
    void ensureOpenRW_();
    void lock_();
    void unlock_();

    Preferences preferences;
    const char* namespaceName = CONFIG_PARTITION;
    bool is_open_ = false;
    bool open_rw_ = false;
    SemaphoreHandle_t mutex_ = nullptr;

    static NVS* s_instance;
};

// Acces rapide
#define CONF NVS::Get()

#endif // NVS_MANAGER_H
