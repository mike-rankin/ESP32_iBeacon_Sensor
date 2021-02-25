//
// Demo sketch to read the data from a RadioLand RDL52832 iBeacon
//
// Displays the data on an ESP32 by parsing (in a brute-force way)
// the large advertisement packet transmitted by the iBeacon.
// 
// Written by Larry Bank
// February 25, 2021
//
// NOTE that each iBeacon appears to have a different mac address, find this on your phone app and match to this line
// if (memcmp(szAddr, "cc:a2:9b", 8) == 0) {

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>

#define LED_ENABLE 23
#define BLUE_LED 9

Adafruit_SSD1306 display(128, 32, &Wire, 21);

static int T, H, iMaxT, iMinT, iMaxH, iMinH;
static int iRSSI;
float X, Y, Z, fDistance;

std::string service_data;
char Scanned_BLE_Name[32];
String Scanned_BLE_Address;
BLEScanResults foundDevices;
BLEScan *pBLEScan;
static BLEAddress *Server_BLE_Address;

void lightSleep(uint64_t time_in_us)
{
  esp_sleep_enable_timer_wakeup(time_in_us);
  esp_light_sleep_start();
}
//
// Data format for the RadioLand RDL52832 iBeacon
//
// service UUID 0x0318:
//    0    1    2    3    4    5    6    7    8    9   10   11   12   13   14   15
// +----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+
// | Th | Tl | Hh | Hl | Xs | Xw | Xt | Xh | Ys | Yw | Yt | Yh | Zs | Zw | Zt | Zh |
// +----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+----+
// Temperature (T), stored as a 16-bit value equal to Celcius * 256
// Humidity (H), stored as a 16-bit value equal to Humidity % * 256
// Accelerometer Axes: (X,Y,Z)
//    s = sign (1=negative, 0=positive)
//    w = whole value (0 or 1)
//    t = tenths (0-9)
//    h = hundredths (0-9)
//
// iBeacon packet introducer
const uint8_t uciBeacon[] = {0x02, 0x01, 0x06, 0x1a, 0xff, 0x4c, 0x0, 0x2, 0x15};

