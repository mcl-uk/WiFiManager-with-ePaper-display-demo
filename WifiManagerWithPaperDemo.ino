/*
 ESP32 WiFi Manager & ePaper display demo
 SJM/MCL Jun 26

 Arduino demonstrator for WiFiManager provisioning library on ESP32 with
 waveshare 2.13" 250x122 B/W ePaper pi-hat featuring QR code display.
 Using a plain vanilla ESP32-WROOM-32E module.

 See:
  https://thepihut.com/products/eink-display-phat-2-13-250x122
  https://www.waveshare.com/wiki/2.13inch_e-Paper_HAT_Manual
  https://github.com/tzapu/WiFiManager

 Inspired by:
  https://www.youtube.com/watch?v=VnfX9YJbaU8

 Libraries used:
  WiFiManager by Tzapu
  GxEPD2 graphics by ZinggJM
  QRCodeGFX by Wallysalami
  ESP32Time by fbiego

 Hold GPIIO "WIFI_PIN" low during power-up/reset to force WiFi Manager to start
 else attempt to use the currently stored wifi credentials.
 If no credentials are available or the current creds don't work WifiManager will start anyway.
 Before WifiManager starts the EPD display shows instructions and QR code graphics for both
 connection to the local access point and linking to the local web portal for completing setup. 
 Once connected to WiFi:
   initialise the real-time clock using NTP,
   print debug info to the EPD (SSID,MAC,IP,RSSI,Time),
   wait 10s,
   fall into the digital clock application.

WaveShare 2.13" e-paper display pi-hat, 122 x 250px 
 Wiring:
  BUSY pur -> IO4
  RST  wht -> IO16
  DC   grn -> IO17
  CS   org -> IO5
  CLK  yel -> IO18
  DIN  blu -> IO23
  GND  brw -> GND
  VCC  gry -> 3.3V

WiFi creds QR code format:
 WIFI:T:<SecurityType>;S:<NetworkName>;P:<Password>;H:<true/false>;;
  where:
   T (Security Type): Typically WPA, WPA2 (most common for home routers), WEP, or nopass (for open networks).
   S (SSID): The exact name of the WiFi network SSID.
   P (Password): The password.
   H (Hidden): Flags whether the network is hidden from the broadcasted scan list.
  eg
   "WIFI:T:WPA2;S:ESPSETUP;P:password;H:false;;"
*/

#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager

#include <GxEPD2_BW.h>   // https://github.com/ZinggJM/GxEPD2
#include <GxEPD2_3C.h>
#include <GxEPD2_4C.h>
#include <GxEPD2_7C.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeMonoBold12pt7b.h>

#include <QRCodeGFX.h>   // https://github.com/wallysalami/QRCodeGFX

#include <ESP32Time.h>   // https://github.com/fbiego/ESP32Time/

#include "numericBitmaps_56x80.h" // My big numeric-only font, built using https://javl.github.io/image2cpp/

#include "rom/rtc.h"     // only to obtain reset reasons

#include <esp_wifi.h>    // only to obtain MAC addr

// ---------------------------------------------------------------------------------------

#define WIFI_PIN 32  // if low @ power-up/reset do a WiFi re-provisioning cycle

#define GxEPD2_DISPLAY_CLASS GxEPD2_BW
#define GxEPD2_DRIVER_CLASS GxEPD2_213_B74 // GDEM0213B74 122x250, SSD1680, FPC-7528B)
GxEPD2_DISPLAY_CLASS<GxEPD2_DRIVER_CLASS, GxEPD2_DRIVER_CLASS::HEIGHT> display(GxEPD2_DRIVER_CLASS(/*CS=5*/ 5, /*DC=*/ 17, /*RST=*/ 16, /*BUSY=*/ 4));

QRCodeGFX qrcode(display);
ESP32Time rtc(0);

struct tm ntpT;

const uint8_t epdLine[7] = {0,11,31,51,71,91,111};  EPD line-spacing for 9pt font

// ---------------------------------------------------------------------------------------

