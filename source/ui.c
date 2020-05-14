/*
 * Copyright (c) 2020 Devin Smith <devin@devinsmith.net>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "offsets.h"
#include "resource.h"
#include "tables.h"
#include "ui.h"
#include "utils.h"
#include "vga.h"

struct viewport_data {
  int xpos;
  int ypos;
  int runlength;
  int numruns;
  int unknown1;
  int unknown2;
  unsigned char *data;
};

struct pic_data {
  uint8_t width;
  uint8_t height;
  uint8_t offset_delta;
  uint8_t y_pos; // Starting line.
  unsigned char *data;
};

// 0x2AAA
uint8_t data_2AAA[0x19];

// 0x2AC3
uint8_t data_2AC3[0x19];

static int loaded = 0;

struct viewport_data viewports[] = {
  {
    0x00, 0x00, 0x04, 0x0A, 0x00, 0x00,
    NULL
  },
  {
    0x98, 0x00, 0x04, 0x0A, 0x00, 0x00,
    NULL
  },
  {
    0x00, 0x7B, 0x04, 0x0D, 0x00, 0x00,
    NULL
  },
  {
    0x98, 0x7B, 0x04, 0x0D, 0x00, 0x00,
    NULL
  }
};

#define COLOR_BLACK 0
#define COLOR_WHITE 0xF

#define UI_PIECE_COUNT 0x2B
#define UI_BRICK_FIRST_PICTURE 0x17
static struct pic_data ui_pieces[UI_PIECE_COUNT];

// 0x288B
// Initially "Loading..."
unsigned char ui_header_loading[] = {
  0xCC, 0xEF, 0xE1, 0xE4, 0xE9, 0xEE, 0xE7, 0xAE, 0xAE, 0xAE
};

struct ui_header ui_header;

// 0x3598
static uint8_t prev_bg_index = 0;
// 0x3599
static uint8_t curr_bg_index = 0;

// 0x359A
static uint16_t current_background = 0xFFFF;
// 0x359C
static uint16_t backgrounds[2] = { 0xFFFF, 0x0000 };

static void reset_ui_background();

/* D88 */
static void process_quadrant(const struct viewport_data *d, unsigned char *data)
{
  int newx, newy;
  uint16_t offset;

  newx = d->xpos >> 1;
  newy = d->ypos << 1;
  printf("%02x %02x, %02x, %02x\n", d->xpos, d->ypos, newx, newy);
  offset = get_offset(d->ypos);
  offset += newx;
  printf("Offset: %04x\n", offset);
  unsigned char *p = data + offset;
  unsigned char *q = d->data;
  for (int i = 0; i < d->numruns; i++) {
    for (int j = 0; j < d->runlength; j++) {
      unsigned char val = *q;
      unsigned char dval = *p;

      dval = dval & get_and_table(val);
      dval = dval | get_or_table(val);

      printf("(%02x %02x) ", dval, val);
      *p = dval;
      p++;
      q++;
    }
    offset += 0x50;
    p = data + offset;
    printf("\n");
  }
}

void draw_viewport()
{
  int rows = 0x88;
  int cols = 0x50;
  int line_num = 8;

  unsigned char *data = calloc(sizeof(unsigned char), rows * cols);
  uint8_t *framebuffer = vga->memory();

  /* Iterate backwards like DW does */
  int vidx = 3;
  while (vidx >= 0) {
    const struct viewport_data *p = &viewports[vidx];
    process_quadrant(p, data);
    vidx--;
  }

  // 0x88 x 0x50
  /* see 0x1060 */
  unsigned char *src = data;
  for (int y = 0; y < rows; y++) {
    uint16_t fb_off = get_line_offset(line_num) + 0x10;
    for (int x = 0; x < cols; x++) {
      uint8_t al = *src++;
      int hi, lo;

      /* Each nibble represents a color */
      /* for example 0x82 represents color 8 then color 2. */
      hi = (al >> 4) & 0xf;
      lo = al & 0xf;

      framebuffer[fb_off++] = hi;
      framebuffer[fb_off++] = lo;
    }
    line_num++;
  }
  vga->update();

  free(data);
}

