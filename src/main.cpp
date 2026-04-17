/**
 * ============================================================================
 * Radio 10 Webradio-Player - ESP32-S3
 * ============================================================================
 *
 * Beschreibung:
 *   Internet-Radio-Player fuer Radio 10 (NL) und seine Themenkanaele.
 *   Touch-Kalibrierung bei jedem Start, danach Senderwahl per Touch.
 *
 * Hardware:
 *   - Board:   ESP32-S3-DevKitC-1
 *   - Display: HSD-9190J-C7 (ST7796S + XPT2046), SPI
 *   - Audio:   MAX98357A, I2S (DIN=GPIO6, BCLK=GPIO4, LRC=GPIO5)
 *
 * Autor:   peterh69
 * Datum:   2026-04-10
 * Lizenz:  MIT
 * ============================================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <AudioFileSourceICYStream.h>
#include <AudioFileSourceBuffer.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutputI2S.h>

// WLAN- und OTA-Zugangsdaten ausgelagert nach include/secrets.h
// (steht in .gitignore - Vorlage: include/secrets.example.h)
#include "secrets.h"

TFT_eSPI tft = TFT_eSPI();
#define TFT_BL_PIN 14

WebServer webOtaServer(80);

AudioFileSourceICYStream *stream = nullptr;
AudioFileSourceBuffer    *puffer = nullptr;
AudioGeneratorMP3        *mp3    = nullptr;
AudioOutputI2S           *ausgang = nullptr;
#define AUDIO_PUFFER_GROESSE 8192

uint16_t calData[5];
Preferences prefs;
static const char *CAL_NS     = "touch";
static const char *CAL_KEY    = "caldata";
static const char *RADIO_NS   = "radio";
static const char *SENDER_KEY = "sender";

struct Sender {
    const char *name;
    const char *url;
    uint16_t   farbe;
};

static Sender sender[] = {
    {"Radio 10",    "http://playerservices.streamtheworld.com/api/livestream-redirect/RADIO10.mp3",   0x001F},
    {"80s Hits",    "http://playerservices.streamtheworld.com/api/livestream-redirect/TLPSTR20.mp3",  0xFBE0},
    {"90s Hits",    "http://playerservices.streamtheworld.com/api/livestream-redirect/TLPSTR22.mp3",  0x07E0},
    {"Top 4000",    "http://playerservices.streamtheworld.com/api/livestream-redirect/TLPSTR24.mp3",  0xF800},
    {"Love Songs",  "http://playerservices.streamtheworld.com/api/livestream-redirect/TLPSTR04.mp3",  0xF81F},
    {"60s & 70s",   "http://playerservices.streamtheworld.com/api/livestream-redirect/TLPSTR18.mp3",  0xFD20},
    {"Disco",       "http://playerservices.streamtheworld.com/api/livestream-redirect/TLPSTR23.mp3",  0x07FF},
    {"Non-Stop",    "http://playerservices.streamtheworld.com/api/livestream-redirect/TLPSTR15.mp3",  0x7BEF},
};
static const int SENDER_ANZAHL = sizeof(sender) / sizeof(sender[0]);
int aktuellerSender = 0;
float lautstaerke = 0.5f;

#define HEADER_HOEHE    70
#define BTN_BEREICH_X   10
#define BTN_BEREICH_Y   HEADER_HOEHE
#define BTN_SPALTEN     4
#define BTN_BREITE      88
#define BTN_HOEHE       55
#define BTN_ABSTAND     5
#define VOL_X           410
#define VOL_Y           HEADER_HOEHE
#define VOL_BREITE      50
#define VOL_HOEHE       200
#define STATUS_Y        250

void stoppeStream();

/* ----------------- Web-OTA ----------------- *
 * Upload: curl -F "update=@firmware.bin" http://radio10.local/update
 * Browser: http://radio10.local/ (einfaches Upload-Formular)        */

void zeigeWebOtaFortschritt(int prozent) {
    int balkenX = 60, balkenY = 200, balkenB = 360, balkenH = 30;
    tft.drawRect(balkenX, balkenY, balkenB, balkenH, TFT_WHITE);
    int fuell = ((balkenB - 4) * prozent) / 100;
    tft.fillRect(balkenX + 2, balkenY + 2, fuell, balkenH - 4, TFT_GREEN);
    tft.fillRect(60, 150, 360, 20, TFT_BLACK);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(60, 150);
    tft.printf("%d%%", prozent);
}

