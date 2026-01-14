#include <NVSManager.hpp>
#include <esp_err.h>
#include <esp_sleep.h>
#include <esp_task_wdt.h>
// -----------------------------------------------------------------------------
// BUT:
//  - Centraliser les parametres persistants (Preferences / NVS ESP32)
//  - Garantir un acces thread-safe pour les ecritures (mutex recursif)
//  - S'assurer que les cles necessaires existent (valeurs par defaut au boot)
//
// IMPORTANT (convention d'architecture):
//  - Device est le seul module qui doit ecrire dans NVS (config).
//  - Les autres modules peuvent lire (pour initialisation / affichage).
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// Singleton
// -----------------------------------------------------------------------------
NVS* NVS::s_instance = nullptr;

void NVS::Init() {
    (void)CONF;
}

NVS* NVS::Get() {
    if (!s_instance) {
        s_instance = new NVS();
    }
    return s_instance;
}

// -----------------------------------------------------------------------------
// Ctor / Dtor
// -----------------------------------------------------------------------------
NVS::NVS() {
    mutex_ = xSemaphoreCreateRecursiveMutex();
}

NVS::~NVS() {
    end();
    if (mutex_) {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
}

// -----------------------------------------------------------------------------
// Internes
// -----------------------------------------------------------------------------
void NVS::lock_() {
    if (mutex_) {
        xSemaphoreTakeRecursive(mutex_, portMAX_DELAY);
    }
}

void NVS::unlock_() {
    if (mutex_) {
        xSemaphoreGiveRecursive(mutex_);
    }
}

void NVS::ensureOpenRW_() {
    if (!is_open_) {
        // Ouvre en lecture/ecriture: necessaire avant tout Put*()
        preferences.begin(namespaceName, false);
        is_open_ = true;
        open_rw_ = true;
    } else if (!open_rw_) {
        // Si deja ouvert en RO, on doit fermer puis re-ouvrir en RW.
        preferences.end();
        preferences.begin(namespaceName, false);
        is_open_ = true;
        open_rw_ = true;
    }
}

static String buildDeviceId_() {
    // Identifiant court derive du MAC eFuse:
    // - stable entre reboots
    // - utile pour identifier l'appareil sur le reseau / UI
    uint64_t mac = ESP.getEfuseMac();
    uint32_t tail = static_cast<uint32_t>(mac & 0xFFFFFFULL);
    char buf[16];
    snprintf(buf, sizeof(buf), "CTRL%06X", tail);
    return String(buf);
}

void NVS::ensureDefaults_() {
    // Cree uniquement les cles manquantes:
    // - ne remplace pas une config existante
    // - permet d'ajouter de nouvelles cles lors d'une mise a jour firmware
    lock_();
    ensureOpenRW_();

    auto ensureBool = [&](const char* key, bool value) {
        if (!preferences.isKey(key)) preferences.putBool(key, value);
    };
    auto ensureInt = [&](const char* key, int value) {
        if (!preferences.isKey(key)) preferences.putInt(key, value);
    };
    auto ensureUInt = [&](const char* key, unsigned int value) {
        if (!preferences.isKey(key)) preferences.putUInt(key, value);
    };
    auto ensureULong64 = [&](const char* key, uint64_t value) {
        if (!preferences.isKey(key)) preferences.putULong64(key, value);
    };
    auto ensureFloat = [&](const char* key, float value) {
        if (!preferences.isKey(key)) preferences.putFloat(key, value);
    };
    auto ensureString = [&](const char* key, const String& value) {
        if (!preferences.isKey(key)) preferences.putString(key, value);
    };

    // Identite
    ensureString(KEY_DEV_ID, buildDeviceId_());
    ensureString(KEY_DEV_NAME, DEFAULT_DEVICE_NAME);
    ensureString(KEY_DEV_SW, DEVICE_SW_VERSION);
    ensureString(KEY_DEV_HW, DEVICE_HW_VERSION);

    // Wi-Fi
    ensureString(KEY_STA_SSID, DEFAULT_STA_SSID);
    ensureString(KEY_STA_PASS, DEFAULT_STA_PASS);
    ensureString(KEY_AP_SSID, DEFAULT_AP_SSID);
    ensureString(KEY_AP_PASS, DEFAULT_AP_PASS);
    ensureInt(KEY_WIFI_MODE, static_cast<int>(WiFiModeSetting::Sta));

    // Courant / ADC
    ensureFloat(KEY_CUR_ZERO, DEFAULT_CURRENT_ZERO_MV);
    ensureFloat(KEY_CUR_SENS, DEFAULT_CURRENT_SENS_MV_A);
    ensureFloat(KEY_CUR_SCALE, DEFAULT_CURRENT_INPUT_SCALE);
    ensureFloat(KEY_ADC_REF, DEFAULT_ADC_REF_V);
    ensureInt(KEY_ADC_MAX, DEFAULT_ADC_MAX);

    // OVC
    ensureFloat(KEY_LIM_CUR, DEFAULT_LIMIT_CURRENT_A);
    ensureInt(KEY_OVC_MODE, static_cast<int>(OvcMode::Latch));
    ensureUInt(KEY_OVC_MIN, DEFAULT_OVC_MIN_DURATION_MS);
    ensureUInt(KEY_OVC_RTRY, DEFAULT_OVC_RETRY_DELAY_MS);

    // Temperatures
    ensureFloat(KEY_TEMP_MOTOR, DEFAULT_TEMP_MOTOR_C);
    ensureFloat(KEY_TEMP_BOARD, DEFAULT_TEMP_BOARD_C);
    ensureFloat(KEY_TEMP_AMB, DEFAULT_TEMP_AMBIENT_C);
    ensureFloat(KEY_TEMP_HYST, DEFAULT_TEMP_HYST_C);
    ensureBool(KEY_LATCH_TEMP, DEFAULT_LATCH_OVERTEMP);

    // Exploitation
    ensureBool(KEY_RELAY_LAST, false);
    ensureBool(KEY_RESET_FLAG, true);
    ensureUInt(KEY_SAMPLING_HZ, DEFAULT_SAMPLING_HZ);
    ensureFloat(KEY_MOTOR_VCC, DEFAULT_MOTOR_VCC_V);
    ensureBool(KEY_BUZZ_EN, DEFAULT_BUZZER_ENABLED);

    // RTC / NTP
    ensureULong64(KEY_RTC_EPOCH, 0ULL);
    ensureString(KEY_TZ, DEFAULT_TZ_NAME);
    ensureInt(KEY_TZ_MIN, DEFAULT_TZ_OFFSET_MIN);
    ensureString(KEY_NTP_SERVER, DEFAULT_NTP_SERVER);
    ensureUInt(KEY_NTP_SYNC, DEFAULT_NTP_SYNC_INTERVAL_S);

    // Auth
    ensureString(KEY_AUTH_MODE, DEFAULT_AUTH_MODE);
    ensureString(KEY_AUTH_USER, DEFAULT_AUTH_USER);
    ensureString(KEY_AUTH_PASS, DEFAULT_AUTH_PASS);
    ensureString(KEY_AUTH_TOKEN, "");

    // Marche temporisee
    ensureUInt(KEY_RUN_DEFAULT, DEFAULT_RUN_DEFAULT_S);
    ensureUInt(KEY_RUN_MAX, DEFAULT_RUN_MAX_S);

    // Stockage SPIFFS
    ensureUInt(KEY_EVENT_MAX, DEFAULT_EVENTLOG_MAX_ENTRIES);
    ensureUInt(KEY_SESS_MAX, DEFAULT_SESSION_MAX_ENTRIES);
    ensureString(KEY_SPIFFS_SESS, DEFAULT_SPIFFS_SESS_FILE);
    ensureString(KEY_SPIFFS_EVT, DEFAULT_SPIFFS_EVT_FILE);

    unlock_();
}

// -----------------------------------------------------------------------------
// API publique
// -----------------------------------------------------------------------------
void NVS::begin() {
    DEBUG_PRINTLN("[NVS] Demarrage Preferences");
    lock_();
    ensureOpenRW_();
    unlock_();

    bool resetFlag = GetBool(KEY_RESET_FLAG, true);
    if (resetFlag) {
         DEBUG_PRINTLN("[NVS]  setting defaults");
        ensureDefaults_();
        PutBool(KEY_RESET_FLAG, false);
        RestartSysDelayDown(3000);
        
    };
     DEBUG_PRINTLN("[NVS] Use default!");
}

void NVS::end() {
    lock_();
    if (is_open_) {
        preferences.end();
        is_open_ = false;
        open_rw_ = false;
    }
    unlock_();
}

// -----------------------------------------------------------------------------
// Ecritures
// -----------------------------------------------------------------------------
void NVS::PutBool(const char* key, bool value) {
    lock_();
    ensureOpenRW_();
    preferences.putBool(key, value);
    unlock_();
}

void NVS::PutInt(const char* key, int value) {
    lock_();
    ensureOpenRW_();
    preferences.putInt(key, value);
    unlock_();
}

void NVS::PutUInt(const char* key, unsigned int value) {
    lock_();
    ensureOpenRW_();
    preferences.putUInt(key, value);
    unlock_();
}

void NVS::PutULong64(const char* key, uint64_t value) {
    lock_();
    ensureOpenRW_();
    preferences.putULong64(key, value);
    unlock_();
}

void NVS::PutFloat(const char* key, float value) {
    lock_();
    ensureOpenRW_();
    preferences.putFloat(key, value);
    unlock_();
}

void NVS::PutString(const char* key, const String& value) {
    lock_();
    ensureOpenRW_();
    preferences.putString(key, value);
    unlock_();
}

// -----------------------------------------------------------------------------
// Lectures
// -----------------------------------------------------------------------------
bool NVS::GetBool(const char* key, bool defaultValue) {
    ensureOpenRW_();
    return preferences.getBool(key, defaultValue);
}

int NVS::GetInt(const char* key, int defaultValue) {
    ensureOpenRW_();
    return preferences.getInt(key, defaultValue);
}

uint32_t NVS::GetUInt(const char* key, uint32_t defaultValue) {
    ensureOpenRW_();
    return preferences.getUInt(key, defaultValue);
}

uint64_t NVS::GetULong64(const char* key, uint64_t defaultValue) {
    ensureOpenRW_();
    return preferences.getULong64(key, defaultValue);
}

float NVS::GetFloat(const char* key, float defaultValue) {
    ensureOpenRW_();
    return preferences.getFloat(key, defaultValue);
}

String NVS::GetString(const char* key, const String& defaultValue) {
    ensureOpenRW_();
    return preferences.getString(key, defaultValue);
}

// -----------------------------------------------------------------------------
// Maintenance
// -----------------------------------------------------------------------------
void NVS::RemoveKey(const char* key) {
    lock_();
    ensureOpenRW_();
    if (preferences.isKey(key)) {
        preferences.remove(key);
    }
    unlock_();
}

void NVS::ClearAll() {
    lock_();
    ensureOpenRW_();
    preferences.clear();
    unlock_();
}
inline void NVS::sleepMs_(uint32_t ms) {
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        vTaskDelay(pdMS_TO_TICKS(ms));
    } else {
        delay(ms);
    }
}