// Enumerate & print the ESP32's last reset reason
void print_reset_reason(RESET_REASON reason)
 {
  switch (reason)
   {
    case POWERON_RESET:          Serial.print ("POWERON_RESET");          break; //  1
    case 2:                      Serial.print ("RESET_REASON#2");         break; //  2
    case SW_RESET:               Serial.print ("SOFTWARE_RESET");         break; //  3
    case OWDT_RESET:             Serial.print ("OWDT_RESET");             break; //  4
    case DEEPSLEEP_RESET:        Serial.print ("DEEPSLEEP_RESET");        break; //  5
    case SDIO_RESET:             Serial.print ("SDIO_RESET");             break; //  6
    case TG0WDT_SYS_RESET:       Serial.print ("TG0WDT_SYS_RESET");       break; //  7
    case TG1WDT_SYS_RESET:       Serial.print ("TG1WDT_SYS_RESET");       break; //  8
    case RTCWDT_SYS_RESET:       Serial.print ("RTCWDT_SYS_RESET");       break; //  9
    case INTRUSION_RESET:        Serial.print ("INTRUSION_RESET");        break; // 10
    case TGWDT_CPU_RESET:        Serial.print ("TGWDT_CPU_RESET");        break; // 11
    case SW_CPU_RESET:           Serial.print ("SW_CPU_RESET");           break; // 12
    case RTCWDT_CPU_RESET:       Serial.print ("RTCWDT_CPU_RESET");       break; // 13
    case EXT_CPU_RESET:          Serial.print ("EXT_CPU_RESET");          break; // 14
    case RTCWDT_BROWN_OUT_RESET: Serial.print ("RTCWDT_BROWN_OUT_RESET"); break; // 15
    case RTCWDT_RTC_RESET:       Serial.print ("RTCWDT_RTC_RESET");       break; // 16
    default:                     Serial.print ("???");
   }
 }

// Calc DST state given dow, mo_no, date_no & hour
//  dow must be Sun-0..6 & mo_no must be 0..11 & date_no 0..xx 
int8_t in_dst(tm* RTC)
 {
  int8_t dst = 0;
  if ((RTC->tm_mon > 2) && (RTC->tm_mon <= 9)) dst = 1;
  if ((RTC->tm_mon == 2) && (RTC->tm_hour >= 24))
   {
    if (RTC->tm_wday > 0) { if (((RTC->tm_mday-1) - RTC->tm_wday) >= 24) dst = 1; }
    else if (RTC->tm_hour >= 1) dst = 1;
   }
  if ((RTC->tm_mon == 9) && ((RTC->tm_mday-1) >= 24))
   {
    if (RTC->tm_wday > 0) { if (((RTC->tm_mday-1) - RTC->tm_wday) >= 24) dst = 0; }
    else if (RTC->tm_hour >= 1) dst = 0;
   }
  return dst; // 0 or 1
 }

// Big numeric digit to epd display
// pos 1..3 - position
// val 0..9 - numeric value
void bigNum(uint8_t pos, uint8_t val)
 {
  uint8_t  tmp[560];
  uint32_t i;
  uint16_t xs[4] = {0,62,133,194};
  //
  for (i=0; i<560; i++) tmp[i] = ~myNumes[val][i];
  display.drawBitmap(xs[pos],5,tmp,56,80,GxEPD_BLACK);  
 }

// ----------------------------------------------------------------------------------

