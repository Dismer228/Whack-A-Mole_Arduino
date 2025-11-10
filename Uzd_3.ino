// WhackAMole_1602_TimeSpawn_NoLEDs.ino
// Whack-A-Mole (4 holes) - Arduino Uno + QAPASS 1602A (16x2)
// - Timer1: 1ms tick -> game tick every GAME_TICK_MS
// - External interrupts: INT0 (D2), INT1 (D3)
// - Pin-change interrupt: PCINT2 for D4, D5
// - EEPROM stores magic/version, highScore, difficulty (write-on-change)
// - No blocking delays in main loop (ISRs set flags)
// - Start screen persists until first button press
// - Score resets immediately on any wrong button press
// - Time-based spawn scheduling (nextSpawnTick)

#include <LiquidCrystal.h>
#include <EEPROM.h>
#include <avr/interrupt.h>

// ----------------- Config / Pins -----------------
#define LCD_RS 8
#define LCD_E  9
#define LCD_D4 10
#define LCD_D5 11
#define LCD_D6 12
#define LCD_D7 13

LiquidCrystal lcd(LCD_RS, LCD_E, LCD_D4, LCD_D5, LCD_D6, LCD_D7);

// Buttons (holes)
const uint8_t BTN_PINS[4] = {2, 3, 4, 5}; // D2 (INT0), D3 (INT1), D4 (PCINT), D5 (PCINT)
const uint8_t NUM_HOLES = 4;

// ----------------- Timing -----------------
const uint16_t TIMER1_MS_TICK = 1;    // 1 ms base tick
const uint16_t GAME_TICK_MS = 50;     // update game every 50 ms
// spawn scheduling (time-based)
const uint16_t SPAWN_MIN_MS = 1000;    // shortest time between spawns
const uint16_t SPAWN_MAX_MS = 1200;   // longest time between spawns
// mole life
const uint16_t MOLE_MIN_MS = 600;
const uint16_t MOLE_MAX_MS = 1200;
const uint16_t BUTTON_DEBOUNCE_MS = 150; // debounce per button

// ----------------- EEPROM layout -----------------
const uint16_t EEPROM_ADDR = 0;
const uint16_t MAGIC = 0xDA7A;
const uint8_t VERSION = 1;
struct Persist {
  uint16_t magic;
  uint8_t version;
  uint16_t highScore;
  uint8_t difficulty; // 0..255 (higher -> less frequent spawns if mapped appropriately)
};
Persist persist;
const uint16_t DEFAULT_HIGH_SCORE = 0;
const uint8_t DEFAULT_DIFFICULTY = 160; // default difficulty (mid-easy)

// ----------------- Game state -----------------
volatile uint32_t msTicks = 0;        // millisecond counter (ISR)
volatile bool gameTickFlag = false;   // set every GAME_TICK_MS by timer ISR

// Button flags (set by ISRs)
volatile uint8_t buttonPressedMask = 0; // bit i -> button i pressed event
volatile uint32_t lastBtnTick[NUM_HOLES] = {0,0,0,0}; // debounce timestamps

// Mole data
struct Mole { bool active; uint32_t spawnTick; uint16_t lifetimeMs; };
Mole moles[NUM_HOLES];

// Main variables
uint16_t score = 0;
uint16_t highScore = 0;
uint8_t difficulty = DEFAULT_DIFFICULTY;
bool gameRunning = false;
bool showStartScreen = true;

// spawn scheduling
volatile uint32_t nextSpawnTick = 0;

// For PCINT detection
volatile uint8_t prevPIND = 0xFF;

// Forward declarations
void timer1_init();
void spawnRandomMole();
void clearAllMoles();
void eepromLoad();
void eepromSaveIfChanged();
void updateLCD();
void beginGame();

// ----------------- Timer1 ISR: 1 ms -----------------
ISR(TIMER1_COMPA_vect) {
  msTicks++;
  static uint16_t tickAcc = 0;
  tickAcc++;
  if (tickAcc >= GAME_TICK_MS) {
    tickAcc = 0;
    gameTickFlag = true;
  }
}

// ----------------- External button ISRs -----------------
// Button 0 -> INT0 (D2)
void button0_isr() {
  uint32_t now = msTicks;
  if (now - lastBtnTick[0] >= BUTTON_DEBOUNCE_MS) {
    lastBtnTick[0] = now;
    buttonPressedMask |= (1 << 0);
  }
}
// Button 1 -> INT1 (D3)
void button1_isr() {
  uint32_t now = msTicks;
  if (now - lastBtnTick[1] >= BUTTON_DEBOUNCE_MS) {
    lastBtnTick[1] = now;
    buttonPressedMask |= (1 << 1);
  }
}

