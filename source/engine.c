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
#include <stdlib.h>
#include <string.h>

#include "engine.h"
#include "player.h"
#include "resource.h"
#include "tables.h"
#include "ui.h"
#include "utils.h"
#include "vga.h"

/* Represents the engine that Dragon wars runs.
 *
 * It appears that Dragon Wars executes a script where each op code
 * does some action.
 *
 * Lots of global variables here until we can figure out how they are used. */

uint16_t word_11C0 = 0;
uint16_t word_11C2 = 0;
uint16_t word_11C4 = 0;
uint16_t word_11C6 = 0;
uint16_t word_11C8 = 0;

uint16_t word_11CA = 0;
uint16_t word_11CC = 0;


uint8_t byte_1CE1 = 0;
uint8_t byte_1CE2 = 0;

// 0x1CE3
// Represents number of bits that are remaining to be read from bit_buffer.
uint8_t num_bits = 0;
// 0x1CE5
// Will contain actual remaining bits.
uint8_t bit_buffer = 0;

uint8_t byte_1BE5 = 0;

uint16_t word_1C63 = 0;
uint8_t byte_1CE4 = 0;
uint8_t byte_1E1F = 0;
uint8_t byte_1E20 = 0;

// This is an unknown size, currently guessing at
// 0x1E21 - 0x1F0F
unsigned char *data_1E21;

// 0x1EB9, 2 bytes, since there's a function at 0x1EBB
unsigned char data_1EB9[] = { 0xC2, 0x00 };

// 0x2C0E
unsigned char data_2C0E[] = { 0x04, 0x82, 0x9B, 0x00, 0x00, 0xFF };

uint8_t byte_1F07 = 0;
uint8_t byte_1F08 = 0;

// 0x246D
uint16_t word_246D;

struct ui_rect data_268F;
uint8_t byte_2476;

uint16_t word_2AA2;
unsigned char *word_2AA4;

uint8_t byte_2AA6;
// 0x2AA7
uint16_t word_2AA7;
uint8_t byte_2AA9;
uint8_t data_2AAA[32] = { 0 };

// 0x2D09
uint16_t word_2D09; // timer ticks?
uint16_t word_2DD7 = 0xFFFF;
uint16_t word_2DD9 = 0xFFFF;
uint8_t data_2DDB[160] = { 0 };

uint16_t word_36C0;
uint16_t word_36C2;
uint16_t g_linenum; // 36C4

uint8_t byte_3855 = 0;
uint16_t word_3856 = 0;
uint8_t byte_3867 = 0;
uint8_t byte_387F = 0;

uint8_t byte_3AE1 = 0;
uint16_t word_3AE2 = 0;
uint16_t word_3AE4 = 0;
uint16_t word_3AE6 = 0;
uint16_t word_3AE8 = 0;
uint16_t word_3AEA = 0;
// 0x3AEC
uint16_t saved_stack = 0;

uint16_t word_3ADB = 0;
unsigned char *data_1CEF;

/* 0x3ADD */
const struct resource *running_script = NULL;
const struct resource *word_3ADF = NULL;

uint16_t word_42D6 = 0;
uint16_t word_4454 = 0;

// 0x4A99
unsigned char data_4A99[] = { 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01 };

uint8_t byte_4F2B = 0;

/* Timers? */
struct timer_ctx {
  uint8_t  timer0; // 0x4C35
  uint8_t  timer1; // 0x4C36
  uint8_t  timer2; // 0x4C37
  uint16_t timer3; // 0x4C38
  uint16_t timer4; // 0x4C3A
  uint8_t  timer5; // 0x4C3C
};

struct timer_ctx timers;

unsigned char *data_2A68 = NULL;
unsigned char *data_D760 = NULL;

// XXX:How big should these be???
// It looks like they can be 0x0E00 bytes, but we round up to 4096.
unsigned char data_CA4C[4096] = { 0 };

// The function signature for this function pointer is not entirely correct
// but we'll figure it out as we decode more of DW.
void (*word_3163)(unsigned char byte);

struct len_bytes {
  uint16_t len;
  uint8_t bytes[40];
};

// We should break this apart.
struct game_state {
  // 0xC6 - 0x?? - New character name.
  unsigned char unknown[256];
};

// 0x3860
static struct game_state game_state = {0};

// Small stack, hopefully we don't use much of it.
#define STACK_SIZE 32

// virtual CPU
struct virtual_cpu {
  // registers
  uint16_t ax;
  uint16_t bx;
  uint16_t cx;

  uint16_t di;

  // stack
  uint8_t stack[STACK_SIZE]; // stacks;
  uint8_t sp;

  // flags
  uint8_t cf;
  uint8_t zf;
  uint8_t sf;

  // program counter
  unsigned char *pc;
  unsigned char *base_pc;
};

/* | Bit # |  Mask  | Abbreviation       | Description                     |
 * +-------+--------+--------------------+---------------------------------+
 * | 0     | 0x0001 | CF                 | Carry flag                      |
 * | 1     | 0x0002 | Reserved, always 1 |                                 |
 * | 2     | 0x0004 | PF                 | Parity flag                     |
 * | 3     | 0x0008 | Reserved           |                                 |
 * | 4     | 0x0010 | AF                 | Adjust flag                     |
 * | 5     | 0x0020 | Reserved[3]        |                                 |
 * | 6     | 0x0040 | ZF                 | Zero flag                       |
 * | 7     | 0x0080 | SF                 | Sign flag                       |
 * | 8     | 0x0100 | TF                 | Trap flag                       |
 * | 9     | 0x0200 | IF                 | Interrupt enable flag           |
 * | 10    | 0x0400 | DF                 | Direction flag                  |
 * | 11    | 0x0800 | OF                 | Overflow flag                   |
 * | 12-13 | 0x3000 | IOPL               | I/O privilege level (286+ only) |
 * | 14    | 0x4000 | NT                 | Nested task flag (286+ only)    |
 * | 15    | 0x8000 | Reserved           |                                 |
*/
#define CARRY_FLAG_MASK 0x1
#define ZERO_FLAG_MASK 0x40
#define SIGN_FLAG_MASK 0x80

struct virtual_cpu cpu;
struct mouse_status mouse;

static void run_script(uint8_t script_index, uint16_t src_offset);
static uint8_t sub_1CF8();
static uint8_t sub_1D8A();
static void sub_3150(unsigned char byte);
static void sub_316C();
static void append_string(unsigned char byte);
static void sub_280E();
static void sub_1C79(unsigned char **src_ptr, uint16_t offset);
static void sub_1BF8(uint8_t color, uint8_t y_adjust);

// Decoded opcode calls, foward definition.
static void op_00();
static void op_01();
static void op_03();
static void op_04();
static void op_05();
static void op_06();
static void op_07();
static void op_08();
static void op_09();
static void op_0A();
static void op_0B();
static void op_0D();
static void op_0F();
static void op_11();
static void op_12();
static void op_13();
static void op_14();
static void op_15();
static void op_17();
static void op_19();
static void op_1A();
static void op_1C();
static void op_1D();
static void op_21();
static void op_22();
static void op_23();
static void op_24();
static void op_25();
static void op_26();
static void op_27();
static void op_28();
static void op_2A();
static void op_2B();
static void op_2D();
static void op_2F();
static void op_30();
static void op_32();
static void op_38();
static void op_39();
static void op_3D();
static void op_3E();
static void op_3F();
static void op_40();
static void op_41();
static void op_42();
static void op_44();
static void op_45();
static void op_47();
static void loop(); // 49
static void op_4A();
static void op_4B();
static void op_4C();
static void op_52();
static void op_53();
static void op_54();
static void op_55();
static void op_56();
static void op_58();
static void op_59();
static void op_5A();
static void op_5C();
static void get_character_data();
static void op_5E();
static void op_61();
static void op_63();
static void op_66();
static void op_69();
static void op_74();
static void op_75();
static void op_76();
static void op_77();
static void op_78();
static void op_7A();
static void read_header_bytes(void); // 7B
static void op_7D();
static void op_80();
static void op_81();
static void op_82();
static void op_83();
static void op_84();
static void op_85();
static void op_86();
static void op_88();
static void op_89();
static void op_8D();
static void op_93();
static void op_94();
static void op_95();
static void op_96();
static void op_97();
static void op_98();
static void op_99();
static void op_9A();

struct op_call_table {
  void (*func)();
  const char *src_offset;
};

struct op_call_table targets[] = {
  { op_00, "0x3B18" },
  { op_01, "0x3B0E" },
  { NULL, "0x3B1F" },
  { op_03, "0x3B2F" },
  { op_04, "0x3B2A" },
  { op_05, "0x3B3D" },
  { op_06, "0x3B4A" },
  { op_07, "0x3B52" },
  { op_08, "0x3B59" },
  { op_09, "0x3B67" },
  { op_0A, "0x3B7A" },
  { op_0B, "0x3B8C" },
  { NULL, "0x3BA2" },
  { op_0D, "0x3BB7" },
  { NULL, "0x3BD0" },
  { op_0F, "0x3BED" },
  { NULL, "0x3C10" },
  { op_11, "0x3C2D" },
  { op_12, "0x3C59" },
  { op_13, "0x3C72" },
  { op_14, "0x3C8F" },
  { op_15, "0x3CAB" },
  { NULL, "0x3CCB" },
  { op_17, "0x3CEF" },
  { NULL, "0x3D19" },
  { op_19, "0x3D3D" },
  { op_1A, "0x3D5A" },
  { NULL, "0x3D73" },
  { op_1C, "0x3D92" },
  { op_1D, "0x4ACC" },
  { NULL, "0x01B2" },
  { NULL, "0x4AF6" },
  { NULL, "0x0000" },
  { op_21, "0x3DAE" },
  { op_22, "0x3DB7" },
  { op_23, "0x3DC0" },
  { op_24, "0x3DD7" },
  { op_25, "0x3DE5" },
  { op_26, "0x3DEC" },
  { op_27, "0x3E06" },
  { op_28, "0x3E14" },
  { NULL, "0x3E1B" },
  { op_2A, "0x3E36" },
  { op_2B, "0x3E45" },
  { NULL, "0x3E4C" },
  { op_2D, "0x3E67" },
  { NULL, "0x3E6E" },
  { op_2F, "0x3E75" },
  { op_30, "0x3E9D" },
  { NULL, "0x3EC1" },
  { op_32, "0x3EEB" },
  { NULL, "0x3F11" },
  { NULL, "0x3F4D" },
  { NULL, "0x3F66" },
  { NULL, "0x3F8C" },
  { NULL, "0x3FAD" },
  { op_38, "0x3FBC" },
  { op_39, "0x3FD4" },
  { NULL, "0x3FEA" },
  { NULL, "0x4002" },
  { NULL, "0x4018" },
  { op_3D, "0x4030" },
  { op_3E, "0x4051" },
  { op_3F, "0x4067" },
  { op_40, "0x4074" },
  { op_41, "0x407C" },
  { op_42, "0x4085" },
  { NULL, "0x408E" },
  { op_44, "0x4099" },
  { op_45, "0x40A3" },
  { NULL, "0x40AF" },
  { op_47, "0x40B8" },
  { NULL, "0x40ED" },
  { loop, "0x4106" },
  { op_4A, "0x4113" },
  { op_4B, "0x4122" },
  { op_4C, "0x412A" },
  { NULL, "0x4132" },
  { NULL, "0x414B" },
  { NULL, "0x4155" },
  { NULL, "0x4161" },
  { NULL, "0x418B" },
  { op_52, "0x41B9" },
  { op_53, "0x41C0" },
  { op_54, "0x41E1" },
  { op_55, "0x41E5" },
  { op_56, "0x41FD" },
  { NULL, "0x4215" },
  { op_58, "0x4239" },
  { op_59, "0x41C8" },
  { op_5A, "0x3AEE" },
  { NULL, "0x427A" },
  { op_5C, "0x4295" },
  { get_character_data, "0x42D8" },
  { op_5E, "0x4322" },
  { NULL, "0x4372" },
  { NULL, "0x438B" },
  { op_61, "0x43A6" },
  { NULL, "0x43BF" },
  { op_63, "0x43F7" },
  { NULL, "0x446E" },
  { NULL, "0x44B8" },
  { op_66, "0x40C1" },
  { NULL, "0x44CB" },
  { NULL, "0x450A" },
  { op_69, "0x453F" },
  { NULL, "0x4573" },
  { NULL, "0x45A1" },
  { NULL, "0x45A8" },
  { NULL, "0x45F0" },
  { NULL, "0x45FA" },
  { NULL, "0x4607" },
  { NULL, "0x4632" },
  { NULL, "0x465B" },
  { NULL, "0x46B6" },
  { NULL, "0x47B7" },
  { op_74, "0x47C0" },
  { op_75, "0x47D1" },
  { op_76, "0x47D9" },
  { op_77, "0x47E3" },
  { op_78, "0x47EC" },
  { NULL, "0x47FA" },
  { op_7A, "0x4801" },
  { read_header_bytes, "0x482D" },
  { NULL, "0x4817" },
  { op_7D, "0x483B" },
  { NULL, "0x4845" },
  { NULL, "0x486D" },
  { op_80, "0x487F" },
  { op_81, "0x48C5" },
  { op_82, "0x48D2" },
  { op_83, "0x48EE" },
  { op_84, "0x4907" },
  { op_85, "0x4920" },
  { op_86, "0x493E" },
  { NULL, "0x4955" },
  { op_88, "0x496D" },
  { op_89, "0x4977" },
  { NULL, "0x498E" },
  { NULL, "0x499B" },
  { NULL, "0x49A5" },
  { op_8D, "0x49D3" },
  { NULL, "0x0000" },
  { NULL, "0x49DD" },
  { NULL, "0x49E7" },
  { NULL, "0x49F3" },
  { NULL, "0x49FD" },
  { op_93, "0x4A67" },
  { op_94, "0x4A6D" },
  { op_95, "0x4894" },
  { op_96, "0x48B5" },
  { op_97, "0x42FB" },
  { op_98, "0x4348" },
  { op_99, "0x40E7" },
  { op_9A, "0x3C42" },
  { NULL, "0x416B" },
  { NULL, "0x4175" },
  { NULL, "0x4181" },
  { NULL, "0x492D" },
  { NULL, "0x4AF0" },
  { NULL, "0x8A06" },
  { NULL, "0xE80E" },
  { NULL, "0x513A" },
  { NULL, "0x36FF" },
  { NULL, "0x3ADB" },
  { NULL, "0x36FF" },
  { NULL, "0x3AEC" },
  { NULL, "0x2689" },
  { NULL, "0x3AEC" },
  { NULL, "0xE8A2" },
  { NULL, "0xA23A" },
  { NULL, "0x3AEA" },
  { NULL, "0xE853" },
  { NULL, "0x0FE5" },
  { NULL, "0x325E" },
  { NULL, "0xA2C0" },
  { NULL, "0x3AE1" },
  { NULL, "0xE3A2" },
  { NULL, "0xEB3A" },
  { NULL, "0x8B04" },
  { NULL, "0xDB36" },
  { NULL, "0x8E3A" },
  { NULL, "0xDD06" },
  { NULL, "0x263A" },
  { NULL, "0x32AC" },
  { NULL, "0x8BE4" },
  { NULL, "0xD1D8" },
  { NULL, "0xFFE3" },
  { NULL, "0x60A7" },
  { NULL, "0x0039" },
  { NULL, "0x0000" },
  { NULL, "0x0000" },
  { NULL, "0x0000" },
  { NULL, "0x0000" },
  { NULL, "0x0000" },
  { NULL, "0x0000" },
  { NULL, "0x0000" },
  { NULL, "0x0000" },
  { NULL, "0x0000" },
  { NULL, "0x268B" },
  { NULL, "0x3AEC" },
  { NULL, "0x068F" },
  { NULL, "0x3AEC" },
  { NULL, "0x068F" },
  { NULL, "0x3ADB" },
  { NULL, "0xA258" },
  { NULL, "0x3AE8" },
  { NULL, "0xEAA2" },
  { NULL, "0xE83A" },
  { NULL, "0x0F9D" },
  { NULL, "0xC032" },
  { NULL, "0xE1A2" },
  { NULL, "0xA23A" },
  { NULL, "0x3AE3" },
  { NULL, "0xC307" },
  { NULL, "0x2688" },
  { NULL, "0x3AE3" },
  { NULL, "0x2688" },
  { NULL, "0x3AE1" },
  { NULL, "0xB7EB" },
  { NULL, "0x06C6" },
  { NULL, "0x3AE1" },
  { NULL, "0xEBFF" },
  { NULL, "0xA0B0" },
  { NULL, "0x3AEA" },
  { NULL, "0x8B4C" },
  { NULL, "0x88EC" },
  { NULL, "0x0046" },
  { NULL, "0xA5EB" },
  { NULL, "0xE8A0" },
  { NULL, "0xEB3A" },
  { NULL, "0x8BF3" },
  { NULL, "0x8AEC" },
  { NULL, "0x0046" },
  { NULL, "0xA244" },
  { NULL, "0x3AEA" },
  { NULL, "0x66E8" },
  { NULL, "0xEB0F" },
  { NULL, "0x2692" },
  { NULL, "0x8BAC" },
  { NULL, "0x8AD8" },
  { NULL, "0x6087" },
  { NULL, "0xA238" },
  { NULL, "0x3AE4" },
  { NULL, "0x85EB" },
  { NULL, "0xAC26" },
  { NULL, "0xE4A2" },
  { NULL, "0xE93A" },
  { NULL, "0xFF7D" },
  { NULL, "0x2688" },
  { NULL, "0x3AE4" },
  { NULL, "0x76E9" },
  { NULL, "0x26FF" },
  { NULL, "0x8BAC" },
  { NULL, "0xA0D8" },
  { NULL, "0x3AE4" }
};


