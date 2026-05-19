#include <msp430.h>   // Die Bibliothek für den MSP430 Mikrocontroller
#include <stdint.h>   // Standard Ganzzahltypen
#include <stdio.h>    // Textformatierungsfunktionen
#include <stdlib.h>   // Fuer Zufallszahl-Funktionen wie rand() und srand() einbinden

#define LCD_ADDR 0x27 // I2C Adresse des LCDs

// --- PIN-DEFINITIONEN ---
#define RGB_R BIT2    // Pin P1.2 -> Rotes Bein der RGB-LED
#define RGB_G BIT3    // Pin P1.3 -> Gruenes Bein der RGB-LED
#define RGB_B BIT4    // Pin P1.4 -> Blaues Bein der RGB-LED
#define BUZZER BIT5   // Pin P1.5 -> Passiver Buzzer zur Soundausgabe

#define TASTER_BLAU  BIT0 // Pin P2.0 -> Blauer Taster
#define TASTER_GRUEN BIT2 // Pin P2.2 -> Gruener Taster
#define TASTER_ROT   BIT4 // Pin P2.4 -> Roter Taster

// BOMBEN-KABEL (An Port 6 angeschlossene Jumper-Kabel zum Durchtrennen)
#define KABEL_1 BIT1 // Pin P6.1 -> 1. Kabel (Orange)
#define KABEL_2 BIT2 // Pin P6.2 -> 2. Kabel (Gruen)
#define KABEL_3 BIT3 // Pin P6.3 -> 3. Kabel (Blau)
#define KABEL_4 BIT4 // Pin P6.4 -> 4. Kabel (Braun)

// Farbnamen mit Zahlen verknuepfen, um sie im Code leichter zu nutzen
#define ROT 0 // Rot = 0
#define GRN 1 // Gruen = 1
#define BLA 2 // Blau = 2

// --- TEXT-SPEICHER (Puffer fuer LCD und den Seriellen Monitor) ---
char zeile1[32];       // 1. Zeile des LCDs
char zeile2[32];       // 2. Zeile des LCDs
char uart_buffer[128]; // lange Texte, die an den PC gesendet werden

// --- SPIEL-VARIABLEN ---
int ziel_wert = 0;    // Der geheime Zielwert, den wir mit dem Potentiometer finden muessen
int restzeit = 60;    // Verbleibende Zeit bis zur Detonation (startet bei 60 Sekunden)
int zeit_zaehler = 0; // Zaehler im Hintergrund, der misst, ob 1 Sekunde vergangen ist
int zeit_limit = 20;  // Nach 20 Schleifendurchlaeufen ist 1 Sek. vergangen (wird verringert, um die Zeit zu beschleunigen)

int gewonnen = 0;     // Haben wir das Spiel gewonnen? (0 = Nein, 1 = Ja)
int spiel_phase = 1;  // Spielphase (1: Poti-Suche, 2: Farbcode-Eingabe, 3: Kabel durchtrennen)
int reihenfolge_status = 0; // Wie viele Farben des Codes wurden bereits richtig eingegeben?
int passwort_gezeigt = 0;   // Wurde das Passwort bereits ueber die RGB-LED animiert?
int lcd_fehler_vorhanden = 0; // Fehler-Flag fuer das LCD, damit das Programm bei einem Kabelbruch nicht abstuerzt

int geheim_passwort[3]; // Array, das den 3-stelligen geheimen Farbcode speichert (z.B. {0, 2, 1})

// Variablen fuer den "Vorherigen Zustand", um nur das ERSTE Druecken (Falling Edge) zu erkennen:
int rst_vorher = 1, t_rot_vorher = 1, t_grn_vorher = 1, t_bla_vorher = 1;

// Kabel-Rollen und Zustaende
int kabel_rollen[4];  // Die geheimen Rollen hinter den 4 Kabeln (Entschaerfen, Zeit halbieren, Beschleunigen, Detonation)
int kabel_getrennt[4] = {0, 0, 0, 0}; // Welche Kabel wurden durchtrennt? (0 = Nicht getrennt, 1 = Getrennt)

// ================= VERZOEGERUNGS-FUNKTION =================
// Haelt das System in Millisekunden (ms) an
void delay_ms(unsigned int ms) {
    while (ms--) { __delay_cycles(1000); } // 1000 Taktzyklen sind etwa 1 Millisekunde
}

