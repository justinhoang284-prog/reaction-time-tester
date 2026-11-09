/*
  Reaction Time Tester
  Arduino Mega 2560 with a 4-digit 7-segment display, pushbutton, LED, and buzzer.

  The tester supports visual, audio, and combined signals. Visual and audio
  trials can include catch trials, while combined mode is intended for normal
  play. Trial results are sent to the Serial Monitor as CSV for later analysis.

  Button use:
    - Tap while idle to begin a trial (the trial starts when the button is released).
    - Hold for 1.5 seconds while idle to change the signal mode.
    - Hold during reset to clear the saved best score.

  Serial Monitor: 115200 baud, newline ending.
    P7  selects participant 7 and resets the trial count
    V   selects visual mode
    A   selects audio mode
    B   selects combined mode
    C10 sets the catch-trial percentage to 10 (allowed range: 0-50)
    ?   prints the current settings

  Each completed trial is printed as:
    participant,mode,trial,catch_trial,outcome,reaction_ms,reaction_us,delay_ms

  Connect the laptop during an experiment so the Serial Monitor can capture
  the results. The button controls are also available when the laptop is not
  connected, but those trials will not be saved to a CSV automatically.
*/

#include <EEPROM.h>

#define COMMON_ANODE 1   // 0 = common cathode, 1 = common anode. The cheaper display is common anode.

// ---- Pin map ----
const int segPins[8]   = {22, 23, 24, 25, 26, 27, 28, 29}; // a,b,c,d,e,f,g,dp
const int digitPins[4] = {30, 31, 32, 33};                 // leftmost -> rightmost digit
const int buttonPin = 2;
const int ledPin    = 3;
const int buzzerPin = 4;

// ---- Segment patterns: bits = a b c d e f g dp (1 = segment ON, before polarity flip) ----
const byte digitPatterns[20] = {
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
  B00000010, // 11 = dash
  B00011100, // 12 = L        // letters for mode names
  B10011110, // 13 = E
  B01111010, // 14 = d
  B00101010, // 15 = n
  B00111110, // 16 = b
  B00111010, // 17 = o
  B00011110, // 18 = t
  B00101110  // 19 = h
};

const int EEPROM_ADDR = 0; // stores best (lowest) reaction time as an int

int bestScore = 9999;
int currentDigits[4] = {10, 10, 10, 10};

const int L_ = 12, E_ = 13, D_ = 14, N_ = 15, B_ = 16, O_ = 17, T_ = 18, H_ = 19, S_ = 5, BL = 10;

enum State { IDLE, ARMED, GO, CATCHWAIT, RESULT, FALSESTART };
State state = IDLE;

enum Mode { MODE_VISUAL, MODE_AUDIO, MODE_BOTH };
Mode mode = MODE_BOTH;
int participant = 0;                                // 0 = not set
int trialNum = 0;
int catchPct = 10;
bool isCatch = false;
const unsigned long CATCH_WINDOW_MS = 2000;

unsigned long stateStartTime = 0;
unsigned long armDelay = 0;
unsigned long reactionStart = 0;
int lastResult = 0;

bool waitForRelease  = false;   // ignore the arming press until it's let go
bool releasedSinceGo = false;
bool catchNeedsRelease = false;
const int MIN_HUMAN_MS = 100;   // faster than this = anticipation, not reaction

String serialBuf = "";

// button handling for short press / long hold
const unsigned long HOLD_MS = 1500;
bool btnDown = false;               // debounced button level (true = pressed)
bool pressStartedInIdle = false;    // only presses that begin in IDLE count
bool holdFired = false;
unsigned long pressStart = 0;
unsigned long modeShowUntil = 0;    // show the mode name until this time

// ---- prototypes ----
int updateButton();
void showMode();
void nextMode();
void showNumber(int value);
void setSegments(byte pattern);
void allOff();
void refreshDisplay();
void beep(int freq, int ms);
void fireSignal();
void signalOff();
void logTrial(const char *outcome, long reactionUs);
void handleSerial();
void printSettings();
const char *modeName();

void setup() {
  Serial.begin(115200);
  for (int i = 0; i < 8; i++) pinMode(segPins[i], OUTPUT);
  for (int i = 0; i < 4; i++) pinMode(digitPins[i], OUTPUT);
  pinMode(buttonPin, INPUT_PULLUP);
  pinMode(ledPin, OUTPUT);
  pinMode(buzzerPin, OUTPUT);

  EEPROM.get(EEPROM_ADDR, bestScore);
  if (bestScore <= 0 || bestScore > 9999) bestScore = 9999;

  randomSeed(analogRead(A0)); // unconnected analog pin, floats for entropy

  // Hold the button down while powering on / resetting to wipe the high score.
  if (digitalRead(buttonPin) == LOW) {
    delay(50);
    if (digitalRead(buttonPin) == LOW) {
      bestScore = 9999;
      EEPROM.put(EEPROM_ADDR, bestScore);
      for (int i = 0; i < 3; i++) {
        digitalWrite(ledPin, HIGH); delay(150);
        digitalWrite(ledPin, LOW);  delay(150);
      }
    }
  }

  Serial.println(F("# Reaction Time Tester v5 - catch release gate"));
  printSettings();
  Serial.println(F("participant,mode,trial,catch_trial,outcome,reaction_ms,reaction_us,delay_ms"));

  btnDown = (digitalRead(buttonPin) == LOW);  // if still held after the reset combo, ignore it
  modeShowUntil = millis() + 1500;            // show the mode name at startup

  state = IDLE;
  stateStartTime = millis();
}

