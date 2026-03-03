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
//#define DEBUG_ENABLED 1

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
#define PWM1_PIN                18
#define PWM2_PIN                19

#define FREQ_UP_PIN             12
#define FREQ_DOWN_PIN           14
#define DUTY_UP_PIN             26
#define DUTY_DOWN_PIN           27
#define CHANNEL_0_ENABLE_PIN    2
#define CHANNEL_1_ENABLE_PIN    23

bool channelEnabled[2] = {false, false};

#define CHANNEL_ENABLE          HIGH
#define CHANNEL_DISABLE         LOW

// LEDC config
#define CHANNEL_0               LEDC_CHANNEL_0
#define CHANNEL_1               LEDC_CHANNEL_1
#define TIMER0_SEL              LEDC_TIMER_0
#define TIMER1_SEL              LEDC_TIMER_1
#define SPEED_MODE              LEDC_HIGH_SPEED_MODE
#define PWM_RESOLUTION          10
#define LEDC_RES                LEDC_TIMER_10_BIT

// Frequency/duty variables
#define MIN_FREQ                10UL
#define MAX_FREQ                10000UL
unsigned long freqHz[2] = {MIN_FREQ, MIN_FREQ};
float duty[2] = {0.5f, 0.5f};
ledc_channel_t ledcChannels = CHANNEL_0;

// Steps
#define FREQ_STEP_FINE          1
#define FREQ_STEP_COARSE        5
unsigned long currentFreqStep = FREQ_STEP_FINE;
bool freqStepFineMode = true;
#define DUTY_STEP               0.05f

// Timing
#define DEBOUNCE_MS             40UL
#define REPEAT_SUPPRESS_MS      120UL
#define TOGGLE_HOLD_MS          800UL
#define TOGGLE_SUPPRESS_MS      1000UL

#define INITIAL_REPEAT_DELAY    400UL   // start repeating after hold
#define FAST_REPEAT_DELAY       100UL   // repeat speed after start

unsigned int holdRepeatCount = 0;
bool smartStepMode = false;

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
  unsigned long pressStart;
  bool repeating;
};

ButtonState btnFreqUp   = {FREQ_UP_PIN,    HIGH, 0, 0, 0, false};
ButtonState btnFreqDown = {FREQ_DOWN_PIN,  HIGH, 0, 0, 0, false};
ButtonState btnDutyUp   = {DUTY_UP_PIN,    HIGH, 0, 0, 0, false};
ButtonState btnDutyDown = {DUTY_DOWN_PIN,  HIGH, 0, 0, 0, false};

WebServer server(80);

// Prototypes
void configureLedcTimer(unsigned long frequencyHz, ledc_channel_t channel);
void configureLedcChannels(ledc_channel_t channel) ;
void applyDutyToChannels(ledc_channel_t channel);
void checkButtonAndAct(ButtonState &btn, unsigned long now, void (*action)());
void changeFrequency(float frequency, ledc_channel_t channel);
void changeDuty(bool increase, ledc_channel_t channel);
void printStatus();
void processFrequencyCommand(bool increase, uint32_t requestedStep, InputSource source);
void handleRoot();
void handleFreqUp();
void handleFreqDown();
void handleDutyUp();
void handleDutyDown();
void handleStatus();
void handleNotFound();

// ================= SETUP =================
void setup() {
	Serial.begin(115200);
	delay(50);

    DEBUG_PRINTLN("=== ESP32 PWM DEBUG START ===");
	pinMode(FREQ_UP_PIN, INPUT_PULLUP);
	pinMode(FREQ_DOWN_PIN, INPUT_PULLUP);
	pinMode(DUTY_UP_PIN, INPUT_PULLUP);
	pinMode(DUTY_DOWN_PIN, INPUT_PULLUP);

	pinMode(CHANNEL_0_ENABLE_PIN, OUTPUT);
    pinMode(CHANNEL_1_ENABLE_PIN, OUTPUT);

	digitalWrite(CHANNEL_0_ENABLE_PIN, CHANNEL_DISABLE);
	digitalWrite(CHANNEL_1_ENABLE_PIN, CHANNEL_DISABLE);

	// initialize lastRaw
	btnFreqUp.lastRaw   = digitalRead(btnFreqUp.pin);
	btnFreqDown.lastRaw = digitalRead(btnFreqDown.pin);
	btnDutyUp.lastRaw   = digitalRead(btnDutyUp.pin);
	btnDutyDown.lastRaw = digitalRead(btnDutyDown.pin);

    DEBUG_PRINTLN("Initial button states captured");

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
    server.on("/channelEnable", handleChannelEnable);
    server.on("/selectChannel", handleSelectChannel);
    server.on("/status", handleStatus);
    server.onNotFound(handleNotFound);

    server.begin();
    DEBUG_PRINTLN("Web Server Started");
}

