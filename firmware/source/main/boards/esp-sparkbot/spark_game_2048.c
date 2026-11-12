// Original movement rules from esp-friends/esp_sparkbot factory_demo_v1.
#include "spark_game_2048.h"
#include <stdlib.h>
#include <time.h>
#include <string.h>
#define MATRIX_SIZE 4
static void addRandom(uint16_t matrix[4][4]);
static void rotate_matrix(uint16_t matrix[4][4]);
static bool find_pair_down(uint16_t matrix[4][4]);
static bool game_over(uint16_t matrix[4][4]);
static void init_matrix_num(uint16_t matrix[MATRIX_SIZE][MATRIX_SIZE])
{
    uint8_t x, y;

    for (x = 0; x < MATRIX_SIZE; x++) {
        for (y = 0; y < MATRIX_SIZE; y++) {
            matrix[x][y] = 0;
        }
    }

    addRandom(matrix);
    addRandom(matrix);
}

static void addRandom(uint16_t matrix[MATRIX_SIZE][MATRIX_SIZE])
{
    static bool initialized = false;
    uint16_t x, y;
    uint16_t r, len = 0;
    uint16_t n, list[MATRIX_SIZE * MATRIX_SIZE][2];

    if (!initialized) {
        srand(time(NULL));
        initialized = true;
    }

    for (x = 0; x < MATRIX_SIZE; x++) {
        for (y = 0; y < MATRIX_SIZE; y++) {
            if (matrix[x][y] == 0) {
                list[len][0] = x;
                list[len][1] = y;
                len++;
            }
        }
    }

    if (len > 0) {
        r = rand() % len;
        x = list[r][0];
        y = list[r][1];
        n = ((rand() % 10) / 9) + 1;
        matrix[x][y] = n;
    }
}



static uint8_t find_target(uint16_t array[MATRIX_SIZE], uint8_t x, uint8_t stop)
{
    uint8_t t;
    // if the position is already on the first, don't evaluate
    if (x == 0) {
        return x;
    }
    for (t = x - 1; ; t--) {
        if (array[t] != 0) {
            if (array[t] != array[x]) {
                // merge is not possible, take next position
                return t + 1;
            }
            return t;
        } else {
            // we should not slide further, return this one
            if (t == stop) {
                return t;
            }
        }
    }
    // we did not find a
    return x;
}

static bool slide_array(uint32_t * score, uint16_t array[MATRIX_SIZE])
{
    bool success = false;
    uint8_t x, t, stop = 0;

    for (x = 0; x < MATRIX_SIZE; x++) {
        if (array[x] != 0) {
            t = find_target(array, x, stop);
            // if target is not original position, then move or merge
            if (t != x) {
                // if target is zero, this is a move
                if (array[t] == 0) {
                    array[t] = array[x];
                } else if (array[t] == array[x]) {
                    // merge (increase power of two)
                    array[t]++;
                    // increase score
                    *score += (uint32_t)1 << array[t];
                    // set stop to avoid double merge
                    stop = t + 1;
                }
                array[x] = 0;
                success = true;
            }
        }
    }
    return success;
}

static void rotate_matrix(uint16_t matrix[MATRIX_SIZE][MATRIX_SIZE])
{
    uint8_t i, j, n = MATRIX_SIZE;
    uint16_t tmp;

    for (i = 0; i < (n / 2); i++) {
        for (j = i; j < (n - i - 1); j++) {
            tmp = matrix[i][j];
            matrix[i][j] = matrix[j][n - i - 1];
            matrix[j][n - i - 1] = matrix[n - i - 1][n - j - 1];
            matrix[n - i - 1][n - j - 1] = matrix[n - j - 1][i];
            matrix[n - j - 1][i] = tmp;
        }
    }
}

static bool move_up(uint32_t * score, uint16_t matrix[MATRIX_SIZE][MATRIX_SIZE])
{
    bool success = false;
    uint8_t x;

    for (x = 0; x < MATRIX_SIZE; x++) {
        success |= slide_array(score, matrix[x]);
    }
    return success;
}

static bool move_left(uint32_t * score, uint16_t matrix[MATRIX_SIZE][MATRIX_SIZE])
{
    bool success;

    rotate_matrix(matrix);

    success = move_up(score, matrix);
    rotate_matrix(matrix);
    rotate_matrix(matrix);
    rotate_matrix(matrix);

    return success;
}

static bool move_down(uint32_t * score, uint16_t matrix[MATRIX_SIZE][MATRIX_SIZE])
{
    bool success;

    rotate_matrix(matrix);
    rotate_matrix(matrix);

    success = move_up(score, matrix);
    rotate_matrix(matrix);
    rotate_matrix(matrix);

    return success;
}

static bool move_right(uint32_t * score, uint16_t matrix[MATRIX_SIZE][MATRIX_SIZE])
{
    bool success;

    rotate_matrix(matrix);
    rotate_matrix(matrix);
    rotate_matrix(matrix);

    success = move_up(score, matrix);
    rotate_matrix(matrix);

    return success;
}

static bool find_pair_down(uint16_t matrix[MATRIX_SIZE][MATRIX_SIZE])
{
    bool success = false;
    uint8_t x, y;

    for (x = 0; x < MATRIX_SIZE; x++) {
        for (y = 0; y < (MATRIX_SIZE - 1); y++) {
            if (matrix[x][y] == matrix[x][y + 1]) {
                return true;
            }
        }
    }
    return success;
}

static uint8_t count_empty(uint16_t matrix[MATRIX_SIZE][MATRIX_SIZE])
{
    uint8_t x, y;
    uint8_t count = 0;

    for (x = 0; x < MATRIX_SIZE; x++) {
        for (y = 0; y < MATRIX_SIZE; y++) {
            if (matrix[x][y] == 0) {
                count++;
            }
        }
    }
    return count;
}


static bool game_over(uint16_t matrix[MATRIX_SIZE][MATRIX_SIZE])
{
    bool ended = true;

    if (count_empty(matrix) > 0) {
        return false;
    }
    if (find_pair_down(matrix)) {
        return false;
    }

    rotate_matrix(matrix);
    if (find_pair_down(matrix)) {
        ended = false;
    }

    rotate_matrix(matrix);
    rotate_matrix(matrix);
    rotate_matrix(matrix);

    return ended;
}

void spark_game_reset(SparkGame2048* game) { game->score = 0; init_matrix_num(game->cells); }
bool spark_game_move(SparkGame2048* game, int direction) {
    bool moved = false;
    switch (direction) {
        case 0: moved = move_up(&game->score, game->cells); break;
        case 1: moved = move_right(&game->score, game->cells); break;
        case 2: moved = move_down(&game->score, game->cells); break;
        case 3: moved = move_left(&game->score, game->cells); break;
    }
    if (moved) addRandom(game->cells);
    return moved;
}
bool spark_game_over(SparkGame2048* game) { return game_over(game->cells); }
