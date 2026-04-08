#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <time.h>
#include <sys/time.h>

//Time
uint64_t latencyTimestampMs = 0;
bool timestampPublished = false;

// WiFi Settings
const char *ssid = "Plymouth";         // Same network as MQTT broker
const char *password = "rwee2763";


char Pressuremsg[10];
char Flowmsg[10];

void ConnectWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi Connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
}

// MQTT Broker Settings
const char *mqtt_broker = "10.229.170.200"; // Remove the trailing space!

const char *topic_1 = "esp32/pressure";
const char *topic_2 = "esp32/flow";
const char *topic_3 = "esp32/pump";
const char *topic_4 = "esp32/top_valve";
const char *topic_5 = "esp32/bottom_valve";
const char *topic_6 = "system_ready";
const char *topic_7 = "system_status";
const char *topic_8 = "system_pause";
const char *topic_9 = "block_threshold"; //displays a horizontal line on the dashboard
const char *topic_10 = "leak_threshold"; //displays a horizontal line on the dashboard
const char *topic_11 = "esp32/latency_test"; //displays a horizontal line on the dashboard


const char *mqtt_username = "master";
const char *mqtt_password = "masterpass";
const int mqtt_port = 1883; 

// Initialize Clients
WiFiClient espClient; // Changed from WiFiClientSecure
PubSubClient client(espClient);

unsigned long lastFlowUpdate = 0;
unsigned long lastPressureRead = 0;
const unsigned long pressureInterval = 500; // 0.5 seconds


  // ESP32 pins
// Mosfet
const int pump = 32;
const int top_valve = 33;
const int bottom_valve = 25;
const int tank_strip = 26;

bool SyncTimeFromNTP() {
    // UTC
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");

    struct timeval tv;
    Serial.print("Syncing time from NTP");

    for (int i = 0; i < 30; i++) {
        gettimeofday(&tv, NULL);

        // If time is valid, tv_sec will be a large Unix timestamp
        if (tv.tv_sec > 1700000000) {
            Serial.println("\nTime synced!");
            return true;
        }

        Serial.print(".");
        delay(500);
    }

    Serial.println("\nFailed to sync time");
    return false;
}

uint64_t GetEpochMs() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return ((uint64_t)tv.tv_sec * 1000ULL) + ((uint64_t)tv.tv_usec / 1000ULL);
}

void PublishLatencyTimestampOnce() {
    if (timestampPublished) return;

    latencyTimestampMs = GetEpochMs();

    char TimeStamp[32];
    snprintf(TimeStamp, sizeof(TimeStamp), "%llu",
             (unsigned long long)latencyTimestampMs);

    client.publish(topic_11, TimeStamp);
    Serial.print("Published latency timestamp: ");
    Serial.println(TimeStamp);

    timestampPublished = true;
}


void OpenTopValve() {
  digitalWrite(top_valve, HIGH);
  //client.publish(topic_4, "OPEN");
}

void OpenBottomValve() {
  digitalWrite(bottom_valve, HIGH);
  client.publish(topic_5, "OPEN");
}

void TurnOnTankStrip() {
  ledcWrite(tank_strip, 255);   // full brightness
}

void TurnOffPump() {
  digitalWrite(pump, LOW);
}

void CloseTopValve() {
  digitalWrite(top_valve, LOW);
  //client.publish(topic_4, "CLOSE");
}

void CloseBottomValve() {
  digitalWrite(bottom_valve, LOW);
  client.publish(topic_5, "CLOSE");
}

void TurnOffTankStrip() {
  ledcWrite(tank_strip, 0);   // full brightness
}

void TurnOnPump() {
    if (digitalRead(top_valve) == LOW && digitalRead(bottom_valve) == LOW) {
        displayStatus("Cannot turn", "on Pump");                                   //Protection for Pump
    } else {
        digitalWrite(pump, HIGH);
    }
}


const int pump_enable = 19;
const int top_enable = 18;
const int bottom_enable = 17;
const int ready_enable = 16;



bool isPumpEnableOn() { return digitalRead(pump_enable) == HIGH; }
bool isTopEnableOn() { return digitalRead(top_enable) == HIGH; }
bool isBottomEnableOn() { return digitalRead(bottom_enable) == HIGH; }
bool isReadyEnableOn() { return digitalRead(ready_enable) == HIGH; }


