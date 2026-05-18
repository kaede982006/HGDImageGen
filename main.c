/*
 * HGDImageGen - PNG image to Geometry Dash .gmd converter
 *
 * Dependencies:
 *   libpng, zlib
 *
 * Visual Studio x64 build:
 *   Open HGDImageGen.slnx
 *   Select Debug | x64 or Release | x64
 *   Runtime library: Debug=/MTd, Release=/MT
 *   Build Solution
 *
 * CLI example:
 *   HGDImageGen.exe -i Test.png -o Test.gmd -n Test --width 64 --height 64
 */

#include <png.h>
#include <zlib.h>

#include <errno.h>
#include <math.h>

#ifdef _MSC_VER
#include <float.h>
#ifndef isfinite
#define isfinite(x) _finite(x)
#endif
#endif

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HGD_VERSION "1.0.0"

#define DEFAULT_TILE_SIZE 30
#define DEFAULT_START_X 15
#define DEFAULT_START_Y 15
#define DEFAULT_MAX_CHANNELS 999
#define DEFAULT_MAX_OBJECTS 50000
#define DEFAULT_QUANT_STEP 32
#define DEFAULT_ALPHA_STEP 32
#define DEFAULT_OBJECT_ID 211

#define DEFAULT_LEVEL_NAME "HGDImageGen"
#define DEFAULT_OUTPUT_PATH "output.gmd"
#define DEFAULT_LOG_PATH "hgdimagegen.log"

#define COLOR_MAP_LOAD_FACTOR_NUM 7
#define COLOR_MAP_LOAD_FACTOR_DEN 10

typedef struct {
    unsigned char r;
    unsigned char g;
    unsigned char b;
    unsigned char a;
} Pixel;

typedef struct {
    Pixel *pixels;
    int width;
    int height;
} Image;

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} StringBuilder;

typedef struct {
    uint32_t key;
    int channel_id;
    bool used;
} ColorMapSlot;

typedef struct {
    ColorMapSlot *slots;
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
    const char *input_path;
    const char *output_path;
    const char *level_name;
    const char *log_path;

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

static void die(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "[ERROR] ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
    exit(EXIT_FAILURE);
}


static void *xmalloc(size_t size) {
    void *ptr = malloc(size == 0 ? 1 : size);
    if (!ptr) {
        die("out of memory");
    }
    return ptr;
}

static void *xcalloc(size_t count, size_t size) {
    void *ptr = calloc(count == 0 ? 1 : count, size == 0 ? 1 : size);
    if (!ptr) {
        die("out of memory");
    }
    return ptr;
}

static size_t checked_pixel_bytes(int width, int height) {
    if (width <= 0 || height <= 0) {
        die("invalid image size");
    }

    size_t w = (size_t)width;
    size_t h = (size_t)height;

    if (w > SIZE_MAX / h) {
        die("image size overflow");
    }

    size_t count = w * h;

    if (count > SIZE_MAX / sizeof(Pixel)) {
        die("image allocation overflow");
    }

    return count * sizeof(Pixel);
}


static void sb_init(StringBuilder *sb) {
    sb->cap = 4096;
    sb->len = 0;
    sb->data = (char *)xmalloc(sb->cap);
    sb->data[0] = '\0';
}

static void sb_free(StringBuilder *sb) {
    free(sb->data);
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

static void sb_reserve(StringBuilder *sb, size_t extra) {
    if (extra > SIZE_MAX - sb->len - 1) {
        die("string size overflow");
    }

    size_t need = sb->len + extra + 1;
    if (need <= sb->cap) {
        return;
    }

    while (sb->cap < need) {
        if (sb->cap > SIZE_MAX / 2) {
            die("string capacity overflow");
        }
        sb->cap *= 2;
    }

    char *new_data = (char *)realloc(sb->data, sb->cap);
    if (!new_data) {
        die("out of memory while resizing string");
    }
    sb->data = new_data;
}

static void sb_append_n(StringBuilder *sb, const char *s, size_t n) {
    sb_reserve(sb, n);
    memcpy(sb->data + sb->len, s, n);
    sb->len += n;
    sb->data[sb->len] = '\0';
}

static void sb_append(StringBuilder *sb, const char *s) {
    sb_append_n(sb, s, strlen(s));
}

static void sb_appendf(StringBuilder *sb, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    va_list copy;
    va_copy(copy, args);
    int needed = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);

    if (needed < 0) {
        va_end(args);
        die("formatting failed");
    }

    sb_reserve(sb, (size_t)needed);
    int written = vsnprintf(sb->data + sb->len, sb->cap - sb->len, fmt, args);
    va_end(args);

    if (written < 0) {
        die("formatting failed");
    }

    sb->len += (size_t)written;
}

static void sb_append_xml_escaped(StringBuilder *sb, const char *s) {
    for (; *s; s++) {
        switch (*s) {
            case '&': sb_append(sb, "&amp;"); break;
            case '<': sb_append(sb, "&lt;"); break;
            case '>': sb_append(sb, "&gt;"); break;
            case '"': sb_append(sb, "&quot;"); break;
            case '\'': sb_append(sb, "&apos;"); break;
            default: sb_append_n(sb, s, 1); break;
        }
    }
}

static int parse_int_arg(const char *text, const char *name) {
    errno = 0;
    char *end = NULL;
    long value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value < INT32_MIN || value > INT32_MAX) {
        die("invalid integer for %s: %s", name, text);
    }
    return (int)value;
}

