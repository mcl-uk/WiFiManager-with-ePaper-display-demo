# WiFiManager-with-ePaper-display-demo
Demonstration of using WiFiManager on ESP32 with small ePaper display for QR codes

 Arduino demonstrator for WiFiManager provisioning library on ESP32 with
 waveshare 2.13" 250x122 B/W ePaper pi-hat featuring QR code display.
 Using a plain vanilla ESP32-WROOM-32E module.

 See:
 
  https://thepihut.com/products/eink-display-phat-2-13-250x122
  
  https://www.waveshare.com/wiki/2.13inch_e-Paper_HAT_Manual
  
  https://github.com/tzapu/WiFiManager

 Inspired by:
 
  https://www.youtube.com/watch?v=VnfX9YJbaU8

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

