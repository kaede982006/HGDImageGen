#include "HGDImageGen.h"

void die(const char* fmt, ...) {
    /* va_list로 가변 인수를 수집한다. */
    va_list args;
    va_start(args, fmt);
    /* stderr에 [ERROR] 접두사와 포맷 메시지를 출력한다. */
    fprintf(stderr, "[ERROR] ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
    /* 비정상 종료 코드로 프로세스를 종료한다. */
    exit(EXIT_FAILURE);
}


void* xmalloc(size_t size) {
    /* size가 0이면 1로 보정하여 NULL 반환을 방지한다. */
    void* ptr = malloc(size == 0 ? 1 : size);
    /* 할당 실패 시 die()로 종료한다. */
    if (!ptr) {
        die("out of memory");
    }
    return ptr;
}

void* xcalloc(size_t count, size_t size) {
    /* count 또는 size가 0이면 1로 보정하여 NULL 반환을 방지한다. */
    void* ptr = calloc(count == 0 ? 1 : count, size == 0 ? 1 : size);
    /* 할당 실패 시 die()로 종료한다. */
    if (!ptr) {
        die("out of memory");
    }
    return ptr;
}

size_t checked_pixel_bytes(int width, int height) {
    /* 너비와 높이가 양수인지 검사한다. */
    if (width <= 0 || height <= 0) {
        die("invalid image size");
    }

    size_t w = (size_t)width;
    size_t h = (size_t)height;

    /* w * h 곱셈이 size_t를 초과하는지 검사한다. */
    if (w > SIZE_MAX / h) {
        die("image size overflow");
    }

    size_t count = w * h;

    /* count * sizeof(Pixel) 곱셈이 size_t를 초과하는지 검사한다. */
    if (count > SIZE_MAX / sizeof(Pixel)) {
        die("image allocation overflow");
    }

    return count * sizeof(Pixel);
}


void sb_init(StringBuilder* sb) {
    /* 초기 버퍼 용량을 4096바이트로 설정하고 xmalloc으로 할당한다. */
    sb->cap = 4096;
    sb->len = 0;
    sb->data = (char*)xmalloc(sb->cap);
    /* 빈 문자열 상태로 초기화한다. */
    sb->data[0] = '\0';
}

void sb_free(StringBuilder* sb) {
    /* 동적 버퍼를 해제하고 모든 필드를 초기 상태로 설정한다. */
    free(sb->data);
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

void sb_reserve(StringBuilder* sb, size_t extra) {
    /* 현재 길이와 extra를 더했을 때 오버플로가 발생하는지 검사한다. */
    if (extra > SIZE_MAX - sb->len - 1) {
        die("string size overflow");
    }

    size_t need = sb->len + extra + 1;
    /* 이미 충분한 용량이 있으면 바로 반환한다. */
    if (need <= sb->cap) {
        return;
    }

    /* cap이 need 이상이 될 때까지 2배씩 늘린다. */
    while (sb->cap < need) {
        if (sb->cap > SIZE_MAX / 2) {
            die("string capacity overflow");
        }
        sb->cap *= 2;
    }

    /* 새 용량으로 버퍼를 재할당한다. */
    char* new_data = (char*)realloc(sb->data, sb->cap);
    if (!new_data) {
        die("out of memory while resizing string");
    }
    sb->data = new_data;
}

void sb_append_n(StringBuilder* sb, const char* s, size_t n) {
    /* n 바이트 이상의 여유 공간을 확보한다. */
    sb_reserve(sb, n);
    /* s에서 n 바이트를 버퍼 끝에 복사한다. */
    memcpy(sb->data + sb->len, s, n);
    /* 길이를 갱신하고 null 종료 문자를 추가한다. */
    sb->len += n;
    sb->data[sb->len] = '\0';
}

void sb_append(StringBuilder* sb, const char* s) {
    /* s의 전체 길이를 sb_append_n에 전달한다. */
    sb_append_n(sb, s, strlen(s));
}

void sb_appendf(StringBuilder* sb, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);

    /* vsnprintf(NULL)로 필요한 출력 바이트 수를 먼저 측정한다. */
    va_list copy;
    va_copy(copy, args);
    int needed = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);

    if (needed < 0) {
        va_end(args);
        die("formatting failed");
    }

    /* 측정된 크기만큼 버퍼를 확보한다. */
    sb_reserve(sb, (size_t)needed);
    /* 버퍼 끝에 포맷 문자열을 실제로 작성한다. */
    int written = vsnprintf(sb->data + sb->len, sb->cap - sb->len, fmt, args);
    va_end(args);

    if (written < 0) {
        die("formatting failed");
    }

    sb->len += (size_t)written;
}

