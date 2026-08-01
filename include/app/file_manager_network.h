#pragma once

#include <Arduino.h>

void file_manager_network_init();
bool file_manager_network_start_ap();
void file_manager_network_stop_ap();
void file_manager_network_process_ap();
bool file_manager_network_ap_active();
String file_manager_network_ap_message();