void loop() {
  if (state == IDLE) handleSerial();   // only take commands between trials
  int edge = (state == GO) ? 0 : updateButton(); // skip in GO to keep the timing loop tight

  switch (state) {

    case IDLE: {
      if (millis() < modeShowUntil) showMode();
      else if ((millis() / 1000) % 2 == 0) showNumber(-1);
      else showNumber(bestScore);
      refreshDisplay();

      // a press must start here; hold = next mode, short press = start trial (on release)
      if (edge == 1) { pressStartedInIdle = true; holdFired = false; pressStart = millis(); }

      if (btnDown && pressStartedInIdle && !holdFired && millis() - pressStart >= HOLD_MS) {
        holdFired = true;
        nextMode();
      }

      if (edge == -1 && pressStartedInIdle) {
        pressStartedInIdle = false;
        if (!holdFired) {
          beep(1000, 60);                                   // arming chirp (all modes)
          armDelay = random(1500, 4000);
          isCatch = (mode != MODE_BOTH) && (random(100) < catchPct); // no catch trials in kiosk mode
          trialNum++;
          stateStartTime = millis();
          waitForRelease = false;                           // already released
          allOff();
          state = ARMED;
        }
      }
      break;
    }

    case ARMED: {
      allOff(); // screen blank on purpose, no cues before GO

      bool raw = digitalRead(buttonPin);
      if (raw == HIGH) {
        waitForRelease = false;
      } else if (!waitForRelease) {       // any NEW contact during the wait
        logTrial("false_start", -1);
        state = FALSESTART;
        stateStartTime = millis();
        break;
      }

      if (millis() - stateStartTime >= armDelay) {
        if (isCatch) {
          // Catch trials have no signal. Count only a fresh press after release.
          catchNeedsRelease = (raw == LOW);
          stateStartTime = millis();
          state = CATCHWAIT;
        } else {
          fireSignal();
          reactionStart = micros();
          releasedSinceGo = (raw == HIGH);
          state = GO;
        }
      }
      break;
    }

    case GO: {
      // no display refresh here on purpose - keeps the loop tight for lowest input latency
      bool raw = digitalRead(buttonPin);
      if (raw == HIGH) {
        releasedSinceGo = true;
      } else if (releasedSinceGo) {
        unsigned long elapsedUs = micros() - reactionStart;
        unsigned long elapsedMs = elapsedUs / 1000;
        signalOff();

        if (elapsedMs < MIN_HUMAN_MS) {
          logTrial("anticipation", elapsedUs);
          state = FALSESTART;
          stateStartTime = millis();
          break;
        }

        lastResult = (elapsedMs > 9999) ? 9999 : (int)elapsedMs;
        logTrial("ok", elapsedUs);

        if (mode == MODE_BOTH && lastResult < bestScore) { // experiment runs don't touch the record
          bestScore = lastResult;
          EEPROM.put(EEPROM_ADDR, bestScore);
          beep(2200, 200);
        } else if (mode == MODE_BOTH) {
          beep(1200, 80);
        }
        state = RESULT;
        stateStartTime = millis();
        break;
      }

      if (micros() - reactionStart > 3000000UL) { // 3 s timeout
        signalOff();
        lastResult = 9999;
        logTrial("timeout", -1);
        state = RESULT;
        stateStartTime = millis();
      }
      break;
    }

    case CATCHWAIT: {
      allOff();

      const bool buttonPressed = digitalRead(buttonPin) == LOW;

      // Ignore a press carried over from the end of the delay until it is released.
      if (catchNeedsRelease) {
        if (!buttonPressed) catchNeedsRelease = false;
      } else if (buttonPressed) {
        logTrial("catch_fail", -1);
        state = FALSESTART;
        stateStartTime = millis();
        break;
      }

      if (millis() - stateStartTime >= CATCH_WINDOW_MS) {
        logTrial("catch_pass", -1);
        lastResult = -1;
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
        pressStartedInIdle = false;   // a press held from the last trial doesn't count
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
        pressStartedInIdle = false;
        stateStartTime = millis();
      }
      break;
    }
  }
}

// Signal output
void fireSignal() {
  if (mode != MODE_AUDIO)  digitalWrite(ledPin, HIGH);
  if (mode != MODE_VISUAL) tone(buzzerPin, 1800, 100);
}

void signalOff() {
  digitalWrite(ledPin, LOW);
}

// Write one trial to the serial output
const char *modeName() {
  if (mode == MODE_VISUAL) return "visual";
  if (mode == MODE_AUDIO)  return "audio";
  return "both";
}

