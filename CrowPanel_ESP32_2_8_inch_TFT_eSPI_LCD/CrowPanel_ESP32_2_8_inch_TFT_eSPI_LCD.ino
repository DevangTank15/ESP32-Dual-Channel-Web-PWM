#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#include <lvgl.h>
#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[320 * 10];

lv_obj_t *freq0_label;
lv_obj_t *freq1_label;
lv_obj_t *duty0_label;
lv_obj_t *duty1_label;
lv_obj_t *channel_label;

lv_obj_t *channel_switch;
lv_obj_t *enable0_switch;
lv_obj_t *enable1_switch;
lv_obj_t *step_dropdown;

const char* ssid = "Devang1";
const char* password = "my@android@12345";
String serverIP = "http://192.168.4.1";

unsigned long lastUpdate = 0;

void connect_wifi()
{
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);
    delay(1000);
    WiFi.begin(ssid,password);

    while(WiFi.status()!=WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }

    Serial.println("WiFi connected");
}

void sendCommand(String url)
{
    HTTPClient http;

    http.begin(serverIP + url);
    http.GET();

    http.end();
}

void update_status()
{
    HTTPClient http;

    http.begin(serverIP + "/status");

    int code = http.GET();

    if(code==200)
    {
        String payload = http.getString();

        StaticJsonDocument<256> doc;
        deserializeJson(doc,payload);

        int freq0 = doc["freq0"];
        int freq1 = doc["freq1"];
        float duty0 = doc["duty0"];
        float duty1 = doc["duty1"];

        lv_label_set_text_fmt(freq0_label,"Frequency Ch0: %d Hz",freq0);
        lv_label_set_text_fmt(freq1_label,"Frequency Ch1: %d Hz",freq1);

        lv_label_set_text_fmt(duty0_label,"Duty Ch0: %.1f %%",duty0);
        lv_label_set_text_fmt(duty1_label,"Duty Ch1: %.1f %%",duty1);
    }

    http.end();
}

static void freq_up_event(lv_event_t * e)
{
    char stepStr[8];

    lv_dropdown_get_selected_str(step_dropdown,stepStr,sizeof(stepStr));

    sendCommand("/frequp?step=" + String(stepStr));
}

static void freq_down_event(lv_event_t * e)
{
    char stepStr[8];

    lv_dropdown_get_selected_str(step_dropdown,stepStr,sizeof(stepStr));

    sendCommand("/freqdown?step=" + String(stepStr));
}

static void duty_up_event(lv_event_t * e)
{
    sendCommand("/dutyup");
}

static void duty_down_event(lv_event_t * e)
{
    sendCommand("/dutydown");
}

static void select_channel_event(lv_event_t * e)
{
    bool state = lv_obj_has_state(channel_switch,LV_STATE_CHECKED);

    int ch = state ? 1 : 0;

    sendCommand("/selectChannel?ch=" + String(ch));

    lv_label_set_text_fmt(channel_label,"Selected Channel: %d",ch);
}

static void enable_channel_event(lv_event_t * e)
{
    lv_obj_t *sw = lv_event_get_target(e);

    int ch = (int)lv_event_get_user_data(e);

    bool state = lv_obj_has_state(sw,LV_STATE_CHECKED);

    String url = "/channelEnable?ch=" + String(ch) +
                 "&state=" + String(state ? 1 : 0);

    sendCommand(url);
}