// ================= UART FUNKTION =================
// Sendet Zeichenkette Buchstabe fuer Buchstabe an den PC
void UART_Drucken(char *str) {
    while (*str) {
        while (!(UCA1IFG & UCTXIFG)); // Warten, bis der Sendepuffer leer ist
        UCA1TXBUF = *str++;           // Zeichen senden und zum naechsten uebergehen
    }
}

// ================= BUZZER TOENE =================
// Erzeugt Toene, indem es den passiven Buzzer vibrieren laesst
void Buzzer_Spielen(unsigned int tonhoehe, unsigned int dauer) {
    unsigned int i, j;
    for (i = 0; i < dauer; i++) {
        P1OUT ^= BUZZER; // Buzzer-Pin umschalten
        for (j = 0; j < tonhoehe; j++) {
            __delay_cycles(10);
        }
    }
    P1OUT &= ~BUZZER; // Buzzer am Ende sicher ausschalten
}
void Fehler_Ton() {
    Buzzer_Spielen(150, 150);
    delay_ms(50);
    Buzzer_Spielen(200, 150);
} // Fehler-Ton
void Richtige_Taste_Ton() {
    Buzzer_Spielen(30, 80);
} // Ton fuer richtige Taste (Klick)
void Erfolg_Ton() {
    Buzzer_Spielen(40, 200);
    delay_ms(50);
    Buzzer_Spielen(30, 200);
    delay_ms(50);
    Buzzer_Spielen(20, 400);
} // Erfolgs Melodie
void Phasen_Uebergang_Ton() {
    Buzzer_Spielen(20, 100);
    delay_ms(50);
    Buzzer_Spielen(15, 150);
} // Phasen Uebergangs Ton
void Kabel_Schock_Ton() {
    Buzzer_Spielen(250, 300);
} // wenn falsches Kabel getrennt wird
void Explosion_Ton() {
    Buzzer_Spielen(300, 800);
} // Langer, tiefer Explosions Ton