// ----------------- Pin-change ISR for D4/D5 (PCINT2_vect) -----------------
// Detect falling edges on PD4 (D4) and PD5 (D5)
ISR(PCINT2_vect) {
  uint8_t cur = PIND; // direct port read
  uint8_t changed = prevPIND & (~cur); // bits that went from 1->0
  prevPIND = cur;

  if (changed & (1 << PD4)) {
    uint32_t now = msTicks;
    if (now - lastBtnTick[2] >= BUTTON_DEBOUNCE_MS) {
      lastBtnTick[2] = now;
      buttonPressedMask |= (1 << 2);
    }
  }
  if (changed & (1 << PD5)) {
    uint32_t now = msTicks;
    if (now - lastBtnTick[3] >= BUTTON_DEBOUNCE_MS) {
      lastBtnTick[3] = now;
      buttonPressedMask |= (1 << 3);
    }
  }
}

// ----------------- Timer1 init -----------------
void timer1_init() {
  cli();
  TCCR1A = 0;
  TCCR1B = 0;
  OCR1A = 249;            // (16MHz / 64) => 1ms tick
  TCCR1B |= (1 << WGM12); // CTC
  TCCR1B |= (1 << CS11) | (1 << CS10); // prescaler 64
  TIMSK1 |= (1 << OCIE1A); // enable compare A interrupt
  sei();
}

// ----------------- Setup -----------------
void setup() {
  // initialize LCD
  lcd.begin(16, 2);
  lcd.clear();

  // Buttons: use INPUT_PULLUP; pressed -> connects to GND -> falling edge
  for (uint8_t i = 0; i < NUM_HOLES; ++i) pinMode(BTN_PINS[i], INPUT_PULLUP);

  // attach external interrupts for D2, D3
  attachInterrupt(digitalPinToInterrupt(BTN_PINS[0]), button0_isr, FALLING);
  attachInterrupt(digitalPinToInterrupt(BTN_PINS[1]), button1_isr, FALLING);

  // enable pin-change interrupts for PD4 and PD5 (D4, D5)
  PCICR |= (1 << PCIE2);              // enable PCINT2 (PCINT[23:16]) for PORTD
  PCMSK2 |= (1 << PD4) | (1 << PD5);  // enable PCINT on PD4 and PD5

  prevPIND = PIND; // initialise previous state

  // Timer
  timer1_init();

  // EEPROM load
  eepromLoad();
  highScore = persist.highScore;
  difficulty = persist.difficulty;

  // initialize game data
  clearAllMoles();
  score = 0;
  gameRunning = false;
  showStartScreen = true;
  nextSpawnTick = 0; // will be set when game starts

  // seed RNG with some entropy
  randomSeed(((uint32_t)analogRead(A0) << 16) ^ micros());

  // initial screen (fully padded)
  char top[17], bot[17];
  for (uint8_t i = 0; i < 16; ++i) { top[i] = ' '; bot[i] = ' '; }
  top[16] = bot[16] = '\0';
  const char *t = "WHACK-A-MOLE";
  const char *b = "Press any button";
  strncpy(top, t, strlen(t));
  strncpy(bot, b, strlen(b));
  lcd.setCursor(0,0); lcd.print(top);
  lcd.setCursor(0,1); lcd.print(bot);
}

// ----------------- Main loop -----------------
void loop() {
  // Handle button events (read and clear mask atomically)
  uint8_t pressed;
  noInterrupts();
  pressed = buttonPressedMask;
  buttonPressedMask = 0;
  interrupts();

  if (pressed) {
    // if showing the start screen, dismiss it on the first press and start game
    if (showStartScreen) {
      showStartScreen = false;
      beginGame();
    }

    // if game not running and start screen already dismissed, ensure game starts
    if (!gameRunning && !showStartScreen) {
      beginGame();
    }

    bool anyWrong = false;
    // process each pressed button
    for (uint8_t b = 0; b < NUM_HOLES; ++b) {
      if (pressed & (1 << b)) {
        if (moles[b].active) {
          // successful whack
          moles[b].active = false;
          score++;
          // update high score
          if (score > highScore) {
            highScore = score;
            persist.highScore = highScore;
            eepromSaveIfChanged();
          }
          // stamp debounce to avoid immediate retrigger
          lastBtnTick[b] = msTicks;
        } else {
          // wrong button pressed -> mark wrong
          anyWrong = true;
        }
      }
    }

    if (anyWrong) {
      // Reset score on any wrong press
      score = 0;
      // optional: clear all moles so player restarts cleanly
      // clearAllMoles();
      // suppress immediate re-penalization by stamping all lastBtnTick
      for (uint8_t i = 0; i < NUM_HOLES; ++i) lastBtnTick[i] = msTicks;
    }
  }

  // Periodic game tick
  if (gameTickFlag) {
    noInterrupts();
    gameTickFlag = false;
    uint32_t now = msTicks;
    interrupts();

    if (gameRunning) {
      // expire moles that exceeded lifetime
      for (uint8_t i = 0; i < NUM_HOLES; ++i) {
        if (moles[i].active) {
          if (now - moles[i].spawnTick >= moles[i].lifetimeMs) {
            moles[i].active = false;
          }
        }
      }

      // time-based spawn: if it's time, spawn and schedule next
      if (now >= nextSpawnTick) {
        spawnRandomMole();
        // schedule next spawn based on difficulty
        // difficulty (0..255) -> produce interval between SPAWN_MIN_MS..SPAWN_MAX_MS
        // here higher difficulty value -> less frequent (longer interval). Invert if you prefer opposite.
        uint16_t minI = map(difficulty, 0, 255, SPAWN_MIN_MS / 2, SPAWN_MIN_MS);
        uint16_t maxI = map(difficulty, 0, 255, SPAWN_MAX_MS / 2, SPAWN_MAX_MS);
        if (maxI < minI) { uint16_t tmp = maxI; maxI = minI; minI = tmp; } // sanity
        uint16_t interval = random(minI, maxI + 1);
        nextSpawnTick = now + interval;
      }
    }

    // Update display every game tick
    updateLCD();
  }
}

