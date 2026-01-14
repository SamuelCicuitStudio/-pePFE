#include <RTCManager.hpp>
#include <time.h>

RTCManager* RTCManager::s_instance = nullptr;

static void applyTimezone_() {
    // Timezone :
    // - Si KEY_TZ contient un nom TZ valide (ex: "CET-1CEST,M3.5.0,M10.5.0/3"),
    //   on l'applique via setenv("TZ", ...).
    // - Sinon on utilise un format simplifie "UTC+H" base sur KEY_TZ_MIN.
    String tz = CONF->GetString(KEY_TZ, DEFAULT_TZ_NAME);
    int offsetMin = CONF->GetInt(KEY_TZ_MIN, DEFAULT_TZ_OFFSET_MIN);

    if (tz.length() > 0 && tz != DEFAULT_TZ_NAME) {
        setenv("TZ", tz.c_str(), 1);
    } else {
        // Format simple: UTC+H ou UTC-H
        int hours = offsetMin / 60;
        char buf[16];
        snprintf(buf, sizeof(buf), "UTC%+d", hours);
        setenv("TZ", buf, 1);
    }
    tzset();
}

void RTCManager::Init() {
    if (!s_instance) {
        s_instance = new RTCManager();
    }
}

RTCManager* RTCManager::Get() {
    if (!s_instance) {
        s_instance = new RTCManager();
    }
    return s_instance;
}

RTCManager* RTCManager::TryGet() {
    return s_instance;
}

RTCManager::RTCManager() {
    mutex_ = xSemaphoreCreateMutex();
    applyTimezone_();

    // Restaure l'epoch sauvegarde en NVS (si disponible).
    uint64_t saved = CONF->GetULong64(KEY_RTC_EPOCH, DEFAULT_RTC_EPOCH);
    if (saved > 0) {
        setUnixTime(saved);
    } else {
        // Pas d'epoch connu -> on initialise le cache avec time() courant
        // (souvent 0 au boot si pas encore regle).
        update();
    }
}

void RTCManager::setUnixTime(uint64_t epoch) {
    if (epoch == 0) return;

    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
        // Met l'horloge systeme.
        struct timeval tv;
        tv.tv_sec = static_cast<time_t>(epoch);
        tv.tv_usec = 0;
        settimeofday(&tv, nullptr);

        // Persist en NVS pour redemarrage.
        CONF->PutULong64(KEY_RTC_EPOCH, epoch);

        // Met a jour les chaines cachees.
        update();
        xSemaphoreGive(mutex_);
    }
}

uint64_t RTCManager::getUnixTime() {
    uint64_t epoch = 0;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
        time_t now = time(nullptr);
        if (now > 0) epoch = static_cast<uint64_t>(now);
        xSemaphoreGive(mutex_);
    }
    return epoch;
}

void RTCManager::update() {
    // Met a jour timeStr_/dateStr_ (cache) a partir de time().
    time_t now = time(nullptr);
    if (now <= 0) return;

    struct tm tmv;
    localtime_r(&now, &tmv);

    char bufTime[16];
    char bufDate[16];
    strftime(bufTime, sizeof(bufTime), "%H:%M", &tmv);
    strftime(bufDate, sizeof(bufDate), "%Y-%m-%d", &tmv);

    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
        timeStr_ = bufTime;
        dateStr_ = bufDate;
        xSemaphoreGive(mutex_);
    }
}

String RTCManager::getTime() {
    String v;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
        v = timeStr_;
        xSemaphoreGive(mutex_);
    }
    return v;
}

String RTCManager::getDate() {
    String v;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
        v = dateStr_;
        xSemaphoreGive(mutex_);
    }
    return v;
}

void RTCManager::setRTCTime(int year, int month, int day,
                            int hour, int minute, int second) {
    // Conversion Y/M/D H:M:S -> epoch (mktime applique la timezone active).
    struct tm tmv{};
    tmv.tm_year = year - 1900;
    tmv.tm_mon = month - 1;
    tmv.tm_mday = day;
    tmv.tm_hour = hour;
    tmv.tm_min = minute;
    tmv.tm_sec = second;

    time_t t = mktime(&tmv);
    if (t > 0) {
        setUnixTime(static_cast<uint64_t>(t));
    }
}