// ================= I2C UND LCD FUNKTIONEN =================
// Hier kommunizieren wir mit dem LCD ueber das I2C Protokoll.
// Ein Timeout wurde hinzugefuegt: Falls ein Kabel bricht, stuerzt der MSP430F5529 nicht ab.
void I2C_Schreiben(unsigned char data) {
    volatile unsigned int timeout = 5000;
    while ((UCB0CTL1 & UCTXSTP) && timeout > 0) timeout--; //  Warten, bis der vorherige STOP-Befehl abgeschlossen ist.
    UCB0CTL1 |= UCTR | UCTXSTT;   // MSP430 als Sender (Transmitter - UCTR) einstellen und START-Bedingung (UCTXSTT) senden
    timeout = 5000;
    while(!(UCB0IFG & UCTXIFG) && !(UCB0IFG & UCNACKIFG) && timeout > 0) timeout--;  // Warten, bis das LCD antwortet (NACK) oder der Puffer bereit ist (TXIFG)
    // Fehlerbehandlung: Wenn das LCD "NACK" (Keine Antwort) sendet oder die Zeit ablaeuft
    if ((UCB0IFG & UCNACKIFG) || timeout == 0) {
        UCB0CTL1 |= UCTXSTP; // STOP Bedingung senden, um die Kommunikation hart abzubrechen
        UCB0STAT &= ~UCNACKIFG; // Das NACK-Fehler-Flag im System wieder loeschen
        lcd_fehler_vorhanden = 1; // Unser eigenes Fehler-Flag setzen (Ueberspringt alle kuenftigen LCD-Befehle)
        return;
    }
    UCB0TXBUF = data;    // Wenn alles gut ist: Das eigentliche Daten-Byte in TXBUF legen
    timeout = 5000;
    while(!(UCB0IFG & UCTXIFG) && timeout > 0) timeout--; //  Warten, bis das Byte vollstaendig ueber das Kabel gesendet wurde
    UCB0CTL1 |= UCTXSTP; // STOP Bedingung (UCTXSTP) senden, um das Ende der Datenuebertragung zu signalisieren
    timeout = 5000;
    while ((UCB0CTL1 & UCTXSTP) && timeout > 0) timeout--; // Warten, bis die STOP Bedingung erfolgreich auf der Hardware-Leitung ausgefuehrt wurde
}
// Sendet einen Befehl oder ein Zeichen im 4-Bit-Modus an das LCD
void LCD_Senden(unsigned char data, unsigned char is_data) {
    if (lcd_fehler_vorhanden) return; // Wenn das LCD fehlerhaft ist, versuche nicht zu senden
    // Das 8-Bit-Datenpaket in zwei Haelften (Nibbles) aufteilen
    unsigned char high_nibble = data & 0xF0; // Die oberen 4 Bits isolieren
    unsigned char low_nibble = (data << 4) & 0xF0; // Die unteren 4 Bits nach links schieben und isolieren
    unsigned char mode = (is_data == 1) ? 0x01 : 0x00; // Register Select Pin bestimmen: 1 = Data, 0 = Command
    // Oberes Nibble senden
    I2C_Schreiben(high_nibble | mode | 0x08 | 0x04); // Enable  = 1 (Puls starten - Tuer auf)
    I2C_Schreiben(high_nibble | mode | 0x08);  // Enable = 0 (Puls beenden, Daten uebernehmen - Tuer zu)
    // Unteres Nibble senden
    I2C_Schreiben(low_nibble | mode | 0x08 | 0x04); // Enable = 1 (Puls starten)
    I2C_Schreiben(low_nibble | mode | 0x08); // Enable = 0 (Puls beenden, Daten uebernehmen)
}
void LCD_Loeschen() {
    LCD_Senden(0x01, 0);
    delay_ms(2);
} // Loescht den Text auf dem Bildschirm
void LCD_CursorSetzen(unsigned char row, unsigned char col) {
    unsigned char pos = (row == 0) ? (0x80 + col) : (0xC0 + col);
    LCD_Senden(pos, 0); // Setzt den Cursor auf die gewuenschte Zeile/Spalte
}
void LCD_Drucken(char *str) {
    while (*str) {
        LCD_Senden(*str++, 1); // 1 bedeutet: Das ist ein Data.
    }
}
void LCD_Init() { // Initialisierungssequenz fuer das LCD
    delay_ms(50);
    lcd_fehler_vorhanden = 0; // Warten, bis das LCD nach dem Einschalten hochgefahren ist (Power-On Reset)
    I2C_Schreiben(0x00); // I2C-Verbindungstest: Sende ein leeres Byte, um zu sehen, ob das LCD antwortet.
    if(lcd_fehler_vorhanden) {
        UART_Drucken("I2C FEHLER!\r\n");
        return;  // Wenn kein LCD da ist, brich die Initialisierung ab!
    }
    // HD44780 (LCD) Datasheet Sequence: Hardware Reset & 4-Bit Mode
    LCD_Senden(0x33, 0); // Hardware Reset (Wake-Up)
    delay_ms(5);
    LCD_Senden(0x32, 0); // 4-Bit Modus aktivieren
    delay_ms(5);
    // LCD-Konfiguration festlegen
    LCD_Senden(0x28, 0); // 4-Bit, 2 Lines, 5x8 Font
    LCD_Senden(0x0C, 0); // Display ON, Cursor OFF
    LCD_Loeschen(); // Bildschirm aufraeumen und Cursor auf Startposition setzen
}

// ================= RGB LED STEUERUNGSFUNKTIONEN =================
// Schaltet die 3 internen Farben der RGB-LED basierend auf 1 (an) und 0 (aus)
void RGB_Farbe_Einstellen(int r, int g, int b) {
    // Bitweises ODER (|=) setzt das Bit auf 1 (LED AN), ohne andere Pins zu stoeren.
    // Bitweises UND NOT (&= ~) loescht das Bit auf 0 (LED AUS), ohne andere Pins zu stoeren.
    if(r) P1OUT |= RGB_R;
    else P1OUT &= ~RGB_R; // rot
    if(g) P1OUT |= RGB_G;
    else P1OUT &= ~RGB_G; // gruen
    if(b) P1OUT |= RGB_B;
    else P1OUT &= ~RGB_B; // blau
}
// Aktiviert einfach die Farbe basierend auf dem uebergebenen Code
void Farbe_Zeigen(int farb_code) {
    if (farb_code == ROT) RGB_Farbe_Einstellen(1, 0, 0); // nur rot an
    else if (farb_code == GRN) RGB_Farbe_Einstellen(0, 1, 0); // nur gruen an
    else if (farb_code == BLA) RGB_Farbe_Einstellen(0, 0, 1); // nur blau am
    else RGB_Farbe_Einstellen(0, 0, 0); // komplett ausschalten
}
// Gibt nur den Anfangsbuchstaben der Farbe zurueck
char Farbe_Buchstabe(int farb_code) {
    if (farb_code == ROT) return 'R';
    if (farb_code == GRN) return 'G';
    if (farb_code == BLA) return 'B';
    return '?';  // Fallback bei fehlerhaftem Code
}
// In Phase 2 (Simon Says) zeigt die RGB-LED den Code durch Blinken an
// Nutzt blockierende Delays, um Benutzereingaben zu verhindern.
void Passwort_Animation_Zeigen() {
    int i;
    RGB_Farbe_Einstellen(0,0,0); // // Sicherstellen, dass die LED aus ist
    delay_ms(500); // Kurze Pause, bevor die Sequenz beginnt
    for(i=0; i<3; i++) {
        Farbe_Zeigen(geheim_passwort[i]); // Zeige die i-te Farbe aus dem Passwort-Array
        Buzzer_Spielen(50, 150); // Kurzer Signalton passend zur Farbe
        delay_ms(550); // Farbe fuer 550ms leuchten lassen
        RGB_Farbe_Einstellen(0,0,0); // LED ausschalten
        delay_ms(300); // Pause zwischen den Farben (Duty Cycle)
    }
}

