/*******************************************************************************
 * Size: 10 px
 * Bpp: 1
 * Opts: --size 10 --bpp 1 --no-compress --format lvgl --font /tmp/hiragino_gb_w3.ttf -r 0x20-0x7E --symbols 位作停入再到台定工已待手按接松框止点短等话说输连键长 --lv-font-name zh10 -o zh10.c
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl.h"
#endif

#ifndef ZH10
#define ZH10 1
#endif

#if ZH10

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+0020 " " */
    0x0,

    /* U+0021 "!" */
    0xfd,

    /* U+0022 "\"" */
    0xb4,

    /* U+0023 "#" */
    0x52, 0xbe, 0xa5, 0x7e, 0x94,

    /* U+0024 "$" */
    0x23, 0xab, 0x4e, 0x1c, 0xb5, 0x71, 0x0,

    /* U+0025 "%" */
    0x62, 0x94, 0x98, 0x6e, 0x19, 0x29, 0x29, 0x46,

    /* U+0026 "&" */
    0x30, 0x91, 0x21, 0x85, 0x31, 0xa3, 0x39,

    /* U+0027 "'" */
    0xc0,

    /* U+0028 "(" */
    0x5a, 0xaa, 0x40,

    /* U+0029 ")" */
    0xa5, 0x55, 0x80,

    /* U+002A "*" */
    0x4d, 0x80,

    /* U+002B "+" */
    0x21, 0x9, 0xf2, 0x10,

    /* U+002C "," */
    0x80,

    /* U+002D "-" */
    0xe0,

    /* U+002E "." */
    0x80,

    /* U+002F "/" */
    0x10, 0x84, 0x42, 0x11, 0x8, 0x84, 0x0,

    /* U+0030 "0" */
    0x7b, 0x38, 0x61, 0x86, 0x1c, 0xde,

    /* U+0031 "1" */
    0xd5, 0x55,

    /* U+0032 "2" */
    0x74, 0x62, 0x22, 0x62, 0x1f,

    /* U+0033 "3" */
    0x39, 0x10, 0x4e, 0xc, 0x14, 0x4e,

    /* U+0034 "4" */
    0x8, 0x62, 0x92, 0x8b, 0xf0, 0x82,

    /* U+0035 "5" */
    0x7d, 0x7, 0x93, 0x4, 0x14, 0xde,

    /* U+0036 "6" */
    0x79, 0xb, 0xb3, 0x86, 0x1c, 0xde,

    /* U+0037 "7" */
    0xf8, 0x84, 0x42, 0x21, 0x8,

    /* U+0038 "8" */
    0x72, 0x28, 0x9c, 0x8a, 0x18, 0x5e,

    /* U+0039 "9" */
    0x74, 0x63, 0x17, 0x86, 0x6e,

    /* U+003A ":" */
    0x84,

    /* U+003B ";" */
    0x84,

    /* U+003C "<" */
    0x9, 0x91, 0x82, 0xc,

    /* U+003D "=" */
    0xf8, 0x3e,

    /* U+003E ">" */
    0x82, 0xc, 0x26, 0x40,

    /* U+003F "?" */
    0x74, 0x42, 0x22, 0x10, 0x4,

    /* U+0040 "@" */
    0x3c, 0x5e, 0x95, 0xa5, 0xa9, 0xab, 0x76, 0x3c,

    /* U+0041 "A" */
    0x10, 0x70, 0xa1, 0x24, 0x4f, 0x90, 0xc1,

    /* U+0042 "B" */
    0xfa, 0x18, 0x7e, 0x86, 0x18, 0x7e,

    /* U+0043 "C" */
    0x38, 0x8a, 0x4, 0x8, 0x10, 0x51, 0x1c,

    /* U+0044 "D" */
    0xfa, 0x28, 0x61, 0x86, 0x18, 0xbc,

    /* U+0045 "E" */
    0xfc, 0x21, 0xf8, 0x42, 0x1f,

    /* U+0046 "F" */
    0xfc, 0x21, 0xe8, 0x42, 0x10,

    /* U+0047 "G" */
    0x39, 0x18, 0x20, 0x8e, 0x14, 0x5e,

    /* U+0048 "H" */
    0x86, 0x18, 0x7f, 0x86, 0x18, 0x61,

    /* U+0049 "I" */
    0xff,

    /* U+004A "J" */
    0x11, 0x11, 0x11, 0x9e,

    /* U+004B "K" */
    0x8a, 0x6b, 0x28, 0xd2, 0x48, 0xa1,

    /* U+004C "L" */
    0x84, 0x21, 0x8, 0x42, 0x1f,

    /* U+004D "M" */
    0x81, 0xc3, 0xc3, 0xa5, 0xa5, 0xa9, 0x99, 0x91,

    /* U+004E "N" */
    0x87, 0x1a, 0x69, 0x96, 0x58, 0xe1,

    /* U+004F "O" */
    0x38, 0x8a, 0xc, 0x18, 0x30, 0x51, 0x1c,

    /* U+0050 "P" */
    0xfa, 0x18, 0x61, 0xfa, 0x8, 0x20,

    /* U+0051 "Q" */
    0x38, 0x8a, 0xc, 0x18, 0x30, 0x53, 0x1e, 0x0,

    /* U+0052 "R" */
    0xf9, 0xa, 0x17, 0xc8, 0x91, 0x21, 0x42,

    /* U+0053 "S" */
    0x74, 0x60, 0x83, 0xc6, 0x2e,

    /* U+0054 "T" */
    0xfe, 0x20, 0x40, 0x81, 0x2, 0x4, 0x8,

    /* U+0055 "U" */
    0x86, 0x18, 0x61, 0x86, 0x1c, 0xde,

    /* U+0056 "V" */
    0x82, 0x89, 0x12, 0x22, 0x85, 0xa, 0x8,

    /* U+0057 "W" */
    0x84, 0x63, 0x24, 0xc9, 0x52, 0x52, 0x94, 0xc3,
    0x30, 0x84,

    /* U+0058 "X" */
    0x44, 0x48, 0xa0, 0x82, 0x85, 0x11, 0x21,

    /* U+0059 "Y" */
    0x82, 0x88, 0xa1, 0x41, 0x2, 0x4, 0x8,

    /* U+005A "Z" */
    0x7c, 0x10, 0x84, 0x20, 0x84, 0x3f,

    /* U+005B "[" */
    0xea, 0xaa, 0xb0,

    /* U+005C "\\" */
    0x84, 0x10, 0x82, 0x10, 0x82, 0x10, 0x80,

    /* U+005D "]" */
    0xd5, 0x55, 0x70,

    /* U+005E "^" */
    0x22, 0x95, 0x10,

    /* U+005F "_" */
    0xf8,

    /* U+0060 "`" */
    0xc0,

    /* U+0061 "a" */
    0x74, 0x5f, 0x19, 0xf4,

    /* U+0062 "b" */
    0x84, 0x2d, 0x98, 0xc6, 0x3e,

    /* U+0063 "c" */
    0x76, 0x61, 0xc, 0xb8,

    /* U+0064 "d" */
    0x8, 0x5b, 0x38, 0xc6, 0x2f,

    /* U+0065 "e" */
    0x74, 0x7f, 0x8, 0xb8,

    /* U+0066 "f" */
    0x74, 0xf4, 0x44, 0x44,

    /* U+0067 "g" */
    0x6c, 0xe3, 0x19, 0xb4, 0x2e,

    /* U+0068 "h" */
    0x84, 0x2d, 0x98, 0xc6, 0x31,

    /* U+0069 "i" */
    0xbf,

    /* U+006A "j" */
    0x20, 0x92, 0x49, 0xbc,

    /* U+006B "k" */
    0x84, 0x25, 0x4c, 0x52, 0x51,

    /* U+006C "l" */
    0xff,

    /* U+006D "m" */
    0xef, 0x26, 0x4c, 0x99, 0x32, 0x40,

    /* U+006E "n" */
    0xb6, 0x63, 0x18, 0xc4,

    /* U+006F "o" */
    0x74, 0x63, 0x18, 0xb8,

    /* U+0070 "p" */
    0xb6, 0x63, 0x18, 0xfa, 0x10,

    /* U+0071 "q" */
    0x6c, 0xe3, 0x19, 0xb4, 0x21,

    /* U+0072 "r" */
    0xfc, 0x88, 0x88,

    /* U+0073 "s" */
    0xe9, 0x87, 0x97,

    /* U+0074 "t" */
    0x44, 0xf4, 0x44, 0x47,

    /* U+0075 "u" */
    0x8c, 0x63, 0x19, 0xb4,

    /* U+0076 "v" */
    0x8a, 0x54, 0xa3, 0x10,

    /* U+0077 "w" */
    0x92, 0x9a, 0x5a, 0x6a, 0x6c, 0x24,

    /* U+0078 "x" */
    0x4a, 0x88, 0x65, 0x44,

    /* U+0079 "y" */
    0x8a, 0x54, 0xa3, 0x10, 0x88,

    /* U+007A "z" */
    0x70, 0x88, 0x84, 0x7c,

    /* U+007B "{" */
    0x69, 0x24, 0x92, 0x4c,

    /* U+007C "|" */
    0xff, 0xc0,

    /* U+007D "}" */
    0xc9, 0x24, 0x92, 0x58,

    /* U+007E "~" */
    0x60, 0xc0,

    /* U+4F4D "位" */
    0x24, 0x12, 0x17, 0xe8, 0xd, 0x12, 0x49, 0x28,
    0x84, 0x5f, 0xa0, 0x0,

    /* U+4F5C "作" */
    0x10, 0x5, 0x2, 0x7c, 0xa8, 0xfb, 0xc8, 0x82,
    0x3c, 0x88, 0x22, 0x8, 0x80,

    /* U+505C "停" */
    0x24, 0x1f, 0xd0, 0xb, 0xec, 0xf2, 0xfd, 0x3e,
    0x88, 0x44, 0x26, 0x0,

    /* U+5165 "入" */
    0x30, 0x8, 0x6, 0x3, 0x2, 0x41, 0x21, 0x9,
    0x2, 0x0, 0x0,

    /* U+518D "再" */
    0xff, 0x84, 0x1f, 0xc9, 0x27, 0xf7, 0xfd, 0x4,
    0x82, 0x43, 0x0,

    /* U+5230 "到" */
    0xf8, 0xa2, 0x65, 0x3d, 0x92, 0x4f, 0xe4, 0x92,
    0x71, 0xc3, 0x80,

    /* U+53F0 "台" */
    0x10, 0x11, 0x10, 0x5f, 0xf0, 0xb, 0xf9, 0x4,
    0x82, 0x7f, 0x20, 0x80,

    /* U+5B9A "定" */
    0x8, 0x7f, 0xe0, 0x3f, 0xf0, 0x82, 0x41, 0x3c,
    0x90, 0x68, 0x4f, 0xc0,

    /* U+5DE5 "工" */
    0x7f, 0x84, 0x2, 0x1, 0x0, 0x80, 0x43, 0xfe,

    /* U+5DF2 "已" */
    0xfe, 0x82, 0x82, 0xfe, 0x80, 0x80, 0x81, 0x81,
    0xfe,

    /* U+5F85 "待" */
    0x44, 0x4f, 0x81, 0xb, 0x6d, 0xfa, 0x89, 0x24,
    0x82, 0x47, 0x0,

    /* U+624B "手" */
    0x3, 0x3e, 0x2, 0x1f, 0xe0, 0x80, 0x43, 0xfe,
    0x10, 0x8, 0x1c, 0x0,

    /* U+6309 "按" */
    0x44, 0x2f, 0xfd, 0x2b, 0xf6, 0xa6, 0x91, 0x30,
    0x96, 0xd1, 0x0,

    /* U+63A5 "接" */
    0x44, 0x2f, 0xd2, 0x4b, 0xf6, 0x46, 0xfd, 0x28,
    0x8c, 0xd9, 0x80,

    /* U+677E "松" */
    0x21, 0x9, 0x47, 0x48, 0xaa, 0x7a, 0x29, 0xa,
    0x48, 0xa2, 0x2f, 0x40,

    /* U+6846 "框" */
    0x5f, 0xff, 0xd4, 0x8a, 0x4f, 0x7e, 0x91, 0x7e,
    0xa0, 0x5f, 0x80,

    /* U+6B62 "止" */
    0x4, 0x1, 0x2, 0x40, 0x9e, 0x24, 0x9, 0x2,
    0x40, 0x90, 0xff, 0xc0,

    /* U+70B9 "点" */
    0x8, 0x7, 0xc2, 0xf, 0xe4, 0x13, 0xf8, 0x5,
    0x29, 0x94, 0x80,

    /* U+77ED "短" */
    0x40, 0x27, 0xdc, 0x15, 0xe2, 0x93, 0x79, 0x44,
    0x8a, 0x8f, 0x80,

    /* U+7B49 "等" */
    0x44, 0x5f, 0xe2, 0xf, 0xe0, 0x83, 0xb9, 0xfe,
    0x44, 0x12, 0x3, 0x0,

    /* U+8BDD "话" */
    0x4f, 0x2, 0x1, 0x1f, 0xf4, 0x42, 0xf9, 0x44,
    0xbe, 0x11, 0x0,

    /* U+8BF4 "说" */
    0x45, 0x89, 0x40, 0xfb, 0xa2, 0x2f, 0x89, 0x42,
    0x50, 0x95, 0x19, 0xc0,

    /* U+8F93 "输" */
    0x42, 0x3d, 0x64, 0xf6, 0x82, 0xff, 0x8b, 0xee,
    0xf8, 0xaa, 0x2c, 0x80,

    /* U+8FDE "连" */
    0x4, 0x17, 0xe2, 0x80, 0x28, 0xf, 0x98, 0x83,
    0xfc, 0x88, 0x72, 0x27, 0xf0,

    /* U+952E "键" */
    0x42, 0x7b, 0x85, 0xeb, 0xe5, 0x27, 0x91, 0x7e,
    0xf4, 0x27, 0x80,

    /* U+957F "长" */
    0x21, 0x13, 0xa, 0x1f, 0xf2, 0x41, 0x20, 0x88,
    0x73,
    /* U+5FF5 "念" */
    0x8, 0xa, 0x14, 0xd1, 0x97, 0xf0, 0x10, 0x51, 0x52, 0xa2, 0x9f, 0x0,
    /* U+788E "碎" */
    0x4, 0x7f, 0x90, 0x12, 0x47, 0xb5, 0x2, 0x91, 0x7e, 0x64, 0x42, 0x0,
    /* U+8BED "语" */
    0x5f, 0x84, 0x7, 0xd9, 0x27, 0xfa, 0xf9, 0x44, 0xfe, 0x11, 0x0,
    /* U+97F3 "音" */
    0x8, 0x3f, 0x88, 0x84, 0x8d, 0xbb, 0xf9, 0x4, 0xfe, 0x7f, 0x20, 0x80,
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 53, .box_w = 1, .box_h = 1, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1, .adv_w = 50, .box_w = 1, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 2, .adv_w = 57, .box_w = 3, .box_h = 2, .ofs_x = 0, .ofs_y = 6},
    {.bitmap_index = 3, .adv_w = 105, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 8, .adv_w = 104, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 15, .adv_w = 141, .box_w = 8, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 23, .adv_w = 128, .box_w = 7, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 30, .adv_w = 31, .box_w = 1, .box_h = 2, .ofs_x = 0, .ofs_y = 6},
    {.bitmap_index = 31, .adv_w = 57, .box_w = 2, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 34, .adv_w = 57, .box_w = 2, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 37, .adv_w = 55, .box_w = 3, .box_h = 3, .ofs_x = 0, .ofs_y = 5},
    {.bitmap_index = 39, .adv_w = 105, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 43, .adv_w = 38, .box_w = 1, .box_h = 2, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 44, .adv_w = 57, .box_w = 3, .box_h = 1, .ofs_x = 0, .ofs_y = 3},
    {.bitmap_index = 45, .adv_w = 38, .box_w = 1, .box_h = 1, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 46, .adv_w = 76, .box_w = 5, .box_h = 10, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 53, .adv_w = 105, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 59, .adv_w = 105, .box_w = 2, .box_h = 8, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 61, .adv_w = 105, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 66, .adv_w = 105, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 72, .adv_w = 106, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 78, .adv_w = 105, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 84, .adv_w = 105, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 90, .adv_w = 105, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 95, .adv_w = 105, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 101, .adv_w = 105, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 106, .adv_w = 38, .box_w = 1, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 107, .adv_w = 38, .box_w = 1, .box_h = 7, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 108, .adv_w = 82, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 112, .adv_w = 105, .box_w = 5, .box_h = 3, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 114, .adv_w = 82, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 118, .adv_w = 91, .box_w = 5, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 123, .adv_w = 145, .box_w = 8, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 131, .adv_w = 120, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 138, .adv_w = 114, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 144, .adv_w = 121, .box_w = 7, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 151, .adv_w = 121, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 157, .adv_w = 103, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 162, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 167, .adv_w = 121, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 173, .adv_w = 124, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 179, .adv_w = 40, .box_w = 1, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 180, .adv_w = 81, .box_w = 4, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 184, .adv_w = 113, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 190, .adv_w = 96, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 195, .adv_w = 151, .box_w = 8, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 203, .adv_w = 124, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 209, .adv_w = 127, .box_w = 7, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 216, .adv_w = 106, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 222, .adv_w = 127, .box_w = 7, .box_h = 9, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 230, .adv_w = 116, .box_w = 7, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 237, .adv_w = 107, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 242, .adv_w = 104, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 249, .adv_w = 124, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 255, .adv_w = 115, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 262, .adv_w = 161, .box_w = 10, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 272, .adv_w = 116, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 279, .adv_w = 110, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 286, .adv_w = 107, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 292, .adv_w = 55, .box_w = 2, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 295, .adv_w = 76, .box_w = 5, .box_h = 10, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 302, .adv_w = 55, .box_w = 2, .box_h = 10, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 305, .adv_w = 85, .box_w = 5, .box_h = 4, .ofs_x = 0, .ofs_y = 4},
    {.bitmap_index = 308, .adv_w = 80, .box_w = 5, .box_h = 1, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 309, .adv_w = 80, .box_w = 2, .box_h = 2, .ofs_x = 1, .ofs_y = 6},
    {.bitmap_index = 310, .adv_w = 92, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 314, .adv_w = 103, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 319, .adv_w = 92, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 323, .adv_w = 103, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 328, .adv_w = 94, .box_w = 5, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 332, .adv_w = 67, .box_w = 4, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 336, .adv_w = 103, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 341, .adv_w = 99, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 346, .adv_w = 38, .box_w = 1, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 347, .adv_w = 42, .box_w = 3, .box_h = 10, .ofs_x = -1, .ofs_y = -2},
    {.bitmap_index = 351, .adv_w = 92, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 356, .adv_w = 37, .box_w = 1, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 357, .adv_w = 149, .box_w = 7, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 363, .adv_w = 99, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 367, .adv_w = 100, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 371, .adv_w = 104, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 376, .adv_w = 103, .box_w = 5, .box_h = 8, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 381, .adv_w = 69, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 384, .adv_w = 86, .box_w = 4, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 387, .adv_w = 63, .box_w = 4, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 391, .adv_w = 99, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 395, .adv_w = 87, .box_w = 5, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 399, .adv_w = 123, .box_w = 8, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 405, .adv_w = 86, .box_w = 5, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 409, .adv_w = 89, .box_w = 5, .box_h = 8, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 414, .adv_w = 80, .box_w = 5, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 418, .adv_w = 56, .box_w = 3, .box_h = 10, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 422, .adv_w = 60, .box_w = 1, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 424, .adv_w = 56, .box_w = 3, .box_h = 10, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 428, .adv_w = 87, .box_w = 5, .box_h = 2, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 430, .adv_w = 160, .box_w = 9, .box_h = 10, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 442, .adv_w = 160, .box_w = 10, .box_h = 10, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 455, .adv_w = 160, .box_w = 9, .box_h = 10, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 467, .adv_w = 160, .box_w = 9, .box_h = 9, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 478, .adv_w = 160, .box_w = 9, .box_h = 9, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 489, .adv_w = 160, .box_w = 9, .box_h = 9, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 500, .adv_w = 160, .box_w = 9, .box_h = 10, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 512, .adv_w = 160, .box_w = 9, .box_h = 10, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 524, .adv_w = 160, .box_w = 9, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 532, .adv_w = 160, .box_w = 8, .box_h = 9, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 541, .adv_w = 160, .box_w = 9, .box_h = 9, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 552, .adv_w = 160, .box_w = 9, .box_h = 10, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 564, .adv_w = 160, .box_w = 9, .box_h = 9, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 575, .adv_w = 160, .box_w = 9, .box_h = 9, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 586, .adv_w = 160, .box_w = 10, .box_h = 9, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 598, .adv_w = 160, .box_w = 9, .box_h = 9, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 609, .adv_w = 160, .box_w = 10, .box_h = 9, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 621, .adv_w = 160, .box_w = 9, .box_h = 9, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 632, .adv_w = 160, .box_w = 9, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 643, .adv_w = 160, .box_w = 9, .box_h = 10, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 655, .adv_w = 160, .box_w = 9, .box_h = 9, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 666, .adv_w = 160, .box_w = 10, .box_h = 9, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 678, .adv_w = 160, .box_w = 10, .box_h = 9, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 690, .adv_w = 160, .box_w = 10, .box_h = 10, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 703, .adv_w = 160, .box_w = 9, .box_h = 9, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 714, .adv_w = 160, .box_w = 9, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 723, .adv_w = 160, .box_w = 9, .box_h = 10, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 735, .adv_w = 160, .box_w = 9, .box_h = 10, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 747, .adv_w = 160, .box_w = 9, .box_h = 9, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 758, .adv_w = 160, .box_w = 9, .box_h = 10, .ofs_x = 1, .ofs_y = -1},
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/

