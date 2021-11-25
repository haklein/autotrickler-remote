/**
   Autotrickler v4 remote control on an ESP32
   Based on the BLE client example (author unknown, updated by chegewara)
   
*/

#include "AiEsp32RotaryEncoder.h"
#include "Arduino.h"

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <EEPROM.h>

#include "BLEDevice.h"
//#include "BLEScan.h"

#define ROTARY_ENCODER_A_PIN 23
#define ROTARY_ENCODER_B_PIN 32
#define ROTARY_ENCODER_BUTTON_PIN 19
#define ROTARY_ENCODER_VCC_PIN 18
#define ROTARY_ENCODER_STEPS 4

// The remote service we wish to connect to.
static BLEUUID serviceUUID("0000FFE0-0000-1000-8000-00805F9B34FB");
// The characteristic of the remote service we are interested in.
static BLEUUID    charUUID("0000FFE1-0000-1000-8000-00805F9B34FB");

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define SCREEN_ADDRESS 0x3c ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

static boolean doConnect = false;
static boolean connected = false;
static boolean doScan = false;
static BLERemoteCharacteristic* pRemoteCharacteristic;
static BLEAdvertisedDevice* myDevice;

static float targetWeight = 0.0;
static String tricklerStatus;

AiEsp32RotaryEncoder rotaryEncoder = AiEsp32RotaryEncoder(ROTARY_ENCODER_A_PIN, ROTARY_ENCODER_B_PIN, ROTARY_ENCODER_BUTTON_PIN, ROTARY_ENCODER_VCC_PIN, ROTARY_ENCODER_STEPS);

void update_display()
{
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.println("Current target:");
  display.setTextSize(2);
  display.println(tricklerStatus);
  display.setTextSize(1);
  display.println("New target:");
  display.setTextSize(2);

  display.print(targetWeight);
  display.display();
}

void rotary_onButtonClick()
{
  static unsigned long lastTimePressed = 0;
  if (millis() - lastTimePressed < 500)
    return; //ignore multiple press in that time milliseconds
  lastTimePressed = millis();
  Serial.println("button pressed");
  String newValue;
  if (targetWeight == 0.0) {
    newValue = "stop";
  } else {
    newValue = "T" + String(targetWeight, 2);
  }
  Serial.println("Setting new characteristic value to \"" + newValue + "\"");

  // Set the characteristic's value to be the array of bytes that is actually a string.
  pRemoteCharacteristic->writeValue(newValue.c_str(), newValue.length());
}


void rotary_loop()
{
  if (rotaryEncoder.encoderChanged())
  {
    Serial.print("Value: ");
    Serial.println(rotaryEncoder.readEncoder());
    targetWeight = float(rotaryEncoder.readEncoder()) / 10.0;
    update_display();
  }
  if (rotaryEncoder.isEncoderButtonClicked())
  {
    rotary_onButtonClick();
  }
}

void IRAM_ATTR readEncoderISR()
{
  rotaryEncoder.readEncoder_ISR();
}


static void notifyCallback(
  BLERemoteCharacteristic* pBLERemoteCharacteristic,
  uint8_t* pData,
  size_t length,
  bool isNotify) {
  Serial.print("Notify callback for characteristic ");
  Serial.print(pBLERemoteCharacteristic->getUUID().toString().c_str());
  Serial.print(" of data length ");
  Serial.println(length);
  Serial.print("data: ");
  Serial.println((char*)pData);
  tricklerStatus = String((char*)pData);
  Serial.print("String: "); Serial.println(tricklerStatus);
  if (tricklerStatus.startsWith("#:")) {
    Serial.println("Target status found");
    tricklerStatus.remove(0, 2);
    tricklerStatus = String(tricklerStatus.toFloat());
  } else if (tricklerStatus.startsWith("#C")) {
    tricklerStatus = "CAL";
  } else {

    tricklerStatus = "IDLE";
  }
  update_display();

}

class MyClientCallback : public BLEClientCallbacks {
    void onConnect(BLEClient* pclient) {
    }

    void onDisconnect(BLEClient* pclient) {
      connected = false;
      Serial.println("onDisconnect");
      tricklerStatus = "SEARCH"; update_display();
    }
};