void sb_append_xml_escaped(StringBuilder* sb, const char* s) {
    /* 문자열을 한 글자씩 순회하며 XML 특수 문자를 해당 엔티티로 치환한다. */
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

int parse_int_arg(const char* text, const char* name) {
    /* strtol로 10진수 정수를 파싱한다. */
    errno = 0;
    char* end = NULL;
    long value = strtol(text, &end, 10);
    /* 변환 오류, 부분 파싱, 또는 int 범위 초과 시 die()를 호출한다. */
    if (errno != 0 || end == text || *end != '\0' || value < INT32_MIN || value > INT32_MAX) {
        die("invalid integer for %s: %s", name, text);
    }
    return (int)value;
}

double parse_double_arg(const char* text, const char* name) {
    /* strtod로 부동소수점 수를 파싱한다. */
    errno = 0;
    char* end = NULL;
    double value = strtod(text, &end);
    /* 변환 오류, 부분 파싱, 또는 무한/NaN 값이면 die()를 호출한다. */
    if (errno != 0 || end == text || *end != '\0' || !isfinite(value)) {
        die("invalid number for %s: %s", name, text);
    }
    return value;
}

int clamp_int(int value, int lo, int hi) {
    /* lo 미만이면 lo, hi 초과이면 hi, 그 외에는 value를 반환한다. */
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

int round_to_int(double x) {
    /* 양수이면 +0.5 후 버림, 음수이면 -0.5 후 버림으로 반올림한다. */
    if (x >= 0.0) {
        return (int)(x + 0.5);
    }
    return (int)(x - 0.5);
}

int quantize_channel(int value, int step) {
    /* 먼저 0~255 범위로 클램프한다. */
    value = clamp_int(value, 0, 255);
    /* step이 1 이하이면 양자화 없이 그대로 반환한다. */
    if (step <= 1) {
        return value;
    }
    /* step 단위로 반올림한 뒤 다시 0~255로 클램프한다. */
    int q = ((value + step / 2) / step) * step;
    return clamp_int(q, 0, 255);
}

uint32_t rgba_key(int r, int g, int b, int a) {
    /* r, g, b, a를 각각 24, 16, 8, 0비트 위치로 이동하여 OR로 합친다. */
    return ((uint32_t)(unsigned)r << 24) |
        ((uint32_t)(unsigned)g << 16) |
        ((uint32_t)(unsigned)b << 8) |
        (uint32_t)(unsigned)a;
}

uint32_t hash_u32(uint32_t x) {
    /* XOR-shift와 곱셈을 조합한 비가역 해시를 세 단계로 적용한다. */
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

size_t next_power_of_two(size_t x) {
    /* p를 1에서 시작해 x 이상이 될 때까지 2배씩 증가시킨다. */
    size_t p = 1;
    while (p < x) {
        if (p > SIZE_MAX / 2) {
            die("capacity overflow");
        }
        p *= 2;
    }
    return p;
}

void color_map_init(ColorMap* map, size_t expected_max_items) {
    /* 부하 계수(COLOR_MAP_LOAD_FACTOR_NUM/DEN)를 고려한 최소 용량을 계산한다. */
    size_t min_cap = (expected_max_items * COLOR_MAP_LOAD_FACTOR_DEN) / COLOR_MAP_LOAD_FACTOR_NUM + 8;
    /* next_power_of_two로 cap을 2의 거듭제곱으로 맞춘다. */
    map->cap = next_power_of_two(min_cap);
    map->size = 0;
    /* xcalloc으로 슬롯 배열을 0 초기화하여 할당한다. */
    map->slots = (ColorMapSlot*)xcalloc(map->cap, sizeof(ColorMapSlot));
}

void color_map_free(ColorMap* map) {
    /* 슬롯 배열을 해제하고 모든 필드를 초기화한다. */
    free(map->slots);
    map->slots = NULL;
    map->cap = 0;
    map->size = 0;
}

int color_map_get(const ColorMap* map, uint32_t key) {
    /* 키를 해시하여 시작 인덱스를 결정한다. */
    uint32_t h = hash_u32(key);
    /* 선형 탐사로 슬롯을 순회한다. */
    for (size_t i = 0; i < map->cap; i++) {
        size_t idx = ((size_t)h + i) & (map->cap - 1);
        /* 빈 슬롯에 도달하면 키가 없다는 의미이므로 -1을 반환한다. */
        if (!map->slots[idx].used) {
            return -1;
        }
        /* 키가 일치하면 저장된 channel_id를 반환한다. */
        if (map->slots[idx].key == key) {
            return map->slots[idx].channel_id;
        }
    }
    return -1;
}

void color_map_put(ColorMap* map, uint32_t key, int channel_id) {
    /* 부하 계수가 허용 한계를 초과하는지 검사한다. */
    if ((map->size + 1) * COLOR_MAP_LOAD_FACTOR_DEN > map->cap * COLOR_MAP_LOAD_FACTOR_NUM) {
        die("internal color map is unexpectedly full");
    }

    /* 키를 해시하여 시작 인덱스를 결정한다. */
    uint32_t h = hash_u32(key);
    /* 선형 탐사로 빈 슬롯 또는 키 일치 슬롯을 찾는다. */
    for (size_t i = 0; i < map->cap; i++) {
        size_t idx = ((size_t)h + i) & (map->cap - 1);
        /* 빈 슬롯이면 새 항목을 삽입한다. */
        if (!map->slots[idx].used) {
            map->slots[idx].used = true;
            map->slots[idx].key = key;
            map->slots[idx].channel_id = channel_id;
            map->size++;
            return;
        }
        /* 키가 이미 존재하면 channel_id를 갱신한다. */
        if (map->slots[idx].key == key) {
            map->slots[idx].channel_id = channel_id;
            return;
        }
    }

    die("internal color map insertion failed");
}

void image_free(Image* img) {
    /* 픽셀 버퍼를 해제하고 크기 필드를 초기화한다. */
    free(img->pixels);
    img->pixels = NULL;
    img->width = 0;
    img->height = 0;
}

void png_read_error_handler(png_structp png, png_const_charp msg) {
    /* png_get_error_ptr로 파일 경로를 꺼낸 뒤 die()를 호출한다. */
    const char* path = (const char*)png_get_error_ptr(png);
    die("libpng error reading '%s': %s", path ? path : "?", msg);
}

Image load_png_rgba(const char* path) {
    /* 바이너리 모드로 파일을 연다. */
    FILE* fp = fopen(path, "rb");
    if (!fp) {
        die("failed to open input PNG: %s", path);
    }

    /* PNG 시그니처 8바이트를 읽어 유효한 PNG인지 검증한다. */
    unsigned char sig[8];
    if (fread(sig, 1, 8, fp) != 8 || png_sig_cmp(sig, 0, 8)) {
        fclose(fp);
        die("not a valid PNG file: %s", path);
    }

    /* png_create_read_struct와 png_create_info_struct로 libpng 컨텍스트를 생성한다. */
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

    /* 파일 포인터를 libpng에 연결하고 헤더 정보를 읽는다. */
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

    /* 16비트 채널을 8비트로 내림, 팔레트를 RGB로, 그레이를 RGB로 변환하도록 설정한다. */
    if (bit_depth == 16) {
        png_set_strip_16(png);
    }

    if (color_type == PNG_COLOR_TYPE_PALETTE) {
        png_set_palette_to_rgb(png);
    }

    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) {
        png_set_expand_gray_1_2_4_to_8(png);
    }

    /* tRNS 청크가 있으면 알파 채널로 변환한다. */
    if (png_get_valid(png, info, PNG_INFO_tRNS)) {
        png_set_tRNS_to_alpha(png);
    }

    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
        png_set_gray_to_rgb(png);
    }

    /* 알파 채널이 없는 형식에 불투명 알파(0xFF)를 채운다. */
    if (color_type == PNG_COLOR_TYPE_RGB ||
        color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_PALETTE) {
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    }

    png_read_update_info(png, info);

    /* 행당 바이트 수가 width * 4인지 확인한다. */
    png_size_t rowbytes = png_get_rowbytes(png, info);
    if (rowbytes != width * 4) {
        die("unexpected PNG row format");
    }

    /* 픽셀 버퍼와 행 포인터 배열을 할당한다. */
    size_t pixel_bytes = checked_pixel_bytes((int)width, (int)height);
    Pixel* pixels = (Pixel*)xmalloc(pixel_bytes);

    png_bytep* rows = (png_bytep*)xmalloc((size_t)height * sizeof(png_bytep));
    for (png_uint_32 y = 0; y < height; y++) {
        rows[y] = (png_bytep)&pixels[(size_t)y * (size_t)width];
    }

    /* 이미지 데이터를 읽고 libpng 자원과 파일을 해제한다. */
    png_read_image(png, rows);
    png_read_end(png, NULL);

    free(rows);
    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);

    /* 읽은 픽셀 데이터로 Image 구조체를 구성하여 반환한다. */
    Image img;
    img.pixels = pixels;
    img.width = (int)width;
    img.height = (int)height;
    return img;
}

