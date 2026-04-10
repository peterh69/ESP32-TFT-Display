/**
 * ============================================================================
 * TFT Touch-Test - ESP32-S3 mit HSD-9190J-C7 (ST7796S + XPT2046)
 * ============================================================================
 *
 * Beschreibung:
 *   Testprogramm fuer den resistiven Touchscreen (XPT2046).
 *   Phase 1: Touch-Kalibrierung ueber 4 Eckpunkte
 *   Phase 2: Zeichenflaeche - Beruehrungen werden als farbige Punkte
 *            dargestellt, Koordinaten auf Serial ausgegeben
 *   Phase 3: Button-Test - 4 Farbbuttons und ein "Loeschen"-Button
 *
 * Hardware:
 *   - Board:   ESP32-S3-DevKitC-1
 *   - Display: HSD-9190J-C7 (ST7796S), 480x320, SPI
 *   - Touch:   XPT2046 (resistiv), gleicher SPI-Bus, CS=GPIO15
 *   - Pinbelegung: siehe Pinbelegung.txt
 *
 * Abhaengigkeiten:
 *   - TFT_eSPI (Bodmer) - inkl. Touch-Support
 *
 * Autor:   peterh69
 * Datum:   2026-04-10
 * Lizenz:  MIT
 * ============================================================================
 */

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SPI.h>

/* --------------------------------------------------------------------------
 * Globale Objekte und Konstanten
 * -------------------------------------------------------------------------- */

TFT_eSPI tft = TFT_eSPI();

#define TFT_BL_PIN 14

/** Touch-Kalibrierungsdaten (werden in Phase 1 ermittelt) */
uint16_t calData[5];

/** Aktuelle Zeichenfarbe */
uint16_t zeichenFarbe = TFT_WHITE;

/** Stiftgroesse in Pixeln */
int stiftGroesse = 4;

/* --------------------------------------------------------------------------
 * Touch-Kalibrierung
 * -------------------------------------------------------------------------- */

/**
 * Zeichnet ein Fadenkreuz an der angegebenen Position.
 * Wird waehrend der Kalibrierung als Zielpunkt verwendet.
 */
void zeichneFadenkreuz(int x, int y, uint16_t farbe) {
    tft.drawLine(x - 10, y, x + 10, y, farbe);
    tft.drawLine(x, y - 10, x, y + 10, farbe);
    tft.drawCircle(x, y, 6, farbe);
}

/**
 * Fuehrt die Touch-Kalibrierung durch.
 * Der Benutzer muss 4 Eckpunkte beruehren. Die Kalibrierungsdaten
 * werden in calData[] gespeichert.
 */
void kalibrierung() {
    Serial.println("[Kalibrierung] Bitte die 4 Eckpunkte beruehren");

    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(40, tft.height() / 2 - 20);
    tft.println("Touch-Kalibrierung");
    tft.setTextSize(1);
    tft.setCursor(40, tft.height() / 2 + 10);
    tft.println("Beruehre die 4 Fadenkreuze nacheinander");
    delay(2000);

    /* TFT_eSPI hat eine eingebaute Kalibrierungsfunktion */
    tft.calibrateTouch(calData, TFT_YELLOW, TFT_BLACK, 20);

    /* Kalibrierungsdaten auf Serial ausgeben (zum Festschreiben) */
    Serial.println("[Kalibrierung] Abgeschlossen. Werte:");
    Serial.printf("  uint16_t calData[5] = {%d, %d, %d, %d, %d};\n",
                  calData[0], calData[1], calData[2], calData[3], calData[4]);

    /* Bestaetigung anzeigen */
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(40, tft.height() / 2 - 10);
    tft.println("Kalibrierung OK!");
    delay(1500);
}

/* --------------------------------------------------------------------------
 * Zeichen-UI
 * -------------------------------------------------------------------------- */

/** Y-Position ab der die Zeichenflaeche beginnt (unterhalb der Toolbar) */
#define TOOLBAR_HOEHE 40

/**
 * Struktur fuer einen Button in der Toolbar.
 */
struct Button {
    int x, y, w, h;
    uint16_t farbe;
    const char *label;
};

/** Toolbar-Buttons: 4 Farben + Loeschen + Stiftgroesse */
static const int BUTTON_ANZAHL = 6;
static Button buttons[BUTTON_ANZAHL];

/**
 * Zeichnet die Toolbar am oberen Bildschirmrand mit Farbbuttons.
 */
