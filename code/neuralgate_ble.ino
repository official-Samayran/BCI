#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Adafruit_ADS1X15.h>

/* ---------- CONFIGURATION ---------- */
// These UUIDs must match your Flutter app's BLE service logic
#define SERVICE_UUID           "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define CHARACTERISTIC_UUID_TX "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
#define CHARACTERISTIC_UUID_RX "6e400002-b5a3-f393-e0a9-e50e24dcca9e"

Adafruit_ADS1115 ads;
BLECharacteristic *pTxCharacteristic;
bool deviceConnected = false;
float threshold = 100.0;
float Q = 0.00005, R = 0.08, P = 1.0, K = 0, X = 0, prev_v = 0;

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) { 
      deviceConnected = true; 
      Serial.println(">>> NeuralGate: Connected!"); 
    };
    
    void onDisconnect(BLEServer* pServer) { 
      deviceConnected = false;
      Serial.println(">>> NeuralGate: Disconnected.");
      
      // Give the phone 500ms to clear the old session
      delay(500); 
      
      // CRITICAL: Restart advertising so it becomes "visible" again
      BLEDevice::startAdvertising(); 
      Serial.println(">>> NeuralGate: Re-advertising...");
    }
};

class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      String value = pCharacteristic->getValue();
      if (value.length() > 0) {
        threshold = value.toFloat();
        Serial.print(">>> DEBUG: New Threshold: ");
        Serial.println(threshold);
      }
    }
};

void setup() {
  Serial.begin(115200);
  // Optional: removes the wait for Serial if you want it to run without a PC
  // while (!Serial); 
  
  Serial.println("--- NEURALGATE SYSTEM START ---");

  if (!ads.begin()) {
    Serial.println("ERROR: ADS1115 Hardware not found!");
    while(1);
  }

  // Set the Broadcast Name to exactly "NeuralGate"
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
  Serial.println("SUCCESS: Advertising as 'NeuralGate'");
}

void loop() {
  // 1. Biosignal Processing [cite: 18, 19, 20]
  float rV = ads.computeVolts(ads.readADC_SingleEnded(0));
  float rW = rV - prev_v; prev_v = rV;
  P = P + Q; K = P / (P + R);
  X = X + K * (rW - X); P = (1 - K) * P;
  float focusPower = abs(X * 8000);

  // 2. Serial Debugging [cite: 22]
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 200) {
    Serial.print("Power: "); Serial.print(focusPower);
    Serial.print(" | Threshold: "); Serial.println(threshold);
    lastPrint = millis();
  }

  // 3. Bluetooth Data Stream [cite: 24]
  if (deviceConnected) {
    pTxCharacteristic->setValue(String(focusPower, 2).c_str());
    pTxCharacteristic->notify();
  }
  
  delay(10); 
}