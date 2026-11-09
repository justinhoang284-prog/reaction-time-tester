# Reaction Time Tester

An Arduino Mega reaction-time game I built as a recruitment demo for the Arroyo Engineering Society. It ran at our club rush booth with a live high score, and students lined up to try to beat it. Version 5 turns it into an instrument for a small experiment: do people react faster to a light, a sound, or both?

**How to play:** tap the button to start a trial (it starts when you release). After a random 1.5–4 s wait, the signal fires. Press as fast as you can. Your time in milliseconds shows on a 4-digit 7-segment display, and in normal play the best score is saved to EEPROM so it survives power cycles.

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

The current display is **common anode**, so `COMMON_ANODE` is set to 1 at the top of the sketch. If you use a common-cathode display, set it to 0.

### Wiring lessons from swapping in a cheaper display

- **Polarity:** the replacement display was common anode, but the code was written for common cathode. The symptom was a "photo negative": lighting only segment `a` lit every segment except `a`. Flipping `COMMON_ANODE` fixed it.
- **Loose legs:** jumper sockets didn't grip the thin display legs, so segments flickered or stayed lit. The display now sits in a breadboard.
- **Resistors:** they go on the 8 segment lines only, never the 4 digit lines, and each resistor's legs must sit in different breadboard strips or it's bypassed.
- `display_test.ino` lights one segment at a time and prints which pin it's driving, which finds a wrong wire in a minute.

## Controls

| Action | Result |
|---|---|
| Tap while idle | Start a trial |
| Hold 1.5 s while idle | Next signal mode: both → visual → audio. The display shows `both`, ` LEd` or ` Snd`. |
| Hold while powering on / resetting | Clear the saved best score (LED flashes 3 times) |

### Serial Monitor (115200 baud, Newline)

| Command | Effect |
|---|---|
| `P7` | Participant P07, trial count reset |
| `V` / `A` / `B` | Visual (LED only) / audio (buzzer only) / both |
| `C10` | Catch trials = 10% of trials (0–50) |
| `?` | Print current settings |

Commands only work between trials.

## How it works

The sketch is a state machine:

| State | What happens |
|---|---|
| `IDLE` | Alternates `----` and the best score. A tap starts a trial; a 1.5 s hold changes mode. |
| `ARMED` | Blank screen, random 1.5–4 s delay. Any press is a false start. |
| `GO` | The signal fires (LED, buzzer or both) and the timer starts (`micros()`). |
| `CATCHWAIT` | Catch trial: no signal for 2 s. Pressing anyway is a catch fail. |
| `RESULT` | Shows the time for 3 s. In "both" mode, a new best is saved to EEPROM. |
| `FALSESTART` | Flashing dashes and a low tone, then back to `IDLE`. |

The display is multiplexed at about 166 Hz (one digit at a time, about 1.5 ms each). During `GO` the display isn't refreshed and the button isn't debounced, which keeps the loop tight so input latency stays in the microseconds.

## The bug: impossible 10 ms scores

Club members discovered that spamming the button produced scores around 10 ms, faster than any human can react.

**Cause (v2):** the false-start check in `ARMED` used the same 25 ms debounced `buttonPressed()` as everything else. Rapid presses kept resetting the debounce timer, so no press ever registered as a false start. When `GO` fired, the press finally "settled," and the debouncer reported it almost instantly.

**Fix (v4):**

- `ARMED` reads the raw pin, so any early press is a false start. The arming press is ignored until it's released.
- `GO` timestamps the first raw press. This also removes the debounce lag from every legitimate result.
- The button has to be released after `GO` before a press counts, so you can't hold it through the start.
- Results under 100 ms are rejected as anticipation, the same idea as a false start in sprinting.

## The experiment (v5)

**Question:** do people react faster to light, sound, or both together? Published studies generally find sound faster than light, and both together faster still (the redundant signals effect).

**Method:**

- Each participant does about 20 trials per signal. The order of signals is mixed between people.
- 10% of visual and audio trials are **catch trials** with no signal, to catch people who are guessing the timing.
- Trials under 100 ms count as anticipation and are excluded. Trials over 1,000 ms count as distracted and are excluded.
- Every trial prints one CSV line over Serial:

```
participant,mode,trial,catch_trial,outcome,reaction_ms,reaction_us,delay_ms
P01,audio,7,0,ok,168,168412,2730
```

Outcomes: `ok`, `false_start`, `anticipation`, `timeout`, `catch_pass`, `catch_fail`.

**Analysis:** per-person averages for each signal, with paired t-tests for audio vs. visual and both vs. audio, in Google Sheets.

**Results:** _[mean ± SD for each signal, n, p-values, box plot]_

**Device error:** _[button polling delay, buzzer onset delay, total ± ms]_

## Version history

| Version | Changes |
|---|---|
| v1 | Original recruitment game: display, button, LED, buzzer, EEPROM best score. |
| v2 | Gameplay refinements; contains the 10 ms bug. |
| v3 | Minor bug fixes (not kept). |
| v4 | Anti-cheat fix (see above). |
| v5 | Experiment features: visual/audio/both modes, catch trials, CSV logging, button mode switching. **Written with AI assistance from my requirements**; I tested and debugged it on hardware, including the display polarity and wiring problems. |
| v5.1 | I rewrote the catch-trial logic with its own release flag, then traced the state machine and found the release gate can't trigger, because `ARMED` already treats any press as a false start. Kept as defensive code. |

## Files

- `firmware/v5.1/`: current version
- `firmware/v4/`: anti-cheat fix, normal game only
- `firmware/v2/`: original version with the 10 ms bug, kept for comparison
- `firmware/display_test/`: one-segment-at-a-time wiring test
- `data/`: experiment results (participant IDs only, no names)
- `analysis/`: spreadsheet and charts