// ================= HILFSFUNKTION FUER UART GEHEIMAGENTEN-PANEL =================
// Druckt die Farbe des Kabels und dessen Funktion auf den Seriellen Monitor
void Kabel_Rolle_Drucken(int rolle, char* kabel_name) {
    UART_Drucken(kabel_name);
    if (rolle == 0) UART_Drucken(" -> RICHTIGES KABEL (Entschaerft Bombe!)\r\n"); // Bombe entschaerft
    else if (rolle == 1) UART_Drucken(" -> FALSCH (Halbiert die Zeit)\r\n"); // Zeit wird halbiert
    else if (rolle == 2) UART_Drucken(" -> FALSCH (Zeit vergeht 4x schneller)\r\n"); // Zeit laeuft 4x schneller ab
    else if (rolle == 3) UART_Drucken(" -> TOEDLICH (Sofortige Detonation!)\r\n"); // Sofortige Explosion
}

// ================= LOGIK ZUM KABEL TRENNEN =================
// Wird aufgerufen, wenn ein Kabel getrennt (vom GND geloest) wird
void Kabel_Getrennt(int index) {
    kabel_getrennt[index] = 1; // Markiere das Kabel als getrennt (damit es nicht mehrmals ausloest)
    int rolle = kabel_rollen[index]; // Pruefe die zufaellig zugewiesene Rolle des Kabels

    if (rolle == 0) { // RICHTIGES KABEL!
        gewonnen = 1; // Spiel gewonnen
        Erfolg_Ton();
        UART_Drucken("\r\n*** RICHTIGES KABEL GETRENNT! BOMBE ENTSCHAERFT! ***\r\n");
    }
    else if (rolle == 1) { // KABEL HALBIERT DIE ZEIT!
        Kabel_Schock_Ton();
        restzeit = restzeit / 2; // Teile die verbleibende Zeit durch 2 (z.B. von 40 sek auf 20 sek)
        RGB_Farbe_Einstellen(1,0,0); delay_ms(100); RGB_Farbe_Einstellen(0,0,0); // Roter Schock-Blitz
        UART_Drucken("\r\n!!! FALSCHES KABEL: Zeit halbiert !!!\r\n");
    }
    else if (rolle == 2) { // KABEL BESCHLEUNIGT DIE ZEIT!
        Kabel_Schock_Ton();
        zeit_limit = 5; // Verringere das Zeitlimit von 20 auf 5. Sekunden vergehen nun 4x schneller!
        RGB_Farbe_Einstellen(1,0,0); delay_ms(100); RGB_Farbe_Einstellen(0,0,0);
        UART_Drucken("\r\n!!! FALSCHES KABEL: Zeit beschleunigt !!!\r\n");
    }
    else if (rolle == 3) { // TOEDLICHES KABEL!
        UART_Drucken("\r\n!!! TOEDLICHES KABEL: BOMBE SOFORT DETONIERT !!!\r\n");
        Explosion_Ton();
        restzeit = 0; // Setze die Zeit sofort auf 0, um den Detonations-Bildschirm auszubloesen
        zeit_zaehler = 0;
    }
}

