/*
  Update.ino, Example for the AutoConnect library.
  Copyright (c) 2019, Hieromon Ikasamo
  https://github.com/Hieromon/AutoConnect
  This software is released under the MIT License.
  https://opensource.org/licenses/MIT

  This example presents the simplest OTA Updates scheme.
*/

#if defined(ARDUINO_ARCH_ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
using WebServerClass = ESP8266WebServer;
#elif defined(ARDUINO_ARCH_ESP32)
#include <WiFi.h>
#include <WebServer.h>
using WebServerClass = WebServer;
#endif
#include <AutoConnect.h>

// for TFT
#include <TFT_eSPI.h>
#include <SPI.h>
#include <Button2.h>
#include "esp_adc_cal.h"
#include "bmp.h"

#ifndef TFT_DISPOFF
#define TFT_DISPOFF 0x28
#endif

#ifndef TFT_SLPIN
#define TFT_SLPIN   0x10
#endif

#define TFT_MOSI            19
#define TFT_SCLK            18
#define TFT_CS              5
#define TFT_DC              16
#define TFT_RST             23

#define TFT_BL          4  // Display backlight control pin
#define ADC_EN          14
#define ADC_PIN         34
#define BUTTON_1        35
#define BUTTON_2        0

TFT_eSPI tft = TFT_eSPI(135, 240); // Invoke custom library
Button2 btn1(BUTTON_1);
Button2 btn2(BUTTON_2);

char buff[512];
int vref = 1100;
int btnCick = false;

//! Long time delay, it is recommended to use shallow sleep, which can effectively reduce the current consumption
void espDelay(int ms)
{   
    esp_sleep_enable_timer_wakeup(ms * 1000);
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH,ESP_PD_OPTION_ON);
    esp_light_sleep_start();
}

void showVoltage()
{
    static uint64_t timeStamp = 0;
    if (millis() - timeStamp > 1000) {
        timeStamp = millis();
        uint16_t v = analogRead(ADC_PIN);
        float battery_voltage = ((float)v / 4095.0) * 2.0 * 3.3 * (vref / 1000.0);
        String voltage = "Voltage :" + String(battery_voltage) + "V";
        Serial.println(voltage);
        tft.fillScreen(TFT_BLACK);
        tft.setTextDatum(MC_DATUM);
        tft.drawString(voltage,  tft.width() / 2, tft.height() / 2 );
    }
}
void wifi_scan()
{
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(1);

    tft.drawString("Scan Network", tft.width() / 2, tft.height() / 2);

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

    int16_t n = WiFi.scanNetworks();
    tft.fillScreen(TFT_BLACK);
    if (n == 0) {
        tft.drawString("no networks found", tft.width() / 2, tft.height() / 2);
    } else {
        tft.setTextDatum(TL_DATUM);
        tft.setCursor(0, 0);
        Serial.printf("Found %d net\n", n);
        for (int i = 0; i < n; ++i) {
            sprintf(buff,
                    "[%d]:%s(%d)",
                    i + 1,
                    WiFi.SSID(i).c_str(),
                    WiFi.RSSI(i));
            tft.println(buff);
        }
    }
    WiFi.mode(WIFI_OFF);
}

void button_init()
{
    btn1.setLongClickHandler([](Button2 & b) {
        btnCick = false;
        int r = digitalRead(TFT_BL);
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("Press again to wake up",  tft.width() / 2, tft.height() / 2 );
        espDelay(6000);
        digitalWrite(TFT_BL, !r);

        tft.writecommand(TFT_DISPOFF);
        tft.writecommand(TFT_SLPIN);
        esp_sleep_enable_ext1_wakeup(GPIO_SEL_35, ESP_EXT1_WAKEUP_ALL_LOW);
        esp_deep_sleep_start();
    });
    btn1.setPressedHandler([](Button2 & b) {
        Serial.println("Detect Voltage..");
        btnCick = true;
    });

    btn2.setPressedHandler([](Button2 & b) {
        btnCick = false;
        Serial.println("btn press wifi scan");
        wifi_scan();
    });
}

void button_loop()
{
    btn1.loop();
    btn2.loop();
}


// Update server setting page
static const char SETUP_PAGE[] PROGMEM = R"(
{
  "title": "Update setup",
  "uri": "/setup",
  "menu": true,
  "element": [
    {
      "name": "caption",
      "type": "ACText",
      "value": "OTA update setup",
      "style": ""
    },
    {
      "name": "isvalid",
      "type": "ACText",
      "style": "color:red"
    },
    {
      "name": "server",
      "type": "ACInput",
      "label": "Update server",
      "pattern": "^((([a-zA-Z]|[a-zA-Z][a-zA-Z0-9-]*[a-zA-Z0-9]).)*([A-Za-z]|[A-Za-z][A-Za-z0-9-]*[A-Za-z0-9]))|((25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)(\\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)){3})$",
      "placeholder": "Your update server address"
    },
    {
      "name": "port",
      "type": "ACInput",
      "label": "port",
      "pattern": "[0-9]{1,4}"
    },
    {
      "name": "path",
      "type": "ACInput",
      "label": "path"
    },
    {
      "name": "apply",
      "type": "ACSubmit",
      "value": "Apply",
      "uri": "/apply"
    },
    {
      "name": "cancel",
      "type": "ACSubmit",
      "value": "Discard",
      "uri": "/"
    }
  ]
}
)";

