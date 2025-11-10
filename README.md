# Whack‑A‑Mole – Arduino Uno + QAPASS 1602A (16×2) LCD  
*An event‑driven embedded game demonstrating interrupts, timer scheduling, EEPROM persistence, and user interaction.*

## Table of Contents  
- [Features](#features)  
- [Hardware Setup](#hardware-setup)  
- [Wiring Diagram](#wiring-diagram)  
- [Code Overview](#code-overview)  
- [Timing & Event Design](#timing--event-design)  
- [EEPROM Usage](#eeprom-usage)  
- [How to Play](#how-to-play)  
- [Known Issues / Limitations](#known-issues--limitations)  
- [Future Enhancements](#future-enhancements)  
- [License](#license)  

---

## Features  
- Uses hardware interrupts (button presses) and timer interrupt (1 ms tick + game tick period)  
- Asynchronous, non‑blocking operation - no `delay()` in main loop  
- 4 “hole” buttons, one “mole” visible at a time, quick reaction required  
- Score and persistent high‑score stored in EEPROM  
- Difficulty (spawn intervals) stored and adjustable  
- Visual feedback via 16×2 LCD showing holes, score & high‑score  
- Compact, safe wiring and full buildability on Arduino Uno  

---

## Hardware Setup  
**Required components**:  
- Arduino Uno  
- QAPASS 1602A 16×2 LCD (HD44780 compatible)  
- 4 × momentary push‑buttons  
- 4 × 220 Ω resistors (for back‑light if needed)  
- Breadboard, jumper wires, 5 V power (USB)  
- (Optional) Piezo buzzer  

---

**Pin connections**:  
- LCD: VSS→GND, VDD→+5 V, VO→GND via 220Ω (contrast)  
- LCD RS→D8, E→D9, D4→D10, D5→D11, D6→D12, D7→D13  
- Back‑light A/K wiring w/ 220 Ω resistor if needed  
- Buttons:  
  - Hole 0 → D2 (INT0) → GND  
  - Hole 1 → D3 (INT1) → GND  
  - Hole 2 → D4 (PCINT) → GND  
  - Hole 3 → D5 (PCINT) → GND  
- All buttons use `INPUT_PULLUP` (so pressed = LOW)  

---

## Code Overview  
**File:** `WhackAMole_1602.ino`  
### Key modules  
- Timer1 ISR: increments `msTicks`, schedules game ticks  
- External/Pin‑change interrupts: detect button press events  
- Random mole spawning, timed expiration  
- EEPROM load/save for difficulty + high score  
- LCD text UI with score and high‑score  

### Constants to tune  
```cpp
const uint16_t SPAWN_MIN_MS = 400;
const uint16_t SPAWN_MAX_MS = 1200;
const uint16_t MOLE_MIN_MS  = 600;
const uint16_t MOLE_MAX_MS  = 1200;
const uint8_t DEFAULT_DIFFICULTY = 160;
```

---

## Timing & Event Design  
- Timer1 → **1 ms heartbeat**
- Every `GAME_TICK_MS` → update movement, spawn check, LCD
- ISR sets flags only → **main loop performs logic**
- Zero busy‑waits → fully asynchronous

Event flow:  
```
Timer1 ISR --> gameTickFlag --> loop(): step game
Button ISR --> button flags --> loop(): hit/miss check
```

---

## EEPROM Usage  
Stored struct:  
```cpp
struct Persist {
  uint16_t magic;
  uint8_t version;
  uint16_t highScore;
  uint8_t difficulty;
};
```
Data validated using **magic number** & version → prevents corruption.

---

## How to Play  
1. Power device → “WHACK‑A‑MOLE” welcome screen  
2. Press any button to start  
3. Mole (“M”) appears in 1 of 4 positions  
4. Hit correct button → score +1  
5. Wrong button pressed → **score resets**  
6. High‑score stored permanently in EEPROM  
7. Difficulty automatically speeds up over time  

Display format:  
Top row → `M---` style holes  
Bottom row → `S:12 HS:34`

---

## Known Issues / Limitations  
- Difficulty UI not final yet  
- Spawn randomness can be uneven  
- No buzzer feedback included by default  

---

## Future Enhancements  
- Sound on hit/miss  
- Difficulty selector button  
- Custom characters for mole animation  
- More holes (expand with PCINTs or shift register)  

---

