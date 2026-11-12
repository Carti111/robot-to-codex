#pragma once
#define ESP_RETURN_ON_FALSE(condition, result, ...) do { if (!(condition)) return (result); } while (0)
