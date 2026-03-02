#include <Arduino.h>

// --- Pins ---
const int PWM1_PIN = 18; // LEDC channel 0
const int PWM2_PIN = 19; // LEDC channel 1

const int FREQ_UP_PIN   = 12;
const int FREQ_DOWN_PIN = 14;
const int DUTY_UP_PIN   = 26;
const int DUTY_DOWN_PIN = 27;

const int DEBUG_TOGGLE_PIN = 2; // toggled each frequency change (measure with scope)
// Frequency/duty variables
unsigned long freqHz = 10;
const unsigned long MIN_FREQ = 1;
const unsigned long MAX_FREQ = 150;
float duty = 0.5f; // 0..1

// Steps
const unsigned long FREQ_STEP_FINE = 1;
const unsigned long FREQ_STEP_COARSE = 5;
unsigned long currentFreqStep = FREQ_STEP_FINE;
bool freqStepFineMode = true;
const float DUTY_STEP = 0.05f; // 5%

// Debounce/repeat
const unsigned long DEBOUNCE_MS = 40;
const unsigned long REPEAT_SUPPRESS_MS = 120;

// Toggle hold
const unsigned long TOGGLE_HOLD_MS = 800;
unsigned long bothFreqHeldSince = 0;
unsigned long lastToggleTime = 0;
const unsigned long TOGGLE_SUPPRESS_MS = 1000;
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
	unsigned long now = millis();

	int rawFreqUp = digitalRead(FREQ_UP_PIN);
	int rawFreqDown = digitalRead(FREQ_DOWN_PIN);

	if (rawFreqUp == LOW && rawFreqDown == LOW) {
		if (bothFreqHeldSince == 0) bothFreqHeldSince = now;
		if ((now - bothFreqHeldSince >= TOGGLE_HOLD_MS) && (now - lastToggleTime >= TOGGLE_SUPPRESS_MS)) {
		Serial.println("Toggling frequency step mode call");
		lastToggleTime = now;
		delay(120);
		}
	} else {
		bothFreqHeldSince = 0;
		checkButtonAndAct(btnFreqUp, now, [](){ changeFrequency(true); });
		checkButtonAndAct(btnFreqDown, now, [](){ changeFrequency(false); });
		checkButtonAndAct(btnDutyUp, now, [](){ changeDuty(true); });
		checkButtonAndAct(btnDutyDown, now, [](){ changeDuty(false); });
	}

	delay(5);
}

void checkButtonAndAct(ButtonState &btn, unsigned long now, void (*action)()) {
	int raw = digitalRead(btn.pin);
	if (raw != btn.lastRaw) {
		btn.lastChange = now;
		btn.lastRaw = raw;
		return;
	}
	if ((now - btn.lastChange) >= DEBOUNCE_MS) {
		if (raw == LOW) {
			if ((now - btn.lastAction) >= REPEAT_SUPPRESS_MS) {
				action();
				btn.lastAction = now;
			}
		}
	}
}

void toggleFreqStepMode() {
  freqStepFineMode = !freqStepFineMode;
  currentFreqStep = freqStepFineMode ? FREQ_STEP_FINE : FREQ_STEP_COARSE;
  Serial.printf("Toggled frequency step -> %s (%lu Hz)\n", freqStepFineMode ? "FINE" : "COARSE", currentFreqStep);
  printStatus();
}

void changeFrequency(bool increase) {
	Serial.println(increase ? "Increasing frequency" : "Decreasing frequency");
	printStatus();
}

void changeDuty(bool increase) {
	Serial.println(increase ? "Increasing duty" : "Decreasing duty");
	printStatus();
}

void printStatus() {
	Serial.printf("Freq: %lu Hz | Duty: %.0f%% | Mode: %s (step %lu Hz)\n", 
					freqHz, 
					duty * 100.0f, 
					freqStepFineMode ? "FINE" : "COARSE", 
					currentFreqStep);
}
