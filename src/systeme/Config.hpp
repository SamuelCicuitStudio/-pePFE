/**************************************************************
 *  Configuration centrale du firmware (ESP32-S3)
 *
 *  Note: commentaires en francais, caracteres ASCII uniquement.
 **************************************************************/
#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

// Namespace NVS
#define CONFIG_PARTITION "config"

// -----------------------------------------------------------------------------
// Mapping GPIO
// -----------------------------------------------------------------------------
// Relais de puissance (commande ON/OFF du moteur)
#define PIN_RELAY            7     // GPIO du relais
// LED indicatrice de surchauffe (etat fixe)
#define PIN_LED_OVERTEMP     15    // GPIO LED overtemp
// LED CMD (accuse reception + codes d'alerte en rafales)
#define PIN_LED_CMD          16    // GPIO LED CMD
// Buzzer (feedback utilisateur)
#define PIN_BUZZER           18    // GPIO buzzer
// Bus OneWire (capteur moteur DS18B20)
#define PIN_DS18B20          6     // GPIO OneWire
// Bus I2C (BME280)
#define PIN_I2C_SDA          5     // GPIO SDA
#define PIN_I2C_SCL          4     // GPIO SCL
// ADC (capteur courant ACS712)
#define PIN_CURRENT_ADC      1     // GPIO ADC
// Bouton Boot/User (marche/arret + reset long)
#define PIN_BUTTON           3     // GPIO bouton

// Polarite du relais:
// - true  : HIGH = ON,  LOW = OFF
// - false : LOW  = ON,  HIGH = OFF
#define RELAY_ACTIVE_HIGH    true

// -----------------------------------------------------------------------------
// Wi-Fi / mDNS
// -----------------------------------------------------------------------------
// Nom mDNS: http://contro.local/
#define MDNS_HOSTNAME        "contro"

// AP par defaut (fallback si STA echoue)
#define DEFAULT_AP_SSID      "contro"
#define DEFAULT_AP_PASS      "12345678"  // 8+ caracteres requis par WiFi.softAP

// STA par defaut (vide => pas de tentative STA)
#define DEFAULT_STA_SSID     "pboard"
#define DEFAULT_STA_PASS     "1234567890"

// Versions (exposees via /api/info)
#define DEVICE_SW_VERSION    "0.1.0"
#define DEVICE_HW_VERSION    "1.0.0"

// Nom logique du device (affichage UI)
#define DEFAULT_DEVICE_NAME  "contro"

// -----------------------------------------------------------------------------
// Echantillonnage et historique
// -----------------------------------------------------------------------------
// Nombre d'echantillons synchronises gardes en RAM (ring buffer)
#define BUS_SAMPLER_HISTORY_SIZE  800U

// Frequence d'echantillonnage par defaut (Hz)
#define DEFAULT_SAMPLING_HZ       50U

// Periode de rafraichissement du snapshot systeme (ms)
#define DEFAULT_SNAPSHOT_PERIOD_MS 250U

// -----------------------------------------------------------------------------
// Seuils et comportements par defaut
// -----------------------------------------------------------------------------
// Seuil surintensite (A)
#define DEFAULT_LIMIT_CURRENT_A    18.0f
// Duree minimale au-dessus du seuil avant declenchement OVC (ms)
#define DEFAULT_OVC_MIN_DURATION_MS 20U
// Delai avant tentative de reprise en mode AutoRetry (ms)
#define DEFAULT_OVC_RETRY_DELAY_MS  5000U

// Seuils temperature (degC)
#define DEFAULT_TEMP_MOTOR_C        85.0f
#define DEFAULT_TEMP_BOARD_C        70.0f
#define DEFAULT_TEMP_AMBIENT_C      60.0f
// Hysteresis temperature (degC) avant re-autorisation
#define DEFAULT_TEMP_HYST_C          5.0f
// true: surchauffe verrouillee (reset/clear_fault requis)
#define DEFAULT_LATCH_OVERTEMP       true

// Tension moteur (V) utilisee pour calculer la puissance: P = V * I
#define DEFAULT_MOTOR_VCC_V          12.0f

// ACS712ELCTR-20A-T (mesure +/-20A)
// Offset "0A" (mV) (typique: ~2500 mV a Vcc=5V)
#define DEFAULT_CURRENT_ZERO_MV      2500.0f
// Sensibilite (mV/A) (datasheet: ~100 mV/A pour ACS712 20A)
#define DEFAULT_CURRENT_SENS_MV_A     100.0f
// Ratio d'adaptation analogique:
//   input_scale = Vadc / Vsensor
// Ex: si un diviseur 2:1 est utilise => Vadc = Vsensor/2 => input_scale = 0.5
#define DEFAULT_CURRENT_INPUT_SCALE  1.0f
// Reference ADC (V) et resolution max (code)
#define DEFAULT_ADC_REF_V            5.0f
#define DEFAULT_ADC_MAX              4095

// Marche temporisee
// Duree par defaut et limite max (secondes)
#define DEFAULT_RUN_DEFAULT_S        60U
#define DEFAULT_RUN_MAX_S            3600U

// NTP / timezone
// Serveur NTP et periode de resync (secondes)
#define DEFAULT_NTP_SERVER           "pool.ntp.org"
#define DEFAULT_NTP_SYNC_INTERVAL_S  3600U

// Timezone:
// - DEFAULT_TZ_NAME peut contenir une regle TZ (ex: "CET-1CEST,M3.5.0,M10.5.0/3")
// - DEFAULT_TZ_OFFSET_MIN est un fallback simple (minutes)
#define DEFAULT_TZ_OFFSET_MIN        0
#define DEFAULT_TZ_NAME              "UTC"

// Epoch par defaut (0 => non calibre)
#define DEFAULT_RTC_EPOCH            0ULL