Pixel pixel_lerp(Pixel a, Pixel b, double t) {
    /* 각 채널에 a + (b - a) * t 공식을 적용하여 선형 보간한 뒤 0~255로 클램프한다. */
    Pixel out;
    out.r = (unsigned char)clamp_int(round_to_int((double)a.r + ((double)b.r - (double)a.r) * t), 0, 255);
    out.g = (unsigned char)clamp_int(round_to_int((double)a.g + ((double)b.g - (double)a.g) * t), 0, 255);
    out.b = (unsigned char)clamp_int(round_to_int((double)a.b + ((double)b.b - (double)a.b) * t), 0, 255);
    out.a = (unsigned char)clamp_int(round_to_int((double)a.a + ((double)b.a - (double)a.a) * t), 0, 255);
    return out;
}

Image resize_nearest(const Image* src, int new_w, int new_h) {
    /* 출력 Image에 메모리를 할당한다. */
    Image out;
    out.width = new_w;
    out.height = new_h;
    out.pixels = (Pixel*)xmalloc(checked_pixel_bytes(new_w, new_h));

    /* 각 출력 픽셀에 대해 소스 좌표를 정수 비율로 산출하여 해당 픽셀을 복사한다. */
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

Image resize_bilinear(const Image* src, int new_w, int new_h) {
    /* 출력 Image에 메모리를 할당한다. */
    Image out;
    out.width = new_w;
    out.height = new_h;
    out.pixels = (Pixel*)xmalloc(checked_pixel_bytes(new_w, new_h));

    /* 소스 좌표계로 변환하기 위한 x/y 비율을 계산한다. */
    double x_ratio = (new_w > 1) ? (double)(src->width - 1) / (double)(new_w - 1) : 0.0;
    double y_ratio = (new_h > 1) ? (double)(src->height - 1) / (double)(new_h - 1) : 0.0;

    for (int y = 0; y < new_h; y++) {
        /* 출력 y 좌표에 대응하는 소스 y 좌표와 인접 두 행 인덱스를 구한다. */
        double sy = y * y_ratio;
        int y0 = (int)floor(sy);
        int y1 = y0 + 1;
        if (y1 >= src->height) y1 = src->height - 1;
        double fy = sy - y0;

        for (int x = 0; x < new_w; x++) {
            /* 출력 x 좌표에 대응하는 소스 x 좌표와 인접 두 열 인덱스를 구한다. */
            double sx = x * x_ratio;
            int x0 = (int)floor(sx);
            int x1 = x0 + 1;
            if (x1 >= src->width) x1 = src->width - 1;
            double fx = sx - x0;

            /* 인접 4픽셀을 가져온다. */
            Pixel p00 = src->pixels[(size_t)y0 * src->width + x0];
            Pixel p10 = src->pixels[(size_t)y0 * src->width + x1];
            Pixel p01 = src->pixels[(size_t)y1 * src->width + x0];
            Pixel p11 = src->pixels[(size_t)y1 * src->width + x1];

            /* x 방향 보간 후 y 방향 보간하여 최종 픽셀을 산출한다. */
            Pixel top = pixel_lerp(p00, p10, fx);
            Pixel bottom = pixel_lerp(p01, p11, fx);
            out.pixels[(size_t)y * new_w + x] = pixel_lerp(top, bottom, fy);
        }
    }

    return out;
}

Image resize_image_if_needed(const Image* src, int target_w, int target_h, ResizeFilter filter) {
    /* 크기가 동일하면 픽셀 버퍼를 그대로 복사한 새 Image를 반환한다. */
    if (target_w == src->width && target_h == src->height) {
        Image copy;
        copy.width = src->width;
        copy.height = src->height;
        size_t bytes = checked_pixel_bytes(copy.width, copy.height);
        copy.pixels = (Pixel*)xmalloc(bytes);
        memcpy(copy.pixels, src->pixels, bytes);
        return copy;
    }

    /* filter 값에 따라 쌍선형 또는 최근접 이웃 리사이즈를 수행한다. */
    if (filter == FILTER_BILINEAR) {
        return resize_bilinear(src, target_w, target_h);
    }
    return resize_nearest(src, target_w, target_h);
}

void apply_grayscale(Image* img) {
    /* 모든 픽셀을 순회한다. */
    for (int y = 0; y < img->height; y++) {
        for (int x = 0; x < img->width; x++) {
            Pixel* p = &img->pixels[(size_t)y * img->width + x];
            /* BT.601 가중치(0.299R + 0.587G + 0.114B)로 휘도를 계산한다. */
            int gray = round_to_int(0.299 * (double)p->r + 0.587 * (double)p->g + 0.114 * (double)p->b);
            gray = clamp_int(gray, 0, 255);
            /* RGB 세 채널을 동일한 gray 값으로 덮어쓴다. */
            p->r = (unsigned char)gray;
            p->g = (unsigned char)gray;
            p->b = (unsigned char)gray;
        }
    }
}

char* base64_urlsafe_encode(const unsigned char* data, size_t len) {
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

    /* 출력 문자 수를 3바이트 → 4문자 비율로 계산하고 버퍼를 할당한다. */
    size_t out_len = ((len + 2) / 3) * 4;
    char* out = (char*)xmalloc(out_len + 1);

    size_t ip = 0;
    size_t op = 0;

    /* 3바이트씩 읽어 24비트 트리플을 구성하고, 6비트씩 나눠 테이블에서 문자를 가져온다. */
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

    /* 나머지 바이트 수에 따라 '=' 패딩을 추가한다. */
    if (len % 3 == 1) {
        out[out_len - 2] = '=';
        out[out_len - 1] = '=';
    }
    else if (len % 3 == 2) {
        out[out_len - 1] = '=';
    }

    out[out_len] = '\0';
    return out;
}

unsigned char* gzip_compress_buffer(const unsigned char* src, size_t src_len, size_t* out_len) {
    /* deflateInit2로 gzip 형식(MAX_WBITS+16)의 z_stream을 초기화한다. */
    z_stream zs;
    memset(&zs, 0, sizeof(zs));

    int ret = deflateInit2(&zs, Z_BEST_COMPRESSION, Z_DEFLATED, MAX_WBITS + 16, 8, Z_DEFAULT_STRATEGY);
    if (ret != Z_OK) {
        die("deflateInit2 failed: %d", ret);
    }

    /* deflateBound로 최대 출력 크기를 추정하고 버퍼를 할당한다. */
    uLong bound = deflateBound(&zs, (uLong)src_len);
    unsigned char* out = (unsigned char*)xmalloc(bound);

    /* 입출력 버퍼를 z_stream에 연결하고 Z_FINISH로 단일 패스 압축을 수행한다. */
    zs.next_in = (Bytef*)src;
    zs.avail_in = (uInt)src_len;
    zs.next_out = out;
    zs.avail_out = (uInt)bound;

    ret = deflate(&zs, Z_FINISH);
    if (ret != Z_STREAM_END) {
        deflateEnd(&zs);
        free(out);
        die("gzip compression failed: %d", ret);
    }

    /* 실제 출력 길이를 기록하고 스트림을 종료한다. */
    *out_len = zs.total_out;
    deflateEnd(&zs);
    return out;
}

char* gzip_base64_urlsafe(const char* text) {
    /* text를 gzip으로 압축한다. */
    size_t compressed_len = 0;
    unsigned char* compressed = gzip_compress_buffer((const unsigned char*)text, strlen(text), &compressed_len);
    /* 압축 결과를 URL-safe Base64로 인코딩한다. */
    char* encoded = base64_urlsafe_encode(compressed, compressed_len);
    /* 압축 버퍼를 해제하고 인코딩 결과를 반환한다. */
    free(compressed);
    return encoded;
}

char* base64_urlsafe_text(const char* text) {
    /* text를 바이트 배열로 간주하여 base64_urlsafe_encode를 호출한다. */
    return base64_urlsafe_encode((const unsigned char*)text, strlen(text));
}

void append_color_channel(StringBuilder* sb, int channel_id, int r, int g, int b, int a) {
    /* 알파 값을 0.0~1.0 불투명도로 변환한다. */
    double opacity = (double)clamp_int(a, 0, 255) / 255.0;

    /* GD 색상 채널 형식 문자열을 sb에 추가한다. */
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

void append_default_color_channels(StringBuilder* sb) {
    /* GD 레벨에서 사용되는 고정 기본 색상 채널 8개를 순서대로 추가한다. */
    append_color_channel(sb, 1000, 255, 255, 255, 255);
    append_color_channel(sb, 1001, 0, 0, 0, 255);
    append_color_channel(sb, 1009, 0, 0, 0, 255);
    append_color_channel(sb, 1013, 0, 0, 0, 255);
    append_color_channel(sb, 1014, 0, 0, 0, 255);
    append_color_channel(sb, 1004, 200, 200, 200, 255);
    append_color_channel(sb, 1005, 255, 255, 255, 255);
    append_color_channel(sb, 1006, 255, 255, 255, 255);
}

void append_level_object(StringBuilder* sb, int object_id, int x, int y, int channel_id) {
    /* GD 오브젝트 속성을 key,value CSV 형식으로 조립하여 sb에 추가한다. */
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

void write_gmd_file(const char* output_path, const char* level_name, const char* inner_level_base64) {
    /* 빈 설명 문자열을 Base64로 인코딩한다. */
    char* description_base64 = base64_urlsafe_text("");

    /* plist XML 구조를 StringBuilder로 조립한다. */
    StringBuilder plist;
    sb_init(&plist);

    sb_append(&plist, "<?xml version=\"1.0\"?>");
    sb_append(&plist, "<plist version=\"1.0\" gjver=\"2.0\">");
    sb_append(&plist, "<dict>");

    /* 레벨 데이터(k4), 이름(k2), 설명(k3), 메타 키를 XML 이스케이프하여 추가한다. */
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

    /* 출력 파일을 바이너리 모드로 열어 조립된 plist를 쓴다. */
    FILE* fp = fopen(output_path, "wb");
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

    /* 파일을 닫고 동적 자원을 해제한다. */
    fclose(fp);
    free(description_base64);
    sb_free(&plist);
}

void print_help(FILE* out) {
    /* 버전, 사용법, 옵션 목록 및 예시를 fprintf로 출력한다. */
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

void options_init(Options* opt) {
    /* memset으로 구조체 전체를 0으로 초기화한다. */
    memset(opt, 0, sizeof(*opt));
    /* 각 필드에 컴파일 타임 기본값을 할당한다. */
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

const char* require_value(int argc, char** argv, int* i, const char* name) {
    /* 다음 인덱스가 argc 이내인지 확인한다. 범위를 벗어나면 die()를 호출한다. */
    if (*i + 1 >= argc) {
        die("missing value for %s", name);
    }
    /* *i를 증가시키고 해당 인수 문자열을 반환한다. */
    (*i)++;
    return argv[*i];
}

void parse_args(Options* opt, int argc, char** argv) {
    /* options_init으로 기본값을 설정한다. */
    options_init(opt);

    /* 인수가 없으면 도움말을 출력하고 종료한다. */
    if (argc == 1) {
        print_help(stderr);
        exit(EXIT_FAILURE);
    }

    /* 인수를 순서대로 순회하며 각 옵션을 파싱하여 opt에 채운다. */
    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];

        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            print_help(stdout);
            exit(EXIT_SUCCESS);
        }
        else if (strcmp(arg, "--version") == 0) {
            printf("HGDImageGen %s\n", HGD_VERSION);
            exit(EXIT_SUCCESS);
        }
        else if (strcmp(arg, "-i") == 0 || strcmp(arg, "--input") == 0) {
            opt->input_path = require_value(argc, argv, &i, arg);
        }
        else if (strcmp(arg, "-o") == 0 || strcmp(arg, "--output") == 0) {
            opt->output_path = require_value(argc, argv, &i, arg);
        }
        else if (strcmp(arg, "-n") == 0 || strcmp(arg, "--level-name") == 0) {
            opt->level_name = require_value(argc, argv, &i, arg);
        }
        else if (strcmp(arg, "-W") == 0 || strcmp(arg, "--width") == 0) {
            opt->target_width = parse_int_arg(require_value(argc, argv, &i, arg), arg);
        }
        else if (strcmp(arg, "-H") == 0 || strcmp(arg, "--height") == 0) {
            opt->target_height = parse_int_arg(require_value(argc, argv, &i, arg), arg);
        }
        else if (strcmp(arg, "--scale") == 0) {
            opt->scale = parse_double_arg(require_value(argc, argv, &i, arg), arg);
        }
        else if (strcmp(arg, "--filter") == 0) {
            const char* value = require_value(argc, argv, &i, arg);
            if (strcmp(value, "nearest") == 0) {
                opt->filter = FILTER_NEAREST;
            }
            else if (strcmp(value, "bilinear") == 0) {
                opt->filter = FILTER_BILINEAR;
            }
            else {
                die("invalid filter: %s. Use nearest or bilinear", value);
            }
        }
        else if (strcmp(arg, "--grayscale") == 0 || strcmp(arg, "--gray") == 0 || strcmp(arg, "--mono") == 0) {
            opt->grayscale = true;
        }
        else if (strcmp(arg, "--color") == 0 || strcmp(arg, "--no-grayscale") == 0) {
            opt->grayscale = false;
        }
        else if (strcmp(arg, "--quant-step") == 0) {
            opt->quant_step = parse_int_arg(require_value(argc, argv, &i, arg), arg);
        }
        else if (strcmp(arg, "--alpha-step") == 0) {
            opt->alpha_step = parse_int_arg(require_value(argc, argv, &i, arg), arg);
        }
        else if (strcmp(arg, "--alpha-skip-below") == 0) {
            opt->alpha_skip_below = parse_int_arg(require_value(argc, argv, &i, arg), arg);
        }
        else if (strcmp(arg, "--max-channels") == 0) {
            opt->max_channels = parse_int_arg(require_value(argc, argv, &i, arg), arg);
        }
        else if (strcmp(arg, "--max-objects") == 0) {
            opt->max_objects = parse_int_arg(require_value(argc, argv, &i, arg), arg);
        }
        else if (strcmp(arg, "--tile-size") == 0) {
            opt->tile_size = parse_int_arg(require_value(argc, argv, &i, arg), arg);
        }
        else if (strcmp(arg, "--start-x") == 0) {
            opt->start_x = parse_int_arg(require_value(argc, argv, &i, arg), arg);
        }
        else if (strcmp(arg, "--start-y") == 0) {
            opt->start_y = parse_int_arg(require_value(argc, argv, &i, arg), arg);
        }
        else if (strcmp(arg, "--object-id") == 0) {
            opt->object_id = parse_int_arg(require_value(argc, argv, &i, arg), arg);
        }
        else if (strcmp(arg, "--log") == 0) {
            opt->log_path = require_value(argc, argv, &i, arg);
        }
        else {
            die("unknown option: %s", arg);
        }
    }

    /* 필수 인수 존재 여부와 각 옵션의 유효 범위를 검사한다. */
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

void compute_output_size(const Options* opt, const Image* src, int* out_w, int* out_h) {
    int w = opt->target_width;
    int h = opt->target_height;

    /* width와 height가 모두 지정되었으면 그대로 사용한다. */
    if (w > 0 && h > 0) {
        *out_w = w;
        *out_h = h;
        return;
    }

    /* width만 지정되었으면 종횡비를 유지하며 height를 계산한다. */
    if (w > 0 && h == 0) {
        h = round_to_int((double)src->height * ((double)w / (double)src->width));
        if (h < 1) h = 1;
        *out_w = w;
        *out_h = h;
        return;
    }

    /* height만 지정되었으면 종횡비를 유지하며 width를 계산한다. */
    if (w == 0 && h > 0) {
        w = round_to_int((double)src->width * ((double)h / (double)src->height));
        if (w < 1) w = 1;
        *out_w = w;
        *out_h = h;
        return;
    }

    /* scale이 지정되었으면 원본 크기에 배율을 적용한다. */
    if (opt->scale > 0.0) {
        w = round_to_int((double)src->width * opt->scale);
        h = round_to_int((double)src->height * opt->scale);
        if (w < 1) w = 1;
        if (h < 1) h = 1;
        *out_w = w;
        *out_h = h;
        return;
    }

    /* 아무 옵션도 지정되지 않으면 원본 크기를 그대로 사용한다. */
    *out_w = src->width;
    *out_h = src->height;
}

void convert_image_to_gmd(const Options* opt) {
    /* 입력 PNG를 RGBA 이미지로 로드한다. */
    Image source = load_png_rgba(opt->input_path);

    /* 출력 크기를 결정하고, 픽셀 수가 max_objects를 초과하면 종료한다. */
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

    /* 필요 시 이미지를 리사이즈하고 원본 이미지를 해제한다. */
    Image img = resize_image_if_needed(&source, out_w, out_h, opt->filter);
    image_free(&source);

    /* grayscale 옵션이 활성화된 경우 그레이스케일 변환을 적용한다. */
    if (opt->grayscale) {
        apply_grayscale(&img);
    }

    /* 로그 파일을 열고 처리 정보를 기록한다. */
    FILE* log = fopen(opt->log_path, "wb");
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

    /* 색상 맵과 채널 배열을 초기화한다. */
    ColorMap color_map;
    color_map_init(&color_map, (size_t)opt->max_channels);

    ColorChannelDef* channels = (ColorChannelDef*)xmalloc((size_t)opt->max_channels * sizeof(ColorChannelDef));
    int channel_count = 0;
    int next_channel_id = 1;
    int skipped_transparent = 0;
    int object_count = 0;

    StringBuilder objects;
    sb_init(&objects);

    /* 픽셀을 좌→우, 위→아래 순으로 순회한다. */
    for (int y = 0; y < img.height; y++) {
        for (int x = 0; x < img.width; x++) {
            Pixel p = img.pixels[(size_t)y * img.width + x];

            int r = p.r;
            int g = p.g;
            int b = p.b;
            int a = p.a;

            /* 알파가 임계값 미만이면 해당 픽셀을 건너뛴다. */
            if (a < opt->alpha_skip_below) {
                skipped_transparent++;
                fprintf(log, "x=%d,y=%d,rgba=(%d,%d,%d,%d),skipped=alpha\n", x, y, r, g, b, a);
                continue;
            }

            /* RGBA 채널을 각각 양자화한다. */
            int qr = quantize_channel(r, opt->quant_step);
            int qg = quantize_channel(g, opt->quant_step);
            int qb = quantize_channel(b, opt->quant_step);
            int qa = quantize_channel(a, opt->alpha_step);

            /* 원래 알파가 0보다 크면 양자화 결과가 0이 되지 않도록 보정한다. */
            if (a > 0 && qa == 0) {
                qa = opt->alpha_step;
            }
            qa = clamp_int(qa, 1, 255);

            /* 양자화된 색상으로 키를 만들어 기존 채널 ID를 조회한다. */
            uint32_t key = rgba_key(qr, qg, qb, qa);
            int channel_id = color_map_get(&color_map, key);

            /* 새로운 색상이면 채널을 등록한다. 최대 채널 수를 초과하면 종료한다. */
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

            /* GD 좌표계로 변환하여 오브젝트 문자열을 추가한다. */
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

    /* raw 레벨 문자열을 조립한다: 기본 색상 채널 + 생성된 채널 + 오브젝트 목록. */
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

    /* raw 문자열을 gzip 압축 후 Base64 인코딩하여 .gmd 파일에 쓴다. */
    char* inner_b64 = gzip_base64_urlsafe(raw.data);
    write_gmd_file(opt->output_path, opt->level_name, inner_b64);

    /* 로그 마무리 항목을 기록하고 파일을 닫는다. */
    fprintf(log, "[LOG] object_count=%d\n", object_count);
    fprintf(log, "[LOG] skipped_transparent=%d\n", skipped_transparent);
    fprintf(log, "[LOG] used_color_channels=%d\n", channel_count);
    fprintf(log, "[LOG] raw_level_string_bytes=%zu\n", raw.len);
    fprintf(log, "[LOG] done\n");
    fclose(log);

    /* 처리 결과를 콘솔에 출력한다. */
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

    /* 모든 동적 자원을 해제한다. */
    free(inner_b64);
    sb_free(&raw);
    sb_free(&objects);
    color_map_free(&color_map);
    free(channels);
    image_free(&img);
}