// ======================================================
// System helpers / reboot paths
// ======================================================
void NVS::RestartSysDelayDown(unsigned long delayTime) {
    unsigned long interval = delayTime / 30;
    DEBUGGSTART();
    DEBUG_PRINTLN("###########################################################");
    DEBUG_PRINTLN("#           Restarting the Device in: " + String(delayTime / 1000)+ " Sec              #");
    DEBUG_PRINTLN("###########################################################");
    DEBUGGSTOP() ;
    for (int i = 0; i < 30; i++) {
        DEBUG_PRINT("#");
        sleepMs_(interval);
        esp_task_wdt_reset();
    }
    DEBUG_PRINTLN();
    DEBUG_PRINTLN("[NVS] Restarting now...");
    DEBUGGSTOP() ;
    simulatePowerDown();
}

void NVS::RestartSysDelay(unsigned long delayTime) {
    unsigned long interval = delayTime / 30;
    DEBUGGSTART() ;
    DEBUG_PRINTLN("###########################################################");
    DEBUG_PRINTLN("#           Restarting the Device in: " + String(delayTime / 1000)+ " Sec              #");
    DEBUG_PRINTLN("###########################################################");
    DEBUGGSTOP() ;
    for (int i = 0; i < 30; i++) {
        DEBUG_PRINT("#");
        sleepMs_(interval);
        esp_task_wdt_reset();
    }
    DEBUG_PRINTLN();
    DEBUG_PRINTLN("[NVS] Restarting now...");
    
    ESP.restart();
}

void NVS::CountdownDelay(unsigned long delayTime) {
    unsigned long interval = delayTime / 32;
    DEBUGGSTART() ;
    DEBUG_PRINTLN("###########################################################");
    DEBUG_PRINT("[NVS] Waiting User Action: ");
    DEBUG_PRINT(delayTime / 1000);
    DEBUG_PRINTLN(" Sec");
    DEBUGGSTOP() ;
    for (int i = 0; i < 32; i++) {
        DEBUG_PRINT("#");
        sleepMs_(interval);
        esp_task_wdt_reset();
    }
    DEBUG_PRINTLN();
    
}

void NVS::simulatePowerDown() {
    esp_sleep_enable_timer_wakeup(1000000); // 1s
    esp_deep_sleep_start();
}
