#pragma once
#include <esp_err.h>

// Initializes peripherals, NVS, networking, etc.
void setup();

// Called repeatedly (either from a while(1) or a FreeRTOS task)
void loop();

// Optional: start loop in its own FreeRTOS task instead of blocking app_main
void start_loop_task(unsigned stackSize = 4096, UBaseType_t priority = 5, BaseType_t core = tskNO_AFFINITY);
