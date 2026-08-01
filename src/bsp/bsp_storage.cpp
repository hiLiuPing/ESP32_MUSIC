#include "bsp/bsp_storage.h"

#include <SD.h>
#include <SPI.h>

#include "bsp/board_config.h"

namespace {
SPIClass sd_spi(HSPI);
bool mounted = false;
}

bool bsp_storage_init() {
    if (mounted && (SD.cardType() != CARD_NONE)) {
        return true;
    }
    if (mounted) {
        SD.end();
        mounted = false;
    }
    sd_spi.begin(BoardConfig::SdClock, BoardConfig::SdMiso,
                 BoardConfig::SdMosi, BoardConfig::SdCs);
    mounted = SD.begin(BoardConfig::SdCs, sd_spi, BoardConfig::SdFrequency);
    return mounted;
}

bool bsp_storage_available() {
    return mounted && (SD.cardType() != CARD_NONE);
}

fs::FS &bsp_storage_fs() {
    return SD;
}

size_t bsp_storage_total_bytes() {
    return bsp_storage_available() ? SD.totalBytes() : 0U;
}

size_t bsp_storage_used_bytes() {
    return bsp_storage_available() ? SD.usedBytes() : 0U;
}
