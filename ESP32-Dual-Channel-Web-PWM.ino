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
#include <WebServer.h>

// ================WIFI CONFIG================
const char* ssid = "ESP32_PWM";
const char* password = "12345678";

// ================= DEBUG CONTROL =================
#define DEBUG_ENABLED 1

#if DEBUG_ENABLED
  #define DEBUG_PRINT(x) Serial.print(x)
  #define DEBUG_PRINTLN(x) Serial.println(x)
  #define DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
  #define DEBUG_PRINTF(...)
#endif
// =================================================

// --- Pins ---
#define PWM1_PIN            18
#define PWM2_PIN            19

#define FREQ_UP_PIN         12
#define FREQ_DOWN_PIN       14
#define DUTY_UP_PIN         26
#define DUTY_DOWN_PIN       27
#define CHANNEL_SELECT_PIN  25
#define DEBUG_TOGGLE_PIN    2
// LEDC config
#define CHANNEL_0 LEDC_CHANNEL_0
#define CHANNEL_1 LEDC_CHANNEL_1
#define TIMER0_SEL LEDC_TIMER_0
#define TIMER1_SEL LEDC_TIMER_1
#define SPEED_MODE LEDC_HIGH_SPEED_MODE
#define PWM_RESOLUTION 10
#define LEDC_RES LEDC_TIMER_10_BIT

// Frequency/duty variables
#define MIN_FREQ 10UL
#define MAX_FREQ 10000UL
unsigned long freqHz[2] = {MIN_FREQ, MIN_FREQ};
float duty[2] = {0.5f, 0.5f};
ledc_channel_t ledcChannels = CHANNEL_0;

// Steps
#define FREQ_STEP_FINE 1
#define FREQ_STEP_COARSE 5
unsigned long currentFreqStep = FREQ_STEP_FINE;
bool freqStepFineMode = true;
#define DUTY_STEP 0.05f

// Timing
#define DEBOUNCE_MS 40UL
#define REPEAT_SUPPRESS_MS 120UL
#define TOGGLE_HOLD_MS 800UL
#define TOGGLE_SUPPRESS_MS 1000UL

#define INITIAL_REPEAT_DELAY 400UL   // start repeating after hold
#define FAST_REPEAT_DELAY    100UL   // repeat speed after start

unsigned long bothFreqHeldSince = 0;
unsigned long lastToggleTime = 0;

enum InputSource {
    INPUT_BUTTON,
    INPUT_WEB
};

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

WebServer server(80);

// Prototypes
void configureLedcTimer(unsigned long frequencyHz, ledc_channel_t channel);
void configureLedcChannels(ledc_channel_t channel) ;
void applyDutyToChannels(ledc_channel_t channel);
void applyFrequencyToChannels();
void checkButtonAndAct(ButtonState &btn, unsigned long now, void (*action)());
void changeFrequency(bool increase);
void changeDuty(bool increase, ledc_channel_t channel);
void toggleFreqStepMode();
void printStatus();
void handleRoot();
void handleFreqUp();
void handleFreqDown();
void handleDutyUp();
void handleDutyDown();
void handleStatus();
void handleNotFound();

void setup() {
	Serial.begin(115200);
	delay(50);

    DEBUG_PRINTLN("=== ESP32 PWM DEBUG START ===");
	pinMode(FREQ_UP_PIN, INPUT_PULLUP);
	pinMode(FREQ_DOWN_PIN, INPUT_PULLUP);
	pinMode(DUTY_UP_PIN, INPUT_PULLUP);
	pinMode(DUTY_DOWN_PIN, INPUT_PULLUP);

	pinMode(DEBUG_TOGGLE_PIN, OUTPUT);
    pinMode(CHANNEL_SELECT_PIN, INPUT);
	digitalWrite(DEBUG_TOGGLE_PIN, LOW);

	// initialize lastRaw
	btnFreqUp.lastRaw   = digitalRead(btnFreqUp.pin);
	btnFreqDown.lastRaw = digitalRead(btnFreqDown.pin);
	btnDutyUp.lastRaw   = digitalRead(btnDutyUp.pin);
	btnDutyDown.lastRaw = digitalRead(btnDutyDown.pin);

    configureLedcTimer(freqHz[0], CHANNEL_0);
    configureLedcTimer(freqHz[1], CHANNEL_1);
    configureLedcChannels(CHANNEL_0);
    configureLedcChannels(CHANNEL_1);

    printStatus();

    WiFi.softAP(ssid, password);

    Serial.println("Access Point Started");
    Serial.print("IP Address: ");
    Serial.println(WiFi.softAPIP());

    server.on("/", handleRoot);
    server.on("/frequp", handleFreqUp);
    server.on("/freqdown", handleFreqDown);
    server.on("/dutyup", handleDutyUp);
    server.on("/dutydown", handleDutyDown);
    server.on("/status", handleStatus);
    server.onNotFound(handleNotFound);

    server.begin();
    DEBUG_PRINTLN("Web Server Started");
}

