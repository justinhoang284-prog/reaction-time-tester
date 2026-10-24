/*
  Reaction Time Tester - club recruitment demo
  Arduino Mega 2560
  4-digit 7-segment display (raw 12-pin, no driver chip), pushbutton, LED, buzzer.

  SETUP:
  1. Wire per the pin map below.
  2. If the display looks inverted/garbled, flip COMMON_ANODE between 0 and 1.
  3. Upload and test.

  GAME FLOW:
  IDLE       -> alternates "----" / best score. Press button to arm.
  ARMED      -> random 1.5-4s wait, screen blank (no cues). Early press = false start.
  GO         -> LED + beep fire, timer starts, waiting for button press.
  RESULT     -> shows reaction time in ms for 3s, updates best score in EEPROM if beaten.
  (loops back to IDLE)
*/

#include <EEPROM.h>

#define COMMON_ANODE 0   // 0 = common cathode, 1 = common anode. Flip if display looks wrong.

// ---- Pin map ----
const int segPins[8]   = {22, 23, 24, 25, 26, 27, 28, 29}; // a,b,c,d,e,f,g,dp
const int digitPins[4] = {30, 31, 32, 33};                 // leftmost -> rightmost digit
const int buttonPin = 2;
const int ledPin    = 3;
const int buzzerPin = 4;

// ---- Segment patterns: bits = a b c d e f g dp (1 = segment ON, before polarity flip) ----
const byte digitPatterns[12] = {
  B11111100, // 0
  B01100000, // 1
  B11011010, // 2
  B11110010, // 3
  B01100110, // 4
  B10110110, // 5
  B10111110, // 6
  B11100000, // 7
  B11111110, // 8
  B11110110, // 9
  B00000000, // 10 = blank
  B00000010  // 11 = dash
};

const int EEPROM_ADDR = 0; // stores best (lowest) reaction time as an int

int bestScore = 9999;
int currentDigits[4] = {10, 10, 10, 10}; // what refreshDisplay() is currently showing

enum State { IDLE, ARMED, GO, RESULT, FALSESTART };
State state = IDLE;

unsigned long stateStartTime = 0;
unsigned long armDelay = 0;
unsigned long reactionStart = 0;
int lastResult = 0;

void setup() {
  for (int i = 0; i < 8; i++) pinMode(segPins[i], OUTPUT);
  for (int i = 0; i < 4; i++) pinMode(digitPins[i], OUTPUT);
  pinMode(buttonPin, INPUT_PULLUP);
  pinMode(ledPin, OUTPUT);
  pinMode(buzzerPin, OUTPUT);

  EEPROM.get(EEPROM_ADDR, bestScore);
  if (bestScore <= 0 || bestScore > 9999) bestScore = 9999; // handle blank/garbage EEPROM

  randomSeed(analogRead(A0)); // unconnected analog pin, floats for entropy

  // Hold the button down while powering on / resetting the Mega to wipe the high score.
  if (digitalRead(buttonPin) == LOW) {
    delay(50); // debounce
    if (digitalRead(buttonPin) == LOW) {
      bestScore = 9999;
      EEPROM.put(EEPROM_ADDR, bestScore);
      for (int i = 0; i < 3; i++) { // flash LED 3x to confirm the reset happened
        digitalWrite(ledPin, HIGH);
        delay(150);
        digitalWrite(ledPin, LOW);
        delay(150);
      }
    }
  }

  state = IDLE;
  stateStartTime = millis();
}

