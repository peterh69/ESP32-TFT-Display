/**
 * ============================================================================
 * Hallo World Blink - ESP32-S3 NeoPixel LED Blink-Programm
 * ============================================================================
 *
 * Beschreibung:
 *   Einfaches "Hallo World"-Blink-Programm fuer das ESP32-S3-DevKitC-1 Board.
 *   Die onboard NeoPixel-LED (WS2812B) an GPIO 48 blinkt abwechselnd in
 *   verschiedenen Farben (Rot, Gruen, Blau) und gibt eine Statusmeldung
 *   ueber den UART-Port (Serial) aus.
 *
 * Hardware:
 *   - Board:  ESP32-S3-DevKitC-1 (oder kompatibel)
 *   - LED:    WS2812B NeoPixel an GPIO 48
 *   - UART:   USB-Bruecke ueber CP2102 an /dev/ttyUSB0 (115200 Baud)
 *   - Upload: Nativer USB-JTAG an /dev/ttyACM0
 *
 * Abhaengigkeiten:
 *   - Adafruit NeoPixel Library (via PlatformIO lib_deps)
 *
 * Autor:   peterh69
 * Datum:   2026-04-10
 * Lizenz:  MIT
 * ============================================================================
 */

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

/* --------------------------------------------------------------------------
 * Konfiguration
 * -------------------------------------------------------------------------- */

/** GPIO-Pin, an dem die NeoPixel-LED angeschlossen ist */
#define NEOPIXEL_PIN    48

/** Anzahl der NeoPixel-LEDs auf dem Board */
#define NEOPIXEL_COUNT  1

/** Helligkeit der LED (0-255). 30 reicht fuer Schreibtisch-Nutzung. */
#define LED_BRIGHTNESS  30

/** Blinkintervall in Millisekunden */
#define BLINK_INTERVAL_MS  1000

/* --------------------------------------------------------------------------
 * Globale Objekte
 * -------------------------------------------------------------------------- */

/**
 * NeoPixel-Objekt: 1 LED, WS2812B-Protokoll (GRB-Farbreihenfolge),
 * angesteuert ueber GPIO 48.
 */
Adafruit_NeoPixel pixel(NEOPIXEL_COUNT, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

/**
 * Farbtabelle: Die LED durchlaeuft zyklisch Rot -> Gruen -> Blau.
 * Jede Farbe ist als 32-Bit-Packed-RGB gespeichert.
 */
struct Color {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    const char *name;  /**< Name der Farbe fuer die serielle Ausgabe */
};

static const Color colors[] = {
    {255,   0,   0, "Rot"  },
    {  0, 255,   0, "Gruen"},
    {  0,   0, 255, "Blau" },
};

/** Anzahl der Farben in der Tabelle */
static const int colorCount = sizeof(colors) / sizeof(colors[0]);

/** Aktueller Index in der Farbtabelle */
static int colorIndex = 0;

/** Zaehler fuer Blink-Zyklen (fuer die serielle Ausgabe) */
static unsigned long blinkCount = 0;

/* --------------------------------------------------------------------------
 * Setup - wird einmal beim Start ausgefuehrt
 * -------------------------------------------------------------------------- */

/**
 * Initialisiert die serielle Schnittstelle und die NeoPixel-LED.
 *
 * - Serial (UART0): 115200 Baud, Ausgabe ueber /dev/ttyUSB0
 * - NeoPixel: GPIO 48, Helligkeit auf LED_BRIGHTNESS
 */
void setup() {
    /* Serielle Kommunikation starten (UART0 -> CP2102 -> /dev/ttyUSB0) */
    Serial.begin(115200);

    /* Kurz warten, damit der Serial-Monitor sich verbinden kann */
    delay(2000);

    Serial.println("============================================");
    Serial.println("  Hallo World! - ESP32-S3 NeoPixel Blink");
    Serial.println("============================================");
    Serial.println();
    Serial.println("Board:    ESP32-S3-DevKitC-1");
    Serial.println("LED:      NeoPixel (WS2812B) an GPIO 48");
    Serial.printf( "Helligkeit: %d/255\n", LED_BRIGHTNESS);
    Serial.printf( "Intervall:  %d ms\n", BLINK_INTERVAL_MS);
    Serial.println();

    /* NeoPixel initialisieren */
    pixel.begin();
    pixel.setBrightness(LED_BRIGHTNESS);
    pixel.clear();
    pixel.show();

    Serial.println("NeoPixel initialisiert. Starte Blink-Schleife...");
    Serial.println();
}

/* --------------------------------------------------------------------------
 * Loop - wird endlos wiederholt
 * -------------------------------------------------------------------------- */

/**
 * Hauptschleife: Wechselt die LED-Farbe im Sekundentakt und gibt den
 * aktuellen Zustand ueber Serial aus.
 *
 * Ablauf pro Zyklus:
 *   1. LED auf aktuelle Farbe setzen und anzeigen
 *   2. Farbe und Zyklus-Nummer auf Serial ausgeben
 *   3. BLINK_INTERVAL_MS warten
 *   4. LED ausschalten
 *   5. BLINK_INTERVAL_MS warten
 *   6. Zur naechsten Farbe weiterschalten
 */
void loop() {
    /* Aktuelle Farbe aus der Tabelle holen */
    const Color &c = colors[colorIndex];

    /* LED einschalten: aktuelle Farbe setzen */
    pixel.setPixelColor(0, pixel.Color(c.r, c.g, c.b));
    pixel.show();

    /* Statusmeldung ueber Serial ausgeben */
    blinkCount++;
    Serial.printf("[%6lu] LED AN  - Farbe: %-5s (R=%3d, G=%3d, B=%3d)\n",
                  blinkCount, c.name, c.r, c.g, c.b);

    /* Leuchtdauer abwarten */
    delay(BLINK_INTERVAL_MS);

    /* LED ausschalten */
    pixel.clear();
    pixel.show();
    Serial.printf("[%6lu] LED AUS\n", blinkCount);

    /* Pause im ausgeschalteten Zustand */
    delay(BLINK_INTERVAL_MS);

    /* Zur naechsten Farbe wechseln (zyklisch) */
    colorIndex = (colorIndex + 1) % colorCount;
}
