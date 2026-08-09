#pragma once

#include <cstdint>

#ifndef HOME_DEMO_ENABLE
#define HOME_DEMO_ENABLE 0
#endif

void home_demo_init(uint32_t now_ms);
void home_demo_service(uint32_t now_ms);
