#include <coreinit/memdefaultheap.h>
#include <coreinit/memory.h>
#include <coreinit/screen.h>
#include <coreinit/time.h>
#include <vpad/input.h>
#include <whb/proc.h>
#include <whb/log.h>
#include <whb/log_udp.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define GRID_W 40
#define GRID_H 28

typedef struct { int x, y; } Cell;

static void* tvBuf = NULL;
static void* drcBuf = NULL;

static void screen_init(void) {
  OSScreenInit();
  int tvSize  = OSScreenGetBufferSizeEx(0);
  int drcSize = OSScreenGetBufferSizeEx(1);
  tvBuf  = MEMAllocFromDefaultHeapEx(tvSize,  0x40);
  drcBuf = MEMAllocFromDefaultHeapEx(drcSize, 0x40);
  OSScreenSetBufferEx(0, tvBuf);
  OSScreenSetBufferEx(1, drcBuf);
OSScreenEnableEx(0, 1);   // TV on
OSScreenEnableEx(1, 1);   // GamePad on


// prime the buffers once so Cemu shows a frame
OSScreenClearBufferEx(0, 0);
OSScreenClearBufferEx(1, 0);
OSScreenFlipBuffersEx(0);
OSScreenFlipBuffersEx(1);
}

static void screen_shutdown(void) {
  OSScreenShutdown();
  if (tvBuf)  { MEMFreeToDefaultHeap(tvBuf);  tvBuf  = NULL; }
  if (drcBuf) { MEMFreeToDefaultHeap(drcBuf); drcBuf = NULL; }
}

static void clear_and_flip(void) {
  OSScreenFlipBuffersEx(0);
  OSScreenFlipBuffersEx(1);
}

static void cls(void) {
  OSScreenClearBufferEx(0, 0);
  OSScreenClearBufferEx(1, 0);
}

static void putxy(int x, int y, const char* s) {
  OSScreenPutFontEx(0, x, y, s);
  OSScreenPutFontEx(1, x, y, s);
}

static uint64_t now_ticks(void) { return OSGetTime(); }
static double   ms_from_ticks(uint64_t t) { return OSTicksToMilliseconds(t); }

static Cell snake[GRID_W * GRID_H];
static int  length = 0;
static int  dirx = 1, diry = 0;
static Cell food;
static bool alive = true;
static int  score = 0;

static bool equals(Cell a, Cell b) { return a.x == b.x && a.y == b.y; }

static bool inside(int x, int y) {
  return x > 0 && x < GRID_W - 1 && y > 0 && y < GRID_H - 1;
}

static bool occupies_snake(Cell c, int upto) {
  for (int i = 0; i < upto; ++i)
    if (equals(snake[i], c)) return true;
  return false;
}

static void place_food(void) {
  for (;;) {
    Cell c = { 1 + rand() % (GRID_W - 2), 1 + rand() % (GRID_H - 2) };
    if (!occupies_snake(c, length)) { food = c; return; }
  }
}

static void new_game(void) {
  length = 3;
  dirx = 1; diry = 0;
  int cx = GRID_W/2, cy = GRID_H/2;
  snake[0] = (Cell){cx, cy};
  snake[1] = (Cell){cx-1, cy};
  snake[2] = (Cell){cx-2, cy};
  alive = true;
  score = 0;
  place_food();
}

static void draw_world(void) {
  cls();

  char header[64];
  snprintf(header, sizeof header, "SNAKE  Score:%d   [+]=Exit  [A]=Restart", score);
  putxy(0, 0, header);

  for (int x = 0; x < GRID_W; ++x) putxy(x, 1, "#");
  for (int y = 2; y < GRID_H; ++y) {
    putxy(0, y, "#");
    for (int x = 1; x < GRID_W - 1; ++x) putxy(x, y, " ");
    putxy(GRID_W - 1, y, "#");
  }
  for (int x = 0; x < GRID_W; ++x) putxy(x, GRID_H, "#");

  putxy(food.x, food.y + 1, "*");

  for (int i = length - 1; i >= 0; --i) {
    putxy(snake[i].x, snake[i].y + 1, (i == 0) ? "O" : "o");
  }

  if (!alive) {
    putxy(2, GRID_H + 2, "Game Over. Press A to restart, + to exit.");
  }

  clear_and_flip();
}

static void step_logic(void) {
  if (!alive) return;

  Cell tail_prev = snake[length - 1];
  for (int i = length - 1; i > 0; --i) snake[i] = snake[i - 1];

  snake[0].x += dirx;
  snake[0].y += diry;

  if (!inside(snake[0].x, snake[0].y)) { alive = false; return; }

  for (int i = 1; i < length; ++i)
    if (equals(snake[0], snake[i])) { alive = false; return; }

  if (equals(snake[0], food)) {
    snake[length] = tail_prev;
    length++;
    score += 10;
    place_food();
  }
}

int main(void) {
  WHBProcInit();
  WHBLogUdpInit();
  srand((unsigned)OSGetTime());

  screen_init();
  new_game();
  draw_world();

  uint64_t last = now_ticks();
  double accumulator_ms = 0.0;
  double step_ms_base = 140.0;
  const double step_ms_min  = 70.0;

  while (WHBProcIsRunning()) {
    VPADStatus v; VPADReadError err;
    VPADRead(0, &v, 1, &err);
    if (v.trigger & VPAD_BUTTON_PLUS) break;
    if (v.trigger & VPAD_BUTTON_A) {
      if (!alive) { new_game(); draw_world(); }
    }
    if (v.trigger & VPAD_BUTTON_LEFT  && dirx != 1)  { dirx = -1; diry = 0; }
    if (v.trigger & VPAD_BUTTON_RIGHT && dirx != -1) { dirx =  1; diry = 0; }
    if (v.trigger & VPAD_BUTTON_UP    && diry != 1)  { dirx = 0; diry = -1; }
    if (v.trigger & VPAD_BUTTON_DOWN  && diry != -1) { dirx = 0; diry =  1; }

    uint64_t now = now_ticks();
    double dt_ms = ms_from_ticks(now - last);
    last = now;
    accumulator_ms += dt_ms;

    double speedup = (length - 3) * 2.0;
    double step_ms = step_ms_base - speedup;
    if (step_ms < step_ms_min) step_ms = step_ms_min;

    while (accumulator_ms >= step_ms) {
      accumulator_ms -= step_ms;
      step_logic();
      draw_world();
    }
  }

  screen_shutdown();
  WHBLogUdpDeinit();
  WHBProcShutdown();
  return 0;
}
