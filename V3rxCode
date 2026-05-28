// ─── 433MHz Receiver — Game Controller (5 teams default) ────────────────────
// Wiring:
//   RX module DATA  → GPIO 36
//   RX module VCC   → 3.3V or 5V
//   RX module GND   → GND
//   WS2812 strip    → GPIO 13
//   Buzzer          → GPIO 26
//
//   Confirmation LEDs → 220Ω → GND:
//   LED_START   → GPIO 25
//   LED_P1      → GPIO 33
//   LED_P2      → GPIO 32
//   LED_MANAGER → GPIO 12
//   LED_ACTION  → GPIO 27
//
// Strip layout (19 LEDs):
//  [0][1][2][3][4][5][6][7][8][9][10][11][12][13][14][15][16][17][18]
//   P1 P2  *  <------- timer LEDs 3-12 -------->   *  P3  P4  P5  --
//   * = rainbow in manager mode

#include "driver/rmt.h"
#include <RH_ASK.h>
#include <SPI.h>
#include <WiFi.h>

// ── RF ───────────────────────────────────────────────────────────────────────
RH_ASK driver(2000, 35, 4, 5);

const int DEDUPE_MS = 1000;
String lastMsg = "";
unsigned long lastReceived = 0;

// ── Confirmation LEDs ─────────────────────────────────────────────────────────
#define LED_START    25
#define LED_P1       33
#define LED_P2       32
#define LED_MANAGER  12
#define LED_ACTION   27
#define CONFIRM_MS   300

// ── WS2812 ───────────────────────────────────────────────────────────────────
#define LED_PIN     13
#define BUZZER_PIN  26
#define NUM_LEDS    19
#define RMT_CHANNEL RMT_CHANNEL_0

struct Color { uint8_t r, g, b; };
Color ledBuffer[NUM_LEDS];
rmt_item32_t rmt_items[NUM_LEDS * 24];

void rmt_init() {
  rmt_config_t config = {};
  config.rmt_mode                 = RMT_MODE_TX;
  config.channel                  = RMT_CHANNEL;
  config.gpio_num                 = (gpio_num_t)LED_PIN;
  config.mem_block_num            = 1;
  config.tx_config.loop_en        = false;
  config.tx_config.carrier_en     = false;
  config.tx_config.idle_output_en = true;
  config.tx_config.idle_level     = RMT_IDLE_LEVEL_LOW;
  config.clk_div                  = 4;
  rmt_config(&config);
  rmt_driver_install(config.channel, 0, 0);
}

void encode_pixel(int index, uint8_t r, uint8_t g, uint8_t b) {
  uint32_t grb = ((uint32_t)g << 16) | ((uint32_t)r << 8) | b;
  for (int bit = 23; bit >= 0; bit--) {
    int i = index * 24 + (23 - bit);
    if ((grb >> bit) & 1) {
      rmt_items[i].level0 = 1; rmt_items[i].duration0 = 14;
      rmt_items[i].level1 = 0; rmt_items[i].duration1 = 12;
    } else {
      rmt_items[i].level0 = 1; rmt_items[i].duration0 = 7;
      rmt_items[i].level1 = 0; rmt_items[i].duration1 = 16;
    }
  }
}

void showLEDs() {
  for (int i = 0; i < NUM_LEDS; i++)
    encode_pixel(i, ledBuffer[i].r, ledBuffer[i].g, ledBuffer[i].b);
  rmt_write_items(RMT_CHANNEL, rmt_items, NUM_LEDS * 24, true);
  rmt_wait_tx_done(RMT_CHANNEL, portMAX_DELAY);
  delayMicroseconds(300);
}

void clearAll() {
  for (int i = 0; i < NUM_LEDS; i++) ledBuffer[i] = {0, 0, 0};
  showLEDs();
}

// Startup test — sweeps white so you can confirm strip is alive
void startupTest() {
  for (int i = 0; i < NUM_LEDS; i++) {
    ledBuffer[i] = {40, 40, 40};
    showLEDs();
    delay(40);
  }
  delay(300);
  clearAll();
  delay(200);
}

// ── Buzzer ───────────────────────────────────────────────────────────────────
void playTone(int freq, int duration) {
  int period = 1000000 / freq;
  long endTime = millis() + duration;
  while (millis() < endTime) {
    digitalWrite(BUZZER_PIN, HIGH); delayMicroseconds(period / 2);
    digitalWrite(BUZZER_PIN, LOW);  delayMicroseconds(period / 2);
  }
}