void zeichneToolbar() {
    int btnBreite = tft.width() / BUTTON_ANZAHL;

    /* Button-Definitionen */
    uint16_t farben[] = {TFT_WHITE, TFT_RED, TFT_GREEN, TFT_BLUE, TFT_YELLOW, TFT_BLACK};
    const char *labels[] = {"Weiss", "Rot", "Gruen", "Blau", "Gelb", "Loeschen"};

    for (int i = 0; i < BUTTON_ANZAHL; i++) {
        buttons[i].x = i * btnBreite;
        buttons[i].y = 0;
        buttons[i].w = btnBreite;
        buttons[i].h = TOOLBAR_HOEHE;
        buttons[i].farbe = farben[i];
        buttons[i].label = labels[i];

        /* Button zeichnen */
        if (i == BUTTON_ANZAHL - 1) {
            /* Loeschen-Button: roter Hintergrund */
            tft.fillRect(buttons[i].x, 0, btnBreite, TOOLBAR_HOEHE, TFT_DARKGREY);
            tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
        } else {
            tft.fillRect(buttons[i].x, 0, btnBreite, TOOLBAR_HOEHE, farben[i]);
            /* Textfarbe: schwarz auf hellen, weiss auf dunklen Buttons */
            uint16_t textFarbe = (i == 3) ? TFT_WHITE : TFT_BLACK;
            tft.setTextColor(textFarbe, farben[i]);
        }

        tft.setTextSize(1);
        tft.setCursor(buttons[i].x + 5, TOOLBAR_HOEHE / 2 - 4);
        tft.print(labels[i]);

        /* Rahmen um den Button */
        tft.drawRect(buttons[i].x, 0, btnBreite, TOOLBAR_HOEHE, TFT_DARKGREY);
    }

    /* Markierung der aktiven Farbe */
    for (int i = 0; i < BUTTON_ANZAHL - 1; i++) {
        if (buttons[i].farbe == zeichenFarbe) {
            tft.drawRect(buttons[i].x + 1, 1, buttons[i].w - 2, TOOLBAR_HOEHE - 2, TFT_MAGENTA);
            tft.drawRect(buttons[i].x + 2, 2, buttons[i].w - 4, TOOLBAR_HOEHE - 4, TFT_MAGENTA);
        }
    }

    /* Trennlinie zwischen Toolbar und Zeichenflaeche */
    tft.drawFastHLine(0, TOOLBAR_HOEHE, tft.width(), TFT_DARKGREY);
}

/**
 * Initialisiert die Zeichenflaeche (schwarzer Hintergrund + Toolbar).
 */
void initZeichenflaeche() {
    tft.fillScreen(TFT_BLACK);
    zeichneToolbar();

    Serial.println("[Zeichnen] Zeichenflaeche bereit");
    Serial.println("  Toolbar: Weiss, Rot, Gruen, Blau, Gelb, Loeschen");
    Serial.println("  Beruehre die Flaeche zum Zeichnen!");
}

/**
 * Prueft ob ein Toolbar-Button getroffen wurde und fuehrt die
 * entsprechende Aktion aus.
 *
 * @param x  Touch-X-Koordinate
 * @param y  Touch-Y-Koordinate
 * @return true wenn ein Button getroffen wurde
 */
bool verarbeiteButton(uint16_t x, uint16_t y) {
    if (y > TOOLBAR_HOEHE) return false;

    for (int i = 0; i < BUTTON_ANZAHL; i++) {
        if (x >= buttons[i].x && x < buttons[i].x + buttons[i].w) {
            if (i == BUTTON_ANZAHL - 1) {
                /* Loeschen-Button: Zeichenflaeche zuruecksetzen */
                Serial.println("  [Button] Loeschen");
                tft.fillRect(0, TOOLBAR_HOEHE + 1, tft.width(),
                             tft.height() - TOOLBAR_HOEHE - 1, TFT_BLACK);
            } else {
                /* Farbbutton: Zeichenfarbe wechseln */
                zeichenFarbe = buttons[i].farbe;
                Serial.printf("  [Button] Farbe: %s\n", buttons[i].label);
                zeichneToolbar();
            }
            delay(200);  /* Entprellung */
            return true;
        }
    }
    return false;
}

/* --------------------------------------------------------------------------
 * Setup
 * -------------------------------------------------------------------------- */

void setup() {
    Serial.begin(115200);
    delay(2000);

    Serial.println("============================================");
    Serial.println("  TFT Touch-Test - HSD-9190J-C7");
    Serial.println("============================================");
    Serial.println();

    /* Hintergrundbeleuchtung einschalten */
    pinMode(TFT_BL_PIN, OUTPUT);
    digitalWrite(TFT_BL_PIN, HIGH);

    /* TFT initialisieren */
    tft.init();
    tft.setRotation(1);  /* Querformat: 480 x 320 */
    tft.fillScreen(TFT_BLACK);
    Serial.printf("Display: %dx%d Pixel\n", tft.width(), tft.height());
    Serial.println("Touch: XPT2046, CS=GPIO15");
    Serial.println();

    /* Touch kalibrieren */
    kalibrierung();

    /* Kalibrierungsdaten aktivieren */
    tft.setTouch(calData);

    /* Zeichenflaeche starten */
    initZeichenflaeche();
}

/* --------------------------------------------------------------------------
 * Loop - Zeichenmodus
 * -------------------------------------------------------------------------- */

void loop() {
    uint16_t touchX, touchY;

    if (tft.getTouch(&touchX, &touchY)) {
        /* Pruefen ob ein Toolbar-Button getroffen wurde */
        if (!verarbeiteButton(touchX, touchY)) {
            /* Nur in der Zeichenflaeche zeichnen (unterhalb Toolbar) */
            if (touchY > TOOLBAR_HOEHE) {
                tft.fillCircle(touchX, touchY, stiftGroesse, zeichenFarbe);
            }
        }
    }

    delay(15);  /* Kurze Pause um CPU-Last zu begrenzen */
}