// ================= LOOP =================
void loop() {
    server.handleClient();

	unsigned long now = millis();

    handleButton(btnFreqUp);
    handleButton(btnFreqDown);
    handleButton(btnDutyUp);
    handleButton(btnDutyDown);

    delay(5);
  }

// ================= BUTTON HANDLER =================
void handleButton(ButtonState &btn)
{
    int raw = digitalRead(btn.pin);
    unsigned long now = millis();

    // Raw change detected ? start debounce
    if (raw != btn.lastRaw) {
        btn.lastChange = now;
        btn.lastRaw = raw;
        return;
    }

    // Wait for debounce stability
    if ((now - btn.lastChange) < DEBOUNCE_MS)
        return;

    // Button pressed (LOW assumed)
    if (raw == LOW) {
        // First press detection
        if (btn.pressStart == 0)
        {
            btn.pressStart = now;
            btn.lastAction = now;
            btn.repeating = false;

            DEBUG_PRINTF("Button %d first press\n", btn.pin);

            // Immediate single action
            performButtonAction(btn.pin);
            return;
        }

        // If not yet repeating ? check hold time
        if (!btn.repeating) {
            if ((now - btn.pressStart) >= INITIAL_REPEAT_DELAY) {
                btn.repeating = true;
                btn.lastAction = now;
                DEBUG_PRINTF("Button %d repeat mode START\n", btn.pin);
            }
        } else {
            // Fast repeating
            if ((now - btn.lastAction) >= FAST_REPEAT_DELAY) {
                btn.lastAction = now;

                holdRepeatCount++;   // ?? increment here

                performButtonAction(btn.pin);
                DEBUG_PRINTF("Button %d REPEAT action\n", btn.pin);
            }
        }
    }
    else
    {
        // Only execute release logic if button was actually pressed
        if (btn.pressStart != 0)
        {
            DEBUG_PRINTF("Button %d released\n", btn.pin);

            btn.pressStart = 0;
            btn.repeating = false;

            holdRepeatCount = 0;
            smartStepMode = false;

            DEBUG_PRINTLN("holdRepeatCount reset to 0 and smartStepMode disabled");
        }
    }
}

void performButtonAction(int pin)
{
    switch (pin)
    {
        case FREQ_UP_PIN:
            processFrequencyCommand(true, 1, INPUT_BUTTON);
            break;

        case FREQ_DOWN_PIN:
            processFrequencyCommand(false, 1, INPUT_BUTTON);
            break;

        case DUTY_UP_PIN:
            DEBUG_PRINTLN("Duty UP");
            changeDuty(true, ledcChannels);
            break;

        case DUTY_DOWN_PIN:
            DEBUG_PRINTLN("Duty DOWN");
            changeDuty(false, ledcChannels);
            break;
    }
}

// ================= CHANGE FREQ =================
void changeFrequency(unsigned long frequency, ledc_channel_t channel) {
    DEBUG_PRINTF("\n=== FREQUENCY DEBUG START ===\n");
    DEBUG_PRINTF("Channel: %d\n", channel);
    DEBUG_PRINTF("New Frequency: %lu Hz\n", frequency);
    esp_err_t err = ledc_set_freq(SPEED_MODE, (channel == CHANNEL_0)? TIMER0_SEL : TIMER1_SEL, frequency);
}

// ================= CHANGE DUTY =================
void changeDuty(bool increase, ledc_channel_t channel)
{
    float &currentDuty = (channel == CHANNEL_0) ? duty[0] : duty[1];

    float oldDuty = currentDuty;

    if (increase) {
        currentDuty = min(currentDuty + DUTY_STEP, 1.0f);
    } else {
        currentDuty = max(currentDuty - DUTY_STEP, 0.0f);
    }

    if (fabs(currentDuty - oldDuty) > 1e-6) {
        DEBUG_PRINTF("Duty change: %.2f -> %.2f\n", oldDuty, currentDuty);
        applyDutyToChannels(channel);
        printStatus();
    }
}

