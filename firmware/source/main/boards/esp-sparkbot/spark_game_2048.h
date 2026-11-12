// Movement rules adapted from the official ESP-SparkBot factory example.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { uint16_t cells[4][4]; uint32_t score; } SparkGame2048;
void spark_game_reset(SparkGame2048* game);
bool spark_game_move(SparkGame2048* game, int direction);
bool spark_game_over(SparkGame2048* game);
#ifdef __cplusplus
}
#endif
