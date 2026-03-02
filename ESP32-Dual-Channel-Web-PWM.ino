/* ESP32 Dual PWM (reliable frequency + duty updates)
   PWM outputs: GPIO18 (CH0), GPIO19 (CH1)
   Buttons (momentary, 4-pin metal): connect one side of a button to GND and the other side to the ESP32 GPIO
     FREQ UP   -> GPIO12
     FREQ DOWN -> GPIO14
     DUTY UP   -> GPIO26
     DUTY DOWN -> GPIO27
   Debug toggle pin: toggles when frequency changes -> measure on scope to confirm period change
   Note: If you have boot issues try GPIO13 instead of GPIO12 (GPIO12 is a boot strapping pin on some boards).
*/

#include <Arduino.h>
#include "driver/ledc.h"
#include <WiFi.h>

// ================WIFI CONFIG================
const char* ssid = "ESP32_PWM";
const char* password = "12345678";

// --- Pins ---
const int PWM1_PIN = 18; // LEDC channel 0
const int PWM2_PIN = 19; // LEDC channel 1

const int FREQ_UP_PIN   = 12;
const int FREQ_DOWN_PIN = 14;
const int DUTY_UP_PIN   = 26;
const int DUTY_DOWN_PIN = 27;

const int DEBUG_TOGGLE_PIN = 2; // toggled each frequency change (measure with scope)

// LEDC config
const ledc_channel_t CHANNEL_0 = LEDC_CHANNEL_0;
const ledc_channel_t CHANNEL_1 = LEDC_CHANNEL_1;
const ledc_timer_t TIMER_SEL = LEDC_TIMER_0;
const ledc_mode_t SPEED_MODE = LEDC_HIGH_SPEED_MODE;
const int PWM_RESOLUTION = 10; // bits
const ledc_timer_bit_t LEDC_RES = LEDC_TIMER_10_BIT;

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

void configureLedcTimer(unsigned long frequencyHz);
void configureLedcChannels();
void applyDutyToChannels();
void applyFrequencyToChannels();
void checkButtonAndAct(ButtonState &btn, unsigned long now, void (*action)());
void changeFrequency(bool increase);
void changeDuty(bool increase);
void toggleFreqStepMode();
void printStatus();

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

    configureLedcTimer(freqHz);
  	configureLedcChannels();
  	applyDutyToChannels();

  	Serial.println("Dual PWM ready");
  	printStatus();
    WiFi.softAP(ssid, password);
}

void loop() {
	unsigned long now = millis();

	int rawFreqUp = digitalRead(FREQ_UP_PIN);
	int rawFreqDown = digitalRead(FREQ_DOWN_PIN);

	if (rawFreqUp == LOW && rawFreqDown == LOW) {
		if (bothFreqHeldSince == 0) bothFreqHeldSince = now;
		if ((now - bothFreqHeldSince >= TOGGLE_HOLD_MS) && (now - lastToggleTime >= TOGGLE_SUPPRESS_MS)) {
        toggleFreqStepMode();
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
  	unsigned long newFreq = freqHz;
  	if (increase) {
	    newFreq = freqHz + currentFreqStep;
	    if (newFreq > MAX_FREQ) {
			newFreq = MAX_FREQ;
		}
	} else {
	    if (freqHz > currentFreqStep) {
			newFreq = freqHz - currentFreqStep;
		} else {
			newFreq = MIN_FREQ;
		}
	    if (newFreq < MIN_FREQ) {
			newFreq = MIN_FREQ;
		}
	}

	if (newFreq != freqHz) {
		Serial.printf("Frequency: %lu -> %lu requested\n", freqHz, newFreq);
	    freqHz = newFreq;
	    applyFrequencyToChannels();

	    // Toggle debug pin to produce one pulse per frequency change (for scope)
	    digitalWrite(DEBUG_TOGGLE_PIN, !digitalRead(DEBUG_TOGGLE_PIN));

	    printStatus();
	}
}

void changeDuty(bool increase) {
	float newDuty = duty;
	if (increase) {
		newDuty = duty + DUTY_STEP;
	}
	if (newDuty > 1.0f) {
		newDuty = 1.0f;
	} else {
		newDuty = duty - DUTY_STEP;
		if (newDuty < 0.0f) {
			newDuty = 0.0f;
		}
	}
	if (fabs(newDuty - duty) > 1e-6) {
		duty = newDuty;
		applyDutyToChannels();
		printStatus();
	}
}

// always zero-init structs to avoid stray fields, and reconfigure channels after timer update
void configureLedcTimer(unsigned long frequencyHz) {
	ledc_timer_config_t ledc_timer;
	memset(&ledc_timer, 0, sizeof(ledc_timer));
	ledc_timer.speed_mode = SPEED_MODE;
	ledc_timer.duty_resolution = LEDC_RES;
	ledc_timer.timer_num = TIMER_SEL;
	ledc_timer.freq_hz = frequencyHz;
	ledc_timer.clk_cfg = LEDC_AUTO_CLK;
	esp_err_t err = ledc_timer_config(&ledc_timer);
	if (err != ESP_OK) {
		Serial.printf("ledc_timer_config err=%d\n", (int)err);
	} else {
		Serial.printf("ledc_timer_config OK freq=%lu\n", frequencyHz);
	}
}

void configureLedcChannels() {
	ledc_channel_config_t ch;
	memset(&ch, 0, sizeof(ch));
	ch.channel = CHANNEL_0;
	ch.duty = 0;
	ch.gpio_num = PWM1_PIN;
	ch.speed_mode = SPEED_MODE;
	ch.hpoint = 0;
	ch.timer_sel = TIMER_SEL;
	esp_err_t err = ledc_channel_config(&ch);
	if (err != ESP_OK) {
		Serial.printf("ch0 config err=%d\n", (int)err);
	}

	memset(&ch, 0, sizeof(ch));
	ch.channel = CHANNEL_1;
	ch.duty = 0;
	ch.gpio_num = PWM2_PIN;
	ch.speed_mode = SPEED_MODE;
	ch.hpoint = 0;
	ch.timer_sel = TIMER_SEL;
	err = ledc_channel_config(&ch);
	if (err != ESP_OK) {
		Serial.printf("ch1 config err=%d\n", (int)err);
	}

	applyDutyToChannels();
}

void applyFrequencyToChannels() {
	// Reconfigure timer, then re-configure channels to ensure the new timer is used by channels.
	configureLedcTimer(freqHz);
	configureLedcChannels();
}

void applyDutyToChannels() {
	int maxVal = (1 << PWM_RESOLUTION) - 1;
	int dutyVal = constrain((int)round(duty * (float)maxVal), 0, maxVal);
	ledc_set_duty(SPEED_MODE, CHANNEL_0, dutyVal);
	ledc_update_duty(SPEED_MODE, CHANNEL_0);
	ledc_set_duty(SPEED_MODE, CHANNEL_1, dutyVal);
	ledc_update_duty(SPEED_MODE, CHANNEL_1);
}

void printStatus() {
	Serial.printf("Freq: %lu Hz | Duty: %.0f%% | Mode: %s (step %lu Hz)\n", 
					freqHz, 
					duty * 100.0f, 
					freqStepFineMode ? "FINE" : "COARSE", 
					currentFreqStep);
}