//Sensors
const int pressureSensorPin = 35;

const int flowSensorPin = 34;
volatile int pulseCount = 0;
float flowRate = 0;

void IRAM_ATTR countPulse() {
  pulseCount++;
}


float getPressure() {
    int rawPressure = analogRead(pressureSensorPin);
    float voltage = (rawPressure / 4095.0) * 3.3;
    float PSI = (voltage / 3.3) * 5.0;
    return PSI;
}



//OLED screen
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R2, U8X8_PIN_NONE);

void OLED(int x, int y, const char* text) {
    u8g2.drawStr(x, y, text); 
}

// =====================================================
// DISPLAY HELPERS
// =====================================================

void drawCenteredText(const char* text, int y) {
  int w = u8g2.getStrWidth(text);
  int x = (128 - w) / 2;
  u8g2.drawStr(x, y, text);
}

void displayStatus(const char* line1, const char* line2) {
  u8g2.clearBuffer();

  u8g2.setFont(u8g2_font_ncenB10_tr);
  drawCenteredText(line1, 25);
  drawCenteredText(line2, 40);

  u8g2.sendBuffer();
}

void displayPressureAndFlow(float psi, float flow, float Pthreshold, float Fthreshold) {
  char line1[30];
  char line2[30];
  char psiStr[10];
  char flowStr[10];

  dtostrf(psi, 4, 2, psiStr);
  dtostrf(flow, 4, 2, flowStr);

  sprintf(line1, "%s PSI", psiStr);
  sprintf(line2, "%s L/min", flowStr);

  u8g2.clearBuffer();

  u8g2.setFont(u8g2_font_ncenB10_tr);
  drawCenteredText(line1, 15);
  drawCenteredText(line2, 40);

  char buffer[50];
  snprintf(buffer, sizeof(buffer), "P:%.2f  F:%.2f", Pthreshold, Fthreshold);

  u8g2.setFont(u8g2_font_5x8_tr);   // smaller font
  drawCenteredText(buffer, 62);

  u8g2.sendBuffer();
}


int flag = 0;

void callback(char *topic, byte *payload, unsigned int length) {
    Serial.print("Message arrived in topic: ");
    Serial.println(topic);

    String message = "";

    Serial.print("Message: ");
    for (unsigned int i = 0; i < length; i++) {
        char c = (char)payload[i];
        Serial.print(c);
        message += c;
    }

    Serial.println();
    Serial.println("-----------------------");

if (String(topic) == topic_3) {   // "esp32/pump"
    if (message == "ON") {
        TurnOnPump();
        flag = 0;
        unsigned long start = millis();
    while (millis() - start < 2000) {
        client.loop();
    }
    }
    else if (message == "OFF") {
        TurnOffPump();
        flag = 1;
    }
}
else if (String(topic) == topic_4) {   // "top_valve"
    if (message == "OPEN") {
        OpenTopValve();
        flag = 1;
    }
    else if (message == "SHUT") {
        CloseTopValve();
        flag = 0;
        unsigned long start = millis();
    while (millis() - start < 6000) {
        client.loop();
    }
    }
}
else if (String(topic) == topic_8) {   // "user_pause"
    if (message == "RESUME") {
        flag = 0;
    }
    else if (message == "PAUSE") {
        flag = 1;
    }
    }
}


void SetupMQTT() {
    Serial.println("\n=== Starting ESP32 as Access Point ===");

    // 1. Create Access Point
    WiFi.softAP("ESP32_Broker", "12345678");
    Serial.println("Access Point Started");
    Serial.print("AP IP Address: ");
    Serial.println(WiFi.softAPIP()); // Should be 192.168.4.1
    
    // 2. Setup MQTT (connecting to broker on THIS device)
    client.setServer(mqtt_broker, mqtt_port);
    client.setCallback(callback);
}
void ConnectMQTT() {
    while (!client.connected() && (!isBottomEnableOn()) ) {
        String client_id = "esp32-client-" + String(WiFi.macAddress());
        Serial.printf("Connecting to MQTT as %s...\n", client_id.c_str());
        displayStatus("Connecting", "to Client");
        
        if (client.connect(client_id.c_str(), mqtt_username, mqtt_password)) {
            Serial.println("MQTT Connected!");
            client.subscribe(topic_3);
            client.subscribe(topic_4);
            client.subscribe(topic_5);      
            client.subscribe(topic_6);      
            client.subscribe(topic_8);                    
        } else {
            Serial.print("Failed, rc=");
            Serial.print(client.state());
            Serial.println(" - try again in 5 seconds");
            int length = 5000;
            for (int i = 0; i < 5000; i++) {
                delay(1);

                if (isBottomEnableOn()) {
                    return;   // skip connecting, leave function immediately
                }
            }
        }
    }
}

