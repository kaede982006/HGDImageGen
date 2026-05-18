#ifndef __TYPE_H__
#define __TYPE_H__

#include <stdint.h>

typedef struct {
    unsigned char r;
    unsigned char g;
    unsigned char b;
    unsigned char a;
} Pixel;

typedef struct {
    Pixel* pixels;
    int width;
    int height;
} Image;

typedef struct {
    char* data;
    size_t len;
    size_t cap;
} StringBuilder;

typedef struct {
    uint32_t key;
    int channel_id;
    bool used;
} ColorMapSlot;

typedef struct {
    ColorMapSlot* slots;
    size_t cap;
    size_t size;
} ColorMap;

typedef struct {
    int channel_id;
    int r;
    int g;
    int b;
    int a;
} ColorChannelDef;

typedef enum {
    FILTER_NEAREST,
    FILTER_BILINEAR
} ResizeFilter;

typedef struct {
    const char* input_path;
    const char* output_path;
    const char* level_name;
    const char* log_path;

    int target_width;
    int target_height;
    double scale;

    bool grayscale;
    ResizeFilter filter;

    int tile_size;
    int start_x;
    int start_y;
    int max_channels;
    int max_objects;
    int quant_step;
    int alpha_step;
    int alpha_skip_below;
    int object_id;
} Options;

#endif