void initWebOTA() {
    webOtaServer.on("/", HTTP_GET, []() {
        webOtaServer.send(200, "text/html",
            "<!DOCTYPE html><html><body style='font-family:sans-serif'>"
            "<h2>Radio 10 Firmware Update</h2>"
            "<form method='POST' action='/update' enctype='multipart/form-data'>"
            "<input type='file' name='update' accept='.bin'><br><br>"
            "<input type='submit' value='Upload'></form></body></html>");
    });
    webOtaServer.on("/update", HTTP_POST,
        []() {
            webOtaServer.sendHeader("Connection", "close");
            webOtaServer.send(200, "text/plain",
                Update.hasError() ? "FAIL" : "OK - rebooting");
            delay(200);
            ESP.restart();
        },
        []() {
            HTTPUpload &u = webOtaServer.upload();
            static size_t erwartet = 0;
            if (u.status == UPLOAD_FILE_START) {
                stoppeStream();
                tft.fillScreen(TFT_BLACK);
                tft.setTextColor(TFT_CYAN, TFT_BLACK);
                tft.setTextSize(3);
                tft.setCursor(60, 100);
                tft.println("Web-OTA");
                tft.setTextSize(2);
                tft.setCursor(60, 150);
                tft.setTextColor(TFT_WHITE, TFT_BLACK);
                tft.print("Empfange...");
                Serial.printf("[WebOTA] Start: %s\n", u.filename.c_str());
                erwartet = UPDATE_SIZE_UNKNOWN;
                if (!Update.begin(erwartet)) {
                    Update.printError(Serial);
                }
            } else if (u.status == UPLOAD_FILE_WRITE) {
                if (Update.write(u.buf, u.currentSize) != u.currentSize) {
                    Update.printError(Serial);
                }
                static unsigned long letzteAnzeige = 0;
                if (millis() - letzteAnzeige > 250) {
                    letzteAnzeige = millis();
                    int p = (int)((Update.progress() * 100) / Update.size());
                    if (p < 0) p = 0; if (p > 100) p = 100;
                    zeigeWebOtaFortschritt(p);
                }
            } else if (u.status == UPLOAD_FILE_END) {
                if (Update.end(true)) {
                    Serial.printf("[WebOTA] OK: %u bytes\n", u.totalSize);
                    zeigeWebOtaFortschritt(100);
                    tft.fillRect(60, 150, 360, 20, TFT_BLACK);
                    tft.setTextColor(TFT_GREEN, TFT_BLACK);
                    tft.setTextSize(2);
                    tft.setCursor(60, 150);
                    tft.print("Fertig - Neustart");
                } else {
                    Update.printError(Serial);
                    tft.setTextColor(TFT_RED, TFT_BLACK);
                    tft.setCursor(60, 150);
                    tft.print("FEHLER");
                }
            }
        });
    webOtaServer.begin();
    Serial.println("[WebOTA] bereit auf http://radio10.local/");
}

void initOTA() {
    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);
    ArduinoOTA.onStart([]() {
        stoppeStream();
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        tft.setTextSize(3);
        tft.setCursor(60, 100);
        tft.println("OTA Update");
        tft.setTextSize(2);
        tft.setCursor(60, 150);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.print("Starte...");
        Serial.println("[OTA] Start");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        int prozent = (progress * 100) / total;
        int balkenX = 60, balkenY = 200, balkenB = 360, balkenH = 30;
        tft.drawRect(balkenX, balkenY, balkenB, balkenH, TFT_WHITE);
        int fuell = ((balkenB - 4) * prozent) / 100;
        tft.fillRect(balkenX + 2, balkenY + 2, fuell, balkenH - 4, TFT_GREEN);
        tft.fillRect(60, 150, 360, 20, TFT_BLACK);
        tft.setTextColor(TFT_YELLOW, TFT_BLACK);
        tft.setTextSize(2);
        tft.setCursor(60, 150);
        tft.printf("%d%%", prozent);
    });
    ArduinoOTA.onEnd([]() {
        tft.fillRect(60, 150, 360, 20, TFT_BLACK);
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.setTextSize(2);
        tft.setCursor(60, 150);
        tft.print("Fertig - Neustart");
        Serial.println("[OTA] Ende");
    });
    ArduinoOTA.onError([](ota_error_t error) {
        tft.fillRect(60, 150, 360, 20, TFT_BLACK);
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.setTextSize(2);
        tft.setCursor(60, 150);
        tft.printf("Fehler %u", error);
        Serial.printf("[OTA] Fehler %u\n", error);
    });
    ArduinoOTA.begin();
    Serial.printf("[OTA] bereit: %s.local (%s)\n",
                  OTA_HOSTNAME, WiFi.localIP().toString().c_str());
}

