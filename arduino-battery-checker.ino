/*
  AA Battery Meter (LCD + LEDs + Serial)
  --------------------------------------
  Hardware (Arduino UNO + Starter Kit, 4-bit parallel LCD):

  LCD (16-pin parallel)
    1 VSS -> GND
    2 VDD -> 5V
    3 VO  -> 10k pot w/ ends to 5V & GND (or tie VO to GND for max contrast)
    4 RS  -> D12
    5 RW  -> GND
    6 E   -> D11
    11 D4 -> D5
    12 D5 -> D4
    13 D6 -> D3
    14 D7 -> D2
    15 A  -> 5V (use 220Ω if too bright)
    16 K  -> GND

  Battery sense divider (AA only)
    Battery + -> 10k -> A0 -> 10k -> GND
    Battery - -> GND  (common ground with Arduino)

  Optional LED “fuel gauge”
    Pin -> 220Ω -> LED anode (+), LED cathode (-) -> GND
    RED  -> D9
    YEL  -> D8
    GRN  -> D7

  Notes
  - Uses INTERNAL ~1.1 V reference for more resolution at low voltages.
  - Calibrate VREF once (see VREF_CAL below) for best absolute accuracy.
  - Presence hysteresis avoids flicker when touching cells to the clips.
*/

#include <LiquidCrystal.h>

// --- LCD wiring (Arduino Starter Kit mapping: RS,E,D4,D5,D6,D7) ---
static const uint8_t LCD_RS = 12;
static const uint8_t LCD_E  = 11;
static const uint8_t LCD_D4 = 5;
static const uint8_t LCD_D5 = 4;
static const uint8_t LCD_D6 = 3;
static const uint8_t LCD_D7 = 2;
LiquidCrystal lcd(LCD_RS, LCD_E, LCD_D4, LCD_D5, LCD_D6, LCD_D7);

// --- Battery sense ---
static const uint8_t PIN_SENSE = A0;   // midpoint of 10k:10k divider

// Calibrate this once to your board (UNO INTERNAL is ~1.1 V but varies ±10%).
// If a known-good meter reads V_true and the sketch displays V_disp:
//   NEW_VREF = OLD_VREF * (V_true / V_disp)
static const float VREF_CAL = 1.085f;  // volts

// Presence hysteresis (BATTERY volts, not A0). Tweak if you like.
static const float BAT_PRESENT_ON  = 0.06f; // becomes present above  60 mV
static const float BAT_PRESENT_OFF = 0.02f; // becomes absent  below  20 mV
static bool batteryPresent = false;

// Status thresholds (BATTERY volts). Keep these consistent everywhere.
static const float V_NEW  = 1.50f; // ≥ 1.50 V  → NEW
static const float V_GOOD = 1.30f; // ≥ 1.30 V  → GOOD
static const float V_LOW  = 1.10f; // ≥ 1.10 V  → LOW
// else < 1.10 V → DEAD

// Optional LED “fuel gauge”
static const bool     USE_LEDS = true;  // set false to disable LEDs
static const uint8_t  LED_RED  = 9;
static const uint8_t  LED_YEL  = 8;
static const uint8_t  LED_GRN  = 7;

// Sampling / printing
static const uint16_t ADC_SAMPLES = 50; // averaging for stable readings
static const uint8_t  LCD_COLS    = 16;

// -------- Helpers --------

// Read battery voltage (in volts) using INTERNAL ref and 2:1 divider.
float readBatteryVolts(uint16_t samples = ADC_SAMPLES) {
  uint32_t acc = 0;
  for (uint16_t i = 0; i < samples; ++i) {
    acc += analogRead(PIN_SENSE);
    delayMicroseconds(250);
  }
  const float avg    = acc / float(samples);
  const float vsense = (avg * VREF_CAL) / 1023.0f; // volts at A0
  return vsense * 2.0f;                             // undo the 2:1 divider
}