void create_ui()
{
    lv_obj_t *title = lv_label_create(lv_scr_act());    // Create a label on the active screen
    lv_label_set_text(title,"ESP32 PWM Controller");    // Set the text of the label
    lv_obj_align(title,LV_ALIGN_TOP_MID,0,5);           // Align the label to the top middle of the screen with an offset of (0, 5)

    freq0_label = lv_label_create(lv_scr_act());
    lv_label_set_text(freq0_label,"Frequency Ch0: 0 Hz");
    lv_obj_align(freq0_label,LV_ALIGN_TOP_LEFT,10,40);

    duty0_label = lv_label_create(lv_scr_act());
    lv_label_set_text(duty0_label,"Duty Ch0: 0 %");
    lv_obj_align(duty0_label,LV_ALIGN_TOP_LEFT,10,60);

    freq1_label = lv_label_create(lv_scr_act());
    lv_label_set_text(freq1_label,"Frequency Ch1: 0 Hz");
    lv_obj_align(freq1_label,LV_ALIGN_TOP_LEFT,10,90);

    duty1_label = lv_label_create(lv_scr_act());
    lv_label_set_text(duty1_label,"Duty Ch1: 0 %");
    lv_obj_align(duty1_label,LV_ALIGN_TOP_LEFT,10,110);

    channel_label = lv_label_create(lv_scr_act());
    lv_label_set_text(channel_label,"Selected Channel: 0");
    lv_obj_align(channel_label,LV_ALIGN_TOP_LEFT,10,140);

    channel_switch = lv_switch_create(lv_scr_act());            
    lv_obj_align(channel_switch,LV_ALIGN_TOP_RIGHT,-20,140);
    lv_obj_add_event_cb(channel_switch,select_channel_event,LV_EVENT_VALUE_CHANGED,NULL);   // Add an event callback to the switch for when its value changes, calling the select_channel_event function

    enable0_switch = lv_switch_create(lv_scr_act());
    lv_obj_align(enable0_switch,LV_ALIGN_TOP_LEFT,10,170);
    lv_obj_add_event_cb(enable0_switch,enable_channel_event,LV_EVENT_VALUE_CHANGED,(void*)0);

    enable1_switch = lv_switch_create(lv_scr_act());
    lv_obj_align(enable1_switch,LV_ALIGN_TOP_LEFT,10,200);
    lv_obj_add_event_cb(enable1_switch,enable_channel_event,LV_EVENT_VALUE_CHANGED,(void*)1);

    lv_obj_t *freqUp = lv_btn_create(lv_scr_act());
    lv_obj_align(freqUp,LV_ALIGN_BOTTOM_LEFT,10,-60);
    lv_obj_add_event_cb(freqUp,freq_up_event,LV_EVENT_CLICKED,NULL);

    lv_obj_t *label1 = lv_label_create(freqUp);
    lv_label_set_text(label1,"Freq +");
    lv_obj_center(label1);                          // Center the label within the button

    lv_obj_t *freqDown = lv_btn_create(lv_scr_act());
    lv_obj_align(freqDown,LV_ALIGN_BOTTOM_LEFT,110,-60);
    lv_obj_add_event_cb(freqDown,freq_down_event,LV_EVENT_CLICKED,NULL);

    lv_obj_t *label2 = lv_label_create(freqDown);
    lv_label_set_text(label2,"Freq -");
    lv_obj_center(label2);

    step_dropdown = lv_dropdown_create(lv_scr_act());
    lv_dropdown_set_options(step_dropdown,"1\n10\n100\n1000");  // Set the options for the dropdown menu, with each option separated by a newline character
    lv_obj_align(step_dropdown,LV_ALIGN_BOTTOM_LEFT,210,-60);

    lv_obj_t *dutyUp = lv_btn_create(lv_scr_act());
    lv_obj_align(dutyUp,LV_ALIGN_BOTTOM_LEFT,10,-20);
    lv_obj_add_event_cb(dutyUp,duty_up_event,LV_EVENT_CLICKED,NULL);

    lv_obj_t *label3 = lv_label_create(dutyUp);
    lv_label_set_text(label3,"Duty +");
    lv_obj_center(label3);

    lv_obj_t *dutyDown = lv_btn_create(lv_scr_act());
    lv_obj_align(dutyDown,LV_ALIGN_BOTTOM_LEFT,110,-20);
    lv_obj_add_event_cb(dutyDown,duty_down_event,LV_EVENT_CLICKED,NULL);

    lv_obj_t *label4 = lv_label_create(dutyDown);
    lv_label_set_text(label4,"Duty -");
    lv_obj_center(label4);
}

void my_disp_flush(lv_disp_drv_t *disp,const lv_area_t *area,lv_color_t *color_p)
{
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    tft.startWrite();                                       // Start a new SPI transaction
    tft.setAddrWindow(area->x1,area->y1,w,h);               // Set the address window to the area that needs to be updated
    tft.pushColors((uint16_t *)&color_p->full,w*h,true);    // Push the color data to the display, converting it to 16-bit format and specifying the number of pixels to update
    tft.endWrite();                                         // End the SPI transaction

    lv_disp_flush_ready(disp);
}

void setup()
{
    Serial.begin(115200);

    connect_wifi();

    tft.begin();
    tft.setRotation(1);                                     // Set the display rotation to landscape mode (1 corresponds to landscape orientation)

    pinMode(TFT_BL,OUTPUT);
    digitalWrite(TFT_BL,HIGH);

    lv_init();

    lv_disp_draw_buf_init(&draw_buf,buf,NULL,320*10);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);                        // Initialize the display driver structure with default values

    disp_drv.hor_res = 320;
    disp_drv.ver_res = 240;

    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;

    lv_disp_drv_register(&disp_drv);                    // Register the display driver with LVGL, allowing it to use the specified flush callback and draw buffer for rendering graphics on the display

    create_ui();
}

void loop()
{
    lv_timer_handler();                                 // Call the LVGL timer handler to process any pending tasks or events, such as button presses or screen updates

    if(millis()-lastUpdate>1000)
    {
        update_status();
        lastUpdate = millis();
    }

    delay(5);
}