static void push_byte(uint8_t val)
{
  cpu.sp--;
  if (cpu.sp == UINT8_MAX) {
    cpu.sp = STACK_SIZE - 1;
  }
  cpu.stack[cpu.sp] = val;
}

static void push_word(uint16_t val)
{
  push_byte((val & 0xFF00) >> 8);
  push_byte((val & 0xFF));
}

static uint8_t pop_byte()
{
  uint8_t val = cpu.stack[cpu.sp++];
  if (cpu.sp >= STACK_SIZE)
    cpu.sp = 0;

  return val;
}

static uint16_t pop_word()
{
  uint16_t val = pop_byte();
  val += pop_byte() << 8;

  return val;
}

static uint16_t peek_word()
{
  uint16_t val = cpu.stack[cpu.sp];
  val += (cpu.stack[cpu.sp + 1] << 8);

  return val;
}

// 0x3B18
static void op_00()
{
  printf("%s - setting 3AE1 to 0xFF\n", __func__);
  byte_3AE1 = 0xFF;
}

// 0x3B0E
static void op_01()
{
  uint8_t ah = (cpu.ax & 0xFF00) >> 8;

  // moves AH into two variables.
  word_3AE2 = (ah << 8) | (word_3AE2 & 0xFF); // lower portion of word_3AE2 takes ah.
  byte_3AE1 = ah;
}

// 0x4AA1
static void populate_3ADD_and_3ADF(void)
{
  running_script = resource_get_by_index(word_3AE8);
  word_3ADF = resource_get_by_index(word_3AEA);
}

// 0x3B2F
static void op_03()
{
  // pops the stack, 1 byte.
  uint8_t al = pop_byte();
  cpu.ax = (cpu.ax & 0xFF00) | al;
  word_3AEA = al;
  populate_3ADD_and_3ADF();
}

// 0x3B2A
static void op_04(void)
{
  uint8_t al = (word_3AE8 & 0x00FF);
  cpu.ax = (cpu.ax & 0xFF00) | al;

  push_byte(al);
}

static void op_05(void)
{
  uint8_t al = *cpu.pc++;
  cpu.ax = (cpu.ax & 0xFF00) | al;
  cpu.bx = cpu.ax;
  al = game_state.unknown[cpu.bx];
  cpu.ax = (cpu.ax & 0xFF00) | al;
  word_3AE4 = al;
}

// 0x3B4A
static void op_06()
{
  uint8_t al = *cpu.pc++;
  cpu.ax = (cpu.ax & 0xFF00) | al;
  word_3AE4 = al;
}

// 0x3B52
static void op_07()
{
  // Pretty much always 0.
  word_3AE4 = (cpu.ax & 0xFF00) >> 8;
}

// 0x3B59
static void op_08(void)
{
  uint8_t al = *cpu.pc++;
  cpu.ax = (cpu.ax & 0xFF00) | al;
  cpu.bx = cpu.ax;
  al = word_3AE4;
  set_game_state(cpu.bx, al);
}

// 0x3B67
static void op_09(void)
{
  uint8_t al = *cpu.pc++;
  cpu.ax = (cpu.ax & 0xFF00) | al;
  word_3AE2 = al;
  if (byte_3AE1 != (cpu.ax >> 8))
  {
    // set high byte
    al = *cpu.pc++;
    cpu.ax = (cpu.ax & 0xFF00) | al;
    word_3AE2 = (al << 8) | (word_3AE2 & 0xFF); // XXX ? Correct.
  }
}

// 0x3B7A
static void op_0A(void)
{
  uint8_t al, ah, saved;
  static int runs = 1;

  al = *cpu.pc++;
  saved = al;
  cpu.ax = (cpu.ax & 0xFF00) | al;
  cpu.bx = cpu.ax;

  // mov ax, [bx + game_state]
  al = game_state.unknown[cpu.bx];
  ah = game_state.unknown[cpu.bx+1];
  ah = ah & byte_3AE1;
  cpu.ax = (ah << 8) | al;
  word_3AE2 = cpu.ax;
  printf("%s - AX: 0x%04X (run num: %d) 0x%02X\n", __func__, cpu.ax, runs,
      saved);
  runs++;
}

// 0x3B8C
static void op_0B()
{
  uint8_t ah, al;
  al = *cpu.pc++;
  cpu.ax = (cpu.ax & 0xFF00) | al;
  cpu.ax += word_3AE4;
  cpu.bx = cpu.ax;
  al = game_state.unknown[cpu.bx];
  ah = game_state.unknown[cpu.bx + 1];

  ah = ah & byte_3AE1;
  cpu.ax = ah << 8 | al;
  word_3AE2 = cpu.ax;
}

// 0x3BB7
static void op_0D()
{
  uint8_t al, ah;

  // es:lodsw
  cpu.ax = *cpu.pc++;
  cpu.ax += ((*cpu.pc++) << 8);

  cpu.ax += word_3AE4;
  cpu.bx = cpu.ax;

  unsigned char *dest = word_3ADF->bytes;
  cpu.ax = dest[cpu.bx];
  cpu.ax += (dest[cpu.bx + 1] << 8);

  ah = (cpu.ax & 0xFF00) >> 8;
  ah = ah & byte_3AE1;
  al = cpu.ax & 0xFF;
  cpu.ax = (ah << 8) | al;
  word_3AE2 = cpu.ax;
}

// 0x3BED
static void op_0F(void)
{
  uint8_t ah, al;
  al = *cpu.pc++;
  cpu.ax = (cpu.ax & 0xFF00) | al;
  cpu.bx = cpu.ax;
  // load di properly...
  printf("OP_0F: BX: 0x%04X\n", cpu.bx);
  cpu.di = game_state.unknown[cpu.bx];
  cpu.di += (game_state.unknown[cpu.bx + 1] << 8);
  uint8_t bl = game_state.unknown[cpu.bx + 2];
  cpu.bx = (cpu.bx & 0xFF00) | bl;

  const struct resource *r = resource_get_by_index(cpu.bx);
  //dump_hex(r->bytes, 0x10);

  cpu.di += word_3AE4;

  // little endian load
  // mov ax, es:[di]
  al = r->bytes[cpu.di];
  ah = r->bytes[cpu.di + 1];
  cpu.ax = (ah << 8) | al;
  ah = ah & byte_3AE1;
  cpu.ax = ah << 8 | al;
  word_3AE2 = (ah << 8) | al;
}

// 0x3C2D
static void op_11(void)
{
  uint8_t al = *cpu.pc++;
  cpu.ax = (cpu.ax & 0xFF00) | al;
  cpu.bx = cpu.ax;
  uint8_t ah = (cpu.ax & 0xFF00) >> 8;
  printf("op_11: 0x%04X ah: 0x%02X\n", cpu.bx, ah);
  set_game_state(cpu.bx, ah);
  if (byte_3AE1 != ah) {
    set_game_state(cpu.bx + 1, ah);
  }
}

// 0x3C59
static void op_12(void)
{
  uint8_t al = *cpu.pc++;
  cpu.ax = (cpu.ax & 0xFF00) | al;
  cpu.bx = cpu.ax;
  cpu.cx = word_3AE2;
  set_game_state(cpu.bx, (cpu.cx & 0xFF));
  if (byte_3AE1 != ((cpu.ax & 0xFF00) >> 8)) {
    set_game_state(cpu.bx + 1, (cpu.cx & 0xFF00) >> 8);
  }
}

// 0x3C72
static void op_13(void)
{
  uint8_t al = *cpu.pc++;
  cpu.ax = (cpu.ax & 0xFF00) | al;
  cpu.bx = cpu.ax;
  cpu.cx = word_3AE2;
  cpu.bx += word_3AE4;
  printf("op_13: 0x%04X\n", cpu.bx);
  set_game_state(cpu.bx, (cpu.cx & 0x00FF));
  if (byte_3AE1 != ((cpu.ax & 0xFF00) >> 8)) {
    set_game_state(cpu.bx + 1, (cpu.cx & 0xFF00) >> 8);
  }
}

// 0x3C8F
static void op_14(void)
{
  uint16_t save_ah = (cpu.ax & 0xFF00) >> 8;

  uint16_t ax = *cpu.pc++;
  ax += *cpu.pc++ << 8;
  cpu.ax = ax;
  cpu.bx = cpu.ax;

  // es:cx
  unsigned char *dest = word_3ADF->bytes;
  uint16_t dest_offset = word_3AE2;

  dest[cpu.bx] = dest_offset;
  if (byte_3AE1 != save_ah) {
    dest[cpu.bx + 1] = (dest_offset & 0xFF00) >> 8;
  }
}