static double parse_double_arg(const char *text, const char *name) {
    errno = 0;
    char *end = NULL;
    double value = strtod(text, &end);
    if (errno != 0 || end == text || *end != '\0' || !isfinite(value)) {
        die("invalid number for %s: %s", name, text);
    }
    return value;
}

static int clamp_int(int value, int lo, int hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

static int round_to_int(double x) {
    if (x >= 0.0) {
        return (int)(x + 0.5);
    }
    return (int)(x - 0.5);
}

static int quantize_channel(int value, int step) {
    value = clamp_int(value, 0, 255);
    if (step <= 1) {
        return value;
    }
    int q = ((value + step / 2) / step) * step;
    return clamp_int(q, 0, 255);
}

static uint32_t rgba_key(int r, int g, int b, int a) {
    return ((uint32_t)(unsigned)r << 24) |
           ((uint32_t)(unsigned)g << 16) |
           ((uint32_t)(unsigned)b << 8) |
           (uint32_t)(unsigned)a;
}

static uint32_t hash_u32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

static size_t next_power_of_two(size_t x) {
    size_t p = 1;
    while (p < x) {
        if (p > SIZE_MAX / 2) {
            die("capacity overflow");
        }
        p *= 2;
    }
    return p;
}

static void color_map_init(ColorMap *map, size_t expected_max_items) {
    size_t min_cap = (expected_max_items * COLOR_MAP_LOAD_FACTOR_DEN) / COLOR_MAP_LOAD_FACTOR_NUM + 8;
    map->cap = next_power_of_two(min_cap);
    map->size = 0;
    map->slots = (ColorMapSlot *)xcalloc(map->cap, sizeof(ColorMapSlot));
}

static void color_map_free(ColorMap *map) {
    free(map->slots);
    map->slots = NULL;
    map->cap = 0;
    map->size = 0;
}

static int color_map_get(const ColorMap *map, uint32_t key) {
    uint32_t h = hash_u32(key);
    for (size_t i = 0; i < map->cap; i++) {
        size_t idx = ((size_t)h + i) & (map->cap - 1);
        if (!map->slots[idx].used) {
            return -1;
        }
        if (map->slots[idx].key == key) {
            return map->slots[idx].channel_id;
        }
    }
    return -1;
}

static void color_map_put(ColorMap *map, uint32_t key, int channel_id) {
    if ((map->size + 1) * COLOR_MAP_LOAD_FACTOR_DEN > map->cap * COLOR_MAP_LOAD_FACTOR_NUM) {
        die("internal color map is unexpectedly full");
    }

    uint32_t h = hash_u32(key);
    for (size_t i = 0; i < map->cap; i++) {
        size_t idx = ((size_t)h + i) & (map->cap - 1);
        if (!map->slots[idx].used) {
            map->slots[idx].used = true;
            map->slots[idx].key = key;
            map->slots[idx].channel_id = channel_id;
            map->size++;
            return;
        }
        if (map->slots[idx].key == key) {
            map->slots[idx].channel_id = channel_id;
            return;
        }
    }

    die("internal color map insertion failed");
}

static void image_free(Image *img) {
    free(img->pixels);
    img->pixels = NULL;
    img->width = 0;
    img->height = 0;
}

static void png_read_error_handler(png_structp png, png_const_charp msg) {
    const char *path = (const char *)png_get_error_ptr(png);
    die("libpng error reading '%s': %s", path ? path : "?", msg);
}

static Image load_png_rgba(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        die("failed to open input PNG: %s", path);
    }

    unsigned char sig[8];
    if (fread(sig, 1, 8, fp) != 8 || png_sig_cmp(sig, 0, 8)) {
        fclose(fp);
        die("not a valid PNG file: %s", path);
    }

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING,
        (png_voidp)path, png_read_error_handler, NULL);
    if (!png) {
        fclose(fp);
        die("png_create_read_struct failed");
    }

    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, NULL, NULL);
        fclose(fp);
        die("png_create_info_struct failed");
    }

    png_init_io(png, fp);
    png_set_sig_bytes(png, 8);
    png_read_info(png, info);

    png_uint_32 width = png_get_image_width(png, info);
    png_uint_32 height = png_get_image_height(png, info);
    int bit_depth = png_get_bit_depth(png, info);
    int color_type = png_get_color_type(png, info);

    if (width == 0 || height == 0 || width > INT32_MAX || height > INT32_MAX) {
        die("unsupported PNG size");
    }

    if (bit_depth == 16) {
        png_set_strip_16(png);
    }

    if (color_type == PNG_COLOR_TYPE_PALETTE) {
        png_set_palette_to_rgb(png);
    }

    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) {
        png_set_expand_gray_1_2_4_to_8(png);
    }

    if (png_get_valid(png, info, PNG_INFO_tRNS)) {
        png_set_tRNS_to_alpha(png);
    }

    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
        png_set_gray_to_rgb(png);
    }

    if (color_type == PNG_COLOR_TYPE_RGB ||
        color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_PALETTE) {
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    }

    png_read_update_info(png, info);

    png_size_t rowbytes = png_get_rowbytes(png, info);
    if (rowbytes != width * 4) {
        die("unexpected PNG row format");
    }

    size_t pixel_bytes = checked_pixel_bytes((int)width, (int)height);
    Pixel *pixels = (Pixel *)xmalloc(pixel_bytes);

    png_bytep *rows = (png_bytep *)xmalloc((size_t)height * sizeof(png_bytep));
    for (png_uint_32 y = 0; y < height; y++) {
        rows[y] = (png_bytep)&pixels[(size_t)y * (size_t)width];
    }

    png_read_image(png, rows);
    png_read_end(png, NULL);

    free(rows);
    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);

    Image img;
    img.pixels = pixels;
    img.width = (int)width;
    img.height = (int)height;
    return img;
}

