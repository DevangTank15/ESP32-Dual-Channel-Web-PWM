#include <WiFi.h>
#include <HTTPClient.h>
#include <lvgl.h>
#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();

const char* ssid = "Devang1";
const char* password = "my@android@12345";
uint16_t touchCalData[5] = { 275, 3620, 264, 3530, 7 }; 
bool touchInitialized = false;

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[320 * 10];

char *button_name[4] = {"Freq+","Freq-","Duty+","Duty-"};

lv_obj_t *freq0_label;

lv_obj_t *duty0_label;
lv_obj_t *freq1_label;
lv_obj_t *duty1_label;
lv_obj_t *channel_label;

static lv_indev_t * indev_touch;

unsigned long lastUpdate = 0;

unsigned int frequency [2] = {0, 0};
unsigned int dutyCycle [2] = {0, 0};
unsigned int channel = 0;
uint16_t steps = 1;

void connect_wifi()
{
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid,password);

    Serial.print("Connecting WiFi");

    while(WiFi.status()!=WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }

    Serial.println("WiFi connected");
}


void init_touch()
{
    // Set rotation same as display
//    tft.setRotation(1);

    // Apply calibration
    tft.setTouch(touchCalData);

    touchInitialized = true;

//    lv_tick_set_cb(millis);

    Serial.println("Touch initialized");
}

void init_touch_driver()
{
    static lv_indev_drv_t indev_drv;

    lv_indev_drv_init(&indev_drv);

    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touch_read;

    indev_touch = lv_indev_drv_register(&indev_drv);
    lv_indev_enable(indev_touch, true);

    Serial.println("LVGL touch driver ready");
}

void my_disp_flush(lv_disp_drv_t *disp,const lv_area_t *area,lv_color_t *color_p)
{
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;

    tft.startWrite();
    tft.setAddrWindow(area->x1,area->y1,w,h);
    tft.pushColors((uint16_t *)&color_p->full,w*h,true);
    tft.endWrite();

    lv_disp_flush_ready(disp);
}

void my_touch_read(lv_indev_drv_t * indev_driver, lv_indev_data_t * data)
{
    uint16_t x, y, threshold;

    bool touched = tft.getTouch(&x, &y, threshold);

    if(!touched)
    {
        data->state = LV_INDEV_STATE_REL;
    }
    else
    {
        Serial.println("Touch detected at (" + String(x) + "," + String(y) + ") with threshold " + String(threshold));

        if(x > 319) x = 319;
        if(y > 239) y = 239;

        data->state = LV_INDEV_STATE_PR;
        data->point.x = x;
        data->point.y = y;
    }
}

void touch_debug(lv_event_t * e)
{
    uint16_t x, y, threshold;
    tft.getTouch(&x, &y, threshold);
    Serial.println("Screen touched at (" + String(x) + "," + String(y) + ") with threshold " + String(threshold));
}