void loop() {
    server.handleClient();
	unsigned long now = millis();

	int rawFreqUp = digitalRead(FREQ_UP_PIN);
	int rawFreqDown = digitalRead(FREQ_DOWN_PIN);

	if (rawFreqUp == LOW && rawFreqDown == LOW) {
		if (bothFreqHeldSince == 0) {
			bothFreqHeldSince = now;
		}
		if ((now - bothFreqHeldSince >= TOGGLE_HOLD_MS) && 
			(now - lastToggleTime >= TOGGLE_SUPPRESS_MS)) {
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
const char MAIN_page[] PROGMEM = R"====(
<!DOCTYPE html>
<html>
<head>
<title>ESP32 PWM Control</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
body { font-family: Arial; text-align: center; }
button { width: 120px; height: 50px; font-size: 18px; margin: 10px; }
.value { font-size: 22px; margin: 10px; }
</style>
</head>
<body>
<h2>ESP32 PWM Controller</h2>
<div class="value">
Frequency Channel 0: <span id="freq0">0</span> Hz
</div>
<div class="value">
Duty Channel 0: <span id="duty0">0</span> %
</div>
<div class="value">
Frequency Channel 1: <span id="freq1">0</span> Hz
</div>
<div class="value">
Duty Channel 1: <span id="duty1">0</span> %
</div>
<div class="value">
Selected Channel: <span id="channel">0</span>
</div>
<h3>Frequency</h3>
<button onclick="changeFreq('/frequp')">Freq +</button>
<button onclick="changeFreq('/freqdown')">Freq -</button>
<select id="stepSelect">
<option value="1">1</option>
<option value="10">10</option>
<option value="100">100</option>
<option value="1000">1000</option>
</select>
<h3>Duty</h3>
<button onclick="sendCmd('/dutyup')">Duty +</button>
<button onclick="sendCmd('/dutydown')">Duty -</button>
<script>
function sendCmd(url) {fetch(url);}
function changeFreq(baseUrl) {var step = document.getElementById("stepSelect").value;fetch(baseUrl + "?step=" + step);}
function updateStatus() {
fetch('/status')
.then(response => response.json())
.then(data => {
document.getElementById('freq0').innerText = data.freq0;
document.getElementById('duty0').innerText = data.duty0;
document.getElementById('freq1').innerText = data.freq1;
document.getElementById('duty1').innerText = data.duty1;
document.getElementById('channel').innerText = data.channel;});}
setInterval(updateStatus, 1000);
updateStatus();
</script>
</body>
</html>
)====";

// Root Page Handler
void handleRoot() {
    server.send_P(200, "text/html", MAIN_page);
    DEBUG_PRINT("Root code Request Jquery");
}

// Frequency button jquery handlers
void handleFreqUp()
{
    uint32_t step = 1;

    if (server.hasArg("step"))
        step = server.arg("step").toInt();

    processFrequencyCommand(true, step, INPUT_WEB);

    server.send(200, "text/plain", "OK");
}

void handleFreqDown()
{
    uint32_t step = 1;

    if (server.hasArg("step"))
        step = server.arg("step").toInt();

    processFrequencyCommand(false, step, INPUT_WEB);

    server.send(200, "text/plain", "OK");
}

// Duty button jquery handlers
void handleDutyUp() {
    changeDuty(true, ledcChannels);
    server.send(200, "text/plain", "OK");
    DEBUG_PRINT("Duty Cycle UP Jquery");
}

void handleDutyDown() {
  changeDuty(false, ledcChannels);
  server.send(200, "text/plain", "OK");
  DEBUG_PRINT("Duty Cycle Down Jquery");
}

void handleStatus() {
  String json = "{";
  json += "\"freq0\":" + String(freqHz[0]) + ",";
  json += "\"duty0\":" + String(duty[0] * 100.0f, 1) + ",";
  json += "\"freq1\":" + String(freqHz[1]) + ",";
  json += "\"duty1\":" + String(duty[1] * 100.0f, 1) + ",";
  json += "\"channel\":" + String((ledcChannels == CHANNEL_0) ? "0" : "1");
  json += "}";

  server.send(200, "application/json", json);
  DEBUG_PRINTLN(json);
}

void handleNotFound() {
    Serial.println("Unknown URL hit");
    server.send(404, "text/plain", "Not found");
}