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

#define SUBSCRIBE_TEST_FIELD_6 // else subscribe to label field 5 (LABEL)
#define PUBLISH_TO_TEST_FIELD  // for testing callback

// Defined in "secrets.h" and "mqtt secrets"
const char wifi_ssid[] = WIFI_SSID;
const char wifi_password[] = WIFI_PASSWORD;
const char thingspeak_client[] = SECRET_MQTT_CLIENT_ID;
const char thingspeak_user[] = SECRET_MQTT_USERNAME;
const char thingspeak_pass[] = SECRET_MQTT_PASSWORD;

const int MQTT_RETRY_DELAY_S = 1;
const int MQTT_ENC = 1883; //TCP, no encryption https://www.mathworks.com/help/thingspeak/mqtt-basics.html 

const int vibration_motor_pin = GPIO_NUM_25; //Analog pin, higher means higher motor strength

const uint8_t min_vibration = 100; //Leave it on as a warning
const uint8_t min_neopixel_brightness = 5;

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

void show_cross(uint8_t brightness, uint8_t r, uint8_t g, uint8_t b);
void show_tick(uint8_t brightness, uint8_t r, uint8_t g, uint8_t b);

void power_vibration_motor(uint8_t strength);

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

    pinMode(vibration_motor_pin, OUTPUT);
    power_vibration_motor(0);

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

    uint8_t brightness;
    uint8_t motor_strength;

    #ifdef PUBLISH_TO_TEST_FIELD
    if(millis() - last_pub_time >= 2000)
    {
        thingspeak_publish(SensorFields::TestField, last_pub);
        last_pub = !last_pub; //Swtich it up every 1s
        last_pub_time = millis();
    }
    #endif //PUBLISH_TO_TEST_FIELD

    //Neopixel LEDs give an idea of recency of when the payload from the topic was recieved
    //after subscribing to it
    pixels.clear();

    uint32_t recency = millis() - g_label_received.timestamp;
    if(recency < 1000)
    {
        recency = 1000 - recency; // Recency higher -> event happened more recently
        brightness = 255 - map(recency,
            0, 1000,
            min_neopixel_brightness, 255);
        
        motor_strength = 255 - map(recency,
            0, 1000,
            min_vibration, 255);

        if(g_label_received.label == ML_Label::FAILURE)
        {
            show_cross(brightness, 255, 0, 0); //Red cross
            power_vibration_motor(motor_strength);
        }
        else //(g_label_received.label == ML_Label::NORMAL)
        {
            show_tick(brightness, 0, 255, 0); ///Green Tick
            power_vibration_motor(false);
        }
    }
    else
    {
        //Brightness if no callback called for 1000ms or more
        //allows the viewer to see last state, and also know it might be "stale"

        if(g_label_received.label == ML_Label::FAILURE)
        {
            show_cross(min_neopixel_brightness, 255, 0, 0); //Red cross
            power_vibration_motor(min_vibration);
        }
        else //(g_label_received.label == ML_Label::NORMAL)
        {
            show_tick(min_neopixel_brightness, 0, 255, 0); ///Green Tick
            power_vibration_motor(false);
        }
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

// MQTT / thingspeak

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
            #ifdef SUBSCRIBE_TEST_FIELD_6
            thingspeak_subscribe(SensorFields::TestField);
            #else //SUBSCRIBE_TEST_FIELD_6 not defined, subscribe to LABEL field
            thingspeak_subscribe(SensorFields::Label);
            #endif
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

    const char  test_field_topic[] = "channels/2868666/subscribe/fields/field6";
    const char label_field_topic[] = "channels/2868666/subscribe/fields/field5";
    
    if( (String(topic) == test_field_topic) || (String(topic) == label_field_topic) )
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


// Neopxiel Patterns

/**
 * @brief Need to call show after this. All params are uint8_t, 0 - 255
 * 
 * @param brightness 
 * @param r 
 * @param g 
 * @param b 
 */
void show_tick(uint8_t brightness, uint8_t r, uint8_t g, uint8_t b)
{
    static uint8_t tick_idxs[] = {9, 13, 15, 17, 21};
    static uint8_t length = sizeof(tick_idxs) / sizeof(tick_idxs[0]);

    pixels.setBrightness(brightness);
    
    for(int idx = 0; idx < length; idx++)
    {
        pixels.setPixelColor(tick_idxs[idx], pixels.Color(r, g, b));
    }
}

/**
 * @brief Need to call show after this. All params are uint8_t, 0 - 255
 * 
 * @param brightness 
 * @param r 
 * @param g 
 * @param b 
 */
void show_cross(uint8_t brightness, uint8_t r, uint8_t g, uint8_t b)
{
    static uint8_t cross_idxs[] = {0, 4, 6, 8, 12, 16, 18, 20, 24};
    static uint8_t length = sizeof(cross_idxs) / sizeof(cross_idxs[0]);

    pixels.setBrightness(brightness);
    
    for(int idx = 0; idx < length; idx++)
    {
        pixels.setPixelColor(cross_idxs[idx], pixels.Color(r, g, b));
    }
}

void power_vibration_motor(uint8_t strength)
{
    analogWrite(vibration_motor_pin, strength);
}