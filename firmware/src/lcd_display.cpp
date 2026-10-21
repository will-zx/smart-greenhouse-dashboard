#include "lcd_display.h"
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

static LiquidCrystal_I2C lcd(LCD_ADDR, LCD_COLS, LCD_ROWS);

// Tulis satu baris, dipangkas + di-padding sampai LCD_COLS supaya sisa
// karakter baris sebelumnya tidak tertinggal (LCD tidak auto-clear).
static void lcdLine(uint8_t row, const char* text) {
    char buf[LCD_COLS + 1];
    snprintf(buf, sizeof(buf), "%-*.*s", LCD_COLS, LCD_COLS, text);
    lcd.setCursor(0, row);
    lcd.print(buf);
}

// Angka sensor yang tidak terbaca dikirim sebagai NaN -> tampilkan "--",
// bukan angka palsu (0,00 atau nilai basi).
static void fmtVal(char* buf, size_t n, float v, int dec, const char* suffix) {
    if (isnan(v)) snprintf(buf, n, "--%s", suffix);
    else snprintf(buf, n, "%.*f%s", dec, v, suffix);
}

void initLcd() {
    Wire.begin(I2C_SDA, I2C_SCL);   // bus sama dengan SHT31 (sensor.cpp); begin ganda aman
    lcd.begin(LCD_COLS, LCD_ROWS);
    lcd.backlight();
    lcdLine(0, "Smart Watering");
    lcdLine(1, "Starting...");
}

void updateLcd(float temp, float humidity, float soilMoisture, float vpd,
               float etc, float tds, const char* status) {
    char l0[24], l1[24], l2[24], l3[24];
    char t[12], h[12], s[12], v[12], e[12], d[12];
    fmtVal(t, sizeof(t), temp, 1, "C");
    fmtVal(h, sizeof(h), humidity, 1, "%");
    fmtVal(s, sizeof(s), soilMoisture, 1, "%");
    fmtVal(v, sizeof(v), vpd, 2, "");
    fmtVal(e, sizeof(e), etc, 1, "mm");
    fmtVal(d, sizeof(d), tds, 0, "");
    snprintf(l0, sizeof(l0), "Suhu %s RH %s", t, h);
    snprintf(l1, sizeof(l1), "Soil %s VPD %s", s, v);
    snprintf(l2, sizeof(l2), "ETc %s TDS %s", e, d);
    snprintf(l3, sizeof(l3), "Status: %s", status);
    lcdLine(0, l0);
    lcdLine(1, l1);
    lcdLine(2, l2);
    lcdLine(3, l3);
}
