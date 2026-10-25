#include "lcd_display.h"
#include "sensor.h"    // i2cLock/i2cUnlock: bus I2C dipakai bersama SHT31
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

static LiquidCrystal_I2C lcd(LCD_ADDR, LCD_COLS, LCD_ROWS);

// Teks baris 4 yang digeser. Ditulis controlTask, dibaca task scroll.
static char scrollText[96] = "";
static SemaphoreHandle_t textMux = nullptr;
static volatile bool scrollReset = true;   // teks berubah -> mulai dari awal

// Tulis satu baris, dipangkas + di-padding sampai LCD_COLS supaya sisa
// karakter baris sebelumnya tidak tertinggal (LCD tidak auto-clear).
// Gagal dapat bus I2C = lewati; menunggu LCD tidak boleh menahan kontrol.
static void lcdLine(uint8_t row, const char* text) {
    char buf[LCD_COLS + 1];
    snprintf(buf, sizeof(buf), "%-*.*s", LCD_COLS, LCD_COLS, text);
    if (!i2cLock(pdMS_TO_TICKS(50))) return;
    lcd.setCursor(0, row);
    lcd.print(buf);
    i2cUnlock();
}

// Angka sensor yang tidak terbaca dikirim sebagai NaN -> tampilkan "--",
// bukan angka palsu (0,00 atau nilai basi).
static void fmtVal(char* buf, size_t n, float v, int dec, const char* suffix) {
    if (isnan(v)) snprintf(buf, n, "--%s", suffix);
    else snprintf(buf, n, "%.*f%s", dec, v, suffix);
}

void initLcd() {
    if (!textMux) textMux = xSemaphoreCreateMutex();
    Wire.begin(I2C_SDA, I2C_SCL);   // bus sama dengan SHT31 (sensor.cpp); begin ganda aman
    lcd.begin(LCD_COLS, LCD_ROWS);
    lcd.backlight();
    lcdLine(0, "Smart Watering");
    lcdLine(1, "Starting...");
}

// Baris 0-2 saja. Baris 4 ditulis task scroll (lcdScrollTask).
void updateLcd(float temp, float humidity, float soilMoisture, float vpd,
               float etc, float tds) {
    char l0[24], l1[24], l2[24];
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
    lcdLine(0, l0);
    lcdLine(1, l1);
    lcdLine(2, l2);
}

void lcdScrollSetStatus(const char* status) {
    if (!status || !textMux) return;
    xSemaphoreTake(textMux, portMAX_DELAY);
    if (strcmp(scrollText, status) != 0) {
        snprintf(scrollText, sizeof(scrollText), "%s", status);
        scrollReset = true;
    }
    xSemaphoreGive(textMux);
}

// Marquee baris 4. Teks <= 20 kolom ditulis statis; lebih dari itu digeser
// satu karakter per LCD_SCROLL_MS, dengan jeda LCD_SCROLL_HOLD_MS di awal dan
// akhir supaya sempat dibaca. Task ini sengaja TIDAK subscribe Task WDT: kalau
// macet, hanya baris status yang berhenti, kontrol/aktuator tidak ikut.
static void lcdScrollTask(void* arg) {
    char text[96];
    char win[LCD_COLS + 1];
    char shown[LCD_COLS + 1] = "";   // isi baris 4 yang sudah benar-benar ditulis
    int  off = 0;
    bool hold = true;
    int  holdTicks = 0;
    TickType_t lastWake = xTaskGetTickCount();

    for (;;) {
        xSemaphoreTake(textMux, portMAX_DELAY);
        snprintf(text, sizeof(text), "%s", scrollText);
        bool reset = scrollReset;
        scrollReset = false;
        xSemaphoreGive(textMux);

        int len = (int)strlen(text);
        if (reset) {
            off = 0;
            hold = true;
            holdTicks = 0;
        }
        if (off > 0 && off > len - LCD_COLS) off = 0;   // teks memendek

        if (len <= LCD_COLS) {
            snprintf(win, sizeof(win), "%-*.*s", LCD_COLS, LCD_COLS, text);
            off = 0;
            hold = true;
            holdTicks = 0;
        } else {
            snprintf(win, sizeof(win), "%-*.*s", LCD_COLS, off + LCD_COLS, text + off);
            if (hold) {
                if (++holdTicks * LCD_SCROLL_MS >= LCD_SCROLL_HOLD_MS) {
                    hold = false;
                    holdTicks = 0;
                }
            } else if (off >= len - LCD_COLS) {
                // Jendela terakhir ditahan dulu sebelum kembali ke awal,
                // supaya ujung teks (penanda error) sempat terbaca.
                if (++holdTicks * LCD_SCROLL_MS >= LCD_SCROLL_HOLD_MS) {
                    off = 0;
                    hold = true;
                    holdTicks = 0;
                }
            } else {
                off++;
            }
        }

        // Tulis hanya kalau isinya berubah: teks pendek atau jeda tahan tidak
        // perlu membanjiri bus I2C yang sama dengan SHT31 (~57 tulis/detik).
        // "shown" ditandai hanya kalau tulis sukses, supaya lock yang gagal
        // dicoba lagi di tick berikutnya.
        if (strcmp(win, shown) != 0 && i2cLock(pdMS_TO_TICKS(50))) {
            lcd.setCursor(0, LCD_ROWS - 1);
            lcd.print(win);
            i2cUnlock();
            snprintf(shown, sizeof(shown), "%s", win);
        }
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(LCD_SCROLL_MS));
    }
}

void lcdScrollStart() {
    if (!textMux) textMux = xSemaphoreCreateMutex();
    xTaskCreatePinnedToCore(lcdScrollTask, "lcd", LCD_SCROLL_STACK, nullptr,
                            LCD_SCROLL_PRIO, nullptr, 0);
}