// 0x3CAB
static void op_15()
{
  uint8_t save_ah = (cpu.ax & 0xFF00) >> 8;

  cpu.ax = *cpu.pc++;
  cpu.ax += ((*cpu.pc++) << 8);
  cpu.bx = cpu.ax;

  // es:cx
  unsigned char *dest = word_3ADF->bytes;
  cpu.cx = word_3AE2;
  cpu.di = word_3AE4;
  dest[cpu.bx + cpu.di] = cpu.cx & 0xFF;
  if (byte_3AE1 != save_ah) {
    dest[cpu.bx + cpu.di + 1] = (cpu.cx & 0xFF00) >> 8;
  }
}

// 0x3CEF
static void op_17(void)
{
  uint8_t al = *cpu.pc++;
  cpu.ax = (cpu.ax & 0xFF00) | al;
  cpu.bx = cpu.ax;
  cpu.di = game_state.unknown[cpu.bx];
  cpu.di += (game_state.unknown[cpu.bx + 1] << 8);
  uint8_t bl = game_state.unknown[cpu.bx + 2];
  cpu.bx = (cpu.bx & 0xFF00) | bl;
  printf("op17  bl: 0x%02X di: 0x%04X\n", bl, cpu.di);
  const struct resource *r = resource_get_by_index(bl);
  //dump_hex(r->bytes, 0x10);
  cpu.di += word_3AE4;
  cpu.cx = word_3AE2;
  printf("  op17: setting byte 0x%04X\n", cpu.di);
  r->bytes[cpu.di] = (cpu.cx & 0x00FF);
  if (byte_3AE1 != ((cpu.ax & 0xFF00) >> 8)) {
    r->bytes[cpu.di + 1] = ((cpu.cx & 0xFF00) >> 8);
  }
}

// 0x3D3D
static void op_19()
{
  uint8_t al = *cpu.pc++;
  cpu.ax = (cpu.ax & 0xFF00) | al;
  cpu.di = cpu.ax;
  al = *cpu.pc++;
  cpu.ax = (cpu.ax & 0xFF00) | al;
  cpu.bx = cpu.ax;

  cpu.cx = game_state.unknown[cpu.di];
  cpu.cx += (game_state.unknown[cpu.di + 1] << 8);
  set_game_state(cpu.bx, cpu.cx & 0xFF);
  if (byte_3AE1 != ((cpu.ax & 0xFF00) >> 8)) {
    set_game_state(cpu.bx + 1, (cpu.cx & 0xFF00) >> 8);
  }
}

// 0x3D5A
static void op_1A(void)
{
  uint8_t al = *cpu.pc++;
  cpu.ax = (cpu.ax & 0xFF00) | al;
  cpu.di = cpu.ax;
  al = *cpu.pc++;
  set_game_state(cpu.di, al);
  cpu.ax = (cpu.ax & 0xFF00) | al;
  if (byte_3AE1 != ((cpu.ax & 0xFF00) >> 8)) {
    al = *cpu.pc++;
    set_game_state(cpu.di + 1, al);
    cpu.ax = (cpu.ax & 0xFF00) | al;
  }
}

// 0x3D92
static void op_1C()
{
  uint8_t save_ah = (cpu.ax & 0xFF00) >> 8;

  cpu.ax = *cpu.pc++;
  cpu.ax += ((*cpu.pc++) << 8);
  cpu.di = cpu.ax;
  uint8_t al = *cpu.pc++;
  cpu.ax = (cpu.ax & 0xFF00) | al;

  unsigned char *ds = word_3ADF->bytes;
  ds[cpu.di] = al;
  if (byte_3AE1 != save_ah) {
    al = *cpu.pc++;
    cpu.ax = (cpu.ax & 0xFF00) | al;
    ds[cpu.di + 1] = al;
  }
}

// 0x4ACC
static void op_1D(void)
{
  // memcpy 0x700 bytes to or from data_D760.
  unsigned char *src = data_D760;
  uint16_t src_offset = 0;
  unsigned char *dest = word_3ADF->bytes;
  uint16_t dest_offset = word_3AE2;
  uint8_t byte_3AE4 = (word_3AE4 & 0x00FF);
  if ((byte_3AE4 & 0x80) != 0) {
    unsigned char *tmp = src;
    src = dest;
    dest = tmp;
    uint16_t tmp_offset = src_offset;
    src_offset = dest_offset;
    dest_offset = tmp_offset;
  }
  // repe movsw (move word ds:si to es:di (si, di += 2), repeat 0x380 times.
  memcpy(src + src_offset, dest + dest_offset, 0x700);
}

// 0x3DAE
// Set byte variable.
static void op_21()
{
  uint8_t byte_3AE4 = (word_3AE2 & 0x00FF);
  word_3AE4 = (word_3AE4 & 0xFF00) | byte_3AE4;
}

// 0x3DB7
// Set word variable.
static void op_22()
{
  cpu.ax = word_3AE4;
  word_3AE2 = cpu.ax;
}

// 0x3DC0
static void op_23(void)
{
  // INCREMENT [memory]

  uint8_t al = *cpu.pc++;
  cpu.ax = (cpu.ax & 0xFF00) | al;
  cpu.di = cpu.ax;
  set_game_state(cpu.di, game_state.unknown[cpu.di] + 1);
  if (game_state.unknown[cpu.di] == 0) {
    if (byte_3AE1 != ((cpu.ax & 0xFF00) >> 8)) {
      set_game_state(cpu.di + 1, game_state.unknown[cpu.di + 1] + 1);
    }
  }
}

// 0x3DD7
static void op_24()
{
  uint8_t ah;

  cpu.ax = word_3AE2;
  cpu.ax++;
  ah = (cpu.ax & 0xFF00) >> 8;
  ah = ah & byte_3AE1;
  cpu.ax = (ah << 8) | (cpu.ax & 0xFF);
  word_3AE2 = cpu.ax;
}

// 0x3DE5
static void op_25()
{
  uint8_t byte_3AE4 = (word_3AE4 & 0x00FF);
  byte_3AE4++;
  word_3AE4 = (word_3AE4 & 0xFF00) | byte_3AE4;
}

// 0x3DEC
static void op_26(void)
{
  uint8_t al = *cpu.pc++;
  cpu.ax = (cpu.ax & 0xFF00) | al;
  cpu.di = cpu.ax;

  cpu.cx = game_state.unknown[cpu.di];
  cpu.cx += (game_state.unknown[cpu.di + 1] << 8);
  cpu.cx--;
  uint8_t cl, ch;
  cl = cpu.cx & 0x00FF;
  ch = (cpu.cx & 0xFF00) >> 8;
  set_game_state(cpu.di, cl);
  if (byte_3AE1 != ((cpu.ax & 0xFF00) >> 8)) {
    set_game_state(cpu.di + 1, ch);
  }
}

// 0x3E06
static void op_27()
{
  uint8_t ah;

  cpu.ax = word_3AE2;
  cpu.ax--;
  ah = (cpu.ax & 0xFF00) >> 8;
  ah = ah & byte_3AE1;
  cpu.ax = (ah << 8) | (cpu.ax & 0xFF);
  word_3AE2 = cpu.ax;
}

// 0x3E14
static void op_28()
{
  uint8_t byte_3AE4 = (word_3AE4 & 0x00FF);
  byte_3AE4--;
  word_3AE4 = (word_3AE4 & 0xFF00) | byte_3AE4;
}

// 0x3E36
static void op_2A()
{
  uint8_t ah;

  cpu.ax = word_3AE2;
  cpu.ax = cpu.ax << 1;

  ah = (cpu.ax & 0xFF00) >> 8;
  ah = ah & byte_3AE1;
  cpu.ax = (ah << 8) | (cpu.ax & 0xFF);
  word_3AE2 = cpu.ax;
}

// 0x3E45
static void op_2B()
{
  uint8_t byte_3AE4 = (word_3AE4 & 0x00FF);
  byte_3AE4 = byte_3AE4 << 1;
  word_3AE4 = (word_3AE4 & 0xFF00) | byte_3AE4;
}

// 0x3E67
static void op_2D()
{
  word_3AE2 = word_3AE2 >> 1;
}

// 0x3E75
static void op_2F()
{
  uint8_t al;

  // shr byte [word_3AE6], 1
  cpu.cf = word_3AE6 & CARRY_FLAG_MASK;
  word_3AE6 = word_3AE6 >> 1;

  al = *cpu.pc++;
  cpu.ax = (cpu.ax & 0xFF00) | al;
  cpu.bx = cpu.ax;
  cpu.cx = game_state.unknown[cpu.bx];
  cpu.cx += (game_state.unknown[cpu.bx + 1] << 8);
  if (byte_3AE1 != (cpu.ax >> 8)) {
    uint16_t tmp = word_3AE2 + cpu.cx;
    cpu.cf = tmp << 16;
    word_3AE2 = tmp;
  } else {
    uint8_t byte_3AE2 = word_3AE2 & 0xFF;
    uint8_t tmp = byte_3AE2 + (cpu.cx & 0xFF);
    cpu.cf = tmp << 8;
    word_3AE2 = (word_3AE2 & 0xFF00) | tmp;
  }

  word_3AE6 = (word_3AE6 & 0xFF00) | (((word_3AE6 & 0xFF) << 1) | cpu.cf);
}

// 0x3E9D
static void op_30()
{
  uint8_t ah, al;

  // shr byte [word_3AE6], 1
  uint8_t cf = 0;
  cf = word_3AE6 & CARRY_FLAG_MASK;
  word_3AE6 = (word_3AE6 & 0xFF00) | ((word_3AE6 & 0xFF) >> 1);

  ah = ((cpu.ax & 0xFF00) >> 8);
  if (byte_3AE1 != ah) {
      // es:lodsw
      uint16_t ax = *cpu.pc++;
      ax += *cpu.pc++ << 8;
      cpu.ax = ax;

      // add [3AE2], ax
      word_3AE2 += ax;
      // rcl byte [3AE6], 1
      word_3AE6 = (word_3AE6 & 0xFF00) | (((word_3AE6 & 0xFF) << 1) | cf);
      return;
  }
  al = *cpu.pc++;
  cpu.ax = (cpu.ax & 0xFF00) | al;
  word_3AE2 += al;
  // rcl byte [3AE6], 1
  word_3AE6 = (word_3AE6 & 0xFF00) | (((word_3AE6 & 0xFF) << 1) | cf);

  return;
}

// 0x3EEB
static void op_32()
{
  uint8_t ah, al;
  unsigned int tmp;

  // shr byte [word_3AE6], 1
  cpu.cf = word_3AE6 & CARRY_FLAG_MASK;
  word_3AE6 = (word_3AE6 & 0xFF00) | ((word_3AE6 & 0xFF) >> 1);

  ah = ((cpu.ax & 0xFF00) >> 8);
  if (byte_3AE1 != ah) {
      // es:lodsw
      uint16_t ax = *cpu.pc++;
      ax += *cpu.pc++ << 8;
      cpu.ax = ax;

      tmp = word_3AE2 - cpu.ax;
      cpu.cf = (tmp & 0x10000) == 0x10000;

      word_3AE2 -= ax;
      cpu.cf = !cpu.cf;
      // rcl byte [3AE6], 1
      word_3AE6 = (word_3AE6 & 0xFF00) | (((word_3AE6 & 0xFF) << 1) | cpu.cf);
  } else {
    al = *cpu.pc++;
    cpu.ax = (cpu.ax & 0xFF00) | al;
    uint8_t byte_3AE2 = word_3AE2 & 0xFF;
    tmp = byte_3AE2 - al;
    cpu.cf = (tmp & 0x100) == 0x100;
    cpu.cf = !cpu.cf;
    byte_3AE2 -= al;
    word_3AE2 = (word_3AE2 & 0xFF00) | byte_3AE2;
    // rcl byte [3AE6], 1
    word_3AE6 = (word_3AE6 & 0xFF00) | (((word_3AE6 & 0xFF) << 1) | cpu.cf);
  }

}

// 0x3FBC
static void op_38()
{
  uint8_t al, ah;

  ah = ((cpu.ax & 0xFF00) >> 8);
  if (byte_3AE1 != ah) {
    al = *cpu.pc++;
    ah = *cpu.pc++;
    cpu.ax = (ah << 8) | al;
    word_3AE2 = word_3AE2 & cpu.ax;
  } else {
    al = *cpu.pc++;
    cpu.ax = (cpu.ax & 0xFF00) | al;
    word_3AE2 = word_3AE2 & al;
  }
}

// 0x3FD4
static void op_39()
{
  uint8_t al, ah;

  al = *cpu.pc++;
  cpu.ax = (cpu.ax & 0xFF00) | al;
  cpu.bx = cpu.ax;

  cpu.ax = game_state.unknown[cpu.bx];
  cpu.ax += game_state.unknown[cpu.bx + 1] << 8;

  cpu.ax |= word_3AE2;
  ah = (cpu.ax & 0xFF00) >> 8;
  ah = ah & byte_3AE1;
  cpu.ax = (ah << 8) | (cpu.ax & 0xFF);
  word_3AE2 = cpu.ax;
}

