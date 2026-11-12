#pragma once
#include "FreeRTOS.h"
inline int xTaskCreate(void (*)(void*), const char*, int, void*, int, void** task) { *task = reinterpret_cast<void*>(1); return 1; }
inline void xTaskNotifyGive(void*) {}
inline unsigned ulTaskNotifyTake(int, unsigned) { return 0; }
inline void vTaskDelay(unsigned) {}
