#ifndef __FUNCTIONS_H__
#define __FUNCTIONS_H__

#include "type.h"

/* 오류 메시지를 stderr에 출력하고 프로그램을 종료한다. */
void die(const char* fmt, ...);
/* 실패 시 die()를 호출하는 malloc 래퍼. */
void* xmalloc(size_t size);
/* 실패 시 die()를 호출하는 calloc 래퍼. */
void* xcalloc(size_t count, size_t size);
/* width * height 픽셀에 필요한 바이트 수를 오버플로 검사와 함께 반환한다. */
size_t checked_pixel_bytes(int width, int height);

/* StringBuilder를 초기 버퍼로 초기화한다. */
void sb_init(StringBuilder* sb);
/* StringBuilder가 보유한 동적 메모리를 해제한다. */
void sb_free(StringBuilder* sb);
/* sb에 extra 바이트 이상의 여유 공간이 확보되도록 내부 버퍼를 확장한다. */
void sb_reserve(StringBuilder* sb, size_t extra);
/* sb에 길이 n인 문자열 s를 추가한다. */
void sb_append_n(StringBuilder* sb, const char* s, size_t n);
/* sb에 null 종료 문자열 s를 추가한다. */
void sb_append(StringBuilder* sb, const char* s);
/* sb에 printf 형식 문자열을 포맷하여 추가한다. */
void sb_appendf(StringBuilder* sb, const char* fmt, ...);
/* sb에 s의 XML 특수 문자를 엔티티로 이스케이프하여 추가한다. */
void sb_append_xml_escaped(StringBuilder* sb, const char* s);
/* 10진수 정수 문자열 text를 int로 변환하여 반환한다. 유효하지 않으면 die()를 호출한다. */
int parse_int_arg(const char* text, const char* name);

/* 부동소수점 문자열 text를 double로 변환하여 반환한다. 유효하지 않으면 die()를 호출한다. */
double parse_double_arg(const char* text, const char* name);
/* value를 [lo, hi] 범위로 클램프하여 반환한다. */
int clamp_int(int value, int lo, int hi);
/* 실수 x를 반올림하여 int로 변환한다. */
int round_to_int(double x);
/* 채널 값 value를 step 단위로 양자화하여 반환한다. */
int quantize_channel(int value, int step);
/* RGBA 네 채널을 32비트 키로 합쳐 반환한다. */
uint32_t rgba_key(int r, int g, int b, int a);
/* 32비트 정수 x를 해시하여 반환한다. */
uint32_t hash_u32(uint32_t x);
/* x 이상인 가장 작은 2의 거듭제곱을 반환한다. */
size_t next_power_of_two(size_t x);
/* ColorMap을 expected_max_items 크기에 맞는 해시 테이블로 초기화한다. */
void color_map_init(ColorMap* map, size_t expected_max_items);

/* ColorMap이 보유한 동적 메모리를 해제한다. */
void color_map_free(ColorMap* map);
/* ColorMap에서 key에 해당하는 channel_id를 조회한다. 없으면 -1을 반환한다. */
int color_map_get(const ColorMap* map, uint32_t key);
/* ColorMap에 key와 channel_id 쌍을 삽입하거나 갱신한다. */
void color_map_put(ColorMap* map, uint32_t key, int channel_id);
/* Image가 보유한 픽셀 버퍼를 해제한다. */
void image_free(Image* img);
/* libpng 읽기 오류 콜백으로, 경로와 오류 메시지를 포함하여 die()를 호출한다. */
void png_read_error_handler(png_structp png, png_const_charp msg);
/* path에서 PNG를 읽어 RGBA 형식 Image로 반환한다. */
Image load_png_rgba(const char* path);
/* 두 픽셀 a, b를 t 비율(0.0~1.0)로 선형 보간하여 반환한다. */
Pixel pixel_lerp(Pixel a, Pixel b, double t);
/* 최근접 이웃 알고리즘으로 이미지를 new_w x new_h 크기로 리사이즈한다. */
Image resize_nearest(const Image* src, int new_w, int new_h);

/* 쌍선형 보간 알고리즘으로 이미지를 new_w x new_h 크기로 리사이즈한다. */
Image resize_bilinear(const Image* src, int new_w, int new_h);
/* target 크기와 다를 때만 filter 방식으로 리사이즈하고, 같으면 복사본을 반환한다. */
Image resize_image_if_needed(const Image* src, int target_w, int target_h, ResizeFilter filter);
/* BT.601 가중치로 이미지를 인플레이스 그레이스케일 변환한다. */
void apply_grayscale(Image* img);
/* data를 URL-safe Base64로 인코딩하여 반환한다 (패딩 '=' 포함). */
char* base64_urlsafe_encode(const unsigned char* data, size_t len);
/* src를 gzip으로 압축하여 결과 버퍼와 길이를 반환한다. */
unsigned char* gzip_compress_buffer(const unsigned char* src, size_t src_len, size_t* out_len);
/* text를 gzip 압축 후 URL-safe Base64로 인코딩하여 반환한다. */
char* gzip_base64_urlsafe(const char* text);
/* text를 URL-safe Base64로 인코딩하여 반환한다 (압축 없음). */
char* base64_urlsafe_text(const char* text);
/* sb에 Geometry Dash 색상 채널 항목을 포맷하여 추가한다. */
void append_color_channel(StringBuilder* sb, int channel_id, int r, int g, int b, int a);

/* sb에 Geometry Dash 기본 색상 채널 세트를 추가한다. */
void append_default_color_channels(StringBuilder* sb);
/* sb에 Geometry Dash 레벨 오브젝트 항목을 포맷하여 추가한다. */
void append_level_object(StringBuilder* sb, int object_id, int x, int y, int channel_id);
/* output_path에 level_name과 inner_level_base64를 담은 .gmd XML 파일을 쓴다. */
void write_gmd_file(const char* output_path, const char* level_name, const char* inner_level_base64);
/* out에 사용법 및 옵션 설명을 출력한다. */
void print_help(FILE* out);
/* Options 구조체를 기본값으로 초기화한다. */
void options_init(Options* opt);
/* argv[*i+1]이 있으면 그 값을 반환하고 *i를 증가시킨다. 없으면 die()를 호출한다. */
const char* require_value(int argc, char** argv, int* i, const char* name);
/* 명령줄 인수를 파싱하여 opt에 채운다. */
void parse_args(Options* opt, int argc, char** argv);
/* opt와 src 이미지를 기반으로 최종 출력 이미지 크기를 계산한다. */
void compute_output_size(const Options* opt, const Image* src, int* out_w, int* out_h);

/* opt 설정에 따라 PNG를 읽고 처리하여 .gmd 파일을 생성한다. */
void convert_image_to_gmd(const Options* opt);

#endif
