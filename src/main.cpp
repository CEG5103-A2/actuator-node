/**
 * @file main.cpp
 * @author Samuel Yow
 * @date 2025-03-11
 * @brief CEG5103 Actuation Node
 * 
 * Purpose: subscribe to channel 2868666, field 5, the label field and perform actuation depending on MQTT results recevied 
 * 
 */
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Adafruit_NeoPixel.h>

#include "secrets.h"
#include "actuation_node_1_mqtt_secrets.h"

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
    Label = 5,
    TestField = 6,
};

enum ML_Label{
    NORMAL = 0,
    FAILURE = 1,
};

struct Label_Received{
    ML_Label label;
    uint32_t timestamp;
};

WiFiClient espClient;
PubSubClient mqttClient(espClient);
Adafruit_NeoPixel pixels(25, GPIO_NUM_27, NEO_GRB + NEO_KHZ800); //M5 Atom Matrix

void connectWifi();
void mqttConnect();

bool thingspeak_subscribe(SensorFields sensor);
bool thingspeak_publish(SensorFields sensor, int value);
void thingspeak_callback(char* topic, byte* message, unsigned int length);

struct Label_Received g_label_received;

uint32_t last_pub_time = millis();
bool last_pub = 0;

void setup() {
    Serial.begin(115200);
    Serial.println("Hello World!");

    WiFi.begin(wifi_ssid, wifi_password);

    pixels.begin();
    pixels.clear();
    pixels.setBrightness(255);
    pixels.show();

    connectWifi();

    //Initialize as normal operation at the start
    g_label_received.label = ML_Label::NORMAL;
    g_label_received.timestamp = 0;

    mqttClient.setServer("mqtt3.thingspeak.com", MQTT_ENC); 
    mqttClient.setCallback(thingspeak_callback);
    mqttConnect();
}

void loop() {
// Call the loop to maintain connection to the server.
    connectWifi();
    mqttConnect();
    mqttClient.loop();

    if(millis() - last_pub_time >= 1000)
    {
        thingspeak_publish(SensorFields::TestField, last_pub);
        last_pub = !last_pub; //Swtich it up every 1s
        last_pub_time = millis();
    }

    if(g_label_received.label == ML_Label::FAILURE)
    {
        pixels.setPixelColor(0, pixels.Color(255, 0, 0));
    }
    else
    {
        pixels.setPixelColor(0, pixels.Color(0, 255, 0));
    }

    //Show recency, turn off indicator if no recv after 1s
    if(millis() - g_label_received.timestamp>=1000)
    {
        pixels.setPixelColor(0, pixels.Color(0, 0, 0));
    }

    pixels.show();

}


/**
 * @brief Connect to WiFi
 * 
 */
void connectWifi()
{
    if (WiFi.status() == WL_CONNECTED) return;
    else
    {
        Serial.print("Connecting to Wi-Fi...");
        while (WiFi.status() != WL_CONNECTED) {
            // Loop until WiFi connection is successful
            Serial.print(".");
            delay(500);
        }
        Serial.println("Connected");
    }
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
            thingspeak_subscribe(SensorFields::TestField);
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
bool thingspeak_publish(SensorFields sensor, int value)
{
    static char topic_buffer[50];
    static char payload[10];

    //2868666 is the channel ID
    snprintf(topic_buffer, sizeof(topic_buffer),
        "channels/2868666/publish/fields/field%i",
        sensor);

    snprintf(payload, sizeof(payload),
        "%i",
        value);

    // Serial.println(topic_buffer);
    // Serial.println(payload);

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
    
    if(String(topic) == "channels/2868666/subscribe/fields/field6")
    {
        if((message[0] == '1') && (length == 1))      g_label_received.label = ML_Label::FAILURE;
        else if((message[0] == '0') && (length == 1)) g_label_received.label = ML_Label::NORMAL;
        else
        {
            Serial.print("Invalid message: ");
            char msg[length + 1];
            memcpy(msg, message, length);
            msg[length] = '\0'; //Null termination is at pos "length"
            Serial.println(msg);
        }

        g_label_received.timestamp = millis();

    }
    else
    {
        Serial.println(topic);
    }

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