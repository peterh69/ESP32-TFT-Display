/**
 * ============================================================================
 * Radio 10 Webradio-Player - ESP32-S3
 * ============================================================================
 *
 * Beschreibung:
 *   Internet-Radio-Player fuer Radio 10 (NL) und seine Themenkanal.
 *   Verbindet sich per WLAN und streamt MP3-Audio ueber I2S zum
 *   MAX98357A Verstaerker. Das TFT-Display zeigt Sender-Buttons,
 *   einen Lautstaerke-Slider und den aktuellen Sender an.
 *   Bedienung komplett per Touchscreen.
 *
 * Sender:
 *   - Radio 10 (Hauptprogramm)
 *   - Radio 10 80s Hits
 *   - Radio 10 90s Hits
 *   - Radio 10 Top 4000
 *   - Radio 10 Love Songs
 *   - Radio 10 60s & 70s
 *   - Radio 10 Disco
 *   - Radio 10 Non-Stop
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

/* --------------------------------------------------------------------------
 * WLAN-Zugangsdaten
 * -------------------------------------------------------------------------- */

#define WIFI_SSID     "REDACTED_SSID"
#define WIFI_PASSWORD "REDACTED_PASSWORD"

/* --------------------------------------------------------------------------
 * Globale Objekte
 * -------------------------------------------------------------------------- */

TFT_eSPI tft = TFT_eSPI();

#define TFT_BL_PIN 14

AudioFileSourceICYStream *stream = nullptr;
AudioFileSourceBuffer    *puffer = nullptr;
AudioGeneratorMP3        *mp3    = nullptr;
AudioOutputI2S           *ausgang = nullptr;

/** Puffergroesse fuer Audio-Streaming (8 KB) */
#define AUDIO_PUFFER_GROESSE 8192

/* --------------------------------------------------------------------------
 * Sender-Definitionen
 * -------------------------------------------------------------------------- */

struct Sender {
    const char *name;       /**< Anzeigename auf dem Button */
    const char *url;        /**< Stream-URL (MP3) */
    uint16_t   farbe;       /**< Button-Hintergrundfarbe */
};

static Sender sender[] = {
    {"Radio 10",    "http://playerservices.streamtheworld.com/api/livestream-redirect/RADIO10.mp3",   0x001F},  /* Dunkelblau */
    {"80s Hits",    "http://playerservices.streamtheworld.com/api/livestream-redirect/TLPSTR20.mp3",  0xFBE0},  /* Gelb */
    {"90s Hits",    "http://playerservices.streamtheworld.com/api/livestream-redirect/TLPSTR22.mp3",  0x07E0},  /* Gruen */
    {"Top 4000",    "http://playerservices.streamtheworld.com/api/livestream-redirect/TLPSTR24.mp3",  0xF800},  /* Rot */
    {"Love Songs",  "http://playerservices.streamtheworld.com/api/livestream-redirect/TLPSTR04.mp3",  0xF81F},  /* Magenta */
    {"60s & 70s",   "http://playerservices.streamtheworld.com/api/livestream-redirect/TLPSTR18.mp3",  0xFD20},  /* Orange */
    {"Disco",       "http://playerservices.streamtheworld.com/api/livestream-redirect/TLPSTR23.mp3",  0x07FF},  /* Cyan */
    {"Non-Stop",    "http://playerservices.streamtheworld.com/api/livestream-redirect/TLPSTR15.mp3",  0x7BEF},  /* Grau */
};

static const int SENDER_ANZAHL = sizeof(sender) / sizeof(sender[0]);

/** Aktuell ausgewaehlter Sender */
int aktuellerSender = 0;

/** Lautstaerke (0.0 - 1.0) */
float lautstaerke = 0.5f;

/** Touch-Kalibrierungsdaten */
uint16_t calData[5];

/** Status-Flags */
bool streamLaeuft = false;

/* --------------------------------------------------------------------------
 * UI-Layout (Querformat 480x320)
 *
 *  +--------------------------------------------------+
 *  |  Radio 10 Player          [WLAN-Status]          |
 *  |  >>> Aktueller Sender <<<                        |
 *  +--------+--------+--------+--------+---+  +------+
 *  |Radio10 | 80s    | 90s    |Top4000 |   |  | Vol  |
 *  +--------+--------+--------+--------+   |  | #### |
 *  |Love    | 60s70s | Disco  |NonStop |   |  | #### |
 *  +--------+--------+--------+--------+---+  +------+
 * -------------------------------------------------------------------------- */

#define HEADER_HOEHE    70
#define BTN_BEREICH_X   10
#define BTN_BEREICH_Y   HEADER_HOEHE
#define BTN_SPALTEN     4
#define BTN_ZEILEN      2
#define BTN_BREITE      88
#define BTN_HOEHE       55
#define BTN_ABSTAND     5