void ladeLetztenSender() {
    prefs.begin(RADIO_NS, true);
    int idx = prefs.getInt(SENDER_KEY, 0);
    prefs.end();
    if (idx < 0 || idx >= SENDER_ANZAHL) idx = 0;
    aktuellerSender = idx;
    Serial.printf("[Radio] Letzter Sender aus NVS: %d (%s)\n",
                  idx, sender[idx].name);
}

void speichereSender() {
    prefs.begin(RADIO_NS, false);
    prefs.putInt(SENDER_KEY, aktuellerSender);
    prefs.end();
}

bool ladeKalibrierung() {
    prefs.begin(CAL_NS, true);
    size_t len = prefs.getBytesLength(CAL_KEY);
    if (len != sizeof(calData)) { prefs.end(); return false; }
    prefs.getBytes(CAL_KEY, calData, sizeof(calData));
    prefs.end();
    return true;
}

void speichereKalibrierung() {
    prefs.begin(CAL_NS, false);
    prefs.putBytes(CAL_KEY, calData, sizeof(calData));
    prefs.end();
}

void kalibriereUndSpeichere() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(60, 130);
    tft.println("Touch-Kalibrierung");
    tft.setTextSize(1);
    tft.setCursor(60, 160);
    tft.println("Beruehre die 4 Pfeilspitzen");
    delay(2500);
    tft.calibrateTouch(calData, TFT_YELLOW, TFT_BLACK, 15);
    tft.setTouch(calData);
    speichereKalibrierung();
    Serial.printf("[Touch] Neue Kalibrierung: %u,%u,%u,%u,%u\n",
                  calData[0], calData[1], calData[2], calData[3], calData[4]);
}

/* Waehrend Splash: Bildschirm beruehren erzwingt Neukalibrierung.
 * Nutzt getTouchRaw(), da calData evtl. noch nicht gesetzt ist. */
bool neukalibrierungAngefordert() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(40, 100);
    tft.println("Radio 10 Player");
    tft.setTextSize(1);
    tft.setCursor(40, 140);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.println("Bildschirm beruehren fuer Neukalibrierung...");
    unsigned long start = millis();
    uint16_t rx, ry;
    while (millis() - start < 2500) {
        if (tft.getTouchRaw(&rx, &ry) && rx > 100 && ry > 100) return true;
        delay(10);
    }
    return false;
}

void zeichneHeader() {
    tft.fillRect(0, 0, 480, HEADER_HOEHE, TFT_BLACK);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(10, 8);
    tft.print("Radio 10 Player");
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setTextSize(3);
    tft.setCursor(10, 35);
    tft.printf(">>> %s", sender[aktuellerSender].name);
    if (WiFi.status() == WL_CONNECTED) {
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.setTextSize(1);
        tft.setCursor(400, 8);
        tft.printf("WiFi %ddBm", WiFi.RSSI());
    }
}