// 0x4030
static void op_3D()
{
  uint8_t ah, al;
  uint16_t flags;

  al = *cpu.pc++;
  cpu.ax = (cpu.ax & 0xFF00) | al;
  cpu.bx = cpu.ax;
  cpu.cx = word_3AE2;
  ah = (cpu.ax & 0xFF00) >> 8;
  if (byte_3AE1 != ah) {
    // 0x403E
    if (cpu.cx < game_state.unknown[cpu.bx]) {
      cpu.cf = 1;
    } else {
      cpu.cf = 0;
    }
  } else {
    // 0x404B
    uint8_t cl;
    cl = cpu.cx & 0xFF;
    if (cl < game_state.unknown[cpu.bx]) {
      cpu.cf = 1;
    } else {
      cpu.cf = 0;
    }
  }
  // flags;
  flags = 0;
  cpu.cf = !cpu.cf;

  flags |= cpu.sf << 7;
  flags |= cpu.zf << 6;
  flags |= 1 << 1; // Always 1, reserved.
  flags |= cpu.cf << 0;
  word_3AE6 = flags;
}

// 0x4051
static void op_3E(void)
{
  uint8_t ah, al;
  cpu.bx = word_3AE2;
  ah = ((cpu.ax & 0xFF00) >> 8);
  if (byte_3AE1 != ah) {
    al = *cpu.pc++;
    ah = *cpu.pc++;
    cpu.ax = (ah << 8) | al;
    cpu.cf = (cpu.bx - cpu.ax) < 0;
    cpu.zf = (cpu.bx - cpu.ax) == 0 ? 1 : 0;
  } else {
    uint8_t bl = cpu.bx & 0x00FF;
    al = *cpu.pc++;
    cpu.ax = (cpu.ax & 0xFF00) | al;
    cpu.cf = (bl - al) < 0;
    cpu.zf = (bl - al) == 0 ? 1 : 0;
  }

  // loc_4042
  cpu.cf = !cpu.cf;
  // pushf
  // pop word [3AE6]
  // We should copy:
  //    Carry flag, parity flag, adjust flag, zero flag
  //    sign flag, trap flag, interupt enable flag, direction, overflow.

  // XXX: Not correct, but maybe it's all we care about?
  word_3AE6 = 0;
  word_3AE6 |= cpu.zf << 6;
  word_3AE6 |= 1 << 1; // Always 1, reserved.
  word_3AE6 |= cpu.cf << 0;
}

// 0x4067
static void op_3F()
{
  uint8_t al = *cpu.pc++;
  cpu.ax = (cpu.ax & 0xFF00) | al;
  cpu.bx = cpu.ax;
  al = word_3AE4;

  // cmp and set flags.
  // I think these are the only flags that matter.
  cpu.zf = al == game_state.unknown[cpu.bx];
  cpu.cf = al < game_state.unknown[cpu.bx];

  // loc_4042
  cpu.cf = !cpu.cf;
  // pushf
  // pop word [3AE6]
  // We should copy:
  //    Carry flag, parity flag, adjust flag, zero flag
  //    sign flag, trap flag, interupt enable flag, direction, overflow.

  // XXX: Not correct, but maybe it's all we care about?
  word_3AE6 = 0;
  word_3AE6 |= cpu.zf << 6;
  word_3AE6 |= 1 << 1; // Always 1, reserved.
  word_3AE6 |= cpu.cf << 0;
}

// 0x4074
static void op_40(void)
{
  uint8_t al = *cpu.pc++;
  cpu.ax = (cpu.ax & 0xFF00) | al;
  uint8_t byte_3AE4 = (word_3AE4 & 0x00FF);
  int cf = 0;
  int zf = 0;

  cf = (byte_3AE4 - al) < 0;
  zf = (byte_3AE4 - al) == 0;
  cf = !cf;
  // pushf
  // pop word [3AE6]
  // We should copy:
  //    Carry flag, parity flag, adjust flag, zero flag
  //    sign flag, trap flag, interupt enable flag, direction, overflow.

  // XXX: Not correct, but maybe it's all we care about?
  word_3AE6 = 0;
  word_3AE6 |= zf << 6;
  word_3AE6 |= 1 << 1; // Always 1, reserved.
  word_3AE6 |= cf << 0;
}

// 0x407C
static void op_41(void)
{
  // Carry flag check
  if ((word_3AE6 & CARRY_FLAG_MASK) == 0) {
    uint16_t new_address = *cpu.pc++;
    new_address += *cpu.pc++ << 8;
    cpu.ax = new_address;
    printf("(op41)    New address: 0x%04x\n", new_address);
    cpu.pc = cpu.base_pc + new_address;
  } else {
    cpu.pc++;
    cpu.pc++;
  }
}

// 0x4085
// Opposite of op_41
static void op_42(void)
{
  // Carry flag check
  if ((word_3AE6 & CARRY_FLAG_MASK) == 0) {
    cpu.pc++;
    cpu.pc++;
  } else {
    uint16_t new_address = *cpu.pc++;
    new_address += *cpu.pc++ << 8;
    cpu.ax = new_address;
    printf("(op42)    New address: 0x%04x\n", new_address);
    cpu.pc = cpu.base_pc + new_address;
  }
}

// 0x4099
// op_44
static void op_44(void)
{
  if ((word_3AE6 & ZERO_FLAG_MASK) == 0) {
    cpu.pc++;
    cpu.pc++;
    return;
  }
  uint16_t new_address = *cpu.pc++;
  new_address += *cpu.pc++ << 8;
  cpu.ax = new_address;
  printf("(op44)    New address: 0x%04x\n", new_address);
  cpu.pc = cpu.base_pc + new_address;
}

// 0x40A3
static void op_45(void)
{
  if ((word_3AE6 & ZERO_FLAG_MASK) != 0) {
    cpu.pc++;
    cpu.pc++;
    return;
  }
  uint16_t new_address = *cpu.pc++;
  new_address += *cpu.pc++ << 8;
  cpu.ax = new_address;
  printf("(op45)    New address: 0x%04x\n", new_address);
  cpu.pc = cpu.base_pc + new_address;
}

// 0x40B8
static void op_47()
{
  if ((word_3AE6 & SIGN_FLAG_MASK) == 0) {
    // 40A0
    op_52();
    return;
  }
  // 40AA
  cpu.pc++;
  cpu.pc++;

}

// 0x4106
// op_49
static void loop(void)
{
  // This is actually more of a LOOP function.
  // The counter is stored in word_3AE4, although it's only an 8 bit
  // counter.

  // byte decrement.
  uint8_t byte_3AE4 = (word_3AE4 & 0x00FF);
  byte_3AE4--;
  word_3AE4 = (word_3AE4 & 0xFF00) | byte_3AE4;

  if (byte_3AE4 != 0xFF) {
    uint16_t new_address = *cpu.pc++;
    new_address += *cpu.pc++ << 8;
    printf("LOOP 0x%04X  Counter: 0x%02X\n", new_address, byte_3AE4);
    cpu.pc = cpu.base_pc + new_address;
  } else {
    // 0x40AA
    cpu.pc++;
    cpu.pc++;
  }
}

// 0x4113
static void op_4A(void)
{
  // Increment byte on word.
  uint8_t byte_3AE4 = word_3AE4 & 0x00FF;
  byte_3AE4++;
  word_3AE4 = (word_3AE4 & 0xFF00) | byte_3AE4;

  uint8_t al = *cpu.pc++;
  cpu.ax = (cpu.ax & 0xFF00) | al;
  if (al == byte_3AE4) {
    // eventually just goes to 0x40AA
    cpu.pc++;
    cpu.pc++;
  } else {
    uint16_t new_address = *cpu.pc++;
    new_address += *cpu.pc++ << 8;
    cpu.ax = new_address;
    printf("(op4A)    New address: 0x%04x\n", new_address);
    cpu.pc = cpu.base_pc + new_address;
  }
}

// 0x4122
static void op_4B(void)
{
  word_3AE6 |= 0x0001;
}

// 0x412A
static void op_4C()
{
  word_3AE6 &= 0xFFFE;
}

// 0x41B9
static void op_52(void)
{
  // CALL function ?
  // Save source index.
  // Jump to new source index.
  uint16_t new_address = *cpu.pc++;
  new_address += *cpu.pc++ << 8;
  printf("New address: 0x%04x\n", new_address);
  uint16_t existing_address = cpu.pc - cpu.base_pc;
  printf("Existing address: 0x%04x\n", existing_address);

  cpu.pc = cpu.base_pc + new_address;
}

// 0x41C0
static void op_53(void)
{
  // CALL function ?
  // Save source index.
  // Jump to new source index.
  uint16_t new_address = *cpu.pc++;
  new_address += *cpu.pc++ << 8;
  printf("New address: 0x%04x\n", new_address);
  uint16_t existing_address = cpu.pc - cpu.base_pc;
  printf("Existing address: 0x%04x\n", existing_address);

  push_word(existing_address);
  cpu.pc = cpu.base_pc + new_address;
}

// 0x41E1
static void op_54()
{
  // RET function.
  uint16_t si = pop_word();
  printf("%s SI: %04X\n", __func__, si);
  cpu.pc = cpu.base_pc + si;
}

// 0x41E5
static void op_55()
{
  uint8_t ah;

  // peek word, pop byte.
  cpu.cx = peek_word();
  pop_byte();
  word_3AE2 = (cpu.cx & 0xFF);
  ah = (cpu.ax & 0xFF00) >> 8;
  if (ah != byte_3AE1) {
    word_3AE2 = cpu.cx;
    pop_byte();
  }
}

// 0x41FD
static void op_56(void)
{
  cpu.cx = word_3AE2;

  uint8_t ah = (cpu.ax & 0xFF00) >> 8;
  if (byte_3AE1 != ah) {
    push_word(cpu.cx);
  } else {
    // store cl into stack.
    push_byte(cpu.cx & 0xFF);
  }
}

// 0x4239
static void op_58(void)
{
  uint8_t al, dl;
  const struct resource *r;

  al = *cpu.pc++;
  cpu.ax = (cpu.ax & 0xFF00) | al;

  // tag lookup item.
  uint16_t tag_item = cpu.ax;

  cpu.ax = *cpu.pc++;
  cpu.ax += ((*cpu.pc++) << 8);
  uint16_t src_offset = cpu.ax;

  uint16_t si = cpu.pc - cpu.base_pc; // is this correct?
  push_word(si);
  // push cs
  // pop es
  // dec sp
  // mov bp, sp
  al = word_3AE8;
  push_byte(al);
  // mov [bp], al
  //
  int found = find_index_by_tag(tag_item);
  if (found != -1) {
    // 0x4254
    r = resource_get_by_index(found);
    if (r->usage_type == 2) {
      dl = 0xFF;
    } else {
      // 0x4259
      // xor dl, dl
      // jmp 4268
      dl = 0;
    }
  } else {
    r = resource_load(tag_item);
    dl = 0xFF;
  }
  // 0x4268
  push_byte(dl); // dl
  al = r->index;
  word_3AE8 = al;
  word_3AEA = al;
  populate_3ADD_and_3ADF();
  cpu.pc = running_script->bytes + src_offset;
  cpu.base_pc = running_script->bytes;
}

// 0x128D
static void sub_128D(int index)
{
  resource_set_usage_type(index, 0x2);
}

// 0x41C8
static void op_59(void)
{
  uint8_t al, ah;
  uint16_t si;

  ah = (cpu.ax & 0xFF00) >> 8;
  if (ah != cpu.stack[cpu.sp]) {
    // 0x41CF
    al = word_3AE8;
    sub_128D(al);
  }
  cpu.ax = pop_word();
  ah = (cpu.ax & 0xFF00) >> 8;
  word_3AE8 = ah;
  word_3AEA = ah;
  populate_3ADD_and_3ADF();
  si = pop_word();
  cpu.pc = running_script->bytes + si;
  cpu.base_pc = running_script->bytes;
}

// 0x3AEE
static void op_5A(void)
{
  // mov sp, [3AEC]
  cpu.sp = saved_stack;
  saved_stack = pop_word();
  word_3ADB = pop_word();
  cpu.ax = pop_word();

  uint8_t al = cpu.ax & 0xFF;
  word_3AE8 = al;
  word_3AEA = al;
  populate_3ADD_and_3ADF();
  al = 0;
  cpu.ax = (cpu.ax & 0xFF00) | al;

  byte_3AE1 = al;
  word_3AE2 = (al << 8) | (word_3AE2 & 0xFF); // lower portion of word_3AE2 takes al.


}

// 0x4295
static void op_5C(void)
{
  op_01();
  cpu.ax = *cpu.pc++;
  cpu.ax += ((*cpu.pc++) << 8);

  word_42D6 = cpu.ax;
  word_3ADB = cpu.pc - cpu.base_pc; // is this correct?
  unsigned char *save_pc = cpu.pc;

  if (game_state.unknown[0x1F] == 0) {
    return;
  }

  uint8_t al = game_state.unknown[6];
  cpu.ax = (cpu.ax & 0xFF00) | al;
  push_word(cpu.ax);
  game_state.unknown[6] = 0;
  while (al < game_state.unknown[0x1F]) {
    cpu.bx = word_42D6;
    al = word_3AE8;
    run_script(al, word_42D6);
    game_state.unknown[6]++;
    al = game_state.unknown[6];
  }
  cpu.ax = pop_word();
  al = cpu.ax & 0xFF;
  game_state.unknown[6] = al;

  cpu.pc = save_pc;
}