/* Lautstaerke-Slider */
#define VOL_X           410
#define VOL_Y           HEADER_HOEHE
#define VOL_BREITE      50
#define VOL_HOEHE       200

/* Status-Bereich unten */
#define STATUS_Y        250

/* --------------------------------------------------------------------------
 * UI-Zeichenfunktionen
 * -------------------------------------------------------------------------- */

/**
 * Zeichnet den Header mit Titel und WLAN-Status.
 */
void zeichneHeader() {
    tft.fillRect(0, 0, 480, HEADER_HOEHE, TFT_BLACK);

    /* Titel */
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(10, 8);
    tft.print("Radio 10 Player");

    /* Aktueller Sender gross anzeigen */
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setTextSize(3);
    tft.setCursor(10, 35);
    tft.printf(">>> %s", sender[aktuellerSender].name);

    /* WLAN-Staerke */
    if (WiFi.status() == WL_CONNECTED) {
        int rssi = WiFi.RSSI();
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.setTextSize(1);
        tft.setCursor(400, 8);
        tft.printf("WiFi %ddBm", rssi);
    }
}

/**
 * Zeichnet einen einzelnen Sender-Button.
 */
void zeichneSenderButton(int index) {
    int spalte = index % BTN_SPALTEN;
    int zeile  = index / BTN_SPALTEN;
    int x = BTN_BEREICH_X + spalte * (BTN_BREITE + BTN_ABSTAND);
    int y = BTN_BEREICH_Y + zeile * (BTN_HOEHE + BTN_ABSTAND);

    /* Hintergrundfarbe: aktiver Sender heller */
    uint16_t bgFarbe = sender[index].farbe;

    tft.fillRoundRect(x, y, BTN_BREITE, BTN_HOEHE, 6, bgFarbe);
    tft.drawRoundRect(x, y, BTN_BREITE, BTN_HOEHE, 6,
                      (index == aktuellerSender) ? TFT_WHITE : TFT_DARKGREY);

    /* Aktiver Sender: dicker Rahmen */
    if (index == aktuellerSender) {
        tft.drawRoundRect(x + 1, y + 1, BTN_BREITE - 2, BTN_HOEHE - 2, 5, TFT_WHITE);
        tft.drawRoundRect(x + 2, y + 2, BTN_BREITE - 4, BTN_HOEHE - 4, 4, TFT_WHITE);
    }

    /* Text (schwarz oder weiss je nach Hintergrund) */
    uint16_t textFarbe = TFT_WHITE;
    if (bgFarbe == 0xFBE0 || bgFarbe == 0x07E0 || bgFarbe == 0x07FF) {
        textFarbe = TFT_BLACK;
    }
    tft.setTextColor(textFarbe, bgFarbe);
    tft.setTextSize(1);

    /* Text zentrieren */
    int textLen = strlen(sender[index].name);
    int textX = x + (BTN_BREITE - textLen * 6) / 2;
    int textY = y + (BTN_HOEHE - 8) / 2;
    tft.setCursor(textX, textY);
    tft.print(sender[index].name);
}

/**
 * Zeichnet alle Sender-Buttons.
 */
void zeichneSenderButtons() {
    for (int i = 0; i < SENDER_ANZAHL; i++) {
        zeichneSenderButton(i);
    }
}

/**
 * Zeichnet den Lautstaerke-Slider.
 */
void zeichneVolSlider() {
    tft.fillRect(VOL_X - 5, VOL_Y - 15, VOL_BREITE + 10, VOL_HOEHE + 40, TFT_BLACK);

    /* Label */
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(VOL_X + 5, VOL_Y - 12);
    tft.print("Volume");

    /* Slider-Hintergrund */
    tft.fillRect(VOL_X, VOL_Y, VOL_BREITE, VOL_HOEHE, TFT_DARKGREY);
    tft.drawRect(VOL_X, VOL_Y, VOL_BREITE, VOL_HOEHE, TFT_WHITE);

    /* Gefuellter Bereich mit Farbverlauf */
    int fuellHoehe = (int)(lautstaerke * VOL_HOEHE);
    int fuellY = VOL_Y + VOL_HOEHE - fuellHoehe;
    for (int y = fuellY; y < VOL_Y + VOL_HOEHE; y++) {
        float a = (float)(VOL_Y + VOL_HOEHE - y) / VOL_HOEHE;
        uint8_t r = (a > 0.7f) ? 255 : (uint8_t)(a / 0.7f * 255);
        uint8_t g = (a < 0.7f) ? 255 : (uint8_t)((1.0f - a) / 0.3f * 255);
        tft.drawFastHLine(VOL_X + 1, y, VOL_BREITE - 2, tft.color565(r, g, 0));
    }

    /* Knopf */
    int knopfY = fuellY - 4;
    if (knopfY < VOL_Y - 4) knopfY = VOL_Y - 4;
    tft.fillRect(VOL_X - 3, knopfY, VOL_BREITE + 6, 8, TFT_WHITE);

    /* Prozentwert */
    tft.fillRect(VOL_X, VOL_Y + VOL_HOEHE + 5, VOL_BREITE, 18, TFT_BLACK);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(VOL_X + 2, VOL_Y + VOL_HOEHE + 5);
    tft.printf("%d%%", (int)(lautstaerke * 100));
}