void create_ui()
{
    lv_obj_add_event_cb(lv_scr_act(), touch_debug, LV_EVENT_CLICKED, NULL);

    lv_obj_t *tabview = lv_tabview_create(lv_scr_act(),LV_DIR_TOP,60);  // Create a tabview object with tabs at the top and a tab button height of 60 pixels
    lv_tabview_set_act(tabview, 1, LV_ANIM_OFF);    // Set the second tab (index 1) as active without animation
    lv_obj_set_size(tabview, 320, 240);     // Set size of the tabview to fill the screen
//    lv_tabview_set_anim_time(tabview, 0);  // Set the animation time for tab switching to 30 milliseconds

    lv_obj_t *tab_status = lv_tabview_add_tab(tabview,"Status");    // Create a tab called "Status"
    lv_obj_t *tab_control = lv_tabview_add_tab(tabview,"Control");  // Create a tab called "Control"

    lv_obj_clear_flag(tab_control, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(tab_status, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *content = lv_tabview_get_content(tabview);
//    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    /* Get tab button container */
    lv_obj_t *tab_btns = lv_tabview_get_tab_btns(tabview);

    /* Make full button clickable */
//    lv_obj_add_flag(tab_btns, LV_OBJ_FLAG_CLICKABLE);   //

    /* Expand clickable area */
    lv_obj_set_style_pad_all(tab_btns, 8, LV_PART_ITEMS);   //

    /* Center labels inside button */
    lv_obj_set_style_text_align(tab_btns, LV_TEXT_ALIGN_CENTER, LV_PART_ITEMS);     //

//    lv_obj_add_event_cb(tab_btns, tab_click_event, LV_EVENT_VALUE_CHANGED, tabview);

//    lv_obj_add_flag(tab_btns, LV_OBJ_FLAG_EVENT_BUBBLE);        // Make the tab buttons send events to their parent (the tabview) when clicked

    /* STATUS TAB */

    lv_obj_set_flex_flow(tab_status,LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(tab_status,10,0);

    for (uint8_t channel_decider = 0; channel_decider < 2; channel_decider++)
    {
        for(uint8_t parameter_decider = 0; parameter_decider < 2; parameter_decider++)
        {
            char label_text[14] = {0};
            sprintf(label_text, "%s%d : 0 %s", (parameter_decider == 0 ? "Freq" : "Duty"), channel_decider, (parameter_decider==0 ? "Hz" : "%"));
            lv_obj_t *label = lv_label_create(tab_status);
            lv_label_set_text(label,label_text);
        }
    }

    channel_label = lv_label_create(tab_status);
    lv_label_set_text(channel_label,"Channel : 0");

    /* CONTROL TAB */

    lv_obj_set_flex_flow(tab_control,LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(tab_control,10,0);

    lv_obj_t *ch_label = lv_label_create(tab_control);
    lv_label_set_text(ch_label,"Channel");

    lv_obj_t *ch_dd = lv_dropdown_create(tab_control);
    lv_dropdown_set_options(ch_dd,"Channel 0\nChannel 1");
    lv_dropdown_set_selected(ch_dd,0);

    lv_obj_set_width(ch_dd, 150);

    lv_obj_add_event_cb(ch_dd, channel_event, LV_EVENT_VALUE_CHANGED, NULL);

    Serial.println("Dropdown created");

    for ( uint8_t channel_number = 0; channel_number < 2; channel_number++ )
    {
        char label_text[20] = {0};
        memset(label_text, 0, sizeof(label_text));
        sprintf(label_text, "Channel %d Enable", channel_number);
        lv_obj_t *enable_label = lv_label_create(tab_control);
        lv_label_set_text(enable_label,label_text);

        lv_obj_t *enable_sw = lv_switch_create(tab_control);

        lv_obj_add_event_cb(enable_sw, enable_event, LV_EVENT_VALUE_CHANGED, (void*)(uintptr_t)channel_number);
    }

    Serial.println("Switches created");
    lv_obj_t *btn;
    lv_obj_t *label;

    for( uint8_t parameter_process_decider = 0; parameter_process_decider < 4; parameter_process_decider++ )
    {
        btn = lv_btn_create(tab_control);
        label = lv_label_create(btn);
        lv_label_set_text(label,button_name[parameter_process_decider]);
        lv_obj_center(label);
        lv_obj_add_event_cb(btn, control_event, LV_EVENT_CLICKED, (void*)button_name[parameter_process_decider]);
        lv_obj_set_size(btn, 140, 50);
    }

    Serial.println("Buttons created");
}

/* void tab_click_event(lv_event_t * e)
{
    lv_obj_t *tabview = (lv_obj_t *)lv_event_get_user_data(e);
    lv_obj_t *btns = lv_event_get_target(e);

    uint32_t id = lv_btnmatrix_get_selected_btn(btns);

    if(id != LV_BTNMATRIX_BTN_NONE)
    {
        lv_tabview_set_act(tabview,id,LV_ANIM_OFF);
    }
} */

void channel_event(lv_event_t * e)
{
    lv_obj_t *dd = lv_event_get_target(e);
    uint16_t active_channel = lv_dropdown_get_selected(dd);

    Serial.printf("Channel selected: %d\n",active_channel);

    send_to_master();
}

void enable_event(lv_event_t * e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    uint8_t channel_to_enable = (uint8_t)(uintptr_t)lv_event_get_user_data(e);

    bool enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);

    Serial.printf("Channel %d %s\n",channel_to_enable,enabled ? "enabled" : "disabled");

    send_to_master();
}

void send_to_master()
{
    if(WiFi.status() == WL_CONNECTED)
    {
        HTTPClient http;

        String url = "http://192.168.4.1/update?";
        url += "freq=" + String(frequency[0]);
        url += "&duty=" + String(dutyCycle[0]);
        url += "&freq1=" + String(frequency[1]);
        url += "&duty1=" + String(dutyCycle[1]);
        url += "&channel=" + String(channel);

        http.begin(url);
        int httpCode = http.GET();

        Serial.println(httpCode);

        http.end();
    }
}

void control_event(lv_event_t * e)
{
    const char * cmd = (const char *)lv_event_get_user_data(e);

    for(uint8_t i = 0; i < 4; i++)
    {
        if(strcmp(cmd, button_name[i]) == 0)
        {
            if (i<2) {
                frequency[channel] += (i==0 ? steps : -steps);
            } else {
                dutyCycle[channel] += (i==2 ? 5 : -5);
            }
        }
    }

    Serial.printf("Freq: %d  Duty: %d\n",frequency[channel],dutyCycle[channel]);

    send_to_master();
}

void setup()
{
    Serial.begin(115200);

    connect_wifi();

    tft.begin();
    tft.setRotation(1);                                     // Set the display rotation to landscape mode (1 corresponds to landscape orientation)

    pinMode(TFT_BL,OUTPUT);
    digitalWrite(TFT_BL,HIGH);

    init_touch();      // initialize touch controller

    lv_init();

    lv_disp_draw_buf_init(&draw_buf,buf,NULL,320*10);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);                        // Initialize the display driver structure with default values

    disp_drv.hor_res = 320;
    disp_drv.ver_res = 240;

    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;

    lv_disp_drv_register(&disp_drv);                    // Register the display driver with LVGL, allowing it to use the specified flush callback and draw buffer for rendering graphics on the display

    init_touch_driver();   // register LVGL touch

    create_ui();
}

void loop()
{
    lv_timer_handler();
    lv_tick_inc(5);
    delay(5);
}