bool connectToServer() {
  Serial.print("Forming a connection to ");
  Serial.println(myDevice->getAddress().toString().c_str());

  BLEClient*  pClient  = BLEDevice::createClient();
  Serial.println(" - Created client");

  pClient->setClientCallbacks(new MyClientCallback());

  // Connect to the remove BLE Server.
  pClient->connect(myDevice);  // if you pass BLEAdvertisedDevice instead of address, it will be recognized type of peer device address (public or private)
  Serial.println(" - Connected to server");
  pClient->setMTU(517); //set client to request maximum MTU from server (default is 23 otherwise)

  // Obtain a reference to the service we are after in the remote BLE server.
  BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
  if (pRemoteService == nullptr) {
    Serial.print("Failed to find our service UUID: ");
    Serial.println(serviceUUID.toString().c_str());
    pClient->disconnect();
    return false;
  }
  Serial.println(" - Found our service");


  // Obtain a reference to the characteristic in the service of the remote BLE server.
  pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
  if (pRemoteCharacteristic == nullptr) {
    Serial.print("Failed to find our characteristic UUID: ");
    Serial.println(charUUID.toString().c_str());
    pClient->disconnect();
    return false;
  }
  Serial.println(" - Found our characteristic");

  // Read the value of the characteristic.
  if (pRemoteCharacteristic->canRead()) {
    std::string value = pRemoteCharacteristic->readValue();
    Serial.print("The characteristic value was: ");
    Serial.println(value.c_str());
  }

  if (pRemoteCharacteristic->canNotify())
    pRemoteCharacteristic->registerForNotify(notifyCallback);

  String newValue = "status"; // fetch instant status right after connecting
  pRemoteCharacteristic->writeValue(newValue.c_str(), newValue.length());

  connected = true;
  return true;
}
/**
   Scan for BLE servers and find the first one that advertises the service we are looking for.
*/
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    /**
        Called for each advertising BLE server.
    */
    void onResult(BLEAdvertisedDevice advertisedDevice) {
      Serial.print("BLE Advertised Device found: ");
      Serial.println(advertisedDevice.toString().c_str());

      // We have found a device, let us now see if it contains the service we are looking for.
      if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(serviceUUID)) {

        BLEDevice::getScan()->stop();
        myDevice = new BLEAdvertisedDevice(advertisedDevice);
        doConnect = true;
        doScan = true;

      } // Found our server
    } // onResult
}; // MyAdvertisedDeviceCallbacks


void setup() {
  Serial.begin(115200);
  Serial.println("Starting Arduino BLE Client application...");
  // while(true);
  rotaryEncoder.begin();
  rotaryEncoder.setup(readEncoderISR);
  bool circleValues = false;
  rotaryEncoder.setBoundaries(0, 3000, circleValues); //minValue, maxValue, circleValues true|false (when max go to min and vice versa)
  rotaryEncoder.setAcceleration(300);

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;); // Don't proceed, loop forever
  }
  display.clearDisplay();
  // Setup text and draw splash screen
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.print("Searching");
  display.display();


  BLEDevice::init("");

  // Retrieve a Scanner and set the callback we want to use to be informed when we
  // have detected a new device.  Specify that we want active scanning and start the
  // scan to run for 5 seconds.
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setInterval(1349);
  pBLEScan->setWindow(449);
  pBLEScan->setActiveScan(true);
  pBLEScan->start(30, false);
} // End of setup.

bool once = false;

// This is the Arduino main loop function.
void loop() {
  rotary_loop();

  // If the flag "doConnect" is true then we have scanned for and found the desired
  // BLE Server with which we wish to connect.  Now we connect to it.  Once we are
  // connected we set the connected flag to be true.
  if (doConnect == true) {
    if (connectToServer()) {
      Serial.println("We are now connected to the BLE Server.");
    } else {
      Serial.println("We have failed to connect to the server; there is nothin more we will do.");
    }
    doConnect = false;
  }

  // If we are connected to a peer BLE Server, update the characteristic each time we are reached
  // with the current time since boot.
  if (connected) {

    if (once) {
      once = false;

      String newValue = "T50.00";
      Serial.println("Setting new characteristic value to \"" + newValue + "\"");

      // Set the characteristic's value to be the array of bytes that is actually a string.
      pRemoteCharacteristic->writeValue(newValue.c_str(), newValue.length());
    }
  } else if (doScan) {
    BLEDevice::getScan()->start(0);  // this is just example to start scan after disconnect, most likely there is better way to do it in arduino
  }

  delay(50); // Delay a second between loops.
} // End of loop