float current_pressure_threshold = 0;
float current_flow_threshold = 0;

float getFlow() {
  return flowRate;
}

void updateFlow() {
  unsigned long now = millis();
  unsigned long elapsed = now - lastFlowUpdate;

  if (elapsed >= 1000) {
    noInterrupts();
    int count = pulseCount;
    pulseCount = 0;
    interrupts();

    float frequency = (count * 1000.0) / elapsed; // pulses per second
    flowRate = frequency / 7.5;                   // L/min

    lastFlowUpdate = now;
  }
}

float leak_buffer = 0.25;
float block_buffer = 2.0;
float critical_buffer = 3.5; 

void CalculateThresholds(int valuesPerSecond, int Seconds) {

    client.publish(topic_7, "WAITING");

    lastFlowUpdate = millis();
    noInterrupts();
    pulseCount = 0;
    interrupts();

    float PressureSum = 0;
    float FlowSum = 0;

    float currentPressureReading = 0;
    float currentFlowReading = 0;

    int totalCalculations = valuesPerSecond * Seconds;
    int delayPerSample = 1000 / valuesPerSecond;

    int flowSamples = 0;              // count how many flow samples we take
    unsigned long lastFlowSample = millis();

    displayStatus("Calculating", "Thresholds");
    int animation_counter = 0;


    char PressuremsgHIGH[10];
    char PressuremsgLOW[10];

    float pressure_blockage = current_pressure_threshold + block_buffer;
    float pressure_leakage = current_pressure_threshold - leak_buffer;

    dtostrf(pressure_blockage, 4, 2, PressuremsgHIGH);  // (value, width, decimal places, buffer)
    dtostrf(pressure_leakage, 4, 2, PressuremsgLOW);  // (value, width, decimal places, buffer)

    for (int i = 0; i < totalCalculations; i++) {

        // --- Always update flow in background ---
        updateFlow();


        const char* animation = "";
        if (animation_counter == 0) {
            animation = "";
        } else if (animation_counter == 1) {
            animation = ".";
        } else if (animation_counter == 2) {
            animation = "..";
        } else if (animation_counter == 3) {
            animation = "...";
            animation_counter = 0;
          }

        animation_counter++;
        char buffer[30];
        snprintf(buffer, sizeof(buffer), "Thresholds %s", animation);

        displayStatus("Calculating", buffer);   
        // --- Pressure sampling (fast) ---
        currentPressureReading = getPressure();
        PressureSum += currentPressureReading;

        // --- Flow sampling (ONLY every 1000 ms) ---
        unsigned long now = millis();
        if (now - lastFlowSample >= 1000) {
            lastFlowSample = now;

            currentFlowReading = getFlow();
            FlowSum += currentFlowReading;
            flowSamples++;

            dtostrf(currentPressureReading, 4, 2, Pressuremsg);  // (value, width, decimal places, buffer)
            dtostrf(currentFlowReading, 4, 2, Flowmsg);  // (value, width, decimal places, buffer)
            
            client.publish(topic_1, Pressuremsg);
            client.publish(topic_2, Flowmsg); 
        }

        unsigned long start = millis();
    while (millis() - start < delayPerSample) {
        client.loop();
    }


    }

    // --- Final averages ---
    current_pressure_threshold = PressureSum / totalCalculations;

    if (flowSamples > 0) {
        current_flow_threshold = FlowSum / flowSamples;
    } else {
        current_flow_threshold = 0; // safety fallback
    }



      pressure_blockage = current_pressure_threshold + block_buffer;
      pressure_leakage = current_pressure_threshold - leak_buffer;

      dtostrf(pressure_blockage, 4, 2, PressuremsgHIGH);  // (value, width, decimal places, buffer)
      dtostrf(pressure_leakage, 4, 2, PressuremsgLOW);  // (value, width, decimal places, buffer)
      
      client.publish(topic_9, PressuremsgHIGH); 
      client.publish(topic_10, PressuremsgLOW); 

      client.publish(topic_7, "IDLE");

}