// 0x42D8 (5D)
// Loads word_3AE2 with character data.
static void get_character_data(void)
{
  int chr_idx;

  uint8_t al = game_state.unknown[6];
  cpu.ax = (cpu.ax & 0xFF00) | al;
  cpu.di = cpu.ax;
  cpu.bx = 0xC960;

  uint8_t bh = (cpu.bx & 0xFF00) >> 8;
  chr_idx = game_state.unknown[cpu.di + 0xA]; // Character selector ?
  bh += chr_idx;
  cpu.bx = bh << 8 | (cpu.bx & 0xFF);

  al = *cpu.pc++; // Character offset
  printf("%s - Player number: %d 0x%02X\n", __func__, chr_idx, al);
  cpu.ax = (cpu.ax & 0xFF00) | al;
  cpu.bx += cpu.ax;

  unsigned char *c960 = get_player_data_base();
  cpu.cx = c960[cpu.bx - 0xC960];

  word_3AE2 = cpu.cx & 0xFF;
  if (byte_3AE1 != 0) {
    // 0x3AE3
    word_3AE2 = (cpu.cx & 0xFF00) | (word_3AE2 & 0xFF); // XXX ? Correct.
  }
}

// 0x4322
// Set properties of character (value is in word_3AE2).
static void op_5E(void)
{
  uint8_t al = game_state.unknown[6];
  cpu.ax = (cpu.ax & 0xFF00) | al;
  cpu.di = cpu.ax;
  game_state.unknown[cpu.di + 0x18] = (cpu.ax & 0xFF00) >> 8;
  cpu.bx = 0xC960;
  uint8_t bh = (cpu.bx & 0xFF00) >> 8;
  bh += game_state.unknown[cpu.di + 0xA];
  cpu.bx = bh << 8 | (cpu.bx & 0xFF);

  al = *cpu.pc++;
  cpu.ax = (cpu.ax & 0xFF00) | al;
  cpu.bx += cpu.ax;
  cpu.cx = word_3AE2;

  // Validation that we aren't writing outside our array.
  if ((cpu.bx - 0xC960) >= 0xE00) {
    printf("Array of data_C960 not large enough!\n");
    exit(1);
  }
  // mov [di], al
  unsigned char *c960 = get_player_data_base();
  c960[cpu.bx - 0xC960] = cpu.cx & 0xFF;
  if (byte_3AE1 != 0) {
    c960[cpu.bx - 0xC960 + 1] = (cpu.cx & 0xFF00) >> 8;
  }

}

// 0x4A7D
static void sub_4A7D()
{
  uint8_t al;

  al = word_3AE2;
  cpu.ax = al;
  cpu.di = cpu.ax;
  cpu.bx = cpu.ax;

  al = *cpu.pc++;
  cpu.ax = al;
  cpu.bx = cpu.bx >> 3;
  cpu.bx += cpu.ax;

  cpu.di += 7;
  al = data_4A99[cpu.di];
  cpu.ax = al;
}

static void sub_40D1()
{
  uint16_t flags = 0;

  flags |= cpu.sf << 7;
  flags |= cpu.zf << 6;
  flags |= 1 << 1; // Always 1, reserved.
  flags |= cpu.cf << 0;
  cpu.ax = flags;

  cpu.ax &= 0xFFFE;
  word_3AE6 &= 0x0001;
  word_3AE6 |= cpu.ax;
}

// 0x43A6
static void op_61()
{
  // 4A7D
  sub_4A7D();
  cpu.cx = game_state.unknown[6];
  cpu.di = cpu.cx;

  cpu.cx = 0xC960;

  uint8_t val = game_state.unknown[cpu.di + 10];
  unsigned char *player = get_player_data(val >> 1);

  // test [bx+di], al
  uint8_t test_val = player[cpu.bx];
  uint8_t test_result = test_val & (cpu.ax & 0xFF);
  cpu.cf = 0;
  cpu.sf = test_result >= 0x80;
  cpu.zf = test_result == 0;
  sub_40D1();
}

// 0x43F7
static void op_63()
{
  uint8_t al, ah;

  ah = (cpu.ax & 0xFF00) >> 8;
  byte_3AE1 = ah;

  // 0x3AE3
  word_3AE2 = (ah << 8) | (word_3AE2 & 0xFF);

  // es:lodsw
  cpu.ax = *cpu.pc++;
  cpu.ax += ((*cpu.pc++) << 8);

  word_4454 = cpu.ax;
  word_3ADB = cpu.pc - cpu.base_pc; // is this correct?
  byte_3867 = 0;

  // 0x440D

  cpu.bx = 0;
  cpu.bx = cpu.bx << 1;
  al = game_state.unknown[6];
  ah = 0;
  cpu.ax = al;
  cpu.di = cpu.ax;
  cpu.ax = 0xCA4C;
  ah = (cpu.ax & 0xFF00) >> 8;
  ah += game_state.unknown[cpu.di + 0xA]; // character select.
  cpu.ax = (ah << 8) | (cpu.ax & 0xFF);
  cpu.ax += get_unknown_4456(cpu.bx);
  cpu.di = cpu.ax;
  if (data_CA4C[cpu.di - 0xCA4C] != 0) {
    // 0x4430
    printf("%s: 0x4430 unimplemented\n", __func__);
    exit(1);
  }
  // 0x444C
  word_3AE6 &= 0xFFFE;
}

// 0x40C1
static void op_66(void)
{
  uint8_t al = *cpu.pc++;
  cpu.ax = (cpu.ax & 0xFF00) | al;
  cpu.bx = cpu.ax;

  int zf = 0;
  int cf = 0;

  cpu.cx = game_state.unknown[cpu.bx];
  cpu.cx += (game_state.unknown[cpu.bx + 1] << 8);
  if (byte_3AE1 == (cpu.ax >> 8)) {
    uint8_t cl = cpu.cx & 0x00FF;
    if ((cl & cl) == 0) {
      zf = 1;
    }
  } else {
    if ((cpu.cx & cpu.cx) == 0) {
      zf = 1;
    }
  }

  uint16_t flags = 0;
  flags |= zf << 6;
  flags |= 1 << 1; // Always 1, reserved.
  flags |= cf << 0;
  flags &= 0xFFFE;
  word_3AE6 &= 0x0001;
  word_3AE6 |= flags;
  cpu.ax = flags;
}

// 0x453F
static void op_69(void)
{
  cpu.bx = game_state.unknown[7];
  // shl bx, 1
  cpu.ax = game_state.unknown[6];
  cpu.di = cpu.ax;
  cpu.ax = 0xCA4C;

  uint8_t ah = (cpu.ax & 0xFF00) >> 8;
  ah += game_state.unknown[0xA + cpu.di];
  cpu.ax = ah << 8 | (cpu.ax & 0xFF);
  cpu.ax += get_unknown_4456(cpu.bx);
  cpu.di = cpu.ax;

  cpu.ax = *cpu.pc++;
  cpu.di += cpu.ax;

  cpu.ax = word_3AE2;
  // Validation that we aren't writing outside our array.
  if ((cpu.di - 0xCA4C) >= sizeof(data_CA4C)) {
    printf("Array of data_CA4C not large enough!\n");
    exit(1);
  }
  // mov [di], al
  data_CA4C[cpu.di - 0xCA4C] = cpu.ax & 0xFF;
  if (byte_3AE1 != 0) {
    data_CA4C[cpu.di - 0xCA4C + 1] = (cpu.ax & 0xFF00) >> 8;
  }

}

// 0x25E0
static void draw_rectangle(void)
{
  int identical = 1;
  int needs_resize = 0;

  data_268F.x = *cpu.pc++;
  data_268F.y = *cpu.pc++;
  data_268F.w = *cpu.pc++;
  data_268F.h = *cpu.pc++;

  ui_draw_string();
  if (ui_drawn_yet != 0) {
    ui_rect_expand();
    if (draw_rect.x < data_268F.x ||
        draw_rect.y > data_268F.y ||
        draw_rect.w < data_268F.w ||
        draw_rect.h > data_268F.h) {
      needs_resize = 1;
      identical = 1;
      // 0x2632
    } else {
      // 2623
      identical = memcmp(&data_268F, &draw_rect, sizeof(data_268F));
    }
  }
  // 0x2632
  if (needs_resize) {
    ui_rect_shrink();
    ui_draw();
  }

  if (identical != 0) {
    // 0x2638
    memcpy(&draw_rect, &data_268F, sizeof(data_268F));
    draw_point.x = draw_rect.x;
    draw_point.y = draw_rect.y;

    // 0x32BF, 0x32C1, 0x80
    printf("sub_269F(%d, %d, 0x80)\n", draw_point.x, draw_point.y);
    // 0x269F
    ui_draw_box_segment(0x80);

    // loc_2668
    // Draw left and right sides.
    while (draw_point.y < draw_rect.h - 8) {
      draw_point.x = draw_rect.x;
      draw_point.y += 8;
      ui_draw_chr_piece(0x83);
      draw_point.x = draw_rect.w - 1;
      ui_draw_chr_piece(0x84);
    }
    draw_point.x = draw_rect.x;
    ui_draw_box_segment(0x85);
  }

  // 0x2683
  ui_drawn_yet = 0xFF;
  ui_rect_shrink();
  draw_pattern(&draw_rect);
  vga->update();
}

// 0x47C0
static void op_74(void)
{
  // draws frame, reads next 4 bytes to get rectangle dimensions.
  draw_rectangle();
}

// 0x47D1
static void op_75(void)
{
  ui_draw_full();
}

// 0x47D9
static void op_76(void)
{
  draw_pattern(&draw_rect);
}

// 0x47E3
static void op_77()
{
  draw_pattern(&draw_rect);
  sub_1C79(&cpu.pc, cpu.pc - cpu.base_pc);
}

// 0x47EC
static void op_78(void)
{
  sub_1C79(&cpu.pc, cpu.pc - cpu.base_pc);
}

// 0x4801
static void op_7A()
{
  unsigned char *src_ptr= word_3ADF->bytes + word_3AE2;
  sub_1C79(&src_ptr, word_3AE2);
  word_3AE2 = cpu.bx;
}

// 0x1A40
// Write character name
static void write_character_name()
{
  uint8_t al, ah;

  cpu.bx = game_state.unknown[6];
  cpu.ax = 0xC960;
  ah = (cpu.ax & 0xFF00) >> 8;
  uint8_t val = game_state.unknown[cpu.bx + 10];
  ah += val;
  cpu.ax = (ah << 8) | (cpu.ax & 0xFF);
  cpu.bx = cpu.ax;

  unsigned char *player = get_player_data(val >> 1);
  printf("%s: 0x%04X, Player number: 0x%02X\n", __func__, cpu.bx, val);
  while (1) {
    al = *player++;
    ah = al;
    cpu.ax = al;
    al = al | 0x80;
    sub_3150(al);
    if ((ah & 0x80) == 0) {
      break;
    }
  }
}

// 0x483B
static void op_7D(void)
{
  printf("op_7D\n");
  write_character_name();
}

static void sub_1BE6()
{
  int counter;
  counter = cpu.ax & 0xFF;

  counter -= draw_point.x;
  if (counter <= 0)
    return;

  cpu.ax = 0xA0;
  for (int i = 0; i < counter; i++) {
    sub_3150(0xA0);
  }
}

// 0x487F
static void op_80(void)
{
  uint8_t al;

  al = *cpu.pc++;
  cpu.ax = (cpu.ax & 0xFF00) | al;

  ui_draw_string();
  al += draw_rect.x;
  cpu.ax = (cpu.ax & 0xFF00) | al;

  sub_1BE6();
}

// 0x1DCA
static void sub_1DCA(uint8_t dl)
{
  uint8_t al, bl;
  unsigned int tmp;

  bl = dl;
  cpu.bx = bl;
  byte_1E20 = 0;
  int counter = cpu.bx << 1;

  // 1DD4 (loop)
  while (counter >= 0) {
    dl = 0xB0;

    while (1) {
      // 1DD6
      cpu.ax = word_11C6;
      uint16_t data_val = data_1E21[counter];
      data_val += (data_1E21[counter + 1] << 8);
      tmp = cpu.ax - data_val;
      cpu.cf = (tmp & 0x10000) == 0x10000;

      cpu.ax = tmp & 0xFFFF;
      cpu.cx = cpu.ax;
      cpu.ax = word_11C8;

      data_val = data_1E21[counter + 0x14];
      data_val += data_1E21[counter + 0x15] << 8;
      tmp = cpu.ax - data_val - cpu.cf;
      cpu.cf = (tmp & 0x10000) == 0x10000;
      cpu.ax = tmp & 0xFFFF;

      if (cpu.cf == 0) {
        // 0x1DE8
        word_11C8 = cpu.ax;
        word_11C6 = cpu.cx;
        dl++;
      } else {
        // 0x1DF3
        break;
      }
    }

    int skip_1E10 = 0;

    // 0x1DF3
    if (counter != 0) {
      // 0x1DF7
      if (dl != 0xB0) {
        byte_1E20 = dl;
      }
      // 0x1E00
      if (byte_1E20 == 0) {
        if (byte_1E1F != 0) {
          dl = 0xA0;
        } else {
          skip_1E10 = 1;
        }
      }
    }
    // 0x1E10
    if (skip_1E10 == 0) {
      al = dl;
      sub_3150(al);
    }
    // 0x1E15
    counter = counter - 2;
  }
  // 0x1E19
  byte_1E1F = 0;
  cpu.bx = 0;

}