// Called for each device found during a BLE scan by the client
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks
{
    void onResult(BLEAdvertisedDevice advertisedDevice) {
//      Serial.printf("Scan Result: %s \n", advertisedDevice.toString().c_str());
      const char *szAddr = advertisedDevice.getAddress().toString().c_str();
        
         if (memcmp(szAddr, "cc:a2:9b", 8) == 0) { // first 3 bytes of MAC for manufacterer that changes per ibeacon

          
          const char *s = service_data.c_str();
          int i, iLen = service_data.length();
          uint8_t *p = (uint8_t *)s; // unsigned data
          service_data = advertisedDevice.getServiceData();
          
//          Serial.printf("Advertised device info: %s \n", advertisedDevice.toString().c_str());
//          Serial.printf("MAC: %s, service data len=%d\n", szAddr, iLen);
//          if (iLen != 0) {
//            for (i=0; i<iLen; i++) {
//              Serial.printf("0x%02x,", p[i]);
//            }
//            Serial.printf("\n");
//          }
// The iBeacon sends a large packet with multiple service UUIDs in it
// the ESP32 BLE library doesn't parse this so we need to ask for the raw payload
// and parse it outselves
          iLen = advertisedDevice.getPayloadLength();
          if (iLen != 0) {
//            Serial.printf("payload size = %d\n", iLen);
            p = (uint8_t *)advertisedDevice.getPayload();
            if (memcmp(p, uciBeacon, 9) == 0) {// iBeacon info?
              iRSSI = advertisedDevice.getRSSI();
              int txCalibratedPower = (int8_t)p[29]; // get the 1 meter RSSI value
//              Serial.printf("iBeacon info - Tx @ 1M = %ddB, RSSI = %ddB\n", txCalibratedPower, iRSSI);
              // Calculate estimated distance
              // The RSSI value seems to drop off too quickly with the M5StickC
              // compared to my Android phone
              int ratio_dB = (txCalibratedPower - iRSSI)/4; // <-- fudge factor for ESP32
              float ratio_linear = powf(10, (float)ratio_dB / 10.0f);
              fDistance = sqrtf(ratio_linear);
//              Serial.printf("Approximate distance = %f\n", fDistance);
            }
            for (i=0; i<iLen; i++) {
              // Search for the start of the 0x0318 UUID with the data we want
              if (p[i] == 0x13 && p[i+1] ==0x16 && p[i+2] == 0x18 && p[i+3] ==0x03) {
//                Serial.print("UUID 0x0318 data received!");
                i += 4; // start of the data we want
                T = (((p[i] << 8) + p[i+1]) * 10) / 256; // 1 decimal place is enough precision
                H = p[i+2]; // no need for fractions of a % because the sensor isn't that good anyway
                if (T > iMaxT) iMaxT = T;
                if (T < iMinT) iMinT = T;
                if (H > iMaxH) iMaxH = H;
                if (H < iMinH) iMinH = H;
                X = (float)p[i+5] + (float)p[i+6]/10.0f + (float)p[i+7] / 100.0f;
                if (p[i+4] == 1) X = -X;
                Y = (float)p[i+9] + (float)p[i+10]/10.0f + (float)p[i+11] / 100.0f;
                if (p[i+8] == 1) Y = -Y;
                Z = (float)p[i+13] + (float)p[i+14]/10.0f + (float)p[i+15] / 100.0f;
                if (p[i+12] == 1) Z = -Z;
                
//                for (; i<iLen; i++) {
//                    Serial.printf("0x%02x,", p[i]);              
//                }
//                Serial.printf("\n");
              }
            }
          }
        }
    }
};
//
// Display all of the sensor info on the M5StickC LCD
//
void ShowInfo(void)
{
char szTemp[64]; char szTemp1[64];
  sprintf(szTemp, "Temp:%d.%01dC", T/10, T % 10);
  display.setCursor(0,0);  //over,down
  display.print(szTemp);
  Serial.println(szTemp);
  
  sprintf(szTemp, "Humid:%d%%", H);
  display.setCursor(70,0);  //over,down
  display.print(szTemp);
  
  sprintf(szTemp, "RSSI:%d, Dist:%03f ", iRSSI, fDistance);
  display.setCursor(0,24);  //over,down
  display.print(szTemp);
  
  //sprintf(szTemp, "X: %.2f ", X);
  //display.setCursor(0,24);  //over,down
  //display.print(szTemp);
  
  //sprintf(szTemp, "Y: %.2f ", Y);
  //display.setCursor(30,24);  //over,down
  //display.print(szTemp);
  
  //sprintf(szTemp, "Z: %.2f ", Z);
  //display.setCursor(60,24);  //over,down
  //display.print(szTemp);
  

  display.display();
  delay(1000);
  display.clearDisplay();
}
void setup() {

  Wire.begin(13, 14); 
  display.setRotation(2);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C); 
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);

  pinMode(LED_ENABLE, OUTPUT);
  pinMode(BLUE_LED, OUTPUT);
  digitalWrite(LED_ENABLE, LOW); //Enable Leds
  digitalWrite(BLUE_LED, HIGH);  //Keep off
  
  iMaxT = 0;
  iMinT = 1000;
  iMaxH = 0;
  iMinH = 99;
  ShowInfo();
  //spilcdSetTXBuffer(ucTXBuf, sizeof(ucTXBuf));
  //spilcdInit(&lcd, LCD_ST7789_135, FLAGS_NONE, 40000000, TFT_CS, TFT_DC, TFT_RST, -1, -1, TFT_MOSI, TFT_CLK); // M5Stick-V pin numbering, 40Mhz
  //spilcdSetOrientation(&lcd, LCD_ORIENTATION_90);
  //spilcdFill(&lcd, 0, DRAW_TO_LCD);
  Serial.begin(115200);
  Serial.println("About to start BLE");
  BLEDevice::init("ESP32BLE");
  pBLEScan = BLEDevice::getScan(); //create new scan
//  Serial.println("getScan returned");
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks()); //Call the class that is defined above
  pBLEScan->setActiveScan(true); //active scan uses more power, but get results faster
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);  // less or equal setInterval value
} /* setup() */

void loop() {
   BLEDevice::init("ESP32BLE");
   foundDevices = pBLEScan->start(5, false); //Scan for 5 seconds to find the Fitness band
   pBLEScan->clearResults();   // delete results fromBLEScan buffer to release memory
   pBLEScan->stop();
   BLEDevice::deinit(false);
   ShowInfo();
   //lightSleep(10000000); // wait 10 seconds, then start another scan
   
   digitalWrite(LED_ENABLE, LOW); //Enable Leds
   digitalWrite(BLUE_LED, LOW);
   delay(250);
   digitalWrite(BLUE_LED, HIGH);
   delay(250);
 
} /* loop */