// ================= SPIEL ZURUECKSETZEN UND NEU STARTEN =================
// Diese Funktion wird ausgefuehrt, wenn das Geraet eingeschaltet oder Reset gedrueckt wird
void Spiel_Zuruecksetzen() {
    // 1. SICHERHEITS-CHECK: Sind alle 4 Jumper-Kabel physisch mit den Pins und GND verbunden?
    // Falls nicht, wird ein Fehler auf dem LCD angezeigt und das Spiel startet nicht.
    while ((P6IN & (KABEL_1 | KABEL_2 | KABEL_3 | KABEL_4)) != 0) {
        LCD_Loeschen();
        LCD_CursorSetzen(0, 0);
        LCD_Drucken("Kabel verbinden!");
        RGB_Farbe_Einstellen(1, 0, 0);
        Buzzer_Spielen(100, 100);
        delay_ms(400);
        RGB_Farbe_Einstellen(0, 0, 0);
        delay_ms(400);
    }

    // 2. ECHTE ZUFALLSZAHLEN GENERIEREN - Hardware Entropy
    ADC12CTL0 |= ADC12SC;
    while ((ADC12IFG & BIT0) == 0); // Poti einmal lesen
    srand(ADC12MEM0 + TA0R); // Analoges Rauschen + Reaktionszeit beim Tastendruck (Timer) als Seed nutzen

    // 3. ZIELE FESTLEGEN
    ziel_wert = rand() % 101; // Zufaelliges Poti-Ziel zwischen 0 und 100
    geheim_passwort[0] = rand() % 3; // 0, 1, 2 (RGB Farben)
    geheim_passwort[1] = rand() % 3;
    geheim_passwort[2] = rand() % 3;

    // 4. KABEL-ROLLEN MISCHEN (Fisher-Yates Shuffle Algorithmus)
    int rollen[4] = {0, 1, 2, 3}; // 0:Richtig, 1:Zeit / 2, 2:Zeit x 4, 3:Toedlich
    int i;
    for (i = 3; i > 0; i--) {
        int j = rand() % (i + 1); // Zufaellige Position finden
        // Werte tauschen (Swap)
        int temp = rollen[i];
        rollen[i] = rollen[j];
        rollen[j] = temp;
    }
    // Die gemischten Rollen den echten Kabeln zuweisen
    kabel_rollen[0] = rollen[0];
    kabel_rollen[1] = rollen[1];
    kabel_rollen[2] = rollen[2];
    kabel_rollen[3] = rollen[3];

    // Alle Kabel als "nicht getrennt" markieren
    kabel_getrennt[0] = 0;
    kabel_getrennt[1] = 0;
    kabel_getrennt[2] = 0;
    kabel_getrennt[3] = 0;
    // Zeit und Limit wieder auf 60 Sekunden normalisieren
    restzeit = 60;
    zeit_zaehler = 0;
    zeit_limit = 20;
    // Spielstatus auf Phase 1 setzen
    gewonnen = 0;
    reihenfolge_status = 0;
    passwort_gezeigt = 0;
    spiel_phase = 1;
    // LEDs und Bildschirm fuer die neue Runde saeubern
    RGB_Farbe_Einstellen(0, 0, 0);
    LCD_Loeschen();

    // 5. AGENTEN-PANEL (UART): DRUCKE ALLE GEHEIMCODES DIESER RUNDE
    UART_Drucken("\r\n========================================\r\n");
    UART_Drucken("         NEUE BOMBE AKTIVIERT!\r\n");
    UART_Drucken("========================================\r\n");
    sprintf(uart_buffer, "1) Potentiometer Ziel    : %d.%d\r\n", (ziel_wert/10), (ziel_wert%10));
    UART_Drucken(uart_buffer);
    sprintf(uart_buffer, "2) RGB Tasten Passwort   : %c - %c - %c\r\n", Farbe_Buchstabe(geheim_passwort[0]), Farbe_Buchstabe(geheim_passwort[1]), Farbe_Buchstabe(geheim_passwort[2]));
    UART_Drucken(uart_buffer);
    UART_Drucken("3) KABELSTATUS (GEHEIM):\r\n");
    Kabel_Rolle_Drucken(kabel_rollen[0], "   - Oranges Kabel (P6.1)");
    Kabel_Rolle_Drucken(kabel_rollen[1], "   - Gruenes Kabel (P6.2)");
    Kabel_Rolle_Drucken(kabel_rollen[2], "   - Blaues Kabel  (P6.3)");
    Kabel_Rolle_Drucken(kabel_rollen[3], "   - Braunes Kabel (P6.4)");
    UART_Drucken("========================================\r\n\r\n");
}

