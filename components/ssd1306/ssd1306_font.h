#ifndef SSD1306_FONT_H
#define SSD1306_FONT_H

#include <stdint.h>

#define SSD1306_FONT_WIDTH  5    /* glyph columns; the 6th column is the gap */
#define SSD1306_FONT_FIRST  0x20 /* first glyph in the table (space) */
#define SSD1306_FONT_LAST   0x7e /* last glyph in the table (tilde) */
#define SSD1306_FONT_GLYPHS (SSD1306_FONT_LAST - SSD1306_FONT_FIRST + 1)

extern const uint8_t ssd1306_font5x7[SSD1306_FONT_GLYPHS][SSD1306_FONT_WIDTH];

#endif /* SSD1306_FONT_H */
