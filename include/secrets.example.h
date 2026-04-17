/**
 * ============================================================================
 * secrets.example.h - Vorlage fuer secrets.h
 * ============================================================================
 *
 * Kopieren nach include/secrets.h und eigene Werte eintragen:
 *   cp include/secrets.example.h include/secrets.h
 *
 * include/secrets.h steht in .gitignore und wird nicht eingecheckt.
 * ============================================================================
 */
#pragma once

// WLAN-Zugangsdaten
#define WIFI_SSID     "DEIN_WLAN_SSID"
#define WIFI_PASSWORD "DEIN_WLAN_PASSWORT"

// OTA (ArduinoOTA + Web-OTA) - Hostname und Passwort
#define OTA_HOSTNAME  "radio10"
#define OTA_PASSWORD  "radio10"