/**
 * Zeigt eine Statusmeldung am unteren Bildschirmrand an.
 */
void zeigeStatus(const char *text, uint16_t farbe) {
    tft.fillRect(0, STATUS_Y, 400, 70, TFT_BLACK);
    tft.setTextColor(farbe, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(10, STATUS_Y + 5);
    tft.print(text);
}

/**
 * Zeichnet das komplette UI.
 */
void zeichneUI() {
    tft.fillScreen(TFT_BLACK);
    zeichneHeader();
    zeichneSenderButtons();
    zeichneVolSlider();
    zeigeStatus("Bereit", TFT_GREEN);
}

/* --------------------------------------------------------------------------
 * Audio-Funktionen
 * -------------------------------------------------------------------------- */

/**
 * Stoppt den aktuellen Stream und gibt Ressourcen frei.
 */
void stoppeStream() {
    if (mp3) {
        if (mp3->isRunning()) mp3->stop();
        delete mp3;
        mp3 = nullptr;
    }
    if (puffer) { delete puffer; puffer = nullptr; }
    if (stream) { delete stream; stream = nullptr; }
    streamLaeuft = false;
}

/**
 * Startet den Stream des aktuell ausgewaehlten Senders.
 */
void starteSender() {
    stoppeStream();

    Serial.printf("[Radio] Starte: %s\n", sender[aktuellerSender].name);
    Serial.printf("  URL: %s\n", sender[aktuellerSender].url);

    char statusText[80];
    snprintf(statusText, sizeof(statusText), "Verbinde: %s ...", sender[aktuellerSender].name);
    zeigeStatus(statusText, TFT_YELLOW);

    stream = new AudioFileSourceICYStream(sender[aktuellerSender].url);

    if (!stream->isOpen()) {
        Serial.println("  FEHLER: Stream konnte nicht geoeffnet werden!");
        zeigeStatus("FEHLER: Stream nicht erreichbar!", TFT_RED);
        delete stream;
        stream = nullptr;
        return;
    }

    puffer = new AudioFileSourceBuffer(stream, AUDIO_PUFFER_GROESSE);
    mp3 = new AudioGeneratorMP3();

    if (!mp3->begin(puffer, ausgang)) {
        Serial.println("  FEHLER: MP3-Decoder konnte nicht starten!");
        zeigeStatus("FEHLER: MP3-Decoder!", TFT_RED);
        stoppeStream();
        return;
    }

    streamLaeuft = true;
    Serial.println("  Stream laeuft!");

    snprintf(statusText, sizeof(statusText), "Spielt: %s", sender[aktuellerSender].name);
    zeigeStatus(statusText, TFT_GREEN);
}

/* --------------------------------------------------------------------------
 * Touch-Verarbeitung
 * -------------------------------------------------------------------------- */

unsigned long letzterTouch = 0;

void verarbeiteTouch() {
    uint16_t tx, ty;
    if (!tft.getTouch(&tx, &ty)) return;

    /* Lautstaerke-Slider */
    if (tx >= VOL_X - 10 && tx <= VOL_X + VOL_BREITE + 10 &&
        ty >= VOL_Y && ty <= VOL_Y + VOL_HOEHE) {

        float neueLS = 1.0f - (float)(ty - VOL_Y) / VOL_HOEHE;
        if (neueLS < 0.0f) neueLS = 0.0f;
        if (neueLS > 1.0f) neueLS = 1.0f;

        if (fabsf(neueLS - lautstaerke) > 0.02f) {
            lautstaerke = neueLS;
            ausgang->SetGain(lautstaerke);
            zeichneVolSlider();
            Serial.printf("Volume: %d%%\n", (int)(lautstaerke * 100));
        }
        return;
    }

    /* Entprellung fuer Buttons */
    if (millis() - letzterTouch < 400) return;
    letzterTouch = millis();

    /* Sender-Buttons pruefen */
    for (int i = 0; i < SENDER_ANZAHL; i++) {
        int spalte = i % BTN_SPALTEN;
        int zeile  = i / BTN_SPALTEN;
        int bx = BTN_BEREICH_X + spalte * (BTN_BREITE + BTN_ABSTAND);
        int by = BTN_BEREICH_Y + zeile * (BTN_HOEHE + BTN_ABSTAND);

        if (tx >= bx && tx <= bx + BTN_BREITE &&
            ty >= by && ty <= by + BTN_HOEHE) {

            if (i != aktuellerSender) {
                Serial.printf("[Touch] Sender: %s\n", sender[i].name);
                aktuellerSender = i;
                zeichneHeader();
                zeichneSenderButtons();
                starteSender();
            }
            return;
        }
    }
}

/* --------------------------------------------------------------------------
 * Setup
 * -------------------------------------------------------------------------- */

void setup() {
    Serial.begin(115200);
    delay(2000);

    Serial.println("============================================");
    Serial.println("  Radio 10 Webradio-Player");
    Serial.println("============================================");
    Serial.println();

    /* Display initialisieren */
    pinMode(TFT_BL_PIN, OUTPUT);
    digitalWrite(TFT_BL_PIN, HIGH);
    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);

    /* Touch kalibrieren */
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(60, 140);
    tft.println("Touch-Kalibrierung");
    tft.setTextSize(1);
    tft.setCursor(60, 170);
    tft.println("Beruehre die 4 Pfeilspitzen");
    delay(2000);

    tft.calibrateTouch(calData, TFT_YELLOW, TFT_BLACK, 15);
    tft.setTouch(calData);
    Serial.printf("calData: {%d, %d, %d, %d, %d}\n",
                  calData[0], calData[1], calData[2], calData[3], calData[4]);

    /* WLAN verbinden */
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(20, 100);
    tft.print("WLAN verbinden...");
    tft.setTextSize(1);
    tft.setCursor(20, 130);
    tft.printf("SSID: %s", WIFI_SSID);

    Serial.printf("[WLAN] Verbinde mit '%s'...\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    WiFi.setAutoReconnect(true);

    int versuche = 0;
    while (WiFi.status() != WL_CONNECTED && versuche < 40) {
        delay(500);
        Serial.print(".");
        tft.print(".");
        versuche++;
    }
    Serial.println();

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WLAN] FEHLER: Verbindung fehlgeschlagen!");
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_RED);
        tft.setTextSize(2);
        tft.setCursor(20, 100);
        tft.println("WLAN FEHLER!");
        while (true) delay(1000);
    }

    Serial.printf("[WLAN] Verbunden! IP: %s\n", WiFi.localIP().toString().c_str());
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(20, 100);
    tft.printf("Verbunden!");
    tft.setTextSize(1);
    tft.setCursor(20, 130);
    tft.printf("IP: %s  RSSI: %d dBm", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    delay(1500);

    /* I2S Audio-Ausgang */
    ausgang = new AudioOutputI2S();
    ausgang->SetPinout(4, 5, 6);
    ausgang->SetGain(lautstaerke);
    Serial.println("[Audio] I2S bereit");

    /* UI zeichnen */
    zeichneUI();

    /* Ersten Sender starten */
    starteSender();
}