// ================= HAUPTPROGRAMM (MAIN) =================
int main(void) {
    WDTCTL = WDTPW | WDTHOLD; // Watchdog-Timer anhalten
    TA0CTL = TASSEL_2 | MC_2; // Timer0 für den Zufalls-Seed starten (laeuft im Hintergrund)

    // --- PIN-RICHTUNGEN (DIR: Direction, REN: Resistor Enable, OUT: Output) ---
    // 1. Ausgabe-Pins (RGB und Summer)
    P1DIR |= BIT0 | RGB_R | RGB_G | RGB_B | BUZZER;
    P4DIR |= BIT7;
    P1OUT &= ~(RGB_R | RGB_G | RGB_B | BUZZER); // Zu Beginn alles ausschalten (0)

    // 2. Reset-Taster (P1.1)
    P1DIR &= ~BIT1; P1REN |= BIT1; P1OUT |= BIT1; // Als Eingang setzen, Widerstand aktivieren, Pull-Up

    // 3. Passwort-Taster (P2.0, P2.2, P2.4)
    P2DIR &= ~(TASTER_BLAU | TASTER_GRUEN | TASTER_ROT);
    P2REN |= (TASTER_BLAU | TASTER_GRUEN | TASTER_ROT);
    P2OUT |= (TASTER_BLAU | TASTER_GRUEN | TASTER_ROT);

    // 4. Bomben-Kabel (PORT 6)
    P6DIR &= ~(KABEL_1 | KABEL_2 | KABEL_3 | KABEL_4);
    P6REN |= (KABEL_1 | KABEL_2 | KABEL_3 | KABEL_4);
    P6OUT |= (KABEL_1 | KABEL_2 | KABEL_3 | KABEL_4); // Intern 3.3V aktivieren. Solange das Kabel GND beruehrt, lesen wir 0. Wenn getrennt, lesen wir 1!

    // --- UART (SERIELLER MONITOR) EINSTELLUNGEN ---
    P4SEL |= BIT4 | BIT5; // P4.4 und P4.5 fuer UART-Modus aktivieren
    UCA1CTL1 |= UCSWRST;
    UCA1CTL1 |= UCSSEL_2;
    UCA1BR0 = 109;
    UCA1BR1 = 0;
    UCA1MCTL = UCBRS_2 | UCBRF_0; // 9600 Baudrate einstellen
    UCA1CTL1 &= ~UCSWRST;

    // --- I2C (LCD DISPLAY) EINSTELLUNGEN ---
    P3SEL |= BIT0 | BIT1;
    P3REN |= BIT0 | BIT1;
    P3OUT |= BIT0 | BIT1; // SDA und SCL Pins (mit Pull-up Widerstaenden)
    UCB0CTL1 |= UCSWRST;
    UCB0CTL0 = UCMST | UCMODE_3 | UCSYNC;
    UCB0CTL1 = UCSSEL_2 | UCSWRST;
    UCB0BR0 = 10; UCB0BR1 = 0; // Kommunikationsgeschwindigkeit
    UCB0I2CSA = LCD_ADDR; UCB0CTL1 &= ~UCSWRST;

    // --- ADC (POTENTIOMETER) EINSTELLUNGEN ---
    P6SEL |= BIT0; // Pin P6.0 in den Analog-Lesemodus versetzen
    ADC12CTL0 &= ~ADC12ENC;
    ADC12CTL0 = ADC12ON | ADC12SHT0_2;
    ADC12CTL1 = ADC12SHP;
    ADC12CTL2 = ADC12RES_2; // 12-Bit Aufloesung (Liest Werte von 0-4095)
    ADC12MCTL0 = ADC12INCH_0;
    ADC12CTL0 |= ADC12ENC;

    LCD_Init(); // Display aufwecken
    Spiel_Zuruecksetzen(); // Spiel initialisieren und Variablen setzen

    // ENDLOSE SPIEL-SCHLEIFE
    while(1) {
        // RESET-TASTER UEBERPRUEFEN
        int rst_jetzt = (P1IN & BIT1) ? 1 : 0;
        if (rst_jetzt == 0 && rst_vorher == 1) { Spiel_Zuruecksetzen();
        } // Knopfdruck abfangen
        rst_vorher = rst_jetzt;

        // WENN DIE ZEIT NOCH NICHT ABGELAUFEN IST UND WIR NOCH NICHT GEWONNEN HABEN:
        if (restzeit > 0 && gewonnen == 0) {

            // 1. POTENTIOMETER KONTINUIERLICH AUSLESEN
            ADC12CTL0 |= ADC12SC; while ((ADC12IFG & BIT0) == 0);
            int poti_wert = ((uint32_t)ADC12MEM0 * 100) / 4095; // Den gelesenen Wert (4095) in einen Prozentwert (100) umwandeln
            int differenz = poti_wert - ziel_wert;
            if (differenz < 0) differenz = -differenz; // Differenz zum Absolutwert (positiv) machen

            // PHASE 1 ERFOLGREICH: Wenn der Poti-Wert nah am Ziel ist (+-9)
            if (differenz <= 9) {
                if (spiel_phase == 1) spiel_phase = 2; // Direkt zu Phase 2 (Simon Says) wechseln
                P4OUT |= BIT7;
                P1OUT &= ~BIT0; // Gruene LED auf dem Board an, rote LED aus
            } else {
                // FALLS IN PHASE 2 ODER 3 DAS POTI VERSEHENTLICH VERSTELLT WIRD:
                // Passwoerter und Phasen werden zurueckgesetzt! Zurueck zu Phase 1 als Strafe.
                spiel_phase = 1;
                passwort_gezeigt = 0;
                reihenfolge_status = 0;
                P4OUT &= ~BIT7;
                P1OUT |= BIT0;
                RGB_Farbe_Einstellen(0,0,0);
            }

            // --- BILDSCHIRM UND LOGIK FUER PHASE 1 (Such-Phase) ---
            if (spiel_phase == 1) {
                sprintf(zeile2, "Ziel: %d.%d       ", (poti_wert/10), (poti_wert%10)); // Poti-Wert mit Komma anzeigen
                LCD_CursorSetzen(1, 0);
                LCD_Drucken(zeile2);
            }

            // --- BILDSCHIRM UND LOGIK FUER PHASE 2 (Simon Says Passwort) ---
            else if (spiel_phase == 2) {
                // Wenn das Passwort zum ersten Mal gefunden wird, spiele die Animation einmal ab
                if (passwort_gezeigt == 0) {
                    Passwort_Animation_Zeigen();
                    passwort_gezeigt = 1;
                    reihenfolge_status = 0;
                } else {
                    // Warte auf die Eingabe des Benutzers
                    int t_bla_jetzt = (P2IN & TASTER_BLAU) ? 1 : 0;
                    int t_grn_jetzt = (P2IN & TASTER_GRUEN) ? 1 : 0;
                    int t_rot_jetzt = (P2IN & TASTER_ROT) ? 1 : 0;

                    int gedrueckte_farbe = -1; // -1 = Nichts wurde gedrueckt

                    // Nur den "ersten Moment" des Drueckens erkennen (Falling Edge Detection)
                    if (t_rot_jetzt == 0 && t_rot_vorher == 1) gedrueckte_farbe = ROT;
                    if (t_grn_jetzt == 0 && t_grn_vorher == 1) gedrueckte_farbe = GRN;
                    if (t_bla_jetzt == 0 && t_bla_vorher == 1) gedrueckte_farbe = BLA;

                    t_rot_vorher = t_rot_jetzt;
                    t_grn_vorher = t_grn_jetzt;
                    t_bla_vorher = t_bla_jetzt;

                    // Wurde ein Taster gedrueckt?
                    if (gedrueckte_farbe != -1) {
                        // WURDE DIE RICHTIGE FARBE GEDRUECKT?
                        if (gedrueckte_farbe == geheim_passwort[reihenfolge_status]) {
                            reihenfolge_status++; Richtige_Taste_Ton(); // Position erhoehen und KLICK-Ton abspielen
                            if (reihenfolge_status == 3) { // Wenn alle 3 richtig sind
                                spiel_phase = 3; // WECHSEL ZU PHASE 3 (Kabel durchtrennen!)
                                Phasen_Uebergang_Ton();
                            }
                        } else {
                            // FALSCHE FARBE GEDRUECKT!
                            Fehler_Ton();
                            passwort_gezeigt = 0;
                            reihenfolge_status = 0; // Flags zuruecksetzen, damit die Animation erneut laeuft
                        }
                    }
                }
                sprintf(zeile2, "Code Eing: %d/3 ", reihenfolge_status); // Fortschritt auf dem LCD anzeigen (0/3, 1/3)
                LCD_CursorSetzen(1, 0);
                LCD_Drucken(zeile2);
            }

            // --- BILDSCHIRM UND LOGIK FUER PHASE 3 (Bombenentschaerfung / Kabel) ---
            else if (spiel_phase == 3) {
                LCD_CursorSetzen(1, 0);
                LCD_Drucken("KABEL TRENNEN!  "); // Nur KABEL TRENNEN Warnung anzeigen

                // Erzeugt einen "Herzschlag"-Ton (Ticken) abhaengig vom Timer
                // Wenn die Sekunden schneller vergehen, wird dieser Rhythmus extrem stressig
                if (zeit_zaehler == 0 || zeit_zaehler == (zeit_limit/2)) {
                    Buzzer_Spielen(20, 20);
                }

                // LESE DEN STATUS DER KABEL (Da sie intern an 3.3V haengen und mit GND verbunden sind, lesen wir 0. Sobald getrennt, lesen wir 1!)
                int k1_jetzt = (P6IN & KABEL_1) ? 1 : 0;
                int k2_jetzt = (P6IN & KABEL_2) ? 1 : 0;
                int k3_jetzt = (P6IN & KABEL_3) ? 1 : 0;
                int k4_jetzt = (P6IN & KABEL_4) ? 1 : 0;

                // Finde das getrennte Kabel und uebergebe es an die Logik, falls es noch nicht verarbeitet wurde
                if (k1_jetzt == 1 && kabel_getrennt[0] == 0) Kabel_Getrennt(0);
                if (k2_jetzt == 1 && kabel_getrennt[1] == 0) Kabel_Getrennt(1);
                if (k3_jetzt == 1 && kabel_getrennt[2] == 0) Kabel_Getrennt(2);
                if (k4_jetzt == 1 && kabel_getrennt[3] == 0) Kabel_Getrennt(3);
            }

            // ZEITZAEHLER FUER JEDE PHASE IN DER OBEREN ZEILE ANZEIGEN
            sprintf(zeile1, "Zeit: %d sek    ", restzeit);
            LCD_CursorSetzen(0, 0);
            LCD_Drucken(zeile1);

            delay_ms(50); // System fuer 50ms anhalten (Das ist die Frame-Rate des Spiels)
            zeit_zaehler++; // Da 50ms vergangen sind, erhoehe den Zeitzähler um 1

            // ZAEHLER HAT DAS LIMIT ERREICHT (z.B. 20 mal 50ms = 1000ms = 1 Sekunde)
            if (zeit_zaehler >= zeit_limit) {
                zeit_zaehler = 0; // Zaehler zuruecksetzen
                if (restzeit > 0) restzeit--; // Die verbleibende Zeit um 1 Sekunde reduzieren

                // Warn-Piepton in den letzten 10 Sekunden (in Phase 3 deaktiviert, da wir den Herzschlag haben)
                if(restzeit <= 10 && restzeit > 0 && spiel_phase != 3) { Buzzer_Spielen(50, 50); }

                // WENN DIE ZEIT NULL IST UND WIR NOCH NICHT GEWONNEN HABEN (BOOOM!)
                if(restzeit == 0 && gewonnen == 0) { Fehler_Ton(); }
            }
        }

        // --- BILDSCHIRME NACH SPIELENDE ---
        else if (gewonnen == 1) {
            // ERFOLGREICHE ENTSCHAERFUNG
            P4OUT |= BIT7; P1OUT &= ~BIT0; // Gruene LED leuchtet
            LCD_CursorSetzen(0,0);
            LCD_Drucken("BOMBE ENTSCHAERF"); // BOMBE ENTSCHAERFT
            LCD_CursorSetzen(1,0);
            LCD_Drucken("Glueckwunsch!   "); // GLUECKWUNSCH!

            // Party-Modus (Gewinn-Animation): Farben und Toene aendern sich wild
            int r_farbe = rand() % 3; Farbe_Zeigen(r_farbe);
            int r_ton = (rand() % 30) + 10; Buzzer_Spielen(r_ton, 80);
        }
        else {
            // DETONATION (Wenn die Zeit abgelaufen ist oder das toedliche Kabel getrennt wurde)
            P4OUT &= ~BIT7; P1OUT |= BIT0; RGB_Farbe_Einstellen(1, 0, 0); // Rote LEDs leuchten
            LCD_CursorSetzen(0,0);
            LCD_Drucken("BOMBE DETONIERT!"); // BOMBE DETONIERT
            LCD_CursorSetzen(1,0);
            LCD_Drucken("Verloren!       "); // VERLOREN
        }
    }
}