void loop() {
  switch (state) {

    case IDLE: {
      if ((millis() / 1000) % 2 == 0) showNumber(-1); else showNumber(bestScore);
      refreshDisplay();

      if (buttonPressed()) {
        beep(1000, 60);
        armDelay = random(1500, 4000);
        stateStartTime = millis();
        allOff();
        state = ARMED;
      }
      break;
    }

    case ARMED: {
      allOff(); // screen blank on purpose, no cues before GO

      if (buttonPressed()) {
        state = FALSESTART;
        stateStartTime = millis();
        break;
      }

      if (millis() - stateStartTime >= armDelay) {
        digitalWrite(ledPin, HIGH);
        beep(1800, 100);
        reactionStart = micros();
        state = GO;
      }
      break;
    }

    case GO: {
      // no display refresh here on purpose - keeps the loop tight for lowest input latency
      if (buttonPressed()) {
        unsigned long elapsedMs = (micros() - reactionStart) / 1000;
        digitalWrite(ledPin, LOW);
        lastResult = (elapsedMs > 9999) ? 9999 : (int)elapsedMs;

        if (lastResult < bestScore) {
          bestScore = lastResult;
          EEPROM.put(EEPROM_ADDR, bestScore);
          beep(2200, 200); // new record chime
        } else {
          beep(1200, 80);
        }
        state = RESULT;
        stateStartTime = millis();
      }
      if (micros() - reactionStart > 3000000UL) { // 3s safety timeout, avoids a stuck kiosk
        digitalWrite(ledPin, LOW);
        lastResult = 9999;
        state = RESULT;
        stateStartTime = millis();
      }
      break;
    }

    case RESULT: {
      showNumber(lastResult);
      refreshDisplay();
      if (millis() - stateStartTime > 3000) {
        state = IDLE;
        stateStartTime = millis();
      }
      break;
    }

    case FALSESTART: {
      static bool toneFired = false;
      if (!toneFired) { beep(300, 300); toneFired = true; }
      bool on = ((millis() - stateStartTime) / 200) % 2 == 0;
      showNumber(on ? -1 : -2);
      refreshDisplay();
      if (millis() - stateStartTime > 1500) {
        toneFired = false;
        state = IDLE;
        stateStartTime = millis();
      }
      break;
    }
  }
}

// ---------- Button (debounced, single-fire on press) ----------
bool buttonPressed() {
  static bool lastReading = HIGH;
  static bool debouncedState = HIGH;
  static unsigned long lastChange = 0;
  const unsigned long debounceMs = 25;

  bool reading = digitalRead(buttonPin);
  if (reading != lastReading) lastChange = millis();

  if (millis() - lastChange > debounceMs) {
    if (reading != debouncedState) {
      debouncedState = reading;
      lastReading = reading;
      if (debouncedState == LOW) return true; // clean press detected
    }
  }
  lastReading = reading;
  return false;
}

// ---------- Display helpers ----------

// value >= 0: right-aligned number, blanked leading zeros
// value == -1: "----"
// value == -2: blank
void showNumber(int value) {
  if (value == -2) {
    for (int i = 0; i < 4; i++) currentDigits[i] = 10;
    return;
  }
  if (value == -1) {
    for (int i = 0; i < 4; i++) currentDigits[i] = 11;
    return;
  }
  if (value > 9999) value = 9999;
  if (value < 0) value = 0;

  int digs[4];
  digs[0] = (value / 1000) % 10;
  digs[1] = (value / 100) % 10;
  digs[2] = (value / 10) % 10;
  digs[3] = value % 10;

  bool leadingZero = true;
  for (int i = 0; i < 4; i++) {
    if (i == 3) { currentDigits[i] = digs[i]; break; } // always show the units digit
    if (digs[i] == 0 && leadingZero) {
      currentDigits[i] = 10; // blank
    } else {
      leadingZero = false;
      currentDigits[i] = digs[i];
    }
  }
}

void setSegments(byte pattern) {
  for (int i = 0; i < 8; i++) {
    bool on = (pattern >> (7 - i)) & 0x01;
    if (COMMON_ANODE) on = !on;
    digitalWrite(segPins[i], on ? HIGH : LOW);
  }
}

void allOff() {
  for (int i = 0; i < 4; i++) digitalWrite(digitPins[i], COMMON_ANODE ? LOW : HIGH); // disable all digits
  for (int i = 0; i < 8; i++) digitalWrite(segPins[i], COMMON_ANODE ? HIGH : LOW);   // segments off
}

void refreshDisplay() {
  // multiplex: light one digit at a time fast enough that it looks solid to the eye
  for (int d = 0; d < 4; d++) {
    for (int i = 0; i < 4; i++) digitalWrite(digitPins[i], COMMON_ANODE ? LOW : HIGH); // all off
    setSegments(digitPatterns[currentDigits[d]]);
    digitalWrite(digitPins[d], COMMON_ANODE ? HIGH : LOW); // enable this digit
    delayMicroseconds(1500); // ~1.5ms/digit -> ~166Hz refresh, flicker-free
  }
}

void beep(int freq, int ms) {
  tone(buzzerPin, freq, ms); // non-blocking, doesn't hold up loop()
}