/* --------------------------------------------------------------------------
 * Loop
 * -------------------------------------------------------------------------- */

/** Zaehler fuer periodische Updates */
unsigned long letzterUIUpdate = 0;

void loop() {
    /* MP3-Decoder mit Daten fuettern */
    if (mp3 && mp3->isRunning()) {
        if (!mp3->loop()) {
            /* Stream abgebrochen - neu verbinden */
            Serial.println("[Radio] Stream unterbrochen, verbinde neu...");
            zeigeStatus("Stream unterbrochen, verbinde...", TFT_ORANGE);
            delay(1000);
            starteSender();
        }
    }

    /* Touch verarbeiten */
    verarbeiteTouch();

    /* Periodisch WLAN-Status aktualisieren (alle 10 Sekunden) */
    if (millis() - letzterUIUpdate > 10000) {
        letzterUIUpdate = millis();

        /* WLAN pruefen */
        if (WiFi.status() != WL_CONNECTED) {
            zeigeStatus("WLAN getrennt! Verbinde...", TFT_RED);
            Serial.println("[WLAN] Verbindung verloren!");
        } else {
            /* RSSI aktualisieren */
            tft.fillRect(380, 0, 100, 20, TFT_BLACK);
            tft.setTextColor(TFT_GREEN, TFT_BLACK);
            tft.setTextSize(1);
            tft.setCursor(400, 8);
            tft.printf("WiFi %ddBm", WiFi.RSSI());
        }
    }
}
