# Reaction Time Tester

An Arduino Mega reaction-time game I built as a recruitment demo for the Arroyo Engineering Society. It ran at our club rush booth with a live high score, and students lined up to try to beat it.

**How to play:** press the button to arm it. After a random 1.5–4 s wait, the LED lights up and the buzzer beeps. Press as fast as you can. Your time in milliseconds shows on a 4-digit 7-segment display, and the best score is saved to EEPROM so it survives power cycles.

## Hardware

- Arduino Mega 2560
- 4-digit 7-segment display (raw 12-pin, multiplexed directly from the Mega, no driver chip)
- Pushbutton (internal pull-up), LED, piezo buzzer
- LEGO enclosure, USB power

| Part | Pins |
|---|---|
| Segments a–g, dp | 22–29 |
| Digits 1–4 (left → right) | 30–33 |
| Button | 2 (`INPUT_PULLUP`) |
| LED | 3 |
| Buzzer | 4 |

If the display looks garbled, flip `COMMON_ANODE` between 0 and 1 at the top of the sketch.

## How it works

The sketch is a state machine:

| State | What happens |
|---|---|
| `IDLE` | Alternates `----` and the best score. A press arms the game. |
| `ARMED` | Blank screen, random 1.5–4 s delay. Any new press is a false start. |
| `GO` | LED and beep fire, and the timer starts (`micros()`). |
| `RESULT` | Shows the time for 3 s and saves a new best to EEPROM. |
| `FALSESTART` | Flashing dashes and a low tone, then back to `IDLE`. |

The display is multiplexed at about 166 Hz (one digit at a time, about 1.5 ms each). During `GO` the display isn't refreshed, which keeps the loop tight so input latency stays low.

Holding the button while the Mega powers on resets the high score.

## The bug: impossible 10 ms scores

Club members discovered that spamming the button produced scores around 10 ms, faster than any human can react.

**Cause (v2):** the false-start check in `ARMED` used the same 25 ms debounced `buttonPressed()` as everything else. Rapid presses kept resetting the debounce timer, so no press ever registered as a false start. When `GO` fired, the press finally "settled," and the debouncer reported it almost instantly.

**Fix (v4):**

- `ARMED` reads the raw pin, so any early press is a false start. The arming press is ignored until it's released.
- `GO` timestamps the first raw press. This also removes the debounce lag from every legitimate result.
- The button has to be released after `GO` before a press counts, so you can't hold it through the start.
- Results under 100 ms are rejected as anticipation, the same idea as a false start in sprinting.

## Files

- `reaction_time_tester4.ino`: current version (v4, with the fix)
- `reaction_time_tester2.ino`: original version with the 10 ms bug, kept for comparison
