#include "oled_display.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

static Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);

void initOled() {
    Wire.begin(OLED_SDA, OLED_SCL);
    display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Smart Watering");
    display.println("Starting...");
    display.display();
}

void updateOled(float temp, float humidity, float soilMoisture, float vpd,
                const char* status) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("Suhu: "); display.print(temp, 1); display.println("C");
    display.setCursor(0, 12);
    display.print("Hum: "); display.print(humidity, 1); display.println("%");
    display.setCursor(0, 24);
    display.print("Soil: "); display.print(soilMoisture, 1); display.println("%");
    display.setCursor(0, 36);
    display.print("VPD: "); display.print(vpd, 2); display.println("kPa");
    display.setCursor(0, 52);
    display.print("Status: "); display.println(status);
    display.display();
}