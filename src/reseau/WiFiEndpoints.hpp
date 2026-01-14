/**************************************************************
 *  WiFiEndpoints - endpoints HTTP pour l'API actuelle
 *
 *  Ce fichier centralise les routes /api/* exposees par WiFiManager.
 *  Objectif : eviter les "magic strings" dispersees.
 **************************************************************/
#ifndef WIFI_ENDPOINTS_H
#define WIFI_ENDPOINTS_H

// ===== Endpoints API =====
#define EP_API_INFO        "/api/info"
#define EP_API_STATUS      "/api/status"
#define EP_API_HISTORY     "/api/history"
#define EP_API_EVENTS      "/api/events"
#define EP_API_CONFIG      "/api/config"
#define EP_API_CONTROL     "/api/control"
#define EP_API_CALIBRATE   "/api/calibrate"
#define EP_API_RTC         "/api/rtc"
#define EP_API_RUN_TIMER   "/api/run_timer"
#define EP_API_SESSIONS    "/api/sessions"

// ===== Headers utiles =====
#define HDR_AUTH_TOKEN     "X-Auth-Token"

// ===== Content types =====
#define CT_APP_JSON        "application/json"

#endif // WIFI_ENDPOINTS_H
