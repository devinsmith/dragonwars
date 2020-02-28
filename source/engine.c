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
#include <stdint.h>

#include "engine.h"
#include "resource.h"
#include "ui.h"
#include "utils.h"

/* Represents the engine that Dragon wars runs.
 *
 * It appears that Dragon Wars executes a script where each op code
 * does some action.
 *
 * Lots of global variables here until we can figure out how they are used. */

uint8_t byte_1CE1 = 0;
uint8_t byte_1CE2 = 0;

// 0x1CE3
// Represents number of bits that are left over to read from byte_1CE5.
uint8_t num_bits = 0;

uint8_t byte_1CE4 = 0;
uint8_t byte_1CE5 = 0;
uint8_t byte_3AE1 = 0;
uint16_t word_3AE2 = 0;

// The function signature for this function pointer is not entirely correct
// but we'll figure it out as we decode more of DW.
void (*word_3163)(unsigned char byte);

struct game_state {
  uint8_t unknown1;
  uint8_t unknown2;
  uint8_t unknown3;
  uint8_t unknown4;
  uint8_t unknown5;
  uint8_t unknown6;
  uint8_t unknown7;
  uint8_t unknown8;
  uint8_t unknown9;
};

// 0x3860
struct game_state game_state;

// x86 CPU registers
uint16_t cpu_ax;

unsigned char *cpu_pc;

static uint8_t sub_1CF8();
static uint8_t sub_1D8A();
static void sub_3150(unsigned char byte);
static void sub_316C();
static void sub_3191(unsigned char byte);
static void sub_280E();

// 0x3B0E
static void op_01(void)
{
  word_3AE2 = (cpu_ax & 0xFF00);
  byte_3AE1 = (cpu_ax & 0xFF00) >> 8;
}

static void sub_1C79(void)
{
  num_bits = 0;

  byte_1CE4 = 0;
  while (1) {
    uint8_t ret = sub_1CF8(); // check for 0
    if (ret == 0) {
      // 1CE6
      return;
    }
    if ((game_state.unknown8 & 0x80) == 0)
    {
      ret |= 0x80;
      game_state.unknown8 = ret;
      ret &= 0x7F;
    }
    // 1C9E
    if (ret == 0xAF) {
      // 0x1CAB
    }
    if (ret == 0xDC) {
      // 0x1CAF
    }
    sub_3150(ret);
  }
}

// 0x1D2A - 0x1D85
// Some type of character alphabet?
unsigned char alphabet[] = {
  0xa0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xeb, 0xec,
  0xed, 0xee, 0xef, 0xf0, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf9, 0xae,
  0xa2, 0xa7, 0xac, 0xa1, 0x8d, 0xea, 0xf1, 0xf8, 0xfa, 0xb0, 0xb1, 0xb2,
  0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0x30, 0x31, 0x32, 0x33, 0x34,
  0x35, 0x36, 0x37, 0x38, 0x39, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
  0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x50, 0x51, 0x52, 0x53,
  0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0xa8, 0xa9, 0xaf, 0xdc, 0xa3,
  0xaa, 0xbf, 0xbc, 0xbe, 0xba, 0xbb, 0xad, 0xa5
};

static uint8_t sub_1CF8()
{
  while (1) {
    uint8_t ret = sub_1D8A();
    if (ret == 0) {
      return 0;
    } else if (ret < 0x1E) {
      // 0x1D0A
      // offset
      uint8_t al = alphabet[ret - 1];
      byte_1CE4 = byte_1CE4 >> 1;
      if (byte_1CE4 >= 0x40 && al >= 0xE1 && al <= 0xFA) {
        al = al & 0xDF;
      }
      // test al, al
      return al;
    } else if (ret == 0x1E) {
      // stc
      // rcr byte [byte_1CE4], 1
      byte_1CE4 = byte_1CE4 >> 1;
      byte_1CE4 += 0x80;
    }
  }
}

// Extract 5 bits out of each byte.
static uint8_t sub_1D8A()
{
  int counter = 5;
  int al = 0;
  int dl = num_bits;
  while (counter > 0) {
    dl--;
    if (dl < 0) {
      dl = *cpu_pc;
      printf("DL=0x%02x\n", dl);
      byte_1CE5 = dl;
      dl = 7;
      cpu_pc++;
    }
    // 0x1D96
    uint8_t tmp = byte_1CE5;
    byte_1CE5 = byte_1CE5 << 1;

    // rcl al, 1
    int carry = 0;
    if (tmp > byte_1CE5) {
      carry = 1;
    }
    al = al << 1;
    al += carry;
    counter--;
  }
  num_bits = dl;
  return al;
}

static void sub_3191(unsigned char byte)
{
}

static void sub_316C()
{
  word_3163 = sub_3191;
}

static void sub_27E3()
{
  word_3163 = ui_header_set_byte;
  ui_header_reset();
  sub_1C79();
  sub_316C();
  sub_280E();
}

static void sub_280E()
{
  // XXX: Unknown.
}

// 0x3150
static void sub_3150(unsigned char byte)
{
  word_3163(byte);
}

// 0x482D
static void op_7B(void)
{
  sub_27E3();
}

void run_engine()
{
  game_state.unknown8 = 0xFF;

  // 0x1A6
  // Loads into 0x1887:0000
  struct resource code_res;
  if (resource_load(RESOURCE_UNKNOWN, &code_res) != 0)
  {
    printf("Failed to load unknown resource\n");
  }
  printf("Resource bytes: %zu\n", code_res.len);
  dump_hex(code_res.bytes, 0x80);

  cpu_pc = code_res.bytes;

  int done = 0;
  cpu_ax = 0;

  // 0x3AA0
  while (!done) {
    uint8_t op_code = *cpu_pc++;
    switch (op_code) {
    case 0x01:
      op_01();
      break;
    case 0x7B:
      op_7B();
      break;
    default:
      printf("Unhandled op code: 0x%02X\n", op_code);
      done = 1;
      break;
    }
  }
}