// Auth HTTP
// Mode: "basic" ou "token"
#define DEFAULT_AUTH_MODE            "basic"
// Identifiants par defaut (a changer en prod)
#define DEFAULT_AUTH_USER            "admin"
#define DEFAULT_AUTH_PASS            "admin123"

// Activation buzzer par defaut
#define DEFAULT_BUZZER_ENABLED       true

// SPIFFS
// Nombre max d'entrees en RAM (puis persistance JSON)
#define DEFAULT_EVENTLOG_MAX_ENTRIES 500U
#define DEFAULT_SESSION_MAX_ENTRIES  200U
// Fichiers JSON sur SPIFFS
#define DEFAULT_SPIFFS_SESS_FILE     "/sessions.json"
#define DEFAULT_SPIFFS_EVT_FILE      "/events.json"

// -----------------------------------------------------------------------------
// Temporisations LED CMD (clignotements rapides)
// -----------------------------------------------------------------------------
// Pattern:
// - Warning: 1 flash prefixe + pause + N flashes
// - Error  : 2 flashes prefixe + pause + N flashes
// - Pauses entre groupes/codes definies ci-dessous
#define CMD_LED_FLASH_ON_MS        120U
#define CMD_LED_FLASH_OFF_MS       120U
#define CMD_LED_PAUSE_GROUP_MS     600U
#define CMD_LED_PAUSE_CODE_MS      1500U

// -----------------------------------------------------------------------------
// Bouton
// -----------------------------------------------------------------------------
// Appui long (ms) => reset force (ESP.restart)
#define BUTTON_LONG_RESET_MS       10000U

// -----------------------------------------------------------------------------
// Etats systeme
// -----------------------------------------------------------------------------
// Etat global du device (expose via snapshot + API)
enum class DeviceState : uint8_t {
    Off = 0,
    Idle,
    Running,
    Fault,
    Shutdown
};

enum class OvcMode : uint8_t {
    Latch = 0,
    AutoRetry = 1
};

enum class WiFiModeSetting : uint8_t {
    Sta = 0,
    Ap = 1
};

enum class EventLevel : uint8_t {
    Warning = 1,
    Error   = 2
};

// Codes d'avertissement (Wxx)
enum class WarnCode : uint16_t {
    // Capteurs
    W01_Ds18Missing = 1,
    W02_BmeMissing  = 2,
    W03_AdcSat      = 3,
    W04_CacheUsed   = 4,
    // Temps
    W05_NtpFailed   = 5,
    W06_RtcNotSet   = 6,
    // Web
    W07_AuthFail    = 7,
    W08_Unauthorized= 8,
    W09_ClientGone  = 9
};

// Codes d'erreur (Exx)
enum class ErrorCode : uint16_t {
    // Protection
    E01_OvcLatched  = 1,
    E02_OverTemp    = 2,
    // Stockage
    E03_NvsWrite    = 3,
    E04_SpiffsWrite = 4,
    // Mesure/serveur
    E05_CurrentLost = 5,
    E06_WebDown     = 6
};

// -----------------------------------------------------------------------------
// NVS keys (6 caracteres max)
// -----------------------------------------------------------------------------
// IMPORTANT: Preferences impose une cle courte (ici <= 6 caracteres).
// Les commentaires ci-dessous servent de "schema" lisible.
#define KEY_DEV_ID        "DEVID"
#define KEY_DEV_NAME      "DEVNM"
#define KEY_DEV_HW        "DEVHW"
#define KEY_DEV_SW        "DEVSW"

#define KEY_STA_SSID      "STASS"
#define KEY_STA_PASS      "STAPS"
#define KEY_AP_SSID       "APSID"
#define KEY_AP_PASS       "APPAS"
#define KEY_WIFI_MODE     "WFMOD"

#define KEY_CUR_ZERO      "CZERO"
#define KEY_CUR_SENS      "CSENS"
#define KEY_CUR_SCALE     "CSCAL"
#define KEY_ADC_REF       "ADCRF"
#define KEY_ADC_MAX       "ADCMX"

#define KEY_LIM_CUR       "LIMIA"
#define KEY_OVC_MODE      "OVCMD"
#define KEY_OVC_MIN       "OVCMN"
#define KEY_OVC_RTRY      "OVCRT"

#define KEY_TEMP_MOTOR    "TMOT"
#define KEY_TEMP_BOARD    "TBOD"
#define KEY_TEMP_AMB      "TAMB"
#define KEY_TEMP_HYST     "THYS"
#define KEY_LATCH_TEMP    "TLAT"

#define KEY_RELAY_LAST    "RLYLS"
#define KEY_RESET_FLAG    "RSTFL"
#define KEY_SAMPLING_HZ   "SMPHZ"
#define KEY_MOTOR_VCC     "MVCC"
#define KEY_BUZZ_EN       "BUZEN"

#define KEY_RTC_EPOCH     "RTCEL"
#define KEY_TZ            "TIMEZ"
#define KEY_TZ_MIN        "TZMIN"
#define KEY_NTP_SERVER    "NTPSV"
#define KEY_NTP_SYNC      "NTPSI"

#define KEY_AUTH_MODE     "AUMOD"
#define KEY_AUTH_USER     "AUUSR"
#define KEY_AUTH_PASS     "AUPAS"
#define KEY_AUTH_TOKEN    "AUTOK"

#define KEY_RUN_DEFAULT   "RNDEF"
#define KEY_RUN_MAX       "RNMAX"

#define KEY_EVENT_MAX     "EVMAX"
#define KEY_SESS_MAX      "SSMAX"
#define KEY_SPIFFS_SESS   "SPSES"
#define KEY_SPIFFS_EVT    "SPEVT"

#endif // CONFIG_H
