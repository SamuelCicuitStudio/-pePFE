/**************************************************************
 *  Author      : Tshibangu Samuel
 *  Role        : Freelance Embedded Systems Engineer
 *  Expertise   : Secure IoT Systems, Embedded C++, RTOS, Control Logic
 *  Contact     : tshibsamuel47@gmail.com
 *  Portfolio   : https://www.freelancer.com/u/tshibsamuel477
 *  Phone       : +216 54 429 793
 **************************************************************/

#ifndef UTILS_H
#define UTILS_H

/**
 * @file Utils.h
 * @brief Utilitaires debug thread-safe (ESP32/Arduino).
 *
 * Fournit :
 *  - Sortie debug non bloquante et thread-safe via tache de fond + queue.
 *  - Impression "groupee" atomique (Debug::groupStart/Stop/Cancel) afin d'eviter
 *    que plusieurs taches melangent leurs traces sur le port serie.
 *  - Journal memo (optionnel) en RAM pour exporter des logs via HTTP.
 */


// ===================== Activation debug globale =====================

#ifndef DEBUGMODE
#define DEBUGMODE true          ///< Activation/Desactivation compile-time du debug
#endif

#ifndef SERIAL_BAUD_RATE
#define SERIAL_BAUD_RATE 250000 ///< Vitesse serie par defaut pour Debug::begin()
#endif
#include <Arduino.h>
#include <pgmspace.h>
#include <stdarg.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
// ===================== API debug thread-safe =====================

namespace Debug {
    // Initialisation (souvent auto-appelee lors du premier print)
    void begin(unsigned long baud = SERIAL_BAUD_RATE);
    void enableMemoryLog(size_t maxBytes = 1048576);
    void disableMemoryLog();
    void clearMemoryLog();
    bool readMemoryLog(String& out, size_t maxBytes = 0);
    bool writeMemoryLog(Print& out, size_t maxBytes = 0);
    size_t memoryLogSize();
    size_t memoryLogCapacity();

    // Sortie string
    void print(const char* s);
    void print(const String& s);
    void print(const __FlashStringHelper* fs);      // F("...")
    void println(const char* s);
    void println(const String& s);
    void println(const __FlashStringHelper* fs);    // F("...")
    void println();                                 // ligne vide

    // Sortie numerique
    void print(int32_t v);
    void print(uint32_t v);
    void print(int64_t v);
    void print(uint64_t v);
    void print(long v);
    void print(unsigned long v);
    void print(float v);
    void print(double v);

    // Numerique avec precision (style Arduino)
    void print(float v, int digits);
    void print(double v, int digits);
    void println(float v, int digits);
    void println(double v, int digits);

    void println(int32_t v);
    void println(uint32_t v);
    void println(int64_t v);
    void println(uint64_t v);
    void println(long v);
    void println(unsigned long v);
    void println(float v);
    void println(double v);

    // printf-style
    void printf(const char* fmt, ...);

    // Acces au mutex serie (si un module doit ecrire directement sur Serial).
    SemaphoreHandle_t serialMutex();

    // ===== Impression groupee (burst atomique) =====

    /**
     * @brief Demarre une section d'impression groupee.
     *
     * La tache appelante devient proprietaire du "groupe".
     * Les Debug::print* suivants depuis cette tache s'accumulent dans un buffer
     * interne jusqu'a groupStop() ou groupCancel().
     */
    void groupStart();

    /**
     * @brief Envoie le contenu groupe en un seul bloc et libere la propriete.
     * @param addTrailingNewline Si true, ajoute un saut de ligne a la fin.
     */
    void groupStop(bool addTrailingNewline = false);

    /**
     * @brief Annule le groupe courant (on jette le buffer sans envoyer).
     */
    void groupCancel();
}

// ===================== Macros debug (usage simplifie) =====================

#if DEBUGMODE

    #define DEBUG_PRINT(...)      Debug::print(__VA_ARGS__)
    #define DEBUG_PRINTLN(...)    Debug::println(__VA_ARGS__)
    #define DEBUG_PRINTF(...)     Debug::printf(__VA_ARGS__)

    #ifndef DEBUGGSTART
    #define DEBUGGSTART()         Debug::groupStart()
    #endif

    #ifndef DEBUGGSTOP
    #define DEBUGGSTOP()          Debug::groupStop(false)
    #endif

#else

    #define DEBUG_PRINT(...)      do {} while (0)
    #define DEBUG_PRINTLN(...)    do {} while (0)
    #define DEBUG_PRINTF(...)     do {} while (0)
    #define DEBUGGSTART()         do {} while (0)
    #define DEBUGGSTOP()          do {} while (0)

#endif // DEBUGMODE

#endif // UTILS_H