void zeichneSenderButton(int index) {
    int spalte = index % BTN_SPALTEN;
    int zeile  = index / BTN_SPALTEN;
    int x = BTN_BEREICH_X + spalte * (BTN_BREITE + BTN_ABSTAND);
    int y = BTN_BEREICH_Y + zeile * (BTN_HOEHE + BTN_ABSTAND);
    uint16_t bgFarbe = sender[index].farbe;
    tft.fillRoundRect(x, y, BTN_BREITE, BTN_HOEHE, 6, bgFarbe);
    tft.drawRoundRect(x, y, BTN_BREITE, BTN_HOEHE, 6,
                      (index == aktuellerSender) ? TFT_WHITE : TFT_DARKGREY);
    if (index == aktuellerSender) {
        tft.drawRoundRect(x + 1, y + 1, BTN_BREITE - 2, BTN_HOEHE - 2, 5, TFT_WHITE);
        tft.drawRoundRect(x + 2, y + 2, BTN_BREITE - 4, BTN_HOEHE - 4, 4, TFT_WHITE);
    }
    uint16_t textFarbe = TFT_WHITE;
    if (bgFarbe == 0xFBE0 || bgFarbe == 0x07E0 || bgFarbe == 0x07FF) textFarbe = TFT_BLACK;
    tft.setTextColor(textFarbe, bgFarbe);
    tft.setTextSize(1);
    int textLen = strlen(sender[index].name);
    tft.setCursor(x + (BTN_BREITE - textLen * 6) / 2, y + (BTN_HOEHE - 8) / 2);
    tft.print(sender[index].name);
}

void zeichneSenderButtons() { for (int i = 0; i < SENDER_ANZAHL; i++) zeichneSenderButton(i); }

