#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Adafruit_ADS1X15.h>
#include <esp_now.h>
#include <WiFi.h>

/* ---------- CONFIGURATION ---------- */
#define SERVICE_UUID           "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define CHARACTERISTIC_UUID_TX "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
#define CHARACTERISTIC_UUID_RX "6e400002-b5a3-f393-e0a9-e50e24dcca9e"

// Relay MAC Address from your reference code
uint8_t relayMAC[] = {0xE8, 0x68, 0xE7, 0xDC, 0xDE, 0x76};

Adafruit_ADS1115 ads;
BLECharacteristic *pTxCharacteristic;
bool deviceConnected = false;
float threshold = 100.0;
float Q = 0.00005, R = 0.08, P = 1.0, K = 0, X = 0, prev_v = 0;
bool relayState = false;

/* ---------- RELAY LOGIC ---------- */
void triggerRelay() {
    relayState = !relayState;
    uint8_t status = relayState ? 1 : 0;
    // Send state to the Relay Module via ESP-NOW
    esp_err_t result = esp_now_send(relayMAC, &status, 1);
    
    Serial.print(">>> RELAY TRIGGERED: ");
    Serial.println(relayState ? "ON" : "OFF");
}

/* ---------- BLE CALLBACKS ---------- */
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) { 
      deviceConnected = true; 
      Serial.println(">>> NeuralGate: Connected!"); 
    };
    
    void onDisconnect(BLEServer* pServer) { 
      deviceConnected = false;
      Serial.println(">>> NeuralGate: Disconnected.");
      delay(500); 
      BLEDevice::startAdvertising(); 
      Serial.println(">>> NeuralGate: Re-advertising...");
    }
};

class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      String value = pCharacteristic->getValue().c_str(); // Use .c_str() for safety
      
      if (value.length() > 0) {
        
        // 👇 THIS IS THE MASTER RULE: Only trigger relay if the App explicitly tells us to!
        if (value.indexOf("R") != -1 || value == "R") {
          Serial.println(">>> BLE: 'R' Command Received! Triggering Relay from App.");
          triggerRelay();
        } 
        else {
          float newThreshold = value.toFloat();
          if (newThreshold > 0) {
            threshold = newThreshold;
            Serial.print(">>> BLE: New Threshold set to: ");
            Serial.println(threshold);
          }
        }
      }
    }
};

void setup() {
  Serial.begin(115200);
  Serial.println("--- NEURALGATE SYSTEM START ---");

  if (!ads.begin()) {
    Serial.println("ERROR: ADS1115 Hardware not found!");
    while(1);
  }

  /* --- ESP-NOW & WIFI SETUP (For Relay) --- */
  WiFi.mode(WIFI_STA); // Must be in Station mode for ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  // Register Peer (Relay Module)
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, relayMAC, 6);
  peerInfo.channel = 0; 
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add relay peer");
  }

  /* --- BLE SETUP --- */
  BLEDevice::init("NeuralGate"); 
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);
  pTxCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID_TX, BLECharacteristic::PROPERTY_NOTIFY);
  pTxCharacteristic->addDescriptor(new BLE2902());

  BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID_RX, BLECharacteristic::PROPERTY_WRITE);
  pRxCharacteristic->setCallbacks(new MyCallbacks());

  pService->start();
  pServer->getAdvertising()->start();
  Serial.println("SUCCESS: BLE Advertising as 'NeuralGate'");
}

void loop() {
  // 1. Biosignal Processing
  float rV = ads.computeVolts(ads.readADC_SingleEnded(0));
  float rW = rV - prev_v; prev_v = rV;
  P = P + Q; K = P / (P + R);
  X = X + K * (rW - X); P = (1 - K) * P;
  float focusPower = abs(X * 8000);

  // 👇 The Hardcoded Trigger Logic was DELETED from here.
  // Now, the Arduino will only trigger the relay when the Flutter App sends the "R" command.

  static unsigned long lastPrint = 0;
  // 2. Serial Debugging
  if (millis() - lastPrint > 200) {
    Serial.print("Power: "); Serial.print(focusPower);
    Serial.print(" | Threshold: "); Serial.println(threshold);
    lastPrint = millis();
  }

  // 3. Bluetooth Data Stream
  if (deviceConnected) {
    pTxCharacteristic->setValue(String(focusPower, 2).c_str());
    pTxCharacteristic->notify();
  }
  
  delay(10); 
}
