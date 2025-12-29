# Firmware de surveillance courant/puissance moteur (ESP32-S3)

Ce firmware surveille le courant moteur et les temperatures, calcule la puissance/energie et applique une protection surintensite (OVC) avec verrouillage. Il expose une interface HTTP via Wi-Fi (STA ou AP) pour les donnees en direct, la configuration et la calibration.

## Cartographie materielle

| Fonction | GPIO | Notes |
| --- | --- | --- |
| Commande relais | 7 | Relais unique |
| LED surchauffe | 15 | Allumee fixe en cas de surchauffe ou defaut temperature | 
| LED CMD | 16 | Clignote a la reception d'une commande et en rafales rapides pour avertissements/erreurs | 
| Buzzer | 18 | Retour audio et alarmes |
| DS18B20 (OneWire) | 6 | Temperature moteur |
| BME280 SDA | 5 | Donnees I2C |
| BME280 SCL | 4 | Horloge I2C |
| ADC capteur courant | 1 | Sortie analogique ACS712ELCTR-20A-T |
| Bouton Boot/User | 0 | Marche/Arret local et acquittement defaut |

## Objectifs du systeme

- Surveiller le courant moteur et calculer puissance/energie.
- Surveiller la temperature moteur (DS18B20) et la temperature/pression carte + ambiante (BME280).
- Diagnostic sante capteurs (DS18B20/BME280 absents, saturation ADC) avec codes d'erreur clairs.
- Appliquer une protection surintensite (OVC) avec verrouillage jusqu'a nouvel ON.
- Permettre de configurer le comportement OVC (verrouillage vs auto-reprise, seuils, temporisations).
- Calibrer l'horloge RTC interne via HTTP et l'utiliser pour horodatage des sessions.
- Lancer le moteur pour une duree definie (marche temporisee).
- Fournir donnees temps reel, modification parametres et calibration via HTTP.
- Afficher des graphiques (courant/temps, temp moteur/temps) et pousser des notifications avertissement/erreur vers l'interface web.
- Mettre en cache les dernieres valeurs capteurs pour conserver l'affichage en cas d'echec de lecture.
- Auto-recuperation DS18B20 (re-scan OneWire en cas de deconnexion).
- Controle d'acces pour endpoints config/controle (token/session ou basic auth).
- Journal persistant avertissements/erreurs en SPIFFS (separe de l'historique sessions).
- Synchronisation NTP en mode STA (reglage manuel toujours possible) avec stockage timezone.
- Persister toute la configuration en NVS, Device est le seul ecrivain.
- Proteger les sections critiques avec semaphores (FreeRTOS).

## Architecture

### Modules principaux

- Device
  - Coordinateur central et machine d'etats.
  - Seul ecrivain NVS (toutes les ecritures passent par Device).
  - Gere relais et securites (OVC/surchauffe).
  - Suit la sante capteurs et remonte des codes d'erreur.
  - Construit un snapshot centralise de l'etat systeme (SystemSnapshot).
  - Publie snapshots et historique vers WiFiManager.

- Peripherals (classes dediees)
  - Relay : controle relais unique, arret par defaut.
  - Ds18b20Sensor : temperature moteur (OneWire direct, capteur unique), cache derniere valeur valide, auto-reconnexion.
  - Bme280Sensor : temperature/pression carte + ambiante (I2C), cache derniere valeur valide.
  - Acs712Sensor : mesure courant et calibration, cache derniere valeur valide.
  - Buzzer : retour audio.
  - StatusLeds : LED surchauffe + LED CMD (clignotements rapides avertissements/erreurs).

- NvsManager (NVS)
  - Stocke le mapping GPIO et tous les parametres persistants.
  - Acces thread-safe via semaphore.

- RTCManager
  - Maintient l'heure systeme (epoch + date/heure formatee).
  - Supporte la calibration RTC via HTTP.
  - Synchronise via NTP en mode STA et stocke la timezone.

- DeviceTransport
  - Pont de messages entre WiFiManager/SwitchManager et Device.
  - File des requetes de controle et retourne les resultats.

- BusSampler
  - Aligne dans le temps courant, DS18B20 et BME280.
  - Stocke un buffer circulaire de 800 echantillons avec horodatage commun.

- SessionHistory
  - Enregistre les sessions terminees en JSON dans SPIFFS.
  - Fournit l'historique des sessions (duree, energie, pics).

- EventLog
  - Journal persistant avertissements/erreurs en SPIFFS.
  - Separe de SessionHistory, utilise pour les notifications UI.

- CurrentSensor (ACS712ELCTR-20A-T)
  - Offset zero et sensibilite calibres (100 mV/A nominal a 5 V).
  - Utilise les donnees de calibration depuis NVS.

- WiFiManager
  - Demarre AP et/ou STA, heberge le serveur HTTP.
  - Tente STA en premier, bascule en AP si echec.
  - Nom mDNS : contro.local.
  - Sert donnees live, config et endpoints de calibration.
  - Applique le controle d'acces sur config/controle.
  - Diffuse les evenements avertissement/erreur vers l'UI.

- SwitchManager
  - Gere le bouton Boot/User.
  - Envoie ON/OFF et acquittement defaut via DeviceTransport.
  - Appui long > 10 s force un reset systeme.

- Relay, Buzzer, Status LEDs
  - Relay controle le chemin de puissance moteur.
  - LED CMD clignote a chaque commande acceptee (hors emission de codes d'alerte).
  - LED surchauffe indique les defauts temperature.
  - Buzzer fournit un feedback audio (activation optionnelle NVS).

### Flux de donnees (niveau haut)

1. Les capteurs alimentent BusSampler (lectures alignees dans le temps).
2. Device lit l'historique BusSampler et applique les protections.
3. Device produit un snapshot pour WiFiManager.
4. WiFiManager sert snapshots/historique et transmet les commandes.
5. Toutes les ecritures de configuration passent par Device -> NVS.

### Modele de concurrence

- Toutes les ressources partagees (NVS, buffers, etat device, caches capteurs) sont protegees par semaphores FreeRTOS.
- Device possede les changements d'etat, les autres modules lisent via accesseurs ou DeviceTransport.

## Organisation des dossiers (src)

- systeme/ : coeur (Device, Config, StatusSnapshot, Utils, DeviceTransport).
- capteurs/ : DS18B20 (unique), BME280, ACS712, BusSampler.
- actionneurs/ : relais.
- controle/ : LEDs + buzzer.
- communication/reseau/ : WiFiManager (HTTP, STA/AP, mDNS).
- communication/entrees/ : SwitchManager (bouton).
- services/ : NVS, RTC, SessionHistory, EventLog, SleepTimer, PowerTracker.

## Snapshot systeme centralise

Le snapshot est une structure unique, mise a jour par Device a intervalle fixe (ex: 200-500 ms). Les autres modules ne font que lire ce snapshot sous semaphore.

### Regles

- Device est l'unique ecrivain.
- Lecture atomique sous semaphore.
- Les capteurs fournissent des valeurs "dernieres valides" (cache) pour eviter les trous.
- Un compteur `seq` permet a l'UI de detecter les changements.

### Structure proposee (C++)

```cpp
struct SystemSnapshot {
  uint32_t seq;
  uint32_t ts_ms;
  uint32_t age_ms;
  uint8_t  state;         // Off, Idle, Running, Fault, Shutdown
  bool     fault_latched;

  bool     relay_on;
  float    current_a;
  float    power_w;
  float    energy_wh;

  float    motor_c;
  float    board_c;
  float    ambient_c;

  bool     ds18_ok;
  bool     bme_ok;
  bool     adc_ok;

  uint16_t last_warning;
  uint16_t last_error;
};
```

### Exemple JSON (/api/status)

```json
{
  "seq": 1284,
  "ts_ms": 452318,
  "age_ms": 120,
  "state": "Running",
  "fault_latched": false,
  "relay_on": true,
  "current_a": 4.72,
  "power_w": 56.6,
  "energy_wh": 0.84,
  "motor_c": 48.2,
  "board_c": 36.4,
  "ambient_c": 32.1,
  "ds18_ok": true,
  "bme_ok": true,
  "adc_ok": true,
  "last_warning": "W03",
  "last_error": ""
}
```

## Conservation des donnees capteurs et recuperation

- Chaque capteur conserve une derniere valeur valide en RAM.
- En cas d'echec de lecture, la valeur en cache est renvoyee et un avertissement est journalise.
- DS18B20 integre une logique de reconnexion : re-scan du bus OneWire et rebinding en cas de deconnexion.

## Machine d'etats et protections

- Etats : Off, Idle, Running, Fault (verrouille), Shutdown.
- Comportement bouton Boot/User :
  - Appui ON depuis Off/Idle -> demande Running.
  - Appui OFF depuis Running -> demande Idle.
  - Si Fault verrouille -> appui ON pour acquitter et re-activer.

### Surintensite (OVC)

- Declenchement : |I| >= current_limit_a (depuis NVS).
- Action : relais OFF, defaut verrouille.
- Acquittement : appui ON (bouton ou HTTP control).
- Comportement configurable (mode + timing), par ex. :
  - Verrouillage (reset manuel obligatoire).
  - Auto-reprise apres cooldown (optionnel).

### Surchauffe

- Entrees :
  - Temperature moteur via DS18B20.
  - Temperature/ambiante via BME280.
- Si une temperature depasse le seuil :
  - LED surchauffe ON.
  - Relais OFF (verrouillage optionnel configurable en NVS).

## Codes d'avertissement et d'erreur (LED CMD + buzzer + web)

Chaque evenement a un code stable Wxx (warning) ou Exx (error). Les erreurs provoquent un arret et peuvent etre verrouillees, les warnings n'arretent pas le systeme.

### Signalisation LED CMD (clignotements rapides)

- La LED CMD emet des rafales rapides pour les warnings/erreurs.
- Le clignotement "commande acceptee" est suspendu pendant l'emission des codes.
- Priorite : erreurs > warnings. Si plusieurs codes sont actifs, emettre en sequence.
- Format : prefixe type puis numero du code.
  - Warning (W) : 1 flash rapide, pause courte, puis N flashes rapides.
  - Error (E) : 2 flashes rapides, pause courte, puis N flashes rapides.
- Durees recommandees :
  - flash rapide : 120 ms ON, 120 ms OFF
  - pause inter-groupe : 600 ms OFF
  - pause inter-code : 1500 ms OFF

### Buzzer (definitions)

Patterns de base utilises par la classe Buzzer :

- WARN : 2 bips courts (100 ms ON, 100 ms OFF, 100 ms ON)
- ERROR : 3 bips longs (400 ms ON, 200 ms OFF) x3
- LATCH : 1 bip long + 2 courts (400 ms ON, 150 ms OFF, 100 ms ON, 150 ms OFF, 100 ms ON), repete toutes les 5 s jusqu'a acquittement
- CLIENT_CONNECT : 1 bip court (120 ms ON)
- CLIENT_DISCONNECT : 2 bips courts (80 ms ON, 80 ms OFF, 80 ms ON)
- AUTH_FAIL : 1 bip long (400 ms ON)

### Liste des warnings/erreurs

| Code | Type | Condition | Clignotements CMD | Buzzer |
| --- | --- | --- | --- | --- |
| W01 | warning | DS18B20 absent/deconnecte (auto-reconnexion active) | W: 1 + 1 | WARN |
| W02 | warning | BME280 absent ou erreur I2C | W: 1 + 2 | WARN |
| W03 | warning | ADC saturation / courant hors plage | W: 1 + 3 | WARN |
| W04 | warning | Lecture capteur invalide, valeur cachee utilisee | W: 1 + 4 | WARN |
| W05 | warning | NTP echec en STA ou timezone non definie | W: 1 + 5 | WARN |
| W06 | warning | RTC non calibre (epoch par defaut) | W: 1 + 6 | WARN |
| W07 | warning | Authentification echouee (identifiants invalides) | W: 1 + 7 | AUTH_FAIL |
| W08 | warning | Acces non autorise a un endpoint protege | W: 1 + 8 | WARN |
| W09 | warning | Deconnexion client HTTP/WebUI | W: 1 + 9 | CLIENT_DISCONNECT |
| E01 | error | OVC verrouille (surintensite) | E: 2 + 1 | LATCH |
| E02 | error | Surchauffe verrouillee (moteur ou carte) | E: 2 + 2 | LATCH |
| E03 | error | Echec ecriture NVS (config non persistee) | E: 2 + 3 | ERROR |
| E04 | error | Echec ecriture SPIFFS (sessions/events) | E: 2 + 4 | ERROR |
| E05 | error | Capteur courant indisponible en Running | E: 2 + 5 | ERROR |
| E06 | error | Serveur web indisponible ou en panne | E: 2 + 6 | ERROR |

### Notifications web

- Les memes codes Wxx/Exx sont envoyes via `/api/events`.
- Champs minimaux : `seq`, `ts_ms`, `level`, `code`, `message`, `source`, `data` (optionnel).

## Echantillonnage et historique

BusSampler stocke jusqu'a 800 echantillons synchronises :

- ts_ms
- current_a
- motor_c (DS18B20)
- bme_c (carte/ambiante)
- bme_pa

La frequence d'echantillonnage est configuree dans NVS (sampling_hz). L'historique est fixe a 800 dans le firmware.
Les echantillons restent en RAM (pas de persistence). Seules les sessions sont persistees en SPIFFS.

## Suivi de puissance

La puissance est calculee via une tension VCC fixe (NVS) :

- power_w = motor_vcc_v * current_a
- energy_wh = somme(power_w * dt_s) / 3600

Si motor_vcc_v n'est pas defini, puissance/energie renvoient 0 ou NaN (choix d'implementation).

Les totaux et statistiques "live" restent en RAM et sont remis a zero au reboot.
Seules les sessions terminees sont persistees en SPIFFS (JSON).

## Marche temporisee

Device supporte un mode de marche temporisee :

- demarrer pour N secondes (ou minutes), puis arreter automatiquement.
- session enregistree dans SessionHistory en fin normale ou en defaut.

## Schema NVS (cles logiques)

Toutes les cles sont persistantes et initialisees au premier boot. Device est le seul ecrivain.

### Mapping GPIO

- pin.relay = 7
- pin.led_overtemp = 15
- pin.led_cmd = 16
- pin.buzzer = 18
- pin.onewire = 6
- pin.i2c_sda = 5
- pin.i2c_scl = 4
- pin.current_adc = 1
- pin.button = 0

### Identite et Wi-Fi

- device.id
- device.name
- device.hw_version
- device.sw_version
- wifi.sta.ssid
- wifi.sta.pass
- wifi.ap.ssid
- wifi.ap.pass
- wifi.mode (sta/ap)

### Calibration et limites

- current.zero_mv
- current.sens_mv_a (defaut 100.0)
- current.input_scale (ratio entree ADC / sortie capteur)
- current.adc_ref_v (reference ADC ESP32)
- current.adc_max
- limit.current_a
- ovc.mode (latch/auto)
- ovc.min_duration_ms
- ovc.retry_delay_ms (si auto-reprise)
- limit.temp_motor_c
- limit.temp_board_c
- limit.temp_ambient_c (optionnel)
- limit.temp_hyst_c
- fault.latch_overtemp (true/false)

### Exploitation

- relay.last_state
- sampling.hz
- motor.vcc_v
- buzzer.enabled
- rtc.epoch_last (dernier epoch enregistre)
- time.tz (timezone ou offset)
- time.tz_offset_min
- ntp.server
- ntp.sync_interval_s
- auth.mode
- auth.user
- auth.pass
- auth.token (optionnel)
- run.default_s
- run.max_s
- eventlog.max_entries
- session.max_entries
- spiffs.sessions_file
- spiffs.events_file

Note : les tokens de cles en code doivent rester <= 6 caracteres pour respecter la limite NVS.

## Valeurs par defaut proposees (pour demarrer l'implementation)

- limit.current_a = 18.0
- ovc.mode = "latch"
- ovc.min_duration_ms = 20
- ovc.retry_delay_ms = 5000
- limit.temp_motor_c = 85.0
- limit.temp_board_c = 70.0
- limit.temp_ambient_c = 60.0
- limit.temp_hyst_c = 5.0
- sampling.hz = 50
- snapshot.period_ms = 250 (const firmware)
- motor.vcc_v = 12.0
- current.zero_mv = 2500.0
- current.sens_mv_a = 100.0
- current.input_scale = 1.0 (mettre le ratio si diviseur)
- current.adc_ref_v = 3.3
- current.adc_max = 4095
- run.default_s = 60
- run.max_s = 3600
- ntp.server = "pool.ntp.org"
- ntp.sync_interval_s = 3600
- time.tz_offset_min = 0
- auth.mode = "basic"
- auth.user = "admin"
- auth.pass = "admin123"
- eventlog.max_entries = 500
- session.max_entries = 200
- spiffs.sessions_file = "/sessions.json"
- spiffs.events_file = "/events.json"

## API HTTP (proposee)

Tous les endpoints acceptent et renvoient du JSON. WiFiManager route toutes les ecritures via DeviceTransport.

- GET /api/info
  - Identite device et versions firmware.

- GET /api/status
  - Snapshot live : etat relais, courant, temperatures, puissance, flags defaut, seq echantillon.

- GET /api/history?since=SEQ&max=N
  - Echantillons du buffer depuis SEQ (jusqu'a N ou defaut).

- GET /api/events?since=SEQ&max=N
  - Evenements avertissement/erreur pour notifications UI (journal SPIFFS).

- GET /api/config
  - Configuration actuelle depuis NVS.

- POST /api/config
  - Mise a jour config (limites, credentials Wi-Fi, sampling rate, motor VCC, buzzer).

- POST /api/control
  - Actions : relay_on, relay_off, clear_fault.

- POST /api/calibrate
  - Actions : current_zero, current_sensitivity (avec courant connu).

- POST /api/rtc
  - Regler l'heure RTC (epoch ou champs date/heure).

- POST /api/run_timer
  - Demarrer une marche temporisee (duree en secondes).

- GET /api/sessions
  - Historique des sessions depuis JSON SPIFFS.

La LED CMD clignote a chaque commande acceptee (hors emission de codes d'alerte).

## Notes de calibration (ACS712ELCTR-20A-T)

- Capteur alimente en 5 V, sensibilite nominale 100 mV/A.
- Le zero courant est autour de 2.5 V, calibrer pour mesurer le vrai milieu.
- Si la sortie depasse la plage ADC ESP32, utiliser diviseur ou adaptation de niveau.
- La calibration stocke zero_mv et sens_mv_a en NVS.

## Build et dependances

Cible : Arduino framework sur ESP32-S3 (PlatformIO).

Librairies requises :
- ESPAsyncWebServer
- OneWire
- DallasTemperature
- BME280 library (Adafruit_BME280 ou equivalent)
- ArduinoJson

## Notes

- Ce README definit le comportement firmware et les responsabilites des modules.
- L'ancien code Powerboard ne correspond pas a ce cahier des charges.

## Ordre d'implementation (valide pour demarrage)

1. Config (pins, constantes, enums)
2. NVS (schema et valeurs par defaut)
3. Peripherals (relais, capteurs, buzzer, leds, bus sampler)
4. Device (etat, protections, snapshot)
5. DeviceTransport (commandes et etats)
6. WiFiManager (API, auth, streaming)