// 0x1DBB
// also called by 0x1DB9 (with high val of 0)
static void sub_1DBB(uint16_t val)
{
  uint8_t dl;

  word_11C6 = val;
  dl = 4;
  word_11C8 = 0;

  sub_1DCA(dl);
}

// 0x1DC8
static void sub_1DC8()
{
  sub_1DCA(9);
}

// 0x48C5
static void op_81()
{
  cpu.ax = word_3AE2;
  sub_1DBB(cpu.ax);
}

// 0x48D2
static void op_82()
{
  uint8_t al;

  al = *cpu.pc++;
  cpu.ax = (cpu.ax & 0xFF00) | al;
  cpu.bx = cpu.ax;

  cpu.ax = game_state.unknown[cpu.bx];
  cpu.ax += game_state.unknown[cpu.bx + 1] << 8;

  word_11C6 = cpu.ax;

  cpu.ax = game_state.unknown[cpu.bx + 2];
  cpu.ax += game_state.unknown[cpu.bx + 3] << 8;

  word_11C8 = cpu.ax;
  sub_1DC8();
}

// 0x48EE
// Writes word_3AE2 out to screen.
// Could be 1 byte or 2 bytes depending on whether byte_3AE1 is set.
static void op_83(void)
{
  uint8_t al;
  if (byte_3AE1 != (cpu.ax >> 8)) {
    // high byte of 3AE2;
    al = ((word_3AE2 & 0xFF00) >> 8);
    sub_3150(al);
  }
  al = word_3AE2;
  sub_3150(al);
}

// 0x4907
static void op_84(void)
{
  struct resource *r = game_memory_alloc(word_3AE2, 1, 0xFFFF);
  cpu.ax = r->index;
  word_3AE2 = cpu.ax;
}

// 0x4920
static void op_85(void)
{
  cpu.ax = word_3AE2;
  resource_index_release(cpu.ax);
}

// 0x493E
static void op_86(void)
{
  const struct resource *r = resource_load(word_3AE2);
  cpu.ax = r->index;
  uint8_t ah = (cpu.ax & 0xFF00) >> 8;
  word_3AE2 = (ah & byte_3AE1) | (cpu.ax & 0x00FF);
  //dump_hex(r->bytes, 0x80);
}

// Uses carry flag as a boolean
static int sub_2752(uint8_t input)
{
  if (ui_drawn_yet == 0)
    return 0;

  return ui_adjust_rect(input);
}

// 0x4B60
static void sub_4B60()
{
  if (sub_2752(9) == 1) {
    return;
  }
  printf("%s: 0x4B68 unimplemented\n", __func__);
  exit(1);
}

// 0x4D5C
static void sub_4D5C()
{
  if (timers.timer2 != 0)
    return;

  if (byte_4F2B == 0)
    return;

  printf("%s: 0x4D6A unimplemented\n", __func__);
  exit(1);
}

// 0x1A72
static void sub_1A72()
{
  if (sub_2752(0xB) == 1) {
    return;
  }
  printf("%s: 0x1A79 unimplemented\n", __func__);
  exit(1);

}

// 0x1F10
static void sub_1F10()
{
  if (byte_3855 == 0) {
    return;
  }

  printf("%s: 0x1F17 unimplemented\n", __func__);
  exit(1);
}

// 0x2CF5
// Get timer ticks?
static void sub_2CF5()
{
  word_2D09 = 0x1234; // can we just use random?
}

// 0x3824
static void sub_3824()
{
  // No support for reading mouse position at this point.
  // This is determined by 0x3855.
  cpu.ax = 0;

  mouse.enabled = 0;
  mouse.x = 0;
  mouse.y = 0;
  mouse.clicked = 0;
}

// 0x2AEE
// Check if mouse is inbounds on a rectangle?
static int sub_2AEE()
{
  word_246D = 2;
  cpu.ax = mouse.x;
  cpu.ax = cpu.ax << 3;

  if (cpu.ax > draw_rect.x) {
    printf("%s: 0x2BO2 unimplemented\n", __func__);
    exit(1);
  }

  if ((word_2AA7 & 0x04) != 0) {
    cpu.ax = cpu.ax & 0xFF00;
    word_246D = 0;
    cpu.cf = 1;
    return 1;
  }
  if ((word_2AA7 & 0x10) != 0) {
    printf("%s: 0x2B43 unimplemented\n", __func__);
    exit(1);
  }
  if ((word_2AA7 & 0x20) != 0) {
    printf("%s: 0x2B88 unimplemented\n", __func__);
    exit(1);
  }

  return 0;
}

// 0x3840
static uint8_t sub_3840()
{
  return mouse.clicked & 0xC0;
}

// 0x2D0B
static uint16_t sub_2D0B()
{
  cpu.bx = word_2DD9;
  if (cpu.bx < 0x8000) {
    printf("%s: 0x2D13 unimplemented\n", __func__);
    exit(1);
  }
  // 0x2D31
  do {
    cpu.ax = vga->getkey();
    if (cpu.ax == 0) {
      return cpu.ax;
    }
    if (cpu.ax == 0x93) {
      // Ctrl-S
      printf("xor byte_107, 0x40\n");
    }
  } while (cpu.ax == 0x93);

  // 0x2D4B
  // We have a key now.
  cpu.bx = word_2DD7;
  if (cpu.bx >= 0x8000) {
    // cpu.bx is signed (negative value)
    return cpu.ax;
  }
  // 0x2D53

  printf("%s: 0x2D53 unimplemented\n", __func__);
  exit(1);

  return cpu.ax;
}

// 0x2BD9
static int sub_2BD9()
{
  sub_4D5C();
  sub_4B60();
  sub_1A72();
  cpu.ax = timers.timer3; // 0x4C38
  uint8_t al = cpu.ax & 0xFF;
  al = al | timers.timer5; // 0x4C3C
  cpu.ax = (cpu.ax & 0xFF00) | al;
  if (cpu.ax != 0) {
    return 0;
  }
  timers.timer3 = 0xB4;
  al = game_state.unknown[5];
  al++;
  al = al & 0x1F;
  game_state.unknown[5] = al;
  return 1;
}

// 0x2ADC
static void sub_2ADC()
{
  if ((word_2AA7 & 0x1) != 0) {
    return;
  }
  memset(&data_2AAA, 0, sizeof(data_2AAA));
}

// 0x2A4C
static void sub_2A4C()
{
  uint8_t al;

  cpu.di++;
  printf("%s DI: 0x%04X\n", __func__, cpu.di);

  cpu.bx = *(cpu.base_pc + cpu.di);
  cpu.bx += *(cpu.base_pc + cpu.di + 1) << 8;

  printf("%s: BX - 0x%04X\n", __func__, cpu.bx);
  printf("%s: AX - 0x%04X\n", __func__, cpu.ax);
  sub_2ADC();
  al = cpu.ax & 0xFF;
  if (al == 1) {
    al = byte_2AA6;
    printf("%s: AL - 0x%02X\n", __func__, al);

    al -= 0xB1;
    game_state.unknown[0x6] = al;
  }
  al = byte_2AA6;
  cpu.ax = (cpu.ax & 0xFF00) | al;
}

// 0x1F8F
static void sub_1F8F()
{
  if (byte_2476 == 0)
    return;

  printf("%s: 0x1F96 unimplemented\n", __func__);
  exit(1);
}

// 0x28B0
// Should we take BX as an argument.
static void sub_28B0(unsigned char **src_ptr, unsigned char *base)
{
  uint8_t al, ah;
  uint8_t bl, bh;

  ui_draw_string();

  cpu.ax = **src_ptr;
  (*src_ptr)++;
  cpu.ax += **src_ptr << 8;
  (*src_ptr)++;

  bl = cpu.bx & 0xFF;

  // 0x28BA
  word_2AA7 = cpu.ax;
  ah = (cpu.ax & 0xFF00) >> 8;
  ah = ah & 0x20;
  al = cpu.ax & 0xFF;

  cpu.ax = (ah << 8) | al;

  timers.timer5 = ah;
  // extract 0x2AA8
  al = (word_2AA7 & 0xFF00) >> 8;
  al = al & 0x10;
  if (al != 0) {
    al = **src_ptr;
    (*src_ptr)++;
  }
  byte_2AA9 = al;
  word_2AA2 = *src_ptr - base; // is this correct? (si)

  word_2AA4 = *src_ptr;

  if ((word_2AA7 & 0x80) != 0) {
    sub_1F8F();
  }
  // 0x28E4
  if ((word_2AA7 & 0x8000) != 0) {
    // 0x28EB
    al = draw_rect.h;
    al -= 8;
    draw_point.y = al;
    // 0x32BF, 0x32C1, 0x80
    ui_set_byte_3236(0);

    bl = (word_2AA7 & 0xFF00) >> 8;
    cpu.bx = (cpu.bx & 0xFF00) | bl;
    cpu.bx = cpu.bx & 0x3;
    al = draw_rect.w;
    al -= draw_rect.x;
    al -= data_2A68[cpu.bx];
    if (al <= 0) {
      al = 0;
    }
    al = al >> 1;
    al += draw_rect.x;
    draw_point.x = al;
    cpu.bx = cpu.bx << 1;
    bl = cpu.bx & 0xFF;
    // mov bx, [bx + 0x2A6C]
    cpu.bx = data_2A68[bl + 4];
    cpu.bx += data_2A68[bl + 5] << 8;

    printf("%s: cpu.bx = 0x%04X\n", __func__, cpu.bx);
    unsigned char *src_ptr = data_2A68 + (cpu.bx - 0x2A68);

    sub_1C79(&src_ptr, (cpu.bx - 0x2A68));
    ui_draw_string();

    bl = draw_point.y;
    bl -= draw_rect.y;
    bl = bl >> 3;
    al = 0xFF;
    data_2AAA[bl] = 0xFF;
    data_2AAA[bl + 25] = 0x9B;
  }

  // 0x2942
  sub_4D5C();
  sub_4B60();
  sub_1A72();
  printf("%s: word_2AA7: 0x%04X\n", __func__, word_2AA7);
  // 0x294B
  while (1) {
    if ((word_2AA7 & 0x0080) == 0) {
      sub_1F10();
    }
    sub_2CF5(); // timer
    sub_3824(); // mouse ?
    sub_2AEE(); // Mouse in bounds?
    uint8_t clicked = sub_3840();
    if (clicked == 0x80) {
      printf("%s: 0x2965 unimplemented\n", __func__);
      exit(1);
    }

    // 0x2985
    al = sub_2D0B(); // key pressed?
    if (al != 0) {
      // A-Z letters.
      if (al >= 0xE1 && al <= 0xFA) {
        // 0x2992
        if ((word_2AA7 & 0x2) == 0) {
          al = al & 0xDF;
        }
      }

      // 0x299B
      // All other keys
      printf("%s: word_2AA7: 0x%04X\n", __func__, word_2AA7);
      if ((word_2AA7 & 0x8000) != 0) {
        if ((word_2AA7 & 0x4000) == 0) {
          if (al == 0xA0) {
            al = 0x9B;
          }
        }
      }
    } else {
      // 0x29B1
      if (sub_2BD9() == 0)
        continue;
      // 0x29B6
      al = 1;
    }

    // 29B8
    byte_2AA6 = al;
    if ((word_2AA7 & 0x40) != 0) {
      sub_2ADC();
      cpu.bx = word_2AA2;
      return;
    }

    // 0x29CC
    cpu.di = word_2AA2;
    cpu.di -= 3;
    *src_ptr -= 3;

    uint8_t dl = byte_2AA6;
    // 0x29DD
    while (1) {
      cpu.di += 3;
      al = *(base + cpu.di);
      cpu.ax = (cpu.ax & 0xFF00) | al;
      if (al == 0) {
        sub_2A4C();
        return;
      }
      if (al == 0xFF) {
        break;
      }

      // 0x29EF
      if (al == 0x01) {
        bl = dl;
        bl -= 0xB1;
        if (bl < game_state.unknown[0x1F]) {
          // 0x29FE
          bh = 0;
          cpu.bx = bl;
          uint16_t si = cpu.bx;
          cpu.bx = 0xC960;
          bh = game_state.unknown[0xA + si];
          cpu.bx += (bh << 8);
          unsigned char *c960 = get_player_data_base();
          cpu.cx = c960[cpu.bx - 0xC960 + 0x4C];
          cpu.cx = cpu.cx & byte_2AA9;
          if (cpu.cx != 0)
            continue;

          sub_2A4C();
          return;
        }
      }
      // 0x2A15
      else if (al == 0x02) {
        printf("%s: 0x2A19 unimplemented\n", __func__);
        exit(1);
      }
      // 0x2A20
      else if (al != 0x80) {

        // 0x2A24
        if (al == 0x81) {
          cpu.di++;
        } else {
          // 0x2A2B
          if ((al & 0x80) == 0) {
            // 0x2A2F
            cpu.di++;
            al = al | 0x80;
            cpu.ax = (cpu.ax & 0xFF00) | al;
            if (al > byte_2AA6)
            {
              continue;
            }
            printf("%s: 0x2A38 unimplemented\n", __func__);
            exit(1);
          }
          if (al == byte_2AA6) {
            // 0x2A4C
            sub_2A4C();
            return;
          }
        }
      }
    }
  }
}