static Pixel pixel_lerp(Pixel a, Pixel b, double t) {
    Pixel out;
    out.r = (unsigned char)clamp_int(round_to_int((double)a.r + ((double)b.r - (double)a.r) * t), 0, 255);
    out.g = (unsigned char)clamp_int(round_to_int((double)a.g + ((double)b.g - (double)a.g) * t), 0, 255);
    out.b = (unsigned char)clamp_int(round_to_int((double)a.b + ((double)b.b - (double)a.b) * t), 0, 255);
    out.a = (unsigned char)clamp_int(round_to_int((double)a.a + ((double)b.a - (double)a.a) * t), 0, 255);
    return out;
}

static Image resize_nearest(const Image *src, int new_w, int new_h) {
    Image out;
    out.width = new_w;
    out.height = new_h;
    out.pixels = (Pixel *)xmalloc(checked_pixel_bytes(new_w, new_h));

    for (int y = 0; y < new_h; y++) {
        int sy = (int)(((int64_t)y * src->height) / new_h);
        if (sy >= src->height) sy = src->height - 1;

        for (int x = 0; x < new_w; x++) {
            int sx = (int)(((int64_t)x * src->width) / new_w);
            if (sx >= src->width) sx = src->width - 1;

            out.pixels[(size_t)y * new_w + x] = src->pixels[(size_t)sy * src->width + sx];
        }
    }

    return out;
}

static Image resize_bilinear(const Image *src, int new_w, int new_h) {
    Image out;
    out.width = new_w;
    out.height = new_h;
    out.pixels = (Pixel *)xmalloc(checked_pixel_bytes(new_w, new_h));

    double x_ratio = (new_w > 1) ? (double)(src->width - 1) / (double)(new_w - 1) : 0.0;
    double y_ratio = (new_h > 1) ? (double)(src->height - 1) / (double)(new_h - 1) : 0.0;

    for (int y = 0; y < new_h; y++) {
        double sy = y * y_ratio;
        int y0 = (int)floor(sy);
        int y1 = y0 + 1;
        if (y1 >= src->height) y1 = src->height - 1;
        double fy = sy - y0;

        for (int x = 0; x < new_w; x++) {
            double sx = x * x_ratio;
            int x0 = (int)floor(sx);
            int x1 = x0 + 1;
            if (x1 >= src->width) x1 = src->width - 1;
            double fx = sx - x0;

            Pixel p00 = src->pixels[(size_t)y0 * src->width + x0];
            Pixel p10 = src->pixels[(size_t)y0 * src->width + x1];
            Pixel p01 = src->pixels[(size_t)y1 * src->width + x0];
            Pixel p11 = src->pixels[(size_t)y1 * src->width + x1];

            Pixel top = pixel_lerp(p00, p10, fx);
            Pixel bottom = pixel_lerp(p01, p11, fx);
            out.pixels[(size_t)y * new_w + x] = pixel_lerp(top, bottom, fy);
        }
    }

    return out;
}