static const uint16_t unicode_list_1[] = {
    0x0, 0xf, 0x10f, 0x218, 0x240, 0x2e3, 0x4a3, 0xc4d,
    0xe98, 0xea5, 0x1038, 0x12fe, 0x13bc, 0x1458, 0x1831, 0x18f9,
    0x1c15, 0x216c, 0x28a0, 0x2bfc, 0x3c90, 0x3ca7, 0x4046, 0x4091,
    0x45e1, 0x4632
};

static const uint16_t unicode_list_new[] = {
    0x0
};

/*Collect the unicode lists and glyph_id offsets*/
/* NOTE: the 4 new glyphs (念 碎 语 音) must be their own single-codepoint cmaps
 * BEFORE the wide CJK cmap below. get_glyph_dsc_id returns as soon as a cmap's
 * range matches — the wide cmap (20301..38271) would otherwise swallow 念/碎/语
 * (which sit inside its range) and miss them, rendering empty boxes. */
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 32, .range_length = 95, .glyph_id_start = 1,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    },
    /* 念 U+5FF5 */
    {
        .range_start = 24565, .range_length = 1, .glyph_id_start = 122,
        .unicode_list = unicode_list_new, .glyph_id_ofs_list = NULL, .list_length = 1, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
    },
    /* 碎 U+788E */
    {
        .range_start = 30862, .range_length = 1, .glyph_id_start = 123,
        .unicode_list = unicode_list_new, .glyph_id_ofs_list = NULL, .list_length = 1, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
    },
    /* 语 U+8BED */
    {
        .range_start = 35821, .range_length = 1, .glyph_id_start = 124,
        .unicode_list = unicode_list_new, .glyph_id_ofs_list = NULL, .list_length = 1, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
    },
    /* 音 U+97F3 */
    {
        .range_start = 38899, .range_length = 1, .glyph_id_start = 125,
        .unicode_list = unicode_list_new, .glyph_id_ofs_list = NULL, .list_length = 1, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
    },
    {
        .range_start = 20301, .range_length = 17971, .glyph_id_start = 96,
        .unicode_list = unicode_list_1, .glyph_id_ofs_list = NULL, .list_length = 26, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
    }
};



/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LVGL_VERSION_MAJOR == 8
/*Store all the custom data of the font*/
static  lv_font_fmt_txt_glyph_cache_t cache;
#endif

#if LVGL_VERSION_MAJOR >= 8
static const lv_font_fmt_txt_dsc_t font_dsc = {
#else
static lv_font_fmt_txt_dsc_t font_dsc = {
#endif
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 6,
    .bpp = 1,
    .kern_classes = 0,
    .bitmap_format = 0,
#if LVGL_VERSION_MAJOR == 8
    .cache = &cache
#endif
};



/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t zh10 = {
#else
lv_font_t zh10 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 11,          /*The maximum line height required by the font*/
    .base_line = 2,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -1,
    .underline_thickness = 1,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if ZH10*/

