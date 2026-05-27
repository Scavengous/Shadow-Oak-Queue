// ─── 433MHz Transmitter — Game Controller ───────────────────────────────────
// Wiring:
//   TX module DATA  → GPIO 14
//   TX module VCC   → VIN (5V)
//   TX module GND   → GND
//   Potentiometer wiper → GPIO 34 (analog in)
//   Potentiometer leg 1 → 3.3V
//   Potentiometer leg 2 → GND
//   Send button     → GPIO 32 → GND
//
//   Preview LEDs (show current selection) → 220Ω → GND:
//   LED 1 (P1 WIN)  → GPIO 13
//   LED 2 (P2 WIN)  → GPIO 12
//   LED 3 (START)   → GPIO 27
//   LED 4 (MANAGER) → GPIO 26
//   LED 5 (TIE)     → GPIO 25
//   LED 6 (BUZZ)    → GPIO 33

#include <RH_ASK.h>
#include <SPI.h>
#include <WiFi.h>

RH_ASK driver(2000, 34, 14, 5);

const int POT_PIN  = 34;
const int BTN_PIN  = 32;
const int NUM_CMDS = 6;

const int PREVIEW_PINS[NUM_CMDS] = {13, 12, 27, 26, 25, 33};

const char* COMMANDS[NUM_CMDS] = {
  "P1",       // 0 — LED 1 — P1 wins (game) / Cursor left (manager)
  "P2",       // 1 — LED 2 — P2 wins (game) / Cursor right (manager)
  "START",    // 2 — LED 3 — Start game (game) / Cycle color (manager)
  "MGR",      // 3 — LED 4 — Toggle manager mode
  "ACT",      // 4 — LED 5 — Tie (game) / Add or remove team (manager)
  "BUZZ"      // 5 — LED 6 — Manual buzzer
};

int selectedCmd = 0;

void setup() {
  WiFi.mode(WIFI_OFF);
  btStop();
  Serial.begin(115200);

  pinMode(BTN_PIN, INPUT_PULLUP);

  for (int i = 0; i < NUM_CMDS; i++) {
    pinMode(PREVIEW_PINS[i], OUTPUT);
    digitalWrite(PREVIEW_PINS[i], LOW);
  }

  if (!driver.init())
    Serial.println("TX INIT FAILED");
  else
    Serial.println("TX ready");

  // Startup test — flash each preview LED in sequence
  for (int i = 0; i < NUM_CMDS; i++) {
    digitalWrite(PREVIEW_PINS[i], HIGH);
    delay(150);
    digitalWrite(PREVIEW_PINS[i], LOW);
  }
}

void loop() {
  int potVal = analogRead(POT_PIN);
  selectedCmd = map(potVal, 0, 4095, 0, NUM_CMDS - 1);
  selectedCmd = constrain(selectedCmd, 0, NUM_CMDS - 1);

  for (int i = 0; i < NUM_CMDS; i++) {
    digitalWrite(PREVIEW_PINS[i], (i == selectedCmd) ? HIGH : LOW);
  }

  if (digitalRead(BTN_PIN) == LOW) {
    const char* msg = COMMANDS[selectedCmd];

    for (int i = 0; i < 5; i++) {
      driver.send((uint8_t*)msg, strlen(msg));
      driver.waitPacketSent();
      delay(50);
    }

    Serial.print("Sent: ");
    Serial.println(msg);

    while (digitalRead(BTN_PIN) == LOW);
    delay(200);
  }

  delay(30);
}