static Image resize_image_if_needed(const Image *src, int target_w, int target_h, ResizeFilter filter) {
    if (target_w == src->width && target_h == src->height) {
        Image copy;
        copy.width = src->width;
        copy.height = src->height;
        size_t bytes = checked_pixel_bytes(copy.width, copy.height);
        copy.pixels = (Pixel *)xmalloc(bytes);
        memcpy(copy.pixels, src->pixels, bytes);
        return copy;
    }

    if (filter == FILTER_BILINEAR) {
        return resize_bilinear(src, target_w, target_h);
    }
    return resize_nearest(src, target_w, target_h);
}

static void apply_grayscale(Image *img) {
    for (int y = 0; y < img->height; y++) {
        for (int x = 0; x < img->width; x++) {
            Pixel *p = &img->pixels[(size_t)y * img->width + x];
            int gray = round_to_int(0.299 * (double)p->r + 0.587 * (double)p->g + 0.114 * (double)p->b);
            gray = clamp_int(gray, 0, 255);
            p->r = (unsigned char)gray;
            p->g = (unsigned char)gray;
            p->b = (unsigned char)gray;
        }
    }
}

static char *base64_urlsafe_encode(const unsigned char *data, size_t len) {
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

    size_t out_len = ((len + 2) / 3) * 4;
    char *out = (char *)xmalloc(out_len + 1);

    size_t ip = 0;
    size_t op = 0;

    while (ip < len) {
        uint32_t a = ip < len ? data[ip++] : 0;
        uint32_t b = ip < len ? data[ip++] : 0;
        uint32_t c = ip < len ? data[ip++] : 0;
        uint32_t triple = (a << 16) | (b << 8) | c;

        out[op++] = table[(triple >> 18) & 0x3F];
        out[op++] = table[(triple >> 12) & 0x3F];
        out[op++] = table[(triple >> 6) & 0x3F];
        out[op++] = table[triple & 0x3F];
    }

    if (len % 3 == 1) {
        out[out_len - 2] = '=';
        out[out_len - 1] = '=';
    } else if (len % 3 == 2) {
        out[out_len - 1] = '=';
    }

    out[out_len] = '\0';
    return out;
}

static unsigned char *gzip_compress_buffer(const unsigned char *src, size_t src_len, size_t *out_len) {
    z_stream zs;
    memset(&zs, 0, sizeof(zs));

    int ret = deflateInit2(&zs, Z_BEST_COMPRESSION, Z_DEFLATED, MAX_WBITS + 16, 8, Z_DEFAULT_STRATEGY);
    if (ret != Z_OK) {
        die("deflateInit2 failed: %d", ret);
    }

    uLong bound = deflateBound(&zs, (uLong)src_len);
    unsigned char *out = (unsigned char *)xmalloc(bound);

    zs.next_in = (Bytef *)src;
    zs.avail_in = (uInt)src_len;
    zs.next_out = out;
    zs.avail_out = (uInt)bound;

    ret = deflate(&zs, Z_FINISH);
    if (ret != Z_STREAM_END) {
        deflateEnd(&zs);
        free(out);
        die("gzip compression failed: %d", ret);
    }

    *out_len = zs.total_out;
    deflateEnd(&zs);
    return out;
}

static char *gzip_base64_urlsafe(const char *text) {
    size_t compressed_len = 0;
    unsigned char *compressed = gzip_compress_buffer((const unsigned char *)text, strlen(text), &compressed_len);
    char *encoded = base64_urlsafe_encode(compressed, compressed_len);
    free(compressed);
    return encoded;
}

static char *base64_urlsafe_text(const char *text) {
    return base64_urlsafe_encode((const unsigned char *)text, strlen(text));
}

static void append_color_channel(StringBuilder *sb, int channel_id, int r, int g, int b, int a) {
    double opacity = (double)clamp_int(a, 0, 255) / 255.0;

    sb_appendf(
        sb,
        "6_%d_1_%d_2_%d_3_%d_11_255_12_255_13_255_4_-1_7_%.6g_8_1_15_1_18_1|",
        channel_id,
        clamp_int(r, 0, 255),
        clamp_int(g, 0, 255),
        clamp_int(b, 0, 255),
        opacity
    );
}

