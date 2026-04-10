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
#include <TFT_eSPI.h>
#include <SPI.h>
#include <AudioFileSourceICYStream.h>
#include <AudioFileSourceBuffer.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutputI2S.h>

#define WIFI_SSID     "REDACTED_SSID"
#define WIFI_PASSWORD "REDACTED_PASSWORD"

TFT_eSPI tft = TFT_eSPI();
#define TFT_BL_PIN 14

AudioFileSourceICYStream *stream = nullptr;
AudioFileSourceBuffer    *puffer = nullptr;
AudioGeneratorMP3        *mp3    = nullptr;
AudioOutputI2S           *ausgang = nullptr;
#define AUDIO_PUFFER_GROESSE 8192

uint16_t calData[5];

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

    /* Touch kalibrieren (Pfeile 30px vom Rand) */
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

    ausgang = new AudioOutputI2S();
    ausgang->SetPinout(4, 5, 6);
    ausgang->SetGain(lautstaerke);

    zeichneUI();
    starteSender();
}

unsigned long letzterUIUpdate = 0;

void loop() {
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