void logTrial(const char *outcome, long reactionUs) {
  Serial.print('P');
  if (participant < 10) Serial.print('0');
  Serial.print(participant);           Serial.print(',');
  Serial.print(modeName());            Serial.print(',');
  Serial.print(trialNum);              Serial.print(',');
  Serial.print(isCatch ? 1 : 0);       Serial.print(',');
  Serial.print(outcome);               Serial.print(',');
  if (reactionUs >= 0) {
    Serial.print(reactionUs / 1000);   Serial.print(',');
    Serial.print(reactionUs);
  } else {
    Serial.print(',');
  }
  Serial.print(',');
  Serial.println(armDelay);
}

// Read commands from the Serial Monitor
void handleSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      serialBuf.trim();
      if (serialBuf.length() > 0) {
        char cmd = toupper(serialBuf.charAt(0));
        int val = serialBuf.substring(1).toInt();
        if (cmd == 'P')      { participant = val; trialNum = 0; }
        else if (cmd == 'V') { mode = MODE_VISUAL; }
        else if (cmd == 'A') { mode = MODE_AUDIO; }
        else if (cmd == 'B') { mode = MODE_BOTH; }
        else if (cmd == 'C') { catchPct = constrain(val, 0, 50); }
        printSettings();
      }
      serialBuf = "";
    } else if (serialBuf.length() < 16) {
      serialBuf += c;
    }
  }
}

void printSettings() {
  Serial.print(F("# participant=P"));
  if (participant < 10) Serial.print('0');
  Serial.print(participant);
  Serial.print(F(" mode="));      Serial.print(modeName());
  Serial.print(F(" catch_pct=")); Serial.print(catchPct);
  Serial.print(F(" trials="));    Serial.println(trialNum);
}

// Read the button and ignore brief contact changes
int updateButton() {
  static bool lastReading = HIGH;
  static unsigned long lastChange = 0;
  const unsigned long debounceMs = 25;

  bool reading = digitalRead(buttonPin);
  if (reading != lastReading) { lastChange = millis(); lastReading = reading; }

  bool down = (reading == LOW);
  if (millis() - lastChange > debounceMs && down != btnDown) {
    btnDown = down;
    return down ? 1 : -1;
  }
  return 0;
}

// Cycle through the available signal modes
void nextMode() {
  if (mode == MODE_BOTH) mode = MODE_VISUAL;
  else if (mode == MODE_VISUAL) mode = MODE_AUDIO;
  else mode = MODE_BOTH;
  modeShowUntil = millis() + 1500;
  if (mode == MODE_AUDIO) beep(1500, 120);    // audible confirmation (silent for visual)
  else if (mode == MODE_BOTH) beep(2000, 120);
  digitalWrite(ledPin, HIGH); delay(80); digitalWrite(ledPin, LOW);
  printSettings();
}

void showMode() {
  if (mode == MODE_VISUAL) { currentDigits[0] = BL; currentDigits[1] = L_; currentDigits[2] = E_; currentDigits[3] = D_; }
  else if (mode == MODE_AUDIO) { currentDigits[0] = BL; currentDigits[1] = S_; currentDigits[2] = N_; currentDigits[3] = D_; }
  else { currentDigits[0] = B_; currentDigits[1] = O_; currentDigits[2] = T_; currentDigits[3] = H_; }
}

// Display helpers
// value >= 0: right-aligned number, blanked leading zeros
// value == -1: "----"
// value == -2: blank
void showNumber(int value) {
  if (value == -2) { for (int i = 0; i < 4; i++) currentDigits[i] = 10; return; }
  if (value == -1) { for (int i = 0; i < 4; i++) currentDigits[i] = 11; return; }
  if (value > 9999) value = 9999;
  if (value < 0) value = 0;

  int digs[4];
  digs[0] = (value / 1000) % 10;
  digs[1] = (value / 100) % 10;
  digs[2] = (value / 10) % 10;
  digs[3] = value % 10;

  bool leadingZero = true;
  for (int i = 0; i < 4; i++) {
    if (i == 3) { currentDigits[i] = digs[i]; break; }
    if (digs[i] == 0 && leadingZero) {
      currentDigits[i] = 10;
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
  for (int i = 0; i < 4; i++) digitalWrite(digitPins[i], COMMON_ANODE ? LOW : HIGH);
  for (int i = 0; i < 8; i++) digitalWrite(segPins[i], COMMON_ANODE ? HIGH : LOW);
}

void refreshDisplay() {
  for (int d = 0; d < 4; d++) {
    for (int i = 0; i < 4; i++) digitalWrite(digitPins[i], COMMON_ANODE ? LOW : HIGH);
    setSegments(digitPatterns[currentDigits[d]]);
    digitalWrite(digitPins[d], COMMON_ANODE ? HIGH : LOW);
    delayMicroseconds(1500);
  }
}

void beep(int freq, int ms) {
  tone(buzzerPin, freq, ms);
}