void zeichneVolSlider() {
    tft.fillRect(VOL_X - 5, VOL_Y - 15, VOL_BREITE + 10, VOL_HOEHE + 40, TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(VOL_X + 5, VOL_Y - 12);
    tft.print("Volume");
    tft.fillRect(VOL_X, VOL_Y, VOL_BREITE, VOL_HOEHE, TFT_DARKGREY);
    tft.drawRect(VOL_X, VOL_Y, VOL_BREITE, VOL_HOEHE, TFT_WHITE);
    int fuellHoehe = (int)(lautstaerke * VOL_HOEHE);
    int fuellY = VOL_Y + VOL_HOEHE - fuellHoehe;
    for (int y = fuellY; y < VOL_Y + VOL_HOEHE; y++) {
        float a = (float)(VOL_Y + VOL_HOEHE - y) / VOL_HOEHE;
        uint8_t r = (a > 0.7f) ? 255 : (uint8_t)(a / 0.7f * 255);
        uint8_t g = (a < 0.7f) ? 255 : (uint8_t)((1.0f - a) / 0.3f * 255);
        tft.drawFastHLine(VOL_X + 1, y, VOL_BREITE - 2, tft.color565(r, g, 0));
    }
    int knopfY = fuellY - 4;
    if (knopfY < VOL_Y - 4) knopfY = VOL_Y - 4;
    tft.fillRect(VOL_X - 3, knopfY, VOL_BREITE + 6, 8, TFT_WHITE);
    tft.fillRect(VOL_X, VOL_Y + VOL_HOEHE + 5, VOL_BREITE, 18, TFT_BLACK);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(VOL_X + 2, VOL_Y + VOL_HOEHE + 5);
    tft.printf("%d%%", (int)(lautstaerke * 100));
}

void zeigeStatus(const char *text, uint16_t farbe) {
    tft.fillRect(0, STATUS_Y, 400, 70, TFT_BLACK);
    tft.setTextColor(farbe, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(10, STATUS_Y + 5);
    tft.print(text);
}

void zeichneUI() {
    tft.fillScreen(TFT_BLACK);
    zeichneHeader();
    zeichneSenderButtons();
    zeichneVolSlider();
    zeigeStatus("Bereit", TFT_GREEN);
}

void stoppeStream() {
    if (mp3) { if (mp3->isRunning()) mp3->stop(); delete mp3; mp3 = nullptr; }
    if (puffer) { delete puffer; puffer = nullptr; }
    if (stream) { delete stream; stream = nullptr; }
}

void starteSender() {
    stoppeStream();
    Serial.printf("[Radio] Starte: %s\n", sender[aktuellerSender].name);
    char st[80];
    snprintf(st, sizeof(st), "Verbinde: %s ...", sender[aktuellerSender].name);
    zeigeStatus(st, TFT_YELLOW);
    stream = new AudioFileSourceICYStream(sender[aktuellerSender].url);
    if (!stream->isOpen()) {
        zeigeStatus("FEHLER: Stream nicht erreichbar!", TFT_RED);
        delete stream; stream = nullptr; return;
    }
    puffer = new AudioFileSourceBuffer(stream, AUDIO_PUFFER_GROESSE);
    mp3 = new AudioGeneratorMP3();
    if (!mp3->begin(puffer, ausgang)) {
        zeigeStatus("FEHLER: MP3-Decoder!", TFT_RED);
        stoppeStream(); return;
    }
    snprintf(st, sizeof(st), "Spielt: %s", sender[aktuellerSender].name);
    zeigeStatus(st, TFT_GREEN);
}

unsigned long letzterTouch = 0;

void verarbeiteTouch() {
    uint16_t tx, ty;
    if (!tft.getTouch(&tx, &ty)) return;
    if (tx >= VOL_X - 10 && tx <= VOL_X + VOL_BREITE + 10 &&
        ty >= VOL_Y && ty <= VOL_Y + VOL_HOEHE) {
        float neueLS = 1.0f - (float)(ty - VOL_Y) / VOL_HOEHE;
        if (neueLS < 0.0f) neueLS = 0.0f;
        if (neueLS > 1.0f) neueLS = 1.0f;
        if (fabsf(neueLS - lautstaerke) > 0.02f) {
            lautstaerke = neueLS;
            ausgang->SetGain(lautstaerke);
            zeichneVolSlider();
        }
        return;
    }
    if (millis() - letzterTouch < 400) return;
    letzterTouch = millis();
    for (int i = 0; i < SENDER_ANZAHL; i++) {
        int spalte = i % BTN_SPALTEN;
        int zeile  = i / BTN_SPALTEN;
        int bx = BTN_BEREICH_X + spalte * (BTN_BREITE + BTN_ABSTAND);
        int by = BTN_BEREICH_Y + zeile * (BTN_HOEHE + BTN_ABSTAND);
        if (tx >= bx && tx <= bx + BTN_BREITE && ty >= by && ty <= by + BTN_HOEHE) {
            if (i != aktuellerSender) {
                aktuellerSender = i;
                speichereSender();
                zeichneHeader();
                zeichneSenderButtons();
                starteSender();
            }
            return;
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("=== Radio 10 Player ===");

    pinMode(TFT_BL_PIN, OUTPUT);
    digitalWrite(TFT_BL_PIN, HIGH);
    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);

    /* Touch-Kalibrierung:
     *   - gespeicherte Werte aus NVS laden, falls vorhanden
     *   - waehrend 2.5s Splash Bildschirm beruehren -> Neukalibrierung
     *   - keine Daten in NVS -> Erst-Kalibrierung */
    bool forceCal = neukalibrierungAngefordert();
    bool haveCal  = !forceCal && ladeKalibrierung();
    if (haveCal) {
        tft.setTouch(calData);
        Serial.printf("[Touch] Kalibrierung aus NVS: %u,%u,%u,%u,%u\n",
                      calData[0], calData[1], calData[2], calData[3], calData[4]);
    } else {
        kalibriereUndSpeichere();
    }

    /* WLAN */
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(20, 100);
    tft.print("WLAN verbinden...");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    WiFi.setAutoReconnect(true);
    int v = 0;
    while (WiFi.status() != WL_CONNECTED && v < 40) { delay(500); tft.print("."); v++; }
    if (WiFi.status() != WL_CONNECTED) {
        tft.setTextColor(TFT_RED); tft.setCursor(20, 140); tft.println("WLAN FEHLER!");
        while (true) delay(1000);
    }
    Serial.printf("[WLAN] IP: %s\n", WiFi.localIP().toString().c_str());

    initOTA();
    initWebOTA();

    ausgang = new AudioOutputI2S();
    ausgang->SetPinout(4, 5, 6);
    ausgang->SetGain(lautstaerke);

    ladeLetztenSender();
    zeichneUI();
    starteSender();
}

unsigned long letzterUIUpdate = 0;

void loop() {
    ArduinoOTA.handle();
    webOtaServer.handleClient();
    if (mp3 && mp3->isRunning()) {
        if (!mp3->loop()) { delay(1000); starteSender(); }
    }
    verarbeiteTouch();
    if (millis() - letzterUIUpdate > 10000) {
        letzterUIUpdate = millis();
        if (WiFi.status() == WL_CONNECTED) {
            tft.fillRect(380, 0, 100, 20, TFT_BLACK);
            tft.setTextColor(TFT_GREEN, TFT_BLACK);
            tft.setTextSize(1);
            tft.setCursor(400, 8);
            tft.printf("WiFi %ddBm", WiFi.RSSI());
        }
    }
}
