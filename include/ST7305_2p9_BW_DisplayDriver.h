#ifndef ST7305_2P9_BW_DISPLAY_DRIVER_H
#define ST7305_2P9_BW_DISPLAY_DRIVER_H

#include <Arduino.h>
#include <SPI.h>
#include "ST73XX_UI.h"

#define ST7305_COLOR_WHITE (0x00)
#define ST7305_COLOR_BLACK (0x01)

#define ST73XX_COLOR_BLACK ST7305_COLOR_BLACK
#define ST73XX_COLOR_WHITE ST7305_COLOR_WHITE

enum class ST7305QuantizeMode : uint8_t {
    Bayer4x4 = 0,
    Threshold,
    MonoAsset,
};

class ST7305_2p9_BW_DisplayDriver : public ST73XX_UI{
public:
    ST7305_2p9_BW_DisplayDriver(int dcPin, int resPin, int csPin, int sclkPin, int sdinPin, SPIClass& spi);
    ~ST7305_2p9_BW_DisplayDriver();

    void initialize();
    void fill(uint8_t data);

    void clearDisplay();

    void blitRgb565(int16_t x, int16_t y, int16_t width, int16_t height,
                    const uint16_t *pixels);
    void setQuantizeMode(ST7305QuantizeMode mode);
    ST7305QuantizeMode getQuantizeMode() const;

    void writePoint(uint x, uint y, bool enabled) override;
    void writePoint(uint x, uint y, uint16_t data) override;

    void display();

    void Initial_ST7305();
    void Low_Power_Mode();
    void High_Power_Mode();
    void display_on(bool enabled);
    void display_sleep(bool enabled);
    void display_Inversion(bool enabled);

private:
    const int DC_PIN;
    const int RES_PIN;
    const int CS_PIN;
    const int SCLK_PIN;
    const int SDIN_PIN;
    const int LCD_WIDTH;
    const int LCD_HIGH;
    const int LCD_DATA_WIDTH;
    const int LCD_DATA_HIGH;
    const int DISPLAY_BUFFER_LENGTH;

    bool HPM_MODE = false;
    bool LPM_MODE = false;
    ST7305QuantizeMode quantize_mode = ST7305QuantizeMode::Threshold;

    uint8_t* display_buffer;
    SPIClass& spiRef;

    void address();
    void Write_Register(uint8_t idat);
    void Write_Parameter(uint8_t ddat);
    void writePackedPoint(uint16_t x, uint16_t y, bool enabled);
};

#endif