// ================= LEDC CONFIG =================
void configureLedcTimer(unsigned long frequencyHz, ledc_channel_t channel)
{
    DEBUG_PRINTF("Configuring timer: %lu Hz\n", frequencyHz);

    ledc_clk_cfg_t selected_clk;
    uint32_t src_clk_freq;

    // ---------- Clock Selection ----------
    if (frequencyHz < 976)
    {
        selected_clk = LEDC_USE_REF_TICK;   // 1 MHz
        src_clk_freq = 1000000;
        DEBUG_PRINTLN("Using REF_TICK (1 MHz)");
    }
    else
    {
        selected_clk = LEDC_USE_APB_CLK;    // 80 MHz
        src_clk_freq = 80000000;
        DEBUG_PRINTLN("Using APB_CLK (80 MHz)");
    }

    // ---------- Configure Timer ----------
    ledc_timer_config_t ledc_timer = {};
    ledc_timer.speed_mode = SPEED_MODE;
    ledc_timer.timer_num = (channel == CHANNEL_0) ? TIMER0_SEL : TIMER1_SEL;
    ledc_timer.freq_hz = frequencyHz;
    ledc_timer.duty_resolution = LEDC_RES;//(ledc_timer_bit_t)duty_resolution;
    ledc_timer.clk_cfg = selected_clk;

    esp_err_t err = ledc_timer_config(&ledc_timer);

    if (err != ESP_OK)
    {
        DEBUG_PRINTF("Timer config failed: %d\n", err);
        return;
    }

    DEBUG_PRINTLN("Timer configured successfully");
}

void configureLedcChannels(ledc_channel_t channel) {

    if (!ledc_get_freq(SPEED_MODE, TIMER0_SEL)) {
        DEBUG_PRINTLN("Timer not active. Skipping channel config.");
        return;
    }


    DEBUG_PRINTLN("Configuring LEDC channels");

    ledc_channel_config_t ch = {};

    ch.channel = (channel == CHANNEL_0)? CHANNEL_0 : CHANNEL_1;
    ch.duty = 0;
    ch.gpio_num = (channel == CHANNEL_0)? PWM1_PIN : PWM2_PIN;
    ch.speed_mode = SPEED_MODE;
    ch.hpoint = 0;
    ch.timer_sel = (channel == CHANNEL_0)? TIMER0_SEL : TIMER1_SEL;

    DEBUG_PRINTF("Channel %d -> GPIO %d\n", ch.channel, ch.gpio_num);
    esp_err_t err = ledc_channel_config(&ch);
    DEBUG_PRINTF("Channel %d config result: %d\n", ch.channel, err);
    applyDutyToChannels(channel);
}

// ================= DUTY APPLY =================
void applyDutyToChannels(ledc_channel_t channel) {
  int maxVal = (1 << PWM_RESOLUTION) - 1;
  int dutyVal = constrain((int)round((channel == CHANNEL_0 ? duty[0] : duty[1]) * maxVal), 0, maxVal);

  DEBUG_PRINTF("Applying duty: %.2f -> raw %d (max %d)\n",
               channel == CHANNEL_0 ? duty[0] : duty[1], dutyVal, maxVal);

  ledc_set_duty(SPEED_MODE, channel, dutyVal);
  ledc_update_duty(SPEED_MODE, channel);
}

void processFrequencyCommand(bool increase, uint32_t requestedStep, InputSource source)
{
    unsigned long current = freqHz[ledcChannels];
    unsigned long step = requestedStep;

    // ================= SMART MODE (ONLY FOR BUTTON) =================
    if (source == INPUT_BUTTON)
    {
        if (holdRepeatCount >= 10)
            smartStepMode = true;

        if (!smartStepMode)
        {
            step = 1;
        }
        else
        {
            if (current < 100)
                step = 10;
            else if (current < 1000)
                step = 100;
            else
                step = 1000;
        }
    }

    // ================= CALCULATE NEW FREQUENCY =================
    unsigned long newFreq;

    if (increase)
        newFreq = current + step;
    else
        newFreq = (current > step) ? current - step : MIN_FREQ;

    if (newFreq > MAX_FREQ)
        newFreq = MAX_FREQ;

    if (newFreq < MIN_FREQ)
        newFreq = MIN_FREQ;

    // ================= 976 Hz TIMER CROSSING HANDLING =================
    if ((current <= 976 && newFreq > 976) ||
        (current > 976 && newFreq <= 976))
    {
        configureLedcTimer(newFreq, ledcChannels);
    }

    // ================= APPLY =================
    freqHz[ledcChannels] = newFreq;
    changeFrequency(newFreq, ledcChannels);

    DEBUG_PRINTF("SRC:%s | Step:%lu | New:%lu | Repeat:%u\n",
        (source == INPUT_BUTTON) ? "BTN" : "WEB",
        step,
        newFreq,
        holdRepeatCount);
}

