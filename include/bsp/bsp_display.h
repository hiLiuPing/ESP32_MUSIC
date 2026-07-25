#pragma once

#include "ST7305_2p9_BW_DisplayDriver.h"
#include "U8g2_for_ST73XX.h"

bool bsp_display_init();
ST7305_2p9_BW_DisplayDriver &bsp_display();
U8G2_FOR_ST73XX &bsp_fonts();