static void append_default_color_channels(StringBuilder *sb) {
    append_color_channel(sb, 1000, 255, 255, 255, 255);
    append_color_channel(sb, 1001, 0, 0, 0, 255);
    append_color_channel(sb, 1009, 0, 0, 0, 255);
    append_color_channel(sb, 1013, 0, 0, 0, 255);
    append_color_channel(sb, 1014, 0, 0, 0, 255);
    append_color_channel(sb, 1004, 200, 200, 200, 255);
    append_color_channel(sb, 1005, 255, 255, 255, 255);
    append_color_channel(sb, 1006, 255, 255, 255, 255);
}

static void append_level_object(StringBuilder *sb, int object_id, int x, int y, int channel_id) {
    sb_appendf(
        sb,
        "1,%d,2,%d,3,%d,21,%d,22,%d,155,1;",
        object_id,
        x,
        y,
        channel_id,
        channel_id
    );
}

static void write_gmd_file(const char *output_path, const char *level_name, const char *inner_level_base64) {
    char *description_base64 = base64_urlsafe_text("");

    StringBuilder plist;
    sb_init(&plist);

    sb_append(&plist, "<?xml version=\"1.0\"?>");
    sb_append(&plist, "<plist version=\"1.0\" gjver=\"2.0\">");
    sb_append(&plist, "<dict>");

    sb_append(&plist, "<k>k4</k><s>");
    sb_append_xml_escaped(&plist, inner_level_base64);
    sb_append(&plist, "</s>");

    sb_append(&plist, "<k>k2</k><s>");
    sb_append_xml_escaped(&plist, level_name);
    sb_append(&plist, "</s>");

    sb_append(&plist, "<k>k3</k><s>");
    sb_append_xml_escaped(&plist, description_base64);
    sb_append(&plist, "</s>");

    sb_append(&plist, "<k>k16</k><i>1</i>");
    sb_append(&plist, "<k>k21</k><i>2</i>");
    sb_append(&plist, "<k>k50</k><i>40</i>");
    sb_append(&plist, "<k>kCEK</k><i>4</i>");

    sb_append(&plist, "</dict>");
    sb_append(&plist, "</plist>");

    FILE *fp = fopen(output_path, "wb");
    if (!fp) {
        free(description_base64);
        sb_free(&plist);
        die("failed to open output file: %s", output_path);
    }

    if (fwrite(plist.data, 1, plist.len, fp) != plist.len) {
        fclose(fp);
        free(description_base64);
        sb_free(&plist);
        die("failed to write output file: %s", output_path);
    }

    fclose(fp);
    free(description_base64);
    sb_free(&plist);
}

static void print_help(FILE *out) {
    fprintf(out,
        "HGDImageGen %s\n"
        "PNG image to Geometry Dash .gmd converter\n\n"
        "Usage:\n"
        "  HGDImageGen -i <input.png> -o <output.gmd> -n <level name> [options]\n\n"
        "Required/primary options:\n"
        "  -i, --input <path>          Input PNG file\n"
        "  -o, --output <path>         Output .gmd file, default: %s\n"
        "  -n, --level-name <name>     Level name, default: %s\n\n"
        "Resize options:\n"
        "  -W, --width <px>            Output image width\n"
        "  -H, --height <px>           Output image height\n"
        "      --scale <factor>        Resize by scale. Ignored if width/height is set\n"
        "      --filter <name>         nearest or bilinear, default: nearest\n\n"
        "Color options:\n"
        "      --grayscale             Convert image to grayscale before quantization\n"
        "      --color                 Force color mode, default\n"
        "      --quant-step <1-255>    RGB quantization step, default: %d\n"
        "      --alpha-step <1-255>    Alpha quantization step, default: %d\n"
        "      --alpha-skip-below <0-255>\n"
        "                             Skip pixels below alpha threshold, default: 1\n"
        "      --max-channels <n>      Max generated color channels, default: %d\n"
        "      --max-objects <n>       Max generated objects, default: %d\n\n"
        "Geometry Dash layout options:\n"
        "      --tile-size <px>        Object spacing, default: %d\n"
        "      --start-x <value>       First object X position, default: %d\n"
        "      --start-y <value>       First object Y margin, default: %d\n"
        "      --object-id <id>        Geometry Dash object ID, default: %d\n\n"
        "Other options:\n"
        "      --log <path>            Log file, default: %s\n"
        "      --version               Print version\n"
        "  -h, --help                  Show this help\n\n"
        "Examples:\n"
        "  HGDImageGen -i Test.png -o Test.gmd -n Test\n"
        "  HGDImageGen -i Test.png -o Test.gmd -n Test --width 64 --height 64\n"
        "  HGDImageGen -i Test.png -o Gray.gmd -n Gray --grayscale\n",
        HGD_VERSION,
        DEFAULT_OUTPUT_PATH,
        DEFAULT_LEVEL_NAME,
        DEFAULT_QUANT_STEP,
        DEFAULT_ALPHA_STEP,
        DEFAULT_MAX_CHANNELS,
        DEFAULT_MAX_OBJECTS,
        DEFAULT_TILE_SIZE,
        DEFAULT_START_X,
        DEFAULT_START_Y,
        DEFAULT_OBJECT_ID,
        DEFAULT_LOG_PATH
    );
}