// 0x2C00
// Takes a pointer?
static void sub_2C00()
{
  cpu.bx = 0x2C0E; // function pointer.
  unsigned char *ptr = data_2C0E + 0;
  sub_28B0(&ptr, data_2C0E);
  draw_pattern(&draw_rect);
}

// 0x496D
static void op_88()
{
  sub_2C00();
}

// 0x4977
static void op_89(void)
{
  printf("%s : 0x4977\n", __func__);
  word_3ADB = cpu.pc - cpu.base_pc; // is this correct?
  cpu.bx = word_3ADB;

  sub_28B0(&cpu.pc, cpu.base_pc);

  // 0x4984 (A good idea to break here so you can trap keypresses).
  // the key pressed will be in AX (OR'd with 0x80).
  cpu.ax = cpu.ax & 0x00FF;
  printf("%s: BX: 0x%04X\n", __func__, cpu.bx);
  cpu.pc = cpu.base_pc + cpu.bx;
  word_3AE2 = cpu.ax;
}

// 0x1EBF
static void sub_1EBF()
{
  int zf = 0;
  uint8_t al;

  uint16_t flags = 0;
  flags |= zf << 6;
  flags |= 1 << 1; // Always 1, reserved.
  flags |= cpu.cf << 0;
  push_word(flags);

  al = draw_rect.w;
  al -= draw_rect.x;

  al = al - (2 + cpu.cf);
  cpu.ax = (cpu.ax & 0xFF00) | al;
  push_word(cpu.ax);

  if (al >= 0x10) {
    al = 0x10;
  }
  byte_1F08 = al;
  cpu.ax = pop_word();
  al = cpu.ax & 0xFF;
  al -= byte_1F08;
  al = al >> 1;
  al += draw_rect.x;
  draw_point.x = al;
  al = 0xBA;
  ui_draw_chr_piece(al);
  if (byte_1F07 != 0) {
    // 0x1EED
    cpu.bx = 0;
    while (cpu.bx < byte_1F07) {
      al = game_state.unknown[cpu.bx + 0xC6];
      ui_draw_chr_piece(al);
      cpu.bx++;
    }
  }
  // 0x1EFD
  flags = pop_word();
  al = 0xA0; // Space
  if (cpu.cf == 0) {
    al = 0xFE;
  }
  ui_draw_chr_piece(al);
}

// 0x1EBB
static void sub_1EBB()
{
  // clc
  cpu.cf = 0;
  sub_1EBF();
}

// 0x1EBE
static void sub_1EBE()
{
  cpu.cf = 1;
  sub_1EBF();
}

// 0x1E49
static void sub_1E49()
{
  uint8_t al, bl;

  ui_draw_string();
  byte_1F07 = 0;
  sub_1EBB();

  // 0x1E54
  while (1) {
    cpu.bx = 0x1EB9; // function pointer.
    unsigned char *ptr = data_1EB9 + 0;
    sub_28B0(&ptr, data_1EB9);

    // Checking for keys.
    al = cpu.ax & 0xFF;

    // Filter out invalid keys.
    if (al == 0xAF || al == 0xDC) {
      // Dragon wars doesn't like '/' or '\' characters.
      continue;
    }

    // 0x1E64
    bl = byte_1F07;
    cpu.bx = bl;
    if (al == 0x88) { // Backspace
      if (cpu.bx == 0) {
        continue;
      }
      byte_1F07--;
      sub_1EBB();
      ui_draw_chr_piece(0xA0);
      continue;
    } else if (al == 0x8D) { // Enter key
      // jmp 1E99
      break;
    } else if (al == 0x9B) { // escape.
      byte_1F07 = 0;
      break;
      // jmp 1E93
    } else if (bl >= byte_1F08) {
      continue;
    } else if (al < 0xA0) {
      continue;
    }
    if (al == 0xA0) {
      // 0x1E82
      if (cpu.bx == 0)
        continue;
        // 1E54
    } else {
      set_game_state(0xC6 + cpu.bx, al);
      byte_1F07++;
      sub_1EBB();
    }
  }
  // 1E99
  al = 0;
  set_game_state(0xC6 + cpu.bx, al);
  sub_1EBE();
  al = 0x8D;
  sub_3150(al);
  return;
}

// 0x49D3
static void op_8D()
{
  printf("%s : 0x49D3\n", __func__);
  sub_1E49();
}

// 0x4A67
static void op_93(void)
{
  uint8_t byte_3AE4 = (word_3AE4 & 0x00FF);
  uint8_t al = byte_3AE4;
  cpu.ax = (cpu.ax & 0xFF00) | al;

  push_byte(al);
}

// 0x4A6D
// Who pushes ?
static void op_94(void)
{
  uint8_t al = pop_byte();
  uint8_t byte_3AE4 = al;

  word_3AE4 = (word_3AE4 & 0xFF00) | byte_3AE4;
}

// 0x4894
static void op_95()
{
  uint8_t byte_3AE4 = (word_3AE4 & 0x00FF);
  uint8_t al;

  ui_draw_string();

  al = byte_3AE4;
  al += draw_rect.y;
  draw_point.y = al;


  al = word_3AE2;
  al += draw_rect.x;
  draw_point.x = al;
  ui_set_byte_3236(al);
}

// 0x48B5
static void op_96()
{
  uint8_t al;

  ui_draw_string();
  al = draw_rect.w;
  cpu.ax = (cpu.ax & 0xFF00) | al;
  sub_1BE6();
}

// 0x42FB
// Loads character data into word_3AE2
static void op_97()
{
  uint8_t al, ch, cl;

  al = game_state.unknown[6];
  cpu.ax = (cpu.ax & 0xFF00) | al;
  cpu.di = cpu.ax;

  cpu.bx = 0xC960;
  uint8_t bh = (cpu.bx & 0xFF00) >> 8;
  bh += game_state.unknown[cpu.di + 0xA]; // Character selector ?
  cpu.bx = bh << 8 | (cpu.bx & 0xFF);

  al = *cpu.pc++;
  cpu.ax = (cpu.ax & 0xFF00) | al;
  cpu.bx += cpu.ax;
  cpu.bx += word_3AE4;

  unsigned char *c960 = get_player_data_base();

  cpu.cx = c960[cpu.bx - 0xC960];
  cl = c960[cpu.bx - 0xC960];
  ch = c960[cpu.bx - 0xC960 + 1];
  cpu.cx = (ch << 8) | cl;
  word_3AE2 = cl;
  if (byte_3AE1 != (cpu.ax & 0xFF00) >> 8) {
    word_3AE2 = cpu.cx;
  }
}

// 0x4348
static void op_98()
{
  uint8_t al;

  al = game_state.unknown[6];
  cpu.ax = (cpu.ax & 0xFF00) | al;
  cpu.di = cpu.ax;
  set_game_state(cpu.di + 0x18, (cpu.ax & 0xFF00) >> 8);
  cpu.bx = 0xC960;

  uint8_t bh = (cpu.bx & 0xFF00) >> 8;
  bh += game_state.unknown[cpu.di + 0xA]; // Character selector ?
  cpu.bx = bh << 8 | (cpu.bx & 0xFF);

  al = *cpu.pc++;
  cpu.ax = (cpu.ax & 0xFF00) | al;
  cpu.bx += cpu.ax;
  cpu.bx += word_3AE4;
  cpu.cx = word_3AE2;

  unsigned char *c960 = get_player_data_base();
  c960[cpu.bx - 0xC960] = cpu.cx & 0xFF;
  if (byte_3AE1 != (cpu.ax & 0xFF00) >> 8) {
    c960[cpu.bx - 0xC960 + 1] = (cpu.cx & 0xFF00) >> 8;
  }
}

// 0x40E7
static void op_99(void)
{
  cpu.cx = word_3AE2;

  // XXX: Figure out what test this is really trying to do.
  // Zero flag, carry flag, sign flag?
  int zf = 0;
  int cf = 0;
  int sf = 0;

  // 40C9
  if (byte_3AE1 == (cpu.ax >> 8)) {
    uint8_t cl = cpu.cx & 0x00FF;
    if ((cl & cl) == 0) {
      zf = 1;
    }
    sf = (cl >= 0x80);
  } else {
    if ((cpu.cx & cpu.cx) == 0) {
      zf = 1;
    }
    sf = (cpu.cx >= 0x8000);
  }

  uint16_t flags = 0;
  flags |= sf << 7;
  flags |= zf << 6;
  flags |= 1 << 1; // Always 1, reserved.
  flags |= cf << 0;
  flags &= 0xFFFE;
  word_3AE6 &= 0x0001;
  word_3AE6 |= flags;
  cpu.ax = flags;
}

// 0x3C42
static void op_9A(void)
{
  uint8_t al = *cpu.pc++;
  cpu.ax = (cpu.ax & 0xFF00) | al;
  cpu.bx = cpu.ax;
  al = 0xFF;
  cpu.ax = (cpu.ax & 0xFF00) | al;
  set_game_state(cpu.bx, al);
  uint8_t ah = (cpu.ax & 0xFF00) >> 8;
  if (byte_3AE1 != ah) {
    set_game_state(cpu.bx + 1, al);
  }
}

