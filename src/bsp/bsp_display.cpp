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
}

bool bsp_display_init() {
    display.initialize();
    display.High_Power_Mode();
    display.display_on(true);
    display.display_Inversion(false);
    display.setRotation(1);
    display.clearDisplay();
    display.display();
    return true;
}

ST7305_2p9_BW_DisplayDriver &bsp_display() {
    return display;
}