// The /apply handler validates the update server settings entered on
// the setup page. APPLY_PAGE exists to enable the /apply handler,
// and its role is a page redirector. If the /apply handler detects some
// errors, the page will redirect to the /setup page with error message.
static const char APPLY_PAGE[] PROGMEM = R"(
{
  "title": "Update setup",
  "uri": "/apply",
  "menu": false,
  "element": [
    {
      "name": "redirect",
      "type": "ACElement",
      "value": "<script type=\"text/javascript\">location.href='__REDIRECT__';</script>"
    }
  ]
}
)";

WebServerClass    server;
AutoConnect       portal(server);
AutoConnectAux    setupPage;
AutoConnectAux    applyPage;
AutoConnectUpdate update;

#define UPDATESERVER_URL  ""    // Define to suit your environment 
#define UPDATESERVER_PORT 8000
#define UPDATESERVER_PATH "bin"

void loadAux() {
  setupPage.load(SETUP_PAGE);
  setupPage["server"].value = UPDATESERVER_URL;
  setupPage["port"].value = String(UPDATESERVER_PORT);
  setupPage["path"].value = UPDATESERVER_PATH;
  applyPage.load(APPLY_PAGE);
}

// The onSetup handler clears the error message field of the /setup page.
// Its field will be cleared after the /setup page generating by the
// effect of the AC_EXIT_LATER option.
String onSetup(AutoConnectAux& aux, PageArgument& arg) {
  setupPage["isvalid"].value = String();
  return String();
}

// The onApply handler validates the update server configuration.
// It does not do any semantic analysis but only verifies that the
// settings match the pattern defined in each field.
// The AutoConnectInput isValid function checks if the current value
// matches the pattern.
String onApply(AutoConnectAux& aux, PageArgument& arg) {
  String  returnUri;

  AutoConnectInput& host = setupPage["server"].as<AutoConnectInput>();
  AutoConnectInput& port = setupPage["port"].as<AutoConnectInput>();
  AutoConnectInput& path = setupPage["path"].as<AutoConnectInput>();

  Serial.printf("host: %s\n", host.value.c_str());
  Serial.printf("port: %s\n", port.value.c_str());
  Serial.printf("uri: %s\n", path.value.c_str());

  if (host.isValid() & port.isValid()) {
    update.host = host.value;
    update.port = port.value.toInt();
    update.uri = path.value;
    setupPage["isvalid"].value = String();
    returnUri = "/";
  }
  else {
    setupPage["isvalid"].value = String("Incorrect value specified.");
    returnUri = "/setup";
  }
  applyPage["redirect"].value.replace("__REDIRECT__", returnUri);
  return String();
}

void setup() {
  delay(1000);
  Serial.begin(115200);
  Serial.println();

  // Responder of root page and apply page  handled directly from WebServer class.
  server.on("/", []() {
    String content = R"(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8" name="viewport" content="width=device-width, initial-scale=1">
</head>
<body>
Place the root page with the sketch application.&ensp;
__AC_LINK__
</body>
</html>
    )";
    content.replace("__AC_LINK__", String(AUTOCONNECT_LINK(COG_16)));
    server.send(200, "text/html", content);
  });

  // AUX page loading
  loadAux();  
  setupPage.on(onSetup, AC_EXIT_LATER);
  applyPage.on(onApply);
  portal.join({ setupPage, applyPage });
  portal.begin();
  update.attach(portal);

//TFT setup:
    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(2);
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(0, 0);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(1);

    if (TFT_BL > 0) { // TFT_BL has been set in the TFT_eSPI library in the User Setup file TTGO_T_Display.h
         pinMode(TFT_BL, OUTPUT); // Set backlight pin to output mode
         digitalWrite(TFT_BL, TFT_BACKLIGHT_ON); // Turn backlight on. TFT_BACKLIGHT_ON has been set in the TFT_eSPI library in the User Setup file TTGO_T_Display.h
    }

    tft.setSwapBytes(true);
    tft.pushImage(0, 0,  240, 135, ttgo);
    espDelay(5000);

    tft.setRotation(0);
    int i = 5;
    while (i--) {
        tft.fillScreen(TFT_RED);
        espDelay(1000);
        tft.fillScreen(TFT_BLUE);
        espDelay(1000);
        tft.fillScreen(TFT_GREEN);
        espDelay(1000);
    }

    button_init();

    esp_adc_cal_characteristics_t adc_chars;
    esp_adc_cal_value_t val_type = esp_adc_cal_characterize((adc_unit_t)ADC_UNIT_1, (adc_atten_t)ADC1_CHANNEL_6, (adc_bits_width_t)ADC_WIDTH_BIT_12, 1100, &adc_chars);
    //Check type of calibration value used to characterize ADC
    if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF) {
        Serial.printf("eFuse Vref:%u mV", adc_chars.vref);
        vref = adc_chars.vref;
    } else if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP) {
        Serial.printf("Two Point --> coeff_a:%umV coeff_b:%umV\n", adc_chars.coeff_a, adc_chars.coeff_b);
    } else {
        Serial.println("Default Vref: 1100mV");
    }
  
}

void loop() {
  portal.handleClient(); // for autoconnect
  
      if (btnCick) {
        showVoltage();
    }
    button_loop();
}