void buzzStart()    { playTone(1046,100); delay(50); playTone(1318,100); delay(50); playTone(1568,200); }
void buzzWin()      { playTone(1568,150); delay(50); playTone(1318,150); delay(50); playTone(1046,300); }
void buzzTie()      { playTone(800,100);  delay(50); playTone(800,100); }
void buzzTimerEnd() { for(int i=0;i<3;i++){ playTone(880,200); delay(100); } }
void buzzClick()    { playTone(800,40); }
void buzzAdd()      { playTone(600,80); }
void buzzRemove()   { playTone(300,80); }

// ── Colors ───────────────────────────────────────────────────────────────────
#define NUM_COLORS 6
Color palette[NUM_COLORS] = {
  {0,   0,   255},  // 0 Blue
  {255, 0,   0  },  // 1 Red
  {0,   255, 0  },  // 2 Green
  {255, 128, 0  },  // 3 Orange
  {128, 0,   255},  // 4 Purple
  {0,   255, 255},  // 5 Cyan
};
bool colorUsed[NUM_COLORS] = {false,false,false,false,false,false};

// ── Queue ─────────────────────────────────────────────────────────────────────
#define MAX_TEAMS 5
int ledForSlot[MAX_TEAMS] = {0, 1, 14, 15, 16};
int teamColor[NUM_LEDS];
int queue[MAX_TEAMS];
int queueSize = 0;
bool tieUsed[MAX_TEAMS] = {false,false,false,false,false};
int allPositions[MAX_TEAMS] = {0, 1, 14, 15, 16};
int cursorIndex = 0;

int firstAvailableColor() {
  for (int i = 0; i < NUM_COLORS; i++) if (!colorUsed[i]) return i;
  return -1;
}

void addTeamAt(int ledPos, int colorIdx) {
  teamColor[ledPos]   = colorIdx;
  colorUsed[colorIdx] = true;
  for (int s = 0; s < MAX_TEAMS; s++) {
    if (ledForSlot[s] == ledPos) {
      if (queueSize < MAX_TEAMS) {
        for (int i = queueSize; i > s; i--) queue[i] = queue[i-1];
        queue[s] = ledPos;
        queueSize++;
      }
      break;
    }
  }
}

void removeTeamAt(int ledPos) {
  if (teamColor[ledPos] < 0) return;
  colorUsed[teamColor[ledPos]] = false;
  teamColor[ledPos] = -1;
  int removedSlot = -1;
  for (int s = 0; s < MAX_TEAMS; s++) {
    if (ledForSlot[s] == ledPos) { removedSlot = s; break; }
  }
  if (removedSlot < 0) return;
  for (int i = 0; i < queueSize; i++) {
    if (queue[i] == ledPos) {
      for (int j = i; j < queueSize - 1; j++) queue[j] = queue[j+1];
      queueSize--;
      break;
    }
  }
  for (int s = removedSlot; s < queueSize; s++) {
    teamColor[ledForSlot[s]]     = teamColor[ledForSlot[s + 1]];
    teamColor[ledForSlot[s + 1]] = -1;
    tieUsed[s]                   = tieUsed[s + 1];
  }
  tieUsed[queueSize] = false;
  queueSize = 0;
  for (int s = 0; s < MAX_TEAMS; s++) {
    if (teamColor[ledForSlot[s]] >= 0) {
      queue[queueSize] = ledForSlot[s];
      queueSize++;
    }
  }
}

// ── Game logic ────────────────────────────────────────────────────────────────
void shiftColor(int destSlot, int srcSlot) {
  teamColor[ledForSlot[destSlot]] = teamColor[ledForSlot[srcSlot]];
  tieUsed[destSlot]               = tieUsed[srcSlot];
}

void p1Wins() {
  int loserColor = teamColor[ledForSlot[1]];
  for (int s = 1; s < queueSize - 1; s++) shiftColor(s, s + 1);
  teamColor[ledForSlot[queueSize-1]] = loserColor;
  tieUsed[queueSize-1] = false;
  Serial.println("P1 wins");
}

void p2Wins() {
  int loserColor = teamColor[ledForSlot[0]];
  for (int s = 0; s < queueSize - 1; s++) shiftColor(s, s + 1);
  teamColor[ledForSlot[queueSize-1]] = loserColor;
  tieUsed[queueSize-1] = false;
  Serial.println("P2 wins");
}