const int pwmFreq = 5000;
const int pwmResolution = 8;   // duty 0-255

void fadeTankStripOnce() {
  for (int duty = 0; duty <= 255; duty++) {
    ledcWrite(tank_strip, duty);
    delay(3);
  }

  for (int duty = 255; duty >= 0; duty--) {
    ledcWrite(tank_strip, duty);
    delay(3);
  }
}

void setup() {


pinMode(pump, OUTPUT);
pinMode(top_valve, OUTPUT);
pinMode(bottom_valve, OUTPUT);
pinMode(tank_strip, OUTPUT);

pinMode(pump_enable, INPUT_PULLUP);
pinMode(top_enable, INPUT_PULLUP);
pinMode(bottom_enable, INPUT_PULLUP);
pinMode(ready_enable, INPUT_PULLUP);

pinMode(pressureSensorPin, INPUT);
pinMode(flowSensorPin, INPUT);
attachInterrupt(digitalPinToInterrupt(flowSensorPin), countPulse, RISING);
lastFlowUpdate = millis();

ledcAttach(tank_strip, pwmFreq, pwmResolution);
ledcWrite(tank_strip, 0);   // start off


    Serial.begin(115200);
    Wire.begin(21, 22);
    u8g2.begin();

    TurnOnTankStrip();

    delay(2000);

    ConnectWiFi();

    SyncTimeFromNTP();

    SetupMQTT();

    ConnectMQTT();

    client.publish(topic_6, "NO"); //Ready
    PublishLatencyTimestampOnce();
 

    //State 3

        while (isBottomEnableOn()) {
            displayStatus("Flip Switch 3", "");
            // waiting...
        }

    //State 4

        OpenBottomValve();
          for (int i = 7; i >= 0; i--) {
              char buffer[32];
              sprintf(buffer, "Opening: %d", i);
              displayStatus(buffer, "");
              unsigned long start = millis();
    while (millis() - start < 1000) {
        client.loop();
    }
          }
        

    //State 5

        TurnOnPump();
        displayStatus("Turning", "on pump");
        unsigned long start = millis();
    while (millis() - start < 3000) {
        client.loop();
    } //let water settle in pipes

        CalculateThresholds(10,10);
        if (!client.connected()) {
    ConnectMQTT();
}
client.loop();

client.publish(topic_6, "YES"); //Ready
client.publish(topic_7, "IDLE");


}



unsigned long previousTime = 0;
const unsigned long interval = 1000;

float current_pressure = 0;
float current_flow = 0;