/* 0x3679 */
void draw_ui_piece(const struct pic_data *pic)
{
  uint16_t starting_off = get_line_offset(pic->y_pos);
  starting_off += (pic->offset_delta * 4);
  uint16_t fb_off = starting_off;
  printf("Line number: %d - FB offset: 0x%04x\n", pic->y_pos, fb_off);
  unsigned char *src = pic->data;
  uint8_t *framebuffer = vga->memory();

  for (int y = 0; y < pic->height; y++) {
    for (int x = 0; x < pic->width; x++) {
      uint8_t al = *src++;
      int hi, lo;

      /* Each nibble represents a color */
      /* for example 0x82 represents color 8 then color 2. */
      hi = (al >> 4) & 0xf;
      lo = al & 0xf;

      framebuffer[fb_off++] = hi;
      framebuffer[fb_off++] = lo;
    }
    starting_off += 0x140;
    fb_off = starting_off;
  }
  vga->update();
}

/* 0x36C8 */
static void draw_solid_color(uint8_t color, uint8_t line_num,
    uint16_t inset, uint16_t count)
{
  uint16_t fb_off = get_line_offset(line_num);
  inset = inset << 2;
  fb_off += inset;
  uint8_t *framebuffer = vga->memory();

  for (uint16_t i = 0; i < count; i++) {
    framebuffer[fb_off++] = color;
    framebuffer[fb_off++] = color;
  }
  //display_update();
}

// 0x3351 (sort of).
// x stored in DX, y = DI
static void draw_character(int x, int y, const unsigned char *chdata)
{
  uint8_t color = COLOR_WHITE; // bh.
  uint8_t ah = (current_background >> 8) & 0xFF;

  uint8_t *framebuffer = vga->memory();
  uint16_t fb_off = get_line_offset(y);
  fb_off += (x << 3); // 8 bytes

  for (int j = 0; j < 8; j++) {
    uint8_t bl;
    uint8_t al = *chdata++;
    al = al ^ ah;
    bl = al;
    for (int i = 0; i < 8; i++) {
      color = COLOR_BLACK;
      bl = (al << 1) & 0xFF;
      if (bl < al) {
        color = COLOR_WHITE;
      }
      framebuffer[fb_off++] = color;
      al = bl;
    }
    fb_off += 0x138;
  }
}

// 0x2AE3
static void zero_out_2AAA()
{
  memset(data_2AAA, 0, sizeof(data_2AAA));
}

// 0x3380
void draw_pattern(struct ui_rect *rect)
{
  int num_lines = rect->h - rect->y;
  int dx = rect->w - rect->x;
  int starting_line = rect->y;
  zero_out_2AAA();
  printf("Number of lines: %d\n", num_lines);
  printf("Line: %d\n", starting_line);
  printf("DX: 0x%04x\n", dx);

  uint16_t ax = 0xFFFF; // word_359A (color)

  uint8_t *framebuffer = vga->memory();

  // 0x3417
  int x_pos = rect->x << 3;
  dx = dx << 2;
  ax = ax & 0x0F0F;
  printf("x_pos: %d\n", x_pos);
  printf("DX: 0x%04x\n", dx);

  for (int i = 0; i < num_lines; i++) {
    uint16_t fb_off = get_line_offset(starting_line);
    fb_off += x_pos;
    for (int j = 0; j < dx; j++) {
      int color1 = (ax >> 8) & 0xFF;
      int color2 = (ax & 0x00FF);
      framebuffer[fb_off++] = color1;
      framebuffer[fb_off++] = color2;
    }
    starting_line++;
  }
}

/* 0x26E9 */
void ui_draw()
{
  for (size_t ui_idx = 0; ui_idx < 10; ui_idx++) {
    draw_ui_piece(&ui_pieces[ui_idx]);
  }
  /* Draw solid colors */
  /* Not the most ideal piece of code, but this is what dragon.com does. */
  /* clear out for character list */
  for (uint8_t i = 0x20; i < 0x90; i++) {
    draw_solid_color(COLOR_BLACK, i, 0x36, 0x30);
  }
  vga->update();

  // Draw upper header.
  //
  // The header is drawn so that there are an appropriate number of
  // bricks around it.
  ui_header_draw();

  // 0x3380
  //
  struct ui_rect r;
  r.x = 1;
  r.y = 0x98;
  r.w = 0x27;
  r.h = 0xB8;
  draw_pattern(&r);
  vga->update();

}