void doTie() {
  if (queueSize == 3) {
    bool p1used = tieUsed[0], p2used = tieUsed[1];
    if (!p1used && !p2used) {
      tieUsed[0] = true;
      int lc = teamColor[ledForSlot[1]];
      shiftColor(1, 2); teamColor[ledForSlot[2]] = lc; tieUsed[2] = false;
      Serial.println("Tie: P1 stays (free tie)");
    } else if (p1used && !p2used) {
      tieUsed[1] = true;
      int lc = teamColor[ledForSlot[0]];
      shiftColor(0, 1); shiftColor(1, 2);
      teamColor[ledForSlot[2]] = lc; tieUsed[2] = false;
      Serial.println("Tie: P1 must leave");
    } else if (!p1used && p2used) {
      tieUsed[0] = true;
      int lc = teamColor[ledForSlot[1]];
      shiftColor(1, 2); teamColor[ledForSlot[2]] = lc; tieUsed[2] = false;
      Serial.println("Tie: P2 must leave");
    } else {
      tieUsed[0] = true;
      int lc = teamColor[ledForSlot[1]];
      shiftColor(1, 2); teamColor[ledForSlot[2]] = lc; tieUsed[2] = false;
      Serial.println("Tie: both used, P1 priority");
    }
  } else {
    int c0 = teamColor[ledForSlot[0]], c1 = teamColor[ledForSlot[1]];
    for (int s = 0; s < queueSize - 2; s++) shiftColor(s, s + 2);
    teamColor[ledForSlot[queueSize-2]] = c0;
    teamColor[ledForSlot[queueSize-1]] = c1;
    tieUsed[queueSize-2] = false; tieUsed[queueSize-1] = false;
    Serial.println("Tie: both leave");
  }
}

// ── Timer & manager state ──────────────────────────────────────────────────────
int  timerSeconds = 300;
bool timerRunning = false;
bool gameActive   = false;
unsigned long lastTick = 0;

bool managerMode      = false;
bool cursorVisible    = true;
unsigned long lastCursorBlink = 0;
unsigned long lastRainbow     = 0;
int  rainbowHue = 0;

int reachableCount() {
  int count = 3;
  if (teamColor[14] >= 0) count++;
  if (teamColor[15] >= 0) count++;
  return count;
}

int cursorLed() {
  int reach = reachableCount();
  if (cursorIndex >= reach) cursorIndex = reach - 1;
  return allPositions[cursorIndex];
}

Color hueToColor(uint8_t hue) {
  uint8_t r, g, b;
  if      (hue < 43)  { r=255; g=hue*6; b=0; }
  else if (hue < 85)  { r=255-(hue-43)*6; g=255; b=0; }
  else if (hue < 128) { r=0; g=255; b=(hue-85)*6; }
  else if (hue < 171) { r=0; g=255-(hue-128)*6; b=255; }
  else if (hue < 213) { r=(hue-171)*6; g=0; b=255; }
  else                { r=255; g=0; b=255-(hue-213)*6; }
  return {r, g, b};
}

Color getTimerLedColor(int led) {
  int blockStart = (9 - led) * 30;
  int blockEnd   = blockStart + 30;
  if (timerSeconds <= blockStart) return {255, 0, 0};
  if (timerSeconds >= blockEnd)   return {0, 255, 0};
  if ((timerSeconds - blockStart) <= 15) return {255, 255, 0};
  return {0, 255, 0};
}

void startWave() {
  for (int led = 12; led >= 3; led--) {
    ledBuffer[led] = {0,255,0}; showLEDs(); delay(30);
  }
  for (int b = 0; b < 2; b++) {
    for (int i = 3; i <= 12; i++) ledBuffer[i] = {0,0,0}; showLEDs(); delay(150);
    for (int i = 3; i <= 12; i++) ledBuffer[i] = {0,255,0}; showLEDs(); delay(150);
  }
}

void finalWhiteBlink() {
  for (int b = 0; b < 5; b++) {
    for (int i = 3; i <= 12; i++) ledBuffer[i] = {255,255,255}; showLEDs(); delay(250);
    for (int i = 3; i <= 12; i++) ledBuffer[i] = {0,0,0}; showLEDs(); delay(250);
  }
}

