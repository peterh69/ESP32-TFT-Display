# ESP32-S3 Modul – Beschreibung

## Identifikation

| Eigenschaft             | Wert                                      |
|-------------------------|-------------------------------------------|
| Board                   | ESP32-S3-DevKitC-1 (oder kompatibel)      |
| Chip                    | ESP32-S3 (QFN56), Revision v0.2           |
| Chip-Variante           | ESP32-S3R8 (mit 8 MB eingebettetem PSRAM) |
| Kerne                   | Dual Core Xtensa LX7 + LP Core, 240 MHz  |
| Wireless                | Wi-Fi 802.11 b/g/n, Bluetooth 5 (LE)     |
| Eingebettetes PSRAM     | 8 MB (Octal SPI, 3,3 V)                  |
| Externer Flash          | 16 MB (Quad-SPI, 3,3 V)                  |
| Kristall                | 40 MHz                                    |
| MAC-Adresse             | b8:f8:62:e2:d6:d0                         |

## Schnittstellen am Host-PC

| Port          | Gerät          | Funktion                               |
|---------------|----------------|----------------------------------------|
| `/dev/ttyACM0`| Espressif native USB (VID 303a, PID 1001) | USB JTAG / serieller Debug-Port (Programmier- und Debug-Port) |
| `/dev/ttyUSB0`| Silicon Labs CP2102 (VID 10c4, PID ea60)  | UART–USB-Brücke (zweiter serieller Port) |

## Flash-Information

| Eigenschaft      | Wert                     |
|------------------|--------------------------|
| Hersteller-ID    | 0x5E                     |
| Device-ID        | 0x4018                   |
| Größe            | 16 MB                    |
| SPI-Modus        | Quad (4 Datenleitungen)  |
| Versorgungsspg.  | 3,3 V                    |

## Ermittlung

Identifiziert am 2026-03-10 mit:
```
esptool v5.1.0 --port /dev/ttyACM0 chip_id
esptool v5.1.0 --port /dev/ttyACM0 flash-id
```

## Relevante Datenblätter

| Dokument                                  | Datei                                      | Quelle         |
|-------------------------------------------|--------------------------------------------|----------------|
| ESP32-S3 Series Datasheet v2.0            | `datasheets/esp32-s3_datasheet_en.pdf`     | Adafruit CDN   |
| ESP32-S3 Technical Reference Manual       | `datasheets/esp32-s3_technical_reference_manual_en.pdf` | Adafruit CDN |
| ESP32-S3 Hardware Design Guidelines       | `datasheets/esp32-s3_hardware_design_guidelines.pdf` | docs.espressif.com |
| CP210x USB-to-UART Bridge Datasheet       | `datasheets/cp210x_datasheet.pdf`          | silabs.com     |

## Onboard-Peripherie

| Komponente       | GPIO  | Typ                  |
|------------------|-------|----------------------|
| RGB-LED          | 48    | NeoPixel (WS2812B)   |

## Hinweise zur Entwicklung

- **Programmierport**: `/dev/ttyACM0` – für `esptool`, `idf.py flash`, Arduino IDE
- **UART-Port**: `/dev/ttyUSB0` – typischerweise UART0 des ESP32-S3 (GPIO43/TX, GPIO44/RX)
- **Bootloader-Modus**: Da der Programmierport nativer USB-JTAG ist, ist kein manueller Boot-Taster nötig
- **Partition Table**: Mit 16 MB Flash steht viel Speicher für OTA, Dateisystem (LittleFS/SPIFFS) etc. zur Verfügung
- **PSRAM**: 8 MB PSRAM ist für speicherintensive Anwendungen (z. B. Bildverarbeitung, große Puffer) nutzbar