static void options_init(Options *opt) {
    memset(opt, 0, sizeof(*opt));
    opt->input_path = NULL;
    opt->output_path = DEFAULT_OUTPUT_PATH;
    opt->level_name = DEFAULT_LEVEL_NAME;
    opt->log_path = DEFAULT_LOG_PATH;
    opt->target_width = 0;
    opt->target_height = 0;
    opt->scale = 0.0;
    opt->grayscale = false;
    opt->filter = FILTER_NEAREST;
    opt->tile_size = DEFAULT_TILE_SIZE;
    opt->start_x = DEFAULT_START_X;
    opt->start_y = DEFAULT_START_Y;
    opt->max_channels = DEFAULT_MAX_CHANNELS;
    opt->max_objects = DEFAULT_MAX_OBJECTS;
    opt->quant_step = DEFAULT_QUANT_STEP;
    opt->alpha_step = DEFAULT_ALPHA_STEP;
    opt->alpha_skip_below = 1;
    opt->object_id = DEFAULT_OBJECT_ID;
}

static const char *require_value(int argc, char **argv, int *i, const char *name) {
    if (*i + 1 >= argc) {
        die("missing value for %s", name);
    }
    (*i)++;
    return argv[*i];
}

static void parse_args(Options *opt, int argc, char **argv) {
    options_init(opt);

    if (argc == 1) {
        print_help(stderr);
        exit(EXIT_FAILURE);
    }

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            print_help(stdout);
            exit(EXIT_SUCCESS);
        } else if (strcmp(arg, "--version") == 0) {
            printf("HGDImageGen %s\n", HGD_VERSION);
            exit(EXIT_SUCCESS);
        } else if (strcmp(arg, "-i") == 0 || strcmp(arg, "--input") == 0) {
            opt->input_path = require_value(argc, argv, &i, arg);
        } else if (strcmp(arg, "-o") == 0 || strcmp(arg, "--output") == 0) {
            opt->output_path = require_value(argc, argv, &i, arg);
        } else if (strcmp(arg, "-n") == 0 || strcmp(arg, "--level-name") == 0) {
            opt->level_name = require_value(argc, argv, &i, arg);
        } else if (strcmp(arg, "-W") == 0 || strcmp(arg, "--width") == 0) {
            opt->target_width = parse_int_arg(require_value(argc, argv, &i, arg), arg);
        } else if (strcmp(arg, "-H") == 0 || strcmp(arg, "--height") == 0) {
            opt->target_height = parse_int_arg(require_value(argc, argv, &i, arg), arg);
        } else if (strcmp(arg, "--scale") == 0) {
            opt->scale = parse_double_arg(require_value(argc, argv, &i, arg), arg);
        } else if (strcmp(arg, "--filter") == 0) {
            const char *value = require_value(argc, argv, &i, arg);
            if (strcmp(value, "nearest") == 0) {
                opt->filter = FILTER_NEAREST;
            } else if (strcmp(value, "bilinear") == 0) {
                opt->filter = FILTER_BILINEAR;
            } else {
                die("invalid filter: %s. Use nearest or bilinear", value);
            }
        } else if (strcmp(arg, "--grayscale") == 0 || strcmp(arg, "--gray") == 0 || strcmp(arg, "--mono") == 0) {
            opt->grayscale = true;
        } else if (strcmp(arg, "--color") == 0 || strcmp(arg, "--no-grayscale") == 0) {
            opt->grayscale = false;
        } else if (strcmp(arg, "--quant-step") == 0) {
            opt->quant_step = parse_int_arg(require_value(argc, argv, &i, arg), arg);
        } else if (strcmp(arg, "--alpha-step") == 0) {
            opt->alpha_step = parse_int_arg(require_value(argc, argv, &i, arg), arg);
        } else if (strcmp(arg, "--alpha-skip-below") == 0) {
            opt->alpha_skip_below = parse_int_arg(require_value(argc, argv, &i, arg), arg);
        } else if (strcmp(arg, "--max-channels") == 0) {
            opt->max_channels = parse_int_arg(require_value(argc, argv, &i, arg), arg);
        } else if (strcmp(arg, "--max-objects") == 0) {
            opt->max_objects = parse_int_arg(require_value(argc, argv, &i, arg), arg);
        } else if (strcmp(arg, "--tile-size") == 0) {
            opt->tile_size = parse_int_arg(require_value(argc, argv, &i, arg), arg);
        } else if (strcmp(arg, "--start-x") == 0) {
            opt->start_x = parse_int_arg(require_value(argc, argv, &i, arg), arg);
        } else if (strcmp(arg, "--start-y") == 0) {
            opt->start_y = parse_int_arg(require_value(argc, argv, &i, arg), arg);
        } else if (strcmp(arg, "--object-id") == 0) {
            opt->object_id = parse_int_arg(require_value(argc, argv, &i, arg), arg);
        } else if (strcmp(arg, "--log") == 0) {
            opt->log_path = require_value(argc, argv, &i, arg);
        } else {
            die("unknown option: %s", arg);
        }
    }

    if (!opt->input_path) {
        die("input PNG is required. Use -i <input.png>");
    }
    if (opt->target_width < 0 || opt->target_height < 0) {
        die("width/height must be positive");
    }
    if (opt->scale < 0.0) {
        die("scale must be positive");
    }
    if (opt->tile_size <= 0) {
        die("tile-size must be positive");
    }
    if (opt->max_channels <= 0) {
        die("max-channels must be positive");
    }
    if (opt->max_objects <= 0) {
        die("max-objects must be positive");
    }
    if (opt->quant_step <= 0 || opt->quant_step > 255) {
        die("quant-step must be in 1..255");
    }
    if (opt->alpha_step <= 0 || opt->alpha_step > 255) {
        die("alpha-step must be in 1..255");
    }
    if (opt->alpha_skip_below < 0 || opt->alpha_skip_below > 255) {
        die("alpha-skip-below must be in 0..255");
    }
}