// ----------------- Game helpers -----------------
void beginGame() {
  clearAllMoles();
  score = 0;
  gameRunning = true;
  // schedule first spawn shortly after start
  nextSpawnTick = msTicks + random(SPAWN_MIN_MS, SPAWN_MAX_MS + 1);
}

void spawnRandomMole() {
  // choose a free hole randomly; if none free, skip
  uint8_t freeList[NUM_HOLES];
  uint8_t freeCount = 0;
  for (uint8_t i = 0; i < NUM_HOLES; ++i) if (!moles[i].active) freeList[freeCount++] = i;
  if (freeCount == 0) return;
  uint8_t sel = freeList[random(0, freeCount)];
  moles[sel].active = true;
  moles[sel].spawnTick = msTicks;
  moles[sel].lifetimeMs = random(MOLE_MIN_MS, MOLE_MAX_MS + 1);
}

void clearAllMoles() {
  for (uint8_t i = 0; i < NUM_HOLES; ++i) moles[i].active = false;
}

// ----------------- LCD update (fully overwrites lines) -----------------
void updateLCD() {
  if (showStartScreen) {
    // keep the start screen exactly (overwrite full lines to avoid trailing chars)
    char top[17], bot[17];
    for (uint8_t i = 0; i < 16; ++i) { top[i] = ' '; bot[i] = ' '; }
    top[16] = bot[16] = '\0';
    const char *t = "WHACK-A-MOLE";
    const char *b = "Press any button";
    strncpy(top, t, strlen(t));
    strncpy(bot, b, strlen(b));
    lcd.setCursor(0,0); lcd.print(top);
    lcd.setCursor(0,1); lcd.print(bot);
    return;
  }

  char line0[17], line1[17];
  for (uint8_t i = 0; i < 16; ++i) { line0[i] = ' '; line1[i] = ' '; }
  line0[16] = line1[16] = '\0';

  // draw holes on top row at cols 0,4,8,12
  for (uint8_t h = 0; h < NUM_HOLES; ++h) {
    uint8_t col = h * 4;
    if (moles[h].active) line0[col] = 'M';
    else line0[col] = '-';
    line0[col+1] = '0' + h;
  }

  // bottom line: score and highscore, padded to full 16 chars
  char tmp[17];
  snprintf(tmp, sizeof(tmp), "S:%3u  HS:%3u", score, highScore);
  uint8_t tlen = strlen(tmp);
  for (uint8_t i = 0; i < 16; ++i) {
    if (i < tlen) line1[i] = tmp[i];
    else line1[i] = ' ';
  }
  lcd.setCursor(0,0); lcd.print(line0);
  lcd.setCursor(0,1); lcd.print(line1);
}

// ----------------- EEPROM helpers -----------------
void eepromLoad() {
  Persist p;
  EEPROM.get(EEPROM_ADDR, p);
  if (p.magic == MAGIC && p.version == VERSION) {
    persist = p;
  } else {
    persist.magic = MAGIC;
    persist.version = VERSION;
    persist.highScore = DEFAULT_HIGH_SCORE;
    persist.difficulty = DEFAULT_DIFFICULTY;
    EEPROM.put(EEPROM_ADDR, persist);
  }
}

void eepromSaveIfChanged() {
  Persist cur;
  EEPROM.get(EEPROM_ADDR, cur);
  bool changed = false;
  if (cur.highScore != persist.highScore) changed = true;
  if (cur.difficulty != persist.difficulty) changed = true;
  if (changed) {
    EEPROM.put(EEPROM_ADDR, persist);
  }
}
