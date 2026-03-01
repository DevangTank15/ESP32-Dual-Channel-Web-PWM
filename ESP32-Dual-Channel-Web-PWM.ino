#include <Arduino.h>

// --- Pins ---
const int PWM1_PIN = 18; // LEDC channel 0
const int PWM2_PIN = 19; // LEDC channel 1

const int FREQ_UP_PIN   = 12;
const int FREQ_DOWN_PIN = 14;
const int DUTY_UP_PIN   = 26;
const int DUTY_DOWN_PIN = 27;

const int DEBUG_TOGGLE_PIN = 2; // toggled each frequency change (measure with scope)
// Button state
struct ButtonState {
  int pin;
  int lastRaw;
  unsigned long lastChange;
  unsigned long lastAction;
};
ButtonState btnFreqUp   = {FREQ_UP_PIN,    HIGH, 0, 0};
ButtonState btnFreqDown = {FREQ_DOWN_PIN,  HIGH, 0, 0};
ButtonState btnDutyUp   = {DUTY_UP_PIN,    HIGH, 0, 0};
ButtonState btnDutyDown = {DUTY_DOWN_PIN,  HIGH, 0, 0};
void setup() {
  Serial.begin(115200);
  delay(50);

  pinMode(FREQ_UP_PIN, INPUT_PULLUP);
  pinMode(FREQ_DOWN_PIN, INPUT_PULLUP);
  pinMode(DUTY_UP_PIN, INPUT_PULLUP);
  pinMode(DUTY_DOWN_PIN, INPUT_PULLUP);

  pinMode(DEBUG_TOGGLE_PIN, OUTPUT);
  digitalWrite(DEBUG_TOGGLE_PIN, LOW);

  // initialize lastRaw
  btnFreqUp.lastRaw   = digitalRead(btnFreqUp.pin);
  btnFreqDown.lastRaw = digitalRead(btnFreqDown.pin);
  btnDutyUp.lastRaw   = digitalRead(btnDutyUp.pin);
  btnDutyDown.lastRaw = digitalRead(btnDutyDown.pin);

}
void loop() {
  delay(5);
}