// 0x2824
void ui_header_draw()
{
  ui_set_background(0x10);

  // Calculate label header starting position.
  int header_start = sizeof(ui_header.data) - ui_header.len;
  header_start = header_start >> 1;
  header_start += 4;

  for (int i = 4; i < header_start; i++) {
    draw_ui_piece(&ui_pieces[i + UI_BRICK_FIRST_PICTURE]);
  }

  for (int i = 0; i < ui_header.len; i++) {
    draw_character(i + header_start, 0, get_chr(ui_header.data[i]));
  }

  for (int i = ui_header.len + header_start; i < 0x14; i++) {
    draw_ui_piece(&ui_pieces[i + UI_BRICK_FIRST_PICTURE]);
  }
  reset_ui_background();
}

void ui_load()
{
  /* Viewport data is stored in the dragon.com file */
  viewports[0].data = com_extract(0x6758 + 4, 4 * 0xA);
  viewports[1].data = com_extract(0x6784 + 4, 4 * 0xA);
  viewports[2].data = com_extract(0x67B0 + 4, 4 * 0xD);
  viewports[3].data = com_extract(0x67E8 + 4, 4 * 0xD);

  unsigned char *ui_piece_offsets_base = com_extract(0x6AE0, UI_PIECE_COUNT * 2);
  unsigned char *ui_piece_offsets = ui_piece_offsets_base;
  printf("UI Pieces:\n");
  dump_hex(ui_piece_offsets, UI_PIECE_COUNT * 2);
  for (size_t ui_idx = 0; ui_idx < UI_PIECE_COUNT; ui_idx++) {
    uint16_t ui_off = *ui_piece_offsets++;
    ui_off += *ui_piece_offsets++ << 8;
    printf("Piece: %zu Offset: %04x\n", ui_idx, ui_off);
    /* Next 4 bytes are encoded into pic data */
    unsigned char *piece_struct = com_extract(ui_off, 4);
    unsigned char *org = piece_struct;
    ui_pieces[ui_idx].width = *piece_struct++;
    ui_pieces[ui_idx].height = *piece_struct++;
    ui_pieces[ui_idx].offset_delta = *piece_struct++;
    ui_pieces[ui_idx].y_pos = *piece_struct;
    free(org);
    size_t data_sz = ui_pieces[ui_idx].width * ui_pieces[ui_idx].height;
    ui_pieces[ui_idx].data = com_extract(ui_off + 4, data_sz);
  }
  free(ui_piece_offsets_base);

  memcpy(ui_header.data, ui_header_loading, strlen("Loading..."));
  ui_header.len = strlen("Loading...");
  loaded = 1;
}

void ui_clean()
{
  free(viewports[0].data);
  free(viewports[1].data);
  free(viewports[2].data);
  free(viewports[3].data);

  for (size_t ui_idx = 0; ui_idx < UI_PIECE_COUNT; ui_idx++) {
    free(ui_pieces[ui_idx].data);
  }
}

void ui_header_reset()
{
  ui_header.len = 0;
}

// 0x27FA (not really, but close enough).
void ui_header_set_byte(unsigned char byte)
{
  ui_header.data[ui_header.len++] = byte;
}

// 0x3237
void ui_draw_chr_piece(uint8_t chr, struct ui_rect *rect, struct ui_rect *other)
{
  if ((chr & 0x80) == 0) {
    int16_t bx = (int16_t)rect->y;
    bx -= other->y;
    if (bx > 0) {
      bx = bx << 3;

      data_2AC3[bx] = chr;
      data_2AAA[bx] = 0xFF;
    }
  }
  if (chr == 0x8D) {
    rect->x = other->x;
    uint8_t al = rect->y;
    al += 8;
    if (al > other->h) {
      printf("BP CS:3275\n");
      exit(1);
    }
    rect->y = al;
    return;
  }
  draw_character(rect->x, rect->y, get_chr(chr));
  rect->x++;
}

// 0x269F
void ui_draw_box_segment(uint8_t chr, struct ui_rect *rect, struct ui_rect *outer)
{
  // Draw corner box.
  ui_draw_chr_piece(chr, rect, outer);
  chr++;

  while (rect->x < outer->w - 1) {
    ui_draw_chr_piece(chr, rect, outer);
  }

  chr++;
  ui_draw_chr_piece(chr, rect, outer);

  vga->update();
}

// 0x3578
void ui_set_background(uint16_t val)
{
  val = val >> 3;
  val = val & 2;

  if (val == 2) {
    current_background = backgrounds[1];
  } else {
    current_background = backgrounds[0];
  }
  prev_bg_index = curr_bg_index;
  curr_bg_index = (val & 0xFF);
}

// 0x3575
static void reset_ui_background()
{
  ui_set_background(prev_bg_index);
}
