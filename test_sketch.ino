#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>

// DHT22 Sensor Setup
#define DHTPIN 26        //DHT pin #22
#define DHTTYPE DHT22   
DHT dht(DHTPIN, DHTTYPE);

// MQTT Broker Settings
const char *mqtt_broker = "192.168.4.2";                    // Kiana's laptop's IP

const char *temp_topic = "esp32/temperature";                // Created Topic #1    
const char *humidity_topic = "esp32/humidity";               // Created Topic #2
  
const char *mqtt_username = "master";                         //MQTT login  
const char *mqtt_password = "masterpass";
const int mqtt_port = 1883;                                   //Port

// Initialize Clients
WiFiClient espClient;
PubSubClient client(espClient);

// Callback function
void callback(char *topic, byte *payload, unsigned int length) {
    Serial.print("Message received on ");
    Serial.print(topic);
    Serial.print(": ");
    for (int i = 0; i < length; i++) {
        Serial.print((char) payload[i]);
    }
    Serial.println();
}

void reconnect() {
    while (!client.connected()) {
        String client_id = "esp32-client-" + String(WiFi.macAddress());
        Serial.println("Connecting to MQTT...");
        
        if (client.connect(client_id.c_str(), mqtt_username, mqtt_password)) {
            Serial.println("MQTT Connected!");
            client.subscribe(temp_topic); // Optional: subscribe to receive commands
        } else {
            Serial.print("Failed, rc=");
            Serial.println(client.state());
            delay(5000);
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("\n=== Starting ESP32 with DHT22 ===");
    
    // Initialize DHT sensor
    dht.begin();
    Serial.println("DHT22 initialized");
    
    // Start Access Point
    WiFi.softAP("ESP32_Broker", "12345678");
    Serial.println("Access Point Started");
    Serial.print("AP IP Address: ");
    Serial.println(WiFi.softAPIP());
    
    // Setup MQTT
    client.setServer(mqtt_broker, mqtt_port);
    client.setCallback(callback);
    
    // Initial MQTT connection
    reconnect();
}

void loop() {
    // Maintain MQTT connection
    if (!client.connected()) {
        reconnect();
    }
    client.loop();
    
    // Read and publish sensor data every 10 seconds
    static unsigned long lastRead = 0;
    unsigned long interval = 1000; // 10 seconds
    
    if (millis() - lastRead >= interval) {
        lastRead = millis();
        
        // Read temperature and humidity
        float temperature = dht.readTemperature(); // Celsius (use readTemperature(true) for Fahrenheit)
        float humidity = dht.readHumidity();
        
        // Check if readings are valid
        if (isnan(temperature) || isnan(humidity)) {
            Serial.println("Failed to read from DHT sensor!");
            return;
        }
        
        // Convert to strings
        char tempString[8];
        char humString[8];
        dtostrf(temperature, 1, 2, tempString); // Format: 1 digit before decimal, 2 after
        dtostrf(humidity, 1, 2, humString);
        
        // Publish to MQTT
        if (client.publish(temp_topic, tempString)) {
            Serial.print("✓ Temperature published: ");
            Serial.print(tempString);
            Serial.println("°C");
        } else {
            Serial.println("✗ Failed to publish temperature");
        }
        
        if (client.publish(humidity_topic, humString)) {
            Serial.print("✓ Humidity published: ");
            Serial.print(humString);
            Serial.println("%");
        } else {
            Serial.println("✗ Failed to publish humidity");
        }
    }
}