void updateLEDs() {
  for (int i = 0; i < NUM_LEDS; i++) ledBuffer[i] = {0,0,0};
  for (int s = 0; s < MAX_TEAMS; s++) {
    int led = ledForSlot[s];
    if (teamColor[led] >= 0) ledBuffer[led] = palette[teamColor[led]];
  }
  if (managerMode) {
    Color rc = hueToColor(rainbowHue);
    ledBuffer[2] = rc; ledBuffer[13] = rc;
  }
  if (gameActive && timerSeconds > 10) {
    for (int led = 0; led < 10; led++)
      ledBuffer[3 + led] = getTimerLedColor(led);
  } else if (!gameActive) {
    for (int i = 3; i <= 12; i++) ledBuffer[i] = {0,0,0};
  }
  if (managerMode) {
    int cLed = cursorLed();
    if (!cursorVisible) ledBuffer[cLed] = {0,0,0};
    else if (teamColor[cLed] < 0) ledBuffer[cLed] = {255,255,255};
  }
  showLEDs();
}

void updateFinalCountdown() {
  int ledsOff = 10 - timerSeconds;
  for (int led = 0; led < 10; led++)
    ledBuffer[3 + led] = (led < ledsOff) ? Color{0,0,0} : Color{255,0,0};
  for (int s = 0; s < MAX_TEAMS; s++) {
    int led = ledForSlot[s];
    if (teamColor[led] >= 0) ledBuffer[led] = palette[teamColor[led]];
  }
  if (managerMode) {
    Color rc = hueToColor(rainbowHue);
    ledBuffer[2] = rc; ledBuffer[13] = rc;
    int cLed = cursorLed();
    if (!cursorVisible) ledBuffer[cLed] = {0,0,0};
    else if (teamColor[cLed] < 0) ledBuffer[cLed] = {255,255,255};
  }
  showLEDs();
}

void confirmLED(int pin) {
  digitalWrite(pin, HIGH);
  delay(CONFIRM_MS);
  digitalWrite(pin, LOW);
}

void handleCommand(String msg) {
  unsigned long now = millis();
  Serial.print("CMD: "); Serial.println(msg);

  // LED 6 on transmitter — manual buzzer trigger
  if (msg == "BUZZ") { playTone(1000, 500); return; }

  if (msg == "MGR") {
    confirmLED(LED_MANAGER);
    managerMode = !managerMode;
    if (managerMode) {
      cursorIndex = reachableCount() - 1;
      cursorVisible = true;
      lastCursorBlink = now;
      Serial.println("Manager ON");
    } else {
      Serial.println("Manager OFF");
    }
    buzzClick(); updateLEDs();
    return;
  }

  if (managerMode) {
    if (msg == "P1") {
      confirmLED(LED_P1);
      int reach = reachableCount();
      cursorIndex = (cursorIndex - 1 + reach) % reach;
      cursorVisible = true; lastCursorBlink = now;
      buzzClick(); updateLEDs();

    } else if (msg == "P2") {
      confirmLED(LED_P2);
      int reach = reachableCount();
      cursorIndex = (cursorIndex + 1) % reach;
      cursorVisible = true; lastCursorBlink = now;
      buzzClick(); updateLEDs();

    } else if (msg == "START") {
      confirmLED(LED_START);
      int cLed = cursorLed();
      if (teamColor[cLed] >= 0) {
        int current = teamColor[cLed], next = -1;
        for (int i = 1; i <= NUM_COLORS; i++) {
          int candidate = (current + i) % NUM_COLORS;
          if (!colorUsed[candidate] || candidate == current) { next = candidate; break; }
        }
        if (next >= 0 && next != current) {
          colorUsed[current] = false; colorUsed[next] = true;
          teamColor[cLed] = next;
          buzzClick(); updateLEDs();
        }
      }

    } else if (msg == "ACT") {
      confirmLED(LED_ACTION);
      int cLed = cursorLed();
      if (teamColor[cLed] >= 0) {
        removeTeamAt(cLed);
        int reach = reachableCount();
        if (cursorIndex >= reach) cursorIndex = reach - 1;
        buzzRemove();
      } else {
        int col = firstAvailableColor();
        if (col >= 0) { addTeamAt(cLed, col); buzzAdd(); }
      }
      updateLEDs();
    }

  } else {
    if (msg == "START") {
      confirmLED(LED_START);
      if (!gameActive && queueSize >= 2) {
        startWave(); buzzStart();
        gameActive = true; timerRunning = true;
        timerSeconds = 300; lastTick = now;
        tieUsed[0] = false; tieUsed[1] = false;
        updateLEDs();
        Serial.println("Game started");
      }

    } else if (msg == "P1") {
      confirmLED(LED_P1);
      buzzWin(); p1Wins();
      gameActive = false; timerRunning = false; timerSeconds = 300;
      updateLEDs();

    } else if (msg == "P2") {
      confirmLED(LED_P2);
      buzzWin(); p2Wins();
      gameActive = false; timerRunning = false; timerSeconds = 300;
      updateLEDs();

    } else if (msg == "ACT") {
      confirmLED(LED_ACTION);
      buzzTie(); doTie();
      gameActive = false; timerRunning = false; timerSeconds = 300;
      updateLEDs();
    }
  }
}

