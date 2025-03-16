/**
 * @file main.cpp
 * @author Samuel Yow
 * @date 2025-03-11
 * @brief 
 * https://www.mathworks.com/help/thingspeak/mqtt-api.html 
 */
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Adafruit_NeoPixel.h>

#include "secrets.h"
#include "actuation_node_1_mqtt_secrets.h"

#define MQT_SEND_DELAY_MS 500 

// Defined in "secrets.h" and "mqtt secrets"
const char wifi_ssid[] = WIFI_SSID;
const char wifi_password[] = WIFI_PASSWORD;
const char thingspeak_client[] = SECRET_MQTT_CLIENT_ID;
const char thingspeak_user[] = SECRET_MQTT_USERNAME;
const char thingspeak_pass[] = SECRET_MQTT_PASSWORD;

const int MQTT_RETRY_DELAY_S = 1;
const int MQTT_ENC = 1883; //TCP, no encryption https://www.mathworks.com/help/thingspeak/mqtt-basics.html 
enum SensorFields{
    Voltage = 1,
    Rotation = 2,
    Pressure = 3,
    Vibration = 4,
    Label = 5,     //Unused, for ML. So should subscribe to it
};

WiFiClient espClient;
PubSubClient mqttClient(espClient);
Adafruit_NeoPixel pixels(25, GPIO_NUM_27, NEO_GRB + NEO_KHZ800); //Specific to M5 Atom

void connectWifi();
void mqttConnect();

bool thingspeak_publish(SensorFields sensor, float value);
bool thingspeak_subscribe(SensorFields sensor);

void thingspeak_callback(char* topic, byte* message, unsigned int length);


uint32_t counter = 0;
uint32_t publish_timer = 0;

void setup() {
    Serial.begin(115200);
    Serial.println("Hello World!");

    pixels.begin();
    pixels.clear();
    pixels.setBrightness(255);
    pixels.show();

    connectWifi();

    mqttClient.setServer("mqtt3.thingspeak.com", 1883); 
    mqttClient.setCallback(thingspeak_callback);
    mqttConnect();
}

void loop() {
// Call the loop to maintain connection to the server.
    mqttConnect();
    mqttClient.loop();

    if(millis() - publish_timer >= 1000)
    {
        uint8_t res = counter%4;
        if(res == 0)       Serial.println(thingspeak_publish(SensorFields::Voltage, counter));
        else if (res == 1) Serial.println(thingspeak_publish(SensorFields::Rotation, counter));
        else if (res == 2) Serial.println(thingspeak_publish(SensorFields::Pressure, counter));
        else if (res == 3) Serial.println(thingspeak_publish(SensorFields::Vibration, counter));
        counter += 1;
        publish_timer = millis();
    }

}


/**
 * @brief Connect to WiFi
 * 
 */
void connectWifi()
{
    Serial.print( "Connecting to Wi-Fi..." );
    // Loop until WiFi connection is successful

    WiFi.begin(wifi_ssid, wifi_password);

    while ( WiFi.status() != WL_CONNECTED ) {
        Serial.print(".");
        delay(500);
    }
    Serial.println( "Connected to Wi-Fi: " + String(WiFi.localIP()) );
}

/**
 * @brief Connect to MQTT server.
 * 
 */
void mqttConnect() {
    // Loop until connected.
    while ( !mqttClient.connected() )
    {
        if (mqttClient.connect(thingspeak_client, thingspeak_user, thingspeak_pass)) 
        {
            Serial.println("MQTT successful." );
            thingspeak_subscribe(SensorFields::Pressure);
            thingspeak_subscribe(SensorFields::Voltage);
            thingspeak_subscribe(SensorFields::Vibration);
            thingspeak_subscribe(SensorFields::Rotation);
        }
        else {
            Serial.print("MQTT connection failed, rc = " );
            Serial.print(mqttClient.state()); //https://pubsubclient.knolleary.net/api#state 
            delay(MQTT_RETRY_DELAY_S*1000);
        }
    }
}

/**
 * @brief Publish to thingspeak
 * 
 * @param sensor 
 * @param value 
 * @return true 
 * @return false 
 */
bool thingspeak_publish(SensorFields sensor, float value)
{
    static char topic_buffer[50];
    static char payload[10];

    //2868666 is the channel ID
    snprintf(topic_buffer, sizeof(topic_buffer),
        "channels/2868666/publish/fields/field%i",
        sensor);

    snprintf(payload, sizeof(payload),
        "%.3f",
        value);

    Serial.println(topic_buffer);
    Serial.println(payload);

    return mqttClient.publish(topic_buffer, payload);
}

/**
 * @brief Function runs when a message to a subscribed topic is recieved
 * 
 * @param topic 
 * @param message 
 * @param length 
 */
void thingspeak_callback(char* topic, byte* message, unsigned int length) {
    // Serial.print("Message arrived on topic: ");
    // Serial.println(topic);

    pixels.clear();
    
    if(String(topic) == "channels/2868666/subscribe/fields/field1")
    {
        pixels.setPixelColor(1, pixels.Color(200,0,0));
    } 
    else if(String(topic) == "channels/2868666/subscribe/fields/field2")
    {
        pixels.setPixelColor(2, pixels.Color(0,200,0));
    } 
    else if(String(topic) == "channels/2868666/subscribe/fields/field3")
    {
        pixels.setPixelColor(3, pixels.Color(0,0,200));
    } 
    else if(String(topic) == "channels/2868666/subscribe/fields/field4")
    {
        pixels.setPixelColor(4, pixels.Color(50,50,50));
    } 
    else{
        Serial.println(topic);
    }

    pixels.show();
}

/**
 * @brief Subscribe to a certain field
 * 
 * @param sensor 
 * @return true 
 * @return false 
 */
bool thingspeak_subscribe(SensorFields sensor)
{
    static char subscribe_topic[200];

    //2868666 is the channel ID
    snprintf(subscribe_topic, sizeof(subscribe_topic),
        "channels/2868666/subscribe/fields/field%i",
        sensor);

    Serial.println(subscribe_topic);

    return mqttClient.subscribe(subscribe_topic);
}