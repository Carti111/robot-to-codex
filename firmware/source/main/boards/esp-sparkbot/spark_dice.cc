/*
 * Adapted from ESP-SparkBot tinyGL_cube.c.
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */
#include "spark_dice.h"
#include "spark_factory_assets.h"
#include <esp_heap_caps.h>
extern "C" {
#include "glu.h"
}

SparkDice::~SparkDice() {
    if (buffer_) {
        glDeleteTextures(6, textures_);
        glClose();
        ZB_close(buffer_);
    }
    heap_caps_free(pixels_);
}

bool SparkDice::Start() {
    if (buffer_) return true;
    pixels_ = static_cast<uint16_t*>(heap_caps_malloc(200 * 200 * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!pixels_) return false;
    buffer_ = ZB_open(200, 200, ZB_MODE_RGB24, 0, nullptr, nullptr, nullptr);
    if (!buffer_) {
        heap_caps_free(pixels_);
        pixels_ = nullptr;
        return false;
    }
    glInit(buffer_);
    glClearColor(0, 0, 0, 1);
    glViewport(0, 0, 200, 200);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);
    glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
    glGenTextures(6, textures_);
    for (int i = 0; i < 6; ++i) {
        glBindTexture(GL_TEXTURE_2D, textures_[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, 3, 120, 120, 0, GL_RGB, GL_UNSIGNED_BYTE, const_cast<uint8_t*>(spark_dice_textures[i]));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    }
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(50, 1, 1, 300);
    glMatrixMode(GL_MODELVIEW);
    image_.header.magic = LV_IMAGE_HEADER_MAGIC;
    image_.header.cf = LV_COLOR_FORMAT_RGB565;
    image_.header.w = 200;
    image_.header.h = 200;
    image_.header.stride = 400;
    image_.data_size = 200 * 200 * 2;
    image_.data = reinterpret_cast<uint8_t*>(pixels_);
    Draw(0, 0, 0);
    return true;
}

void SparkDice::Draw(float x, float y, float z) {
    if (!buffer_) return;
    static const float vertices[8][3] = {
        {-1, 1, 1}, {-1, -1, 1}, {1, -1, 1}, {1, 1, 1},
        {-1, 1, -1}, {-1, -1, -1}, {1, -1, -1}, {1, 1, -1}
    };
    static const int faces[6][4] = {
        {0, 1, 2, 3}, {0, 3, 7, 4}, {4, 7, 6, 5},
        {5, 6, 2, 1}, {7, 3, 2, 6}, {5, 1, 0, 4}
    };
    static const float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    static const int texture_order[6][4] = {
        {3, 0, 1, 2}, {1, 2, 3, 0}, {2, 3, 0, 1},
        {3, 0, 1, 2}, {3, 0, 1, 2}, {3, 0, 1, 2}
    };
    glLoadIdentity();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glTranslatef(0, 0, -6);
    glRotatef(x, 1, 0, 0);
    glRotatef(y, 0, 1, 0);
    glRotatef(z, 0, 0, 1);
    for (int face = 0; face < 6; ++face) {
        glBindTexture(GL_TEXTURE_2D, textures_[face]);
        glBegin(GL_QUADS);
        for (int corner = 0; corner < 4; ++corner) {
            const auto& coordinate = uv[texture_order[face][corner]];
            const auto& vertex = vertices[faces[face][corner]];
            glTexCoord2f(coordinate[0], coordinate[1]);
            glVertex3f(vertex[0], vertex[1], vertex[2]);
        }
        glEnd();
    }
    const auto source = reinterpret_cast<const uint8_t*>(buffer_->pbuf);
    for (int i = 0; i < 200 * 200; ++i) {
        uint8_t r = source[i * 3 + 2];
        uint8_t g = source[i * 3 + 1];
        uint8_t b = source[i * 3];
        pixels_[i] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    }
}
