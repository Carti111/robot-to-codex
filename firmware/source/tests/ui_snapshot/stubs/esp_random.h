#pragma once
#include <cstdlib>
inline unsigned esp_random() { return static_cast<unsigned>(rand()); }
