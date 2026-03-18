#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
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
		
		Serial.println("Frequency 0 = " + String(freq0) + ",Duty Cycle 0 = " + String(duty0));
		Serial.println("Frequency 1 = " + String(freq1) + ",Duty Cycle 1 = " + String(duty1));
    }
	http.end();
}
void setup()
{
    Serial.begin(115200);

    connect_wifi();
}

void loop()
{
    if(millis()-lastUpdate>1000)
    {
        update_status();
        lastUpdate = millis();
    }

    delay(5);
}