// ================= STATUS =================
void printStatus() {
    DEBUG_PRINTF("STATUS | Freq1: %lu Hz | Freq2: %lu Hz | Duty1: %.0f%% | Duty2: %.0f%% | Mode: %s (step %lu Hz)\n",
                freqHz[0],
                freqHz[1],
                duty[0] * 100.0f,
                duty[1] * 100.0f,
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
button { width:120px; height: 50px; font-size: 18px; margin: 10px; }
.value { font-size:22px; margin: 10px; }
.toggle { width: 50px;height: 25px;appearance: none;background: #ccc;border-radius: 25px;position: relative;cursor: pointer;outline: none;transition: 0.3s;}
.toggle:checked { background: #4CAF50;}
.toggle::before { content: "";width: 21px;height: 21px;background: white;position: absolute;top: 2px;left: 2px;border-radius: 50%;transition: 0.3s;}
.toggle:checked::before { transform: translateX(25px);}
h3 { font-size: 24px; }
.channel-label {font-size: 22px;margin: 12px;display: flex;justify-content: center;align-items: center;gap: 15px;}
</style>
</head>
<body>
<h2>ESP32 PWM Controller</h2>
<div class="value">Frequency Channel 0: <span id="freq0">0</span> Hz</div>
<div class="value">Duty Channel 0: <span id="duty0">0</span> %</div>
<div class="value">Frequency Channel 1: <span id="freq1">0</span> Hz</div>
<div class="value">Duty Channel 1: <span id="duty1">0</span> %</div>
<div class="value">Selected Channel: <span id="channel">0</span></div>
<h3>Select Active Channel</h3>
<div class="channel-label">Channel 0<input type="checkbox" id="chSelect" class="toggle selectSwitch">Channel 1</div>
<h3>Channel Enable</h3>
<div class="channel-label">Channel 0<input type="checkbox" class="toggle enableSwitch" data-ch="0"></div><br>
<div class="channel-label">Channel 1<input type="checkbox" class="toggle enableSwitch" data-ch="1"></div>
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
document.getElementById("chSelect").addEventListener("change", function() {let ch = this.checked ? 1 : 0;fetch("/selectChannel?ch=" + ch);document.getElementById('channel').innerText = ch;});
document.querySelectorAll(".enableSwitch").forEach(el => {
    el.addEventListener("change", function() {
        let ch = this.dataset.ch;
        let state = this.checked ? 1 : 0;
        fetch("/channelEnable?ch=" + ch + "&state=" + state);
    });
});
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
}).catch(error => console.error('Error fetching status:', error));
}
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
    DEBUG_PRINTLN("Duty Cycle UP Jquery");
}

void handleDutyDown() {
  changeDuty(false, ledcChannels);
  server.send(200, "text/plain", "OK");
  DEBUG_PRINTLN("Duty Cycle Down Jquery");
}

void handleStatus() {
    String json = "{";

    json += "\"freq0\":" + String(freqHz[0]) + ",";
    json += "\"duty0\":" + String(duty[0] * 100.0f, 1) + ",";
    json += "\"freq1\":" + String(freqHz[1]) + ",";
    json += "\"duty1\":" + String(duty[1] * 100.0f, 1) + ",";
    json += "\"channel\":" + String((ledcChannels == CHANNEL_0) ? 0 : 1) + ",";
    json += "\"ch0\":" + String(channelEnabled[0] ? "true" : "false") + ",";
    json += "\"ch1\":" + String(channelEnabled[1] ? "true" : "false");

    json += "}";
    DEBUG_PRINTLN("Status JSON: " + json);

    server.send(200, "application/json", json);
}

void handleNotFound() {
    Serial.println("Unknown URL hit");
    server.send(404, "text/plain", "Not found");
}

void handleChannelEnable()
{
    if (!server.hasArg("ch") || !server.hasArg("state"))
    {
        server.send(400, "text/plain", "Bad Request");
        return;
    }

    int ch = server.arg("ch").toInt();
    int state = server.arg("state").toInt();

    if (ch < 0 || ch > 1)
    {
        server.send(400, "text/plain", "Invalid Channel");
        return;
    }

    channelEnabled[ch] = (state == 1);

    if (ch == 0)
        digitalWrite(CHANNEL_0_ENABLE_PIN, channelEnabled[0] ? HIGH : LOW);
    else
        digitalWrite(CHANNEL_1_ENABLE_PIN, channelEnabled[1] ? HIGH : LOW);

    server.send(200, "text/plain", "OK");
    DEBUG_PRINTLN("Channel " + String(ch) + " enable state: " + (channelEnabled[ch] ? "ENABLED" : "DISABLED"));
}

void handleSelectChannel()
{
    if (!server.hasArg("ch"))
    {
        server.send(400, "text/plain", "Bad Request");
        return;
    }

    int ch = server.arg("ch").toInt();

    if (ch < 0 || ch > 1)
    {
        server.send(400, "text/plain", "Invalid Channel");
        return;
    }

    // Update active channel
    ledcChannels = (ch == 0) ? CHANNEL_0 : CHANNEL_1;

    server.send(200, "text/plain", "OK");
    DEBUG_PRINTLN("Selected active channel: " + String(ch));
}