// ── Setup ──────────────────────────────────────────────────────────────────────
void setup() {
  // RMT must init BEFORE WiFi.mode(WIFI_OFF) on some board versions
  rmt_init();
  delay(100);

  WiFi.mode(WIFI_OFF);
  btStop();
  Serial.begin(115200);

  pinMode(LED_START,   OUTPUT); digitalWrite(LED_START,   LOW);
  pinMode(LED_P1,      OUTPUT); digitalWrite(LED_P1,      LOW);
  pinMode(LED_P2,      OUTPUT); digitalWrite(LED_P2,      LOW);
  pinMode(LED_MANAGER, OUTPUT); digitalWrite(LED_MANAGER, LOW);
  pinMode(LED_ACTION,  OUTPUT); digitalWrite(LED_ACTION,  LOW);
  pinMode(BUZZER_PIN,  OUTPUT); digitalWrite(BUZZER_PIN,  LOW);

  // Startup sweep — confirms strip is alive before anything else
  startupTest();

  if (!driver.init())
    Serial.println("RX INIT FAILED");
  else
    Serial.println("RX ready");

  for (int i = 0; i < NUM_LEDS; i++) teamColor[i] = -1;
  for (int i = 0; i < MAX_TEAMS; i++) tieUsed[i] = false;

  // Default 5 teams
  // Near input port:
  teamColor[0]  = 0; colorUsed[0] = true;  // Blue
  teamColor[1]  = 1; colorUsed[1] = true;  // Red
  // After 12-LED timer gap:
  teamColor[14] = 2; colorUsed[2] = true;  // Green
  teamColor[15] = 3; colorUsed[3] = true;  // Orange
  teamColor[16] = 4; colorUsed[4] = true;  // Purple

  queue[0]=0; queue[1]=1; queue[2]=14; queue[3]=15; queue[4]=16;
  queueSize = 5;

  cursorIndex  = reachableCount() - 1;
  timerSeconds = 300;
  gameActive   = false;

  updateLEDs();
  Serial.println("Ready — 5 teams loaded");
}

// ── Loop ───────────────────────────────────────────────────────────────────────
void loop() {
  unsigned long now = millis();

  if (managerMode) {
    if (now - lastRainbow >= 20)     { lastRainbow = now; rainbowHue = (rainbowHue + 2) % 256; }
    if (now - lastCursorBlink >= 400){ lastCursorBlink = now; cursorVisible = !cursorVisible; }
  }

  if (timerRunning) {
    if (timerSeconds > 10) {
      if (now - lastTick >= 1000) {
        lastTick = now; timerSeconds--;
        updateLEDs(); Serial.println(timerSeconds);
      }
    } else {
      if (now - lastTick >= 1000) {
        lastTick = now; timerSeconds--;
        Serial.println(timerSeconds);
        if (timerSeconds <= 0) {
          timerRunning = false; gameActive = false;
          finalWhiteBlink();
          for (int i = 3; i <= 12; i++) ledBuffer[i] = {0,0,0};
          showLEDs(); buzzTimerEnd();
          timerSeconds = 300; updateLEDs();
          Serial.println("Timer done");
        } else {
          updateFinalCountdown();
        }
      }
    }
  }

  uint8_t buf[8];
  uint8_t buflen = sizeof(buf);
  if (driver.recv(buf, &buflen)) {
    buf[buflen] = '\0';
    String msg  = (char*)buf;
    unsigned long now2 = millis();
    if (msg != lastMsg || (now2 - lastReceived) > DEDUPE_MS) {
      lastMsg = msg; lastReceived = now2;
      handleCommand(msg);
    }
  }

  if (managerMode) updateLEDs();
}