static void compute_output_size(const Options *opt, const Image *src, int *out_w, int *out_h) {
    int w = opt->target_width;
    int h = opt->target_height;

    if (w > 0 && h > 0) {
        *out_w = w;
        *out_h = h;
        return;
    }

    if (w > 0 && h == 0) {
        h = round_to_int((double)src->height * ((double)w / (double)src->width));
        if (h < 1) h = 1;
        *out_w = w;
        *out_h = h;
        return;
    }

    if (w == 0 && h > 0) {
        w = round_to_int((double)src->width * ((double)h / (double)src->height));
        if (w < 1) w = 1;
        *out_w = w;
        *out_h = h;
        return;
    }

    if (opt->scale > 0.0) {
        w = round_to_int((double)src->width * opt->scale);
        h = round_to_int((double)src->height * opt->scale);
        if (w < 1) w = 1;
        if (h < 1) h = 1;
        *out_w = w;
        *out_h = h;
        return;
    }

    *out_w = src->width;
    *out_h = src->height;
}

static void convert_image_to_gmd(const Options *opt) {
    Image source = load_png_rgba(opt->input_path);

    int out_w = 0;
    int out_h = 0;
    compute_output_size(opt, &source, &out_w, &out_h);

    int64_t requested_objects = (int64_t)out_w * (int64_t)out_h;
    if (requested_objects > opt->max_objects) {
        image_free(&source);
        die(
            "output image would create too many objects: %lld > %d. "
            "Use smaller --width/--height or increase --max-objects.",
            (long long)requested_objects,
            opt->max_objects
        );
    }

    Image img = resize_image_if_needed(&source, out_w, out_h, opt->filter);
    image_free(&source);

    if (opt->grayscale) {
        apply_grayscale(&img);
    }

    FILE *log = fopen(opt->log_path, "wb");
    if (!log) {
        image_free(&img);
        die("failed to open log file: %s", opt->log_path);
    }

    fprintf(log, "[LOG] HGDImageGen %s\n", HGD_VERSION);
    fprintf(log, "[LOG] input=%s\n", opt->input_path);
    fprintf(log, "[LOG] output=%s\n", opt->output_path);
    fprintf(log, "[LOG] level_name=%s\n", opt->level_name);
    fprintf(log, "[LOG] final_size=%dx%d\n", img.width, img.height);
    fprintf(log, "[LOG] grayscale=%s\n", opt->grayscale ? "true" : "false");
    fprintf(log, "[LOG] filter=%s\n", opt->filter == FILTER_BILINEAR ? "bilinear" : "nearest");
    fprintf(log, "[LOG] quant_step=%d alpha_step=%d max_channels=%d\n", opt->quant_step, opt->alpha_step, opt->max_channels);

    ColorMap color_map;
    color_map_init(&color_map, (size_t)opt->max_channels);

    ColorChannelDef *channels = (ColorChannelDef *)xmalloc((size_t)opt->max_channels * sizeof(ColorChannelDef));
    int channel_count = 0;
    int next_channel_id = 1;
    int skipped_transparent = 0;
    int object_count = 0;

    StringBuilder objects;
    sb_init(&objects);

    for (int y = 0; y < img.height; y++) {
        for (int x = 0; x < img.width; x++) {
            Pixel p = img.pixels[(size_t)y * img.width + x];

            int r = p.r;
            int g = p.g;
            int b = p.b;
            int a = p.a;

            if (a < opt->alpha_skip_below) {
                skipped_transparent++;
                fprintf(log, "x=%d,y=%d,rgba=(%d,%d,%d,%d),skipped=alpha\n", x, y, r, g, b, a);
                continue;
            }

            int qr = quantize_channel(r, opt->quant_step);
            int qg = quantize_channel(g, opt->quant_step);
            int qb = quantize_channel(b, opt->quant_step);
            int qa = quantize_channel(a, opt->alpha_step);

            if (a > 0 && qa == 0) {
                qa = opt->alpha_step;
            }
            qa = clamp_int(qa, 1, 255);

            uint32_t key = rgba_key(qr, qg, qb, qa);
            int channel_id = color_map_get(&color_map, key);

            if (channel_id < 0) {
                if (next_channel_id > opt->max_channels) {
                    fclose(log);
                    color_map_free(&color_map);
                    free(channels);
                    sb_free(&objects);
                    image_free(&img);
                    die(
                        "color channel count exceeded %d. Try smaller --width/--height, larger --quant-step, larger --alpha-step, or --grayscale.",
                        opt->max_channels
                    );
                }

                channel_id = next_channel_id++;
                color_map_put(&color_map, key, channel_id);

                channels[channel_count].channel_id = channel_id;
                channels[channel_count].r = qr;
                channels[channel_count].g = qg;
                channels[channel_count].b = qb;
                channels[channel_count].a = qa;
                channel_count++;
            }

            int obj_x = opt->start_x + opt->tile_size * x;
            int obj_y = opt->tile_size * img.height - opt->tile_size * y - opt->start_y;
            append_level_object(&objects, opt->object_id, obj_x, obj_y, channel_id);
            object_count++;

            fprintf(
                log,
                "x=%d,y=%d,rgba=(%d,%d,%d,%d),quant=(%d,%d,%d,%d),channel=%d,obj=(%d,%d)\n",
                x, y, r, g, b, a, qr, qg, qb, qa, channel_id, obj_x, obj_y
            );
        }
    }

    StringBuilder raw;
    sb_init(&raw);
    sb_append(&raw, "kS38,");
    append_default_color_channels(&raw);

    for (int i = 0; i < channel_count; i++) {
        append_color_channel(
            &raw,
            channels[i].channel_id,
            channels[i].r,
            channels[i].g,
            channels[i].b,
            channels[i].a
        );
    }

    sb_append(&raw, ",;");
    sb_append(&raw, objects.data);

    char *inner_b64 = gzip_base64_urlsafe(raw.data);
    write_gmd_file(opt->output_path, opt->level_name, inner_b64);

    fprintf(log, "[LOG] object_count=%d\n", object_count);
    fprintf(log, "[LOG] skipped_transparent=%d\n", skipped_transparent);
    fprintf(log, "[LOG] used_color_channels=%d\n", channel_count);
    fprintf(log, "[LOG] raw_level_string_bytes=%zu\n", raw.len);
    fprintf(log, "[LOG] done\n");
    fclose(log);

    printf("HGDImageGen %s\n", HGD_VERSION);
    printf("input: %s\n", opt->input_path);
    printf("output: %s\n", opt->output_path);
    printf("level: %s\n", opt->level_name);
    printf("size: %dx%d\n", img.width, img.height);
    printf("mode: %s\n", opt->grayscale ? "grayscale" : "color");
    printf("objects: %d\n", object_count);
    printf("color_channels: %d / %d\n", channel_count, opt->max_channels);
    printf("skipped_transparent: %d\n", skipped_transparent);
    printf("log: %s\n", opt->log_path);

    free(inner_b64);
    sb_free(&raw);
    sb_free(&objects);
    color_map_free(&color_map);
    free(channels);
    image_free(&img);
}

int main(int argc, char **argv) {
    Options opt;
    parse_args(&opt, argc, argv);
    convert_image_to_gmd(&opt);
    return EXIT_SUCCESS;
}