void loop() {

  updateFlow();


  unsigned long now = millis();


  // --- read pressure every 500 ms ---
  if (now - lastPressureRead >= pressureInterval) {
    lastPressureRead = now;

    current_pressure = getPressure();
    current_flow = getFlow();

    Serial.print("Pressure: ");
    Serial.println(current_pressure);



      dtostrf(current_pressure, 4, 2, Pressuremsg);  // (value, width, decimal places, buffer)
      dtostrf(current_flow, 4, 2, Flowmsg);  // (value, width, decimal places, buffer)

      displayPressureAndFlow(current_pressure,current_flow, current_pressure_threshold, current_flow_threshold);

      client.publish(topic_1, Pressuremsg);
      client.publish(topic_2, Flowmsg); 

  }




if ((current_pressure <= (current_pressure_threshold - leak_buffer)) && flag == 0) {                             //Leak Detected
    client.publish(topic_6, "NO");
    client.publish(topic_7, "LEAK");
    displayStatus("Leak Detected", "");
    TurnOffPump();
    for (int i = 0; i < 2; i++) {
      fadeTankStripOnce();
    }
      unsigned long lastPublish = 0;
      const unsigned long publishInterval = 500; // 0.5 sec

        while (!isReadyEnableOn()) {
            client.loop();  // VERY IMPORTANT (so MQTT still works)

            updateFlow();
            displayStatus("Toggle Switch #4", "reset");

            unsigned long now = millis();

            current_pressure = getPressure();
            current_flow = getFlow();

            dtostrf(current_pressure, 4, 2, Pressuremsg);  // (value, width, decimal places, buffer)
            dtostrf(current_flow, 4, 2, Flowmsg);  // (value, width, decimal places, buffer)

            if (now - lastPublish >= publishInterval) {
                lastPublish = now;

                client.publish(topic_1, Pressuremsg);
                client.publish(topic_2, Flowmsg); 
            }
        }
    while (isReadyEnableOn()) {
        displayStatus("Toggle Switch #4", "to reset");
    }
    if (digitalRead(top_valve) == HIGH ) {
      displayStatus("Resetting", "Valves");
      CloseTopValve();
      delay(6000);      
    }
    TurnOnPump();
    TurnOnTankStrip();
    displayStatus("Turning", "on Pump");
    delay(3000);
    CalculateThresholds(10, 10);
    flag = 0;
    client.publish(topic_6, "YES");
    client.publish(topic_7, "IDLE");

}
else if (current_pressure >= (current_pressure_threshold + critical_buffer)) {                             //Critical Detected
      displayStatus("Pressure too high", "system shutdown");
      client.publish(topic_6, "NO");
      for (int i = 0; i < 2; i++) {
        fadeTankStripOnce();
      }
      TurnOffPump(); 

      unsigned long lastPublish = 0;
      const unsigned long publishInterval = 500; // 0.5 sec

        while (!isReadyEnableOn()) {
            client.loop();  // VERY IMPORTANT (so MQTT still works)

            displayStatus("Toggle Switch #4", "reset");

            unsigned long now = millis();

            if (now - lastPublish >= publishInterval) {
                lastPublish = now;

                client.publish(topic_1, Pressuremsg);
                client.publish(topic_2, Flowmsg); 
            }
        }
      while (isReadyEnableOn()) {
        displayStatus("Toggle Switch #4", "to reset");
      }
      CalculateThresholds(10,10);
      client.publish(topic_6, "YES");
      flag = 0;
}
else if (current_pressure >= (current_pressure_threshold + block_buffer)) {                             //Blockage Detected
      client.publish(topic_6, "NO");
      client.publish(topic_7, "BLOCK");
      displayStatus("Blockage", "Detected");


      OpenTopValve();
      for (int i = 0; i < 2; i++) {
        fadeTankStripOnce();
          displayStatus("Rerouting Water", "");         
      }
      TurnOnTankStrip();
      CloseBottomValve();   

      unsigned long lastPublish = 0;
      const unsigned long publishInterval = 500; // 0.5 sec

        while (!isReadyEnableOn()) {
            client.loop();  // VERY IMPORTANT (so MQTT still works)

            updateFlow();
            displayStatus("Toggle Switch #4", "reset");

            unsigned long now = millis();

            current_pressure = getPressure();
            current_flow = getFlow();

            dtostrf(current_pressure, 4, 2, Pressuremsg);  // (value, width, decimal places, buffer)
            dtostrf(current_flow, 4, 2, Flowmsg);  // (value, width, decimal places, buffer)

            if (now - lastPublish >= publishInterval) {
                lastPublish = now;

                client.publish(topic_1, Pressuremsg);
                client.publish(topic_2, Flowmsg); 
            }
        }
      while (isReadyEnableOn()) {
        displayStatus("Toggle Switch #4", "to reset");
        TurnOffPump();
      }
      OpenBottomValve();
      CloseTopValve();
        for (int i = 7; i >= 0; i--) {
      char buffer[32];
      sprintf(buffer, "Resetting: %d", i);
      displayStatus(buffer, "");
      delay(1000);
         }
      TurnOnPump();
      CalculateThresholds(10,10);
      client.publish(topic_6, "YES");   
      client.publish(topic_7, "IDLE");
      flag = 0;
}






  // if (isPumpEnableOn()) {
  //     TurnOnPump();

  // if (!isPumpEnableOn()) {
  //     TurnOffPump();
  // }


  // if (isTopEnableOn()) {
  //     OpenTopValve();
  // }

  // if (isBottomEnableOn()) {
  //     OpenBottomValve();
  // }


  if (!client.connected()) {
     ConnectMQTT();
  }
  client.loop();
}