void setup()  // Start WiFi etc
 {
  uint32_t i;
  uint8_t  MAC[6] = {0,0,0,0,0,0};  // MAC address as byte array
  char     myMac[18];               // MAC address as ascii string
  bool     wRstRq = false;
  // setup IO
  pinMode(WIFI_PIN, INPUT_PULLUP);  // lo @start-up to request WiFi re-config
  // setup ntp time server
  configTime(0, 0, "pool.ntp.org");
  // setup display
  display.init(115200, true, 2, false);
  display.setRotation(1);
  display.setFont(&FreeMonoBold9pt7b);
  display.setTextColor(GxEPD_BLACK);
  display.setFullWindow();
  // setup serial
  Serial.begin(115200);
  while ((!Serial) && (millis() < 500)) delay(10);
  // init WiFi
  WiFi.mode(WIFI_STA);
  // get MAC
  esp_wifi_get_mac(WIFI_IF_STA, MAC);
  sprintf(myMac, "%02x:%02x:%02x:%02x:%02x:%02x",MAC[0],MAC[1],MAC[2],MAC[3],MAC[4],MAC[5]);
  // Check WiFi re-provision request, or start WiFi with current creds
  if (digitalRead(WIFI_PIN) == 0) wRstRq = true;
   else
   {
    WiFi.begin();
    i = 0;
    while ((!WiFi.isConnected()) && (++i < 100)) delay(100);  // wait up to 10s for Wifi connection
    if (!WiFi.isConnected()) Serial.println("Timed out waiting for WiFi to connect");
   }
  // Check WiFi, maybe startup Wifi_Manager
  while (!WiFi.isConnected())  // if unconnected invoke WiFiManager
   {
    Serial.println("No WiFi, please configure via local AP 'ESPSETUP'");
    //
    display.fillScreen(GxEPD_WHITE);
    display.setCursor(0, 11); // x,y
    display.print("QR-1 to connect WiFi");
    display.setCursor(0, 29); // x,y
    display.print("then browse to");
    display.setCursor(0, 47); // x,y
    display.print("QR-2 to re-configure");
    qrcode.setScale(2);
    qrcode.draw("WIFI:T:WPA2;S:ESPSETUP;P:password;H:false;;", 0, 52);
    qrcode.draw("http://192.168.4.1/", 176, 52);
    display.display(false);
    display.hibernate();
    WiFiManager wm;
    if (wRstRq)
     {
      Serial.println("Resetting wifi credentials");
      wm.resetSettings();
     }
    wm.setTimeout(180);
    if (!wm.autoConnect("ESPSETUP", "password")) // start a password protected AP
     {
      Serial.println("Failed to connect");
      delay(5000);
      ESP.restart();
     }
   }

  // reset reason
  Serial.print("\n--- ");
  print_reset_reason(rtc_get_reset_reason(1));
  Serial.print(" ---\n");
  Serial.printf("Wifi connected, MAC: %s, IP: ", myMac); Serial.println(WiFi.localIP());

  // Get ntp time with re-try
  if (getLocalTime(&ntpT)) rtc.setTimeStruct(ntpT);
   else
   {
    delay(1000);
    if (getLocalTime(&ntpT)) rtc.setTimeStruct(ntpT);
   }

  display.fillScreen(GxEPD_WHITE);
  display.setCursor(0, epdLine[1]); display.print("WiFi connected...");
  display.setCursor(0, epdLine[2]); display.print("SSID "); display.print(WiFi.SSID());
  display.setCursor(0, epdLine[3]); display.print("MAC  "); display.print(myMac);
  display.setCursor(0, epdLine[4]); display.print("IP   "); display.print(WiFi.localIP());
  display.setCursor(0, epdLine[5]); display.printf("RSSI %ddBm", WiFi.RSSI());
  display.setCursor(0, epdLine[6]); display.print("UTC  "); display.print(rtc.getTime("%y-%m-%d %H:%M:%S"));
  display.display(false); // full update
  display.hibernate();
  delay(10000);
 }

// -------------------------------------------------------------------------------------------------

void loop()
 {
  display.setFont(&FreeMonoBold12pt7b);  // bigger font
  while(true)
   {
    ntpT = rtc.getTimeStruct();
    if (in_dst(&ntpT)) ntpT.tm_hour = (ntpT.tm_hour + 1) % 24;
    Serial.printf("%02d:%02d:%02d\n", ntpT.tm_hour, ntpT.tm_min, ntpT.tm_sec);
    display.fillScreen(GxEPD_WHITE);
    // print big time
    bigNum(0, ntpT.tm_hour/10); bigNum(1, ntpT.tm_hour%10); bigNum(2, ntpT.tm_min/10); bigNum(3, ntpT.tm_min%10);
    display.setCursor(0, 114); // x,y
    display.print(" "); display.print(rtc.getDate());  // Add date info
    //
    // 'partial' update every minute, full update every hour 
    //
    display.display((ntpT.tm_min != 0)); // false = full-update, true = partial
    display.hibernate();
    delay(1000 * (62 - ntpT.tm_sec));
    if ((ntpT.tm_min == 59) && (getLocalTime(&ntpT)))
     {
      rtc.setTimeStruct(ntpT);
      Serial.print(rtc.getTime("%y-%m-%d %H:%M:%S"));
     }
   }
 }
