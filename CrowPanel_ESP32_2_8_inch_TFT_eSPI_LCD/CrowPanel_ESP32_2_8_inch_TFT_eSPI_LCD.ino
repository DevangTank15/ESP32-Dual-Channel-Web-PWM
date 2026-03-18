#include <WiFi.h>
const char* ssid = "Devang1";
const char* password = "my@android@12345";
String serverIP = "http://192.168.4.1";
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

void setup()
{
    Serial.begin(115200);

    connect_wifi();
}

void loop()
{
}