// Print exactly 16 characters on an LCD line (pads with spaces).
void lcdPrintPadded(uint8_t row, const char* text) {
  lcd.setCursor(0, row);
  uint8_t i = 0;
  for (; i < LCD_COLS && text[i]; ++i) lcd.print(text[i]);
  for (; i < LCD_COLS;              ++i) lcd.print(' ');
}

// Compose and show two LCD lines (mV below 1.000 V; padded to 16 cols).
void lcdShow(float v, bool present) {
  char line[33];

  // Line 1: Voltage (mV below 1.000 V for readability)
  if (v < 1.0f) {
    const int mv = int(v * 1000.0f + 0.5f);
    snprintf(line, sizeof(line), "V: %d mV", mv);
  } else {
    char vbuf[12];
    dtostrf(v, 0, 3, vbuf); // 3 decimals
    snprintf(line, sizeof(line), "V: %s V", vbuf);
  }
  lcdPrintPadded(0, line);

  // Line 2: Status or prompt
  const char* status = "Insert AA cell";
  if (present) {
    if      (v <  V_LOW)  status = "Status: DEAD";
    else if (v <  V_GOOD) status = "Status: LOW";
    else if (v <  V_NEW)  status = "Status: GOOD";
    else                  status = "Status: NEW";
  }
  lcdPrintPadded(1, status);
}

// LEDs reflect the same thresholds; off when no battery detected.
void ledsForVoltage(float v, bool present) {
  if (!USE_LEDS) return;

  if (!present) {
    digitalWrite(LED_RED, LOW);
    digitalWrite(LED_YEL, LOW);
    digitalWrite(LED_GRN, LOW);
    return;
  }

  bool r=false, y=false, g=false;
  if      (v <  V_LOW)  r = true;       // DEAD
  else if (v <  V_GOOD) y = true;       // LOW
  else if (v <  V_NEW)  y = true;       // GOOD
  else                  g = true;       // NEW

  digitalWrite(LED_RED, r ? HIGH : LOW);
  digitalWrite(LED_YEL, y ? HIGH : LOW);
  digitalWrite(LED_GRN, g ? HIGH : LOW);
}

// -------- Arduino lifecycle --------

void setup() {
  Serial.begin(115200);
  analogReference(INTERNAL);      // AVR UNO/Nano: ~1.1 V internal reference
  delay(10);

  lcd.begin(16, 2);
  lcdPrintPadded(0, "AA Battery Meter");
  lcdPrintPadded(1, "Plug cell...");

  if (USE_LEDS) {
    pinMode(LED_RED, OUTPUT);
    pinMode(LED_YEL, OUTPUT);
    pinMode(LED_GRN, OUTPUT);
    digitalWrite(LED_RED, LOW);
    digitalWrite(LED_YEL, LOW);
    digitalWrite(LED_GRN, LOW);
  }
}

void loop() {
  const float v = readBatteryVolts();

  // Presence update with hysteresis
  if (!batteryPresent && v >= BAT_PRESENT_ON) batteryPresent = true;
  if ( batteryPresent && v <= BAT_PRESENT_OFF) batteryPresent = false;

  // LCD + LEDs
  lcdShow(v, batteryPresent);
  ledsForVoltage(v, batteryPresent);

  // Serial (mirrors LCD thresholds)
  Serial.print(F("Battery: "));
  Serial.print(v, 3);
  Serial.println(F(" V (open-circuit)"));
  if (batteryPresent) {
    if      (v <  V_LOW)  Serial.println(F("Status: DEAD"));
    else if (v <  V_GOOD) Serial.println(F("Status: LOW"));
    else if (v <  V_NEW)  Serial.println(F("Status: GOOD"));
    else                  Serial.println(F("Status: NEW"));
  } else {
    Serial.println(F("Waiting for battery..."));
  }
  Serial.println(F("----"));

  delay(1000);
}
