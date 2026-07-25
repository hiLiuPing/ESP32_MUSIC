#include "bsp/bsp_display.h"

#include <SPI.h>

#include "bsp/board_config.h"

namespace {
ST7305_2p9_BW_DisplayDriver display(
    BoardConfig::DisplayDc,
    BoardConfig::DisplayReset,
    BoardConfig::DisplayCs,
    BoardConfig::DisplayClock,
    BoardConfig::DisplayData,
    SPI);
U8G2_FOR_ST73XX fonts;
}

bool bsp_display_init() {
    display.initialize();
    display.High_Power_Mode();
    display.display_on(true);
    display.display_Inversion(false);
    display.setRotation(1);
    fonts.begin(display);
    fonts.setFont(u8g2_font_wqy12_t_gb2312);
    fonts.setFontMode(1);
    fonts.setForegroundColor(ST7305_COLOR_BLACK);
    display.clearDisplay();
    display.display();
    return true;
}

ST7305_2p9_BW_DisplayDriver &bsp_display() {
    return display;
}

U8G2_FOR_ST73XX &bsp_fonts() {
    return fonts;
}