static void sub_1C79(unsigned char **src_ptr, uint16_t offset)
{
  num_bits = 0;
  cpu.bx = offset;

  byte_1CE4 = 0;
  while (1) {
    uint8_t ret = sub_1CF8(src_ptr); // check for 0
    if (ret == 0) {
      // 1CE6
      return;
    }
    if ((game_state.unknown[8] & 0x80) == 0)
    {
      ret |= 0x80;
      game_state.unknown[8] = ret;
      ret &= 0x7F;
    }
    // 1C9E
    if (ret == 0xAF) {
      // 0x1CAB
      printf("Unhandled engine code: 0x1CAB\n");
      exit(1);
      return;
    }
    if (ret == 0xDC) {
      // 0x1CAF
      printf("Unhandled engine code: 0x1CAF\n");
      exit(1);
      return;
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

static uint8_t sub_1D86(unsigned char **src_ptr)
{
  uint8_t al, bl;

  bl = 0x06;

  // 0x1D8C
  // xor al, al
  cpu.ax = cpu.ax & 0xFF00;
  al = 0;
  int8_t dl = (int8_t)num_bits;
  while (bl != 0) {
    dl--;
    if (dl < 0) {
      // 0x1DA5
      //
      dl = **src_ptr;
      bit_buffer = dl;
      dl = 7;
      (*src_ptr)++;
      cpu.bx++;
    }

    int cf = 0;
    if (bit_buffer & 0x80) {
      cf = 1;
    }
    bit_buffer = bit_buffer << 1;
    al = al << 1;
    al |= cf;
    bl--;
  }

  num_bits = dl; // leftover bits
  return al;
}

static uint8_t sub_1CF8(unsigned char **src_ptr)
{
  while (1) {
    uint8_t ret = sub_1D8A(src_ptr);
    if (ret == 0)
      return 0;

    if (ret == 0x1E) {
      // stc
      // rcr byte [byte_1CE4], 1
      // rotate carry right bit.
      byte_1CE4 = byte_1CE4 >> 1;
      byte_1CE4 += 0x80;
      continue;
    }

    if (ret > 0x1E) {
      ret = sub_1D86(src_ptr);
      ret += 0x1E;
    }

    // ret != 0x1E

    // 0x1D0A
    // offset
    uint8_t al = alphabet[ret - 1];
    byte_1CE4 = byte_1CE4 >> 1;
    if (byte_1CE4 >= 0x40 && al >= 0xE1 && al <= 0xFA) {
      al = al & 0xDF;
    }
    // test al, al
    return al;
  }
}

// Extract 5 bits out of each byte.
// bit_buffer contains leftover bit buffer.
static uint8_t sub_1D8A(unsigned char **src_ptr)
{
  int counter = 5;
  int al = 0;
  int dl = num_bits;
  while (counter > 0) {
    dl--;
    if (dl < 0) {
      dl = **src_ptr;
      bit_buffer = dl;
      dl = 7;
      (*src_ptr)++;
      cpu.bx++; // Just for keeping track of how many bytes we advance.
    }
    // 0x1D96
    uint8_t tmp = bit_buffer;
    bit_buffer = bit_buffer << 1;

    // rcl al, 1
    int carry = 0;
    if (tmp > bit_buffer) {
      carry = 1;
    }
    al = al << 1;
    al += carry;
    counter--;
  }
  num_bits = dl; // leftover bits
  return al;
}

// 0x3191
// Append's a byte to string buffer. This may break a long string up into
// multiple lines.
static void append_string(unsigned char byte)
{
  uint8_t al;
  uint16_t ax, bx, len, dx;

  bx = ui_string.len;
  ui_string.bytes[bx] = byte;
  ui_string.len++;

  printf("%s: 0x%02X %c\n", __func__, byte, byte & 0x7F);
  if (byte == 0x8D) { // new line.
    printf("data len: %d\n", ui_string.len);
    printf("2697: 0x%04x\n", draw_rect.x);
    printf("2697: 0x%04x\n", draw_rect.y);
    printf("32BF: 0x%04x\n", draw_point.x);
    printf("32BF: 0x%04x\n", draw_point.y);
    ui_draw_string();
    return;
  }

  ax = bx;

  // Validate that string doesn't run past rectangle.
  ax += ui_get_byte_3236();
  if (ax < draw_rect.w)
    return;

  // String does run past rectangle, so break apart at space.
  // 0x31AE
  ui_string.len--;
  if (ui_string.len != 0) {
    len = bx;

    // 0x31B6
    while (len > 0) {
      al = ui_string.bytes[len];
      // search for previous space character.
      if (al == 0xA0) {
        bx = len;
        printf("%s: 0x31D2 %02d\n", __func__, len);
        if (len != 0) {
          // mov si, 320E
          uint16_t si = 0;

          // redraw only up to space (line break)
          while (len > 0) {
            // lodsb  al = ds:si [si++]
            al = ui_string.bytes[si++];
            ui_draw_chr_piece(al);
            len--;
          }
        }
        // 31E2
        ui_string.len++;
        len = 0;

        // 0x31E8
        bx++;
        while (bx < ui_string.len) {

          // 0x31EF
          // move string up.
          al = ui_string.bytes[bx];
          ui_string.bytes[len] = al;
          len++;
          bx++;
        }
        // 0x31FA
        dx = len;
        break;
      }
      len--;
    }
  }
  else {
    // 31C1 - length is 0.
    ui_draw_string();
    ui_string.bytes[0] = ui_string.bytes[bx];
    dx = 1;
  }
  // 0x31FC
  ui_string.len = dx;
  al = 0x8D;
  ui_draw_chr_piece(al);
  ui_set_byte_3236(draw_point.x);
}

static void sub_3165()
{
  word_3163 = ui_draw_chr_piece;
}

static void sub_316C()
{
  word_3163 = append_string;
}

static void sub_27E3()
{
  word_3163 = ui_header_set_byte;
  ui_header_reset();
  sub_1C79(&cpu.pc, cpu.pc - cpu.base_pc);
  sub_316C();
  sub_280E();
}

static void sub_280E()
{
  // check length against previous length (max length?)
  // XXX: Unknown.
  ui_header_draw();
  vga->update();
}

// 0x3150
static void sub_3150(unsigned char byte)
{
  word_3163(byte);
}

// 0x482D
static void read_header_bytes(void)
{
  sub_27E3();
}

// 0x3AA0
static void run_script(uint8_t script_index, uint16_t src_offset)
{
  int done = 0;
  uint8_t prev_op = 0;
  uint8_t op_code = 0;

  uint8_t cl = word_3AE8;
  cpu.cx = (cpu.cx & 0xFF00) | cl;

  push_word(cpu.cx);
  push_word(word_3ADB);
  push_word(saved_stack);
  saved_stack = cpu.sp;

  word_3AE8 = script_index;
  word_3AEA = script_index;
  populate_3ADD_and_3ADF();

  cpu.pc = running_script->bytes + src_offset;
  cpu.base_pc = running_script->bytes;

  while (!done) {
    prev_op = op_code;
    // es lodsb
    op_code = *cpu.pc++;
    // xor ah, ah
    cpu.ax = op_code;
    cpu.bx = cpu.ax;

    void (*callfunc)(void) = targets[op_code].func;
    if (callfunc != NULL) {
      callfunc();
      if (op_code == 0x5A)
        done = 1;

    } else {
      printf("OpenDW has reached an unhandled op code and will terminate.\n");
      printf("  Opcode: 0x%02X (Addr: %s), Previous op: 0x%02X\n", op_code,
          targets[op_code].src_offset, prev_op);
      exit(1);
      done = 1;
    }
  }
}

void run_engine()
{
  timers.timer3 = 1;

  game_state.unknown[8] = 0xFF;
  memset(&cpu, 0, sizeof(struct virtual_cpu));
  cpu.sp = STACK_SIZE; // stack grows downward...

  ui_set_background(0x0000); // Not correct.

  // load unknown data from COM file.
  data_2A68 = com_extract(0x2A68, 0x39);
  data_D760 = com_extract(0xD760, 0x700);
  data_1E21 = com_extract(0x1E21, 0xEF);

  // 0x1A6
  // Loads into 0x1887:0000
  const struct resource *code_res = resource_load(RESOURCE_SCRIPT);
  if (code_res == NULL)
  {
    printf("Failed to load unknown resource\n");
    return;
  }
  printf("Resource bytes: %zu\n", code_res->len);
  dump_hex(code_res->bytes, 0x80);

  // 0x3AA0
  run_script(code_res->index, 0);

  free(data_D760);
}

void set_game_state(int offset, unsigned char value)
{
  printf("%s - [%d] = 0x%02X\n", __func__, offset, value);
  game_state.unknown[offset] = value;
}

// 0x1ABD
// input will be 0x01 or 0x10
static void sub_1ABD(uint8_t val)
{
  uint8_t al, ah, bl;
  uint16_t fill_color; // 3713
  int si;

  cpu.ax = (cpu.ax & 0xFF00) | val;
  cpu.bx = cpu.ax;
  bl = cpu.bx & 0xFF;
  bl = bl & 0xF; // make sure it's not higher than 0xF.
  byte_1BE5 = bl; // color.

  ui_set_background(cpu.ax);
  bl = game_state.unknown[0x6];
  al = bl;
  al = al << 4;
  al += 0x20;
  draw_point.y = al;
  draw_point.x = 0x1B;
  cpu.bx = bl; // xor bh, bh
  cpu.ax = 0xC960;
  ah = (cpu.ax & 0xFF00) >> 8;

  ah += game_state.unknown[cpu.bx + 0xA]; // Character selector ?
  cpu.ax = (ah << 8) | (cpu.ax & 0xFF);
  word_1C63 = cpu.ax;
  al = game_state.unknown[6];
  if (al >= game_state.unknown[31]) {
    // 1AF6
    al = byte_1BE5;
    fill_color = al; // color ?
    al = draw_point.y;
    cpu.ax = al;
    g_linenum = cpu.ax; // line number
    word_36C0 = 0x36; // starting point
    word_36C2 = 0x4E; // ending point

    while (1) {
      ui_draw_solid_color(fill_color, word_36C0, word_36C2, g_linenum);
      g_linenum++;

      if ((g_linenum & 0x0F) == 0) {
        break;
      }
    }
    reset_ui_background();
    return;
  }
  // 1B22
  al = 0xC;
  cpu.ax = (cpu.ax & 0xFF00) | al;
  unsigned char *c960 = get_player_data_base();
  unsigned char *di = c960 + (word_1C63 - 0xC960);
  di--;

  // 1B29
  do {
    di++;
    cpu.ax--;
  } while ((*di & 0x80) == 0x80);

  // 1B30
  al = cpu.ax & 0xFF;
  uint8_t carry = al & 0x1;
  al = al >> 1;
  al = al + 0x1B + carry; // adc al, 0x1B
  cpu.ax = (cpu.ax & 0xFF00) | al;
  sub_1BE6();
  write_character_name();
  al = 0x27;
  cpu.ax = (cpu.ax & 0xFF00) | al;
  sub_1BE6();
  di = c960 + (word_1C63 - 0xC960);
  al = di[0x4C];

  si = 3;

  // 1B4A
  int found = 0;
  while (si >= 0) {
    uint8_t si_val = get_1BC1_table(si);
    if ((si_val & al) != 0) {
      found = 1;
      break; // 1B95
    }
    si--;
  }
  if (found == 0) {
    // 1B53 (not found)
    uint8_t dl = 2; // health
    cpu.bx = 0x14;
    al = 8;
    cpu.ax = (cpu.ax & 0xFF00) | al;
    sub_1BF8(dl, al);
    // 0x1B5D
    dl = 3; // stun
    cpu.bx = 0x18;
    al = 0x0B;
    cpu.ax = (cpu.ax & 0xFF00) | al;
    sub_1BF8(dl, al);
    dl = 4; // magic
    cpu.bx = 0x1C;
    al = 0x0E;
    sub_1BF8(dl, al);
    al = byte_1BE5;
    fill_color = al;
    g_linenum--;
    word_36C0 = 0x36;
    word_36C2 = 0x4E;
    // 0x1B87
    ui_draw_solid_color(fill_color, word_36C0, word_36C2, g_linenum);
    g_linenum -= 3;
    ui_draw_solid_color(fill_color, word_36C0, word_36C2, g_linenum);
    reset_ui_background();
    return;
  }
  // 1B95
  printf("%s 0x1B95 %d %d unimplemented\n", __func__, si, found);
  exit(1);
}

// 0x1A68
void reset_game_state()
{
  uint8_t al, ah;
  uint8_t val;

  memset(game_state.unknown + 0x18, 0, 7);
  if (sub_2752(0xB) == 1) {
    return;
  }

  al = draw_point.x;
  ah = draw_point.y;
  cpu.ax = (ah << 8) | al;
  push_word(cpu.ax);
  cpu.ax = (ah << 8) | game_state.unknown[6];
  push_word(cpu.ax);

  sub_3165();
  int counter = 6;
  while (counter >= 0) {
    al = game_state.unknown[0x18 + counter];
    if (al < 0x80) {
      set_game_state(6, counter & 0xFF);
      ah = 0;
      // si = ax

      if (al == 0) {
        val = 0x10;
      } else {
        val = 0x01;
      }
      al = val;
      sub_1ABD(al);
      set_game_state(0x18 + counter, 0xFF);
    }
    counter--;
  }
  cpu.ax = pop_word();
  set_game_state(6, cpu.ax & 0xFF);
  cpu.ax = pop_word();
  draw_point.y = (cpu.ax & 0xFF00) >> 8;
  draw_point.x = cpu.ax & 0xFF;
  sub_316C();
}

static int sub_1C57()
{
  uint16_t di = word_1C63;
  unsigned char *c960 = get_player_data_base();

  cpu.ax = c960[di - 0xC960 + cpu.bx];
  cpu.ax += (c960[di - 0xC960 + 1 + cpu.bx]) << 8;

  word_11C0 = cpu.ax;
  return cpu.ax;
}

static void sub_11A0()
{
  uint32_t result;

  word_11C4 = 0;
  cpu.ax = word_11C2;
  result = cpu.ax * word_11C0;
  word_11C6 = result & 0xFFFF;
  word_11C8 = (result & 0xFFFF0000) >> 16;
  // What is the point of this? multiplying by 0 ??
  cpu.ax = word_11C4;
  result = cpu.ax * word_11C0;
  word_11C8 += result & 0xFFFF;
}

static void sub_11CE()
{
  int old_carry = 0;
  int carry = 0;

  word_11CA = 0;
  word_11CC = 0;
  cpu.cx = 0x20; // 32 times.

  // 11DD
  for (uint16_t i = 0; i < cpu.cx; i++) {
    carry = word_11C6 & 0x8000 ? 1 : 0;
    word_11C6 = word_11C6 << 1;
    old_carry = carry;

    // rcl word_11C8, 1
    carry = word_11C8 & 0x8000 ? 1 : 0;
    word_11C8 = (word_11C8 << 1) + old_carry;
    old_carry = carry;

    carry = word_11CA & 0x8000 ? 1 : 0;
    word_11CA = (word_11CA << 1) + old_carry;
    old_carry = carry;

    carry = word_11CC & 0x8000 ? 1 : 0;
    word_11CC = (word_11CC << 1) + old_carry;
    old_carry = carry;

    printf("%s: 0x%04X 0x%04X 0x%04X 0x%04X\n", __func__, word_11C6, word_11C8, word_11CA, word_11CC);

    cpu.ax = word_11CA;
    uint16_t old16 = cpu.ax;
    cpu.ax -= word_11C0;
    if (cpu.ax > old16) {
      carry = 1;
    } else {
      carry = 0;
    }
    cpu.bx = word_11CC;
    old16 = cpu.bx;
    cpu.bx -= carry;

    if (cpu.bx > old16) {
      carry = 1;
    } else {
      carry = 0;
    }

    if (carry != 1) {
      word_11CA = cpu.ax;
      word_11CC = cpu.bx;
      word_11C6++;
    }
  }
}

static void sub_1C49(uint16_t fill_color)
{
  g_linenum++;
  ui_draw_solid_color(fill_color, word_36C0, word_36C2, g_linenum);
  g_linenum--;
  ui_draw_solid_color(fill_color, word_36C0, word_36C2, g_linenum);
}

static void sub_1BF8(uint8_t color, uint8_t y_adjust)
{
  uint16_t fill_color;

  fill_color = color;
  cpu.ax = draw_point.y + y_adjust;
  g_linenum = cpu.ax; // line number
  if (sub_1C57() != 0) {
    cpu.bx += 2;
    push_word(cpu.bx);
    word_11C2 = 0x17;
    sub_11A0();
    cpu.bx = pop_word();
    if (sub_1C57() != 0) {
      sub_11CE();
      cpu.ax = word_11C6;
      cpu.ax++;
    }
  }
  cpu.ax += 0x36;
  word_36C2 = cpu.ax;
  word_36C0 = 0x36;
  sub_1C49(fill_color);
  cpu.ax = word_36C2;
  if (cpu.ax == 0x4E) {
    return;
  }

  // 1C3A
  word_36C0 = cpu.ax;
  word_36C2 = 0x4E;
  fill_color = byte_1BE5; // color.
  sub_1C49(fill_color);
}

