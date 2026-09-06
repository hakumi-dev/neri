#include "neri/runtime_abi.h"
#include <SDL3/SDL.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* SDL layouts and 32-bit values stay on this side of the scalar ABI. */
static SDL_Window *window;
static SDL_Renderer *renderer;
static int64_t generation;
static int registered;
static char error[512];

static bool fail(const char *message) {
  SDL_strlcpy(error, message, sizeof(error));
  return false;
}
static bool valid(int64_t token) { return window && token == generation; }
static void cleanup(void) {
  if (renderer) SDL_DestroyRenderer(renderer);
  if (window) SDL_DestroyWindow(window);
  renderer = NULL;
  window = NULL;
  SDL_QuitSubSystem(SDL_INIT_VIDEO);
}
static bool result(bool ok) { return ok || fail(SDL_GetError()); }
static bool coordinate(double value) { return isfinite(value) && fabs(value) <= 1000000; }

int64_t neri_rt_v1_window_open(const uint8_t *title, int64_t width, int64_t height) {
  if (window) { fail("A window loop is already active"); return 0; }
  error[0] = 0;
  if (!title || width < 1 || width > 16384 || height < 1 || height > 16384 || generation == INT64_MAX) {
    fail("Invalid window options"); return 0;
  }
  if (!registered) {
    if (atexit(cleanup) != 0) { fail("Cannot register window cleanup"); return 0; }
    registered = 1;
  }
  if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) { fail(SDL_GetError()); return 0; }
  if (!SDL_CreateWindowAndRenderer((const char *)title, (int)width, (int)height,
                                  SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY,
                                  &window, &renderer)) {
    fail(SDL_GetError()); cleanup(); return 0;
  }
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  return ++generation;
}
void neri_rt_v1_window_close(int64_t token) { if (valid(token)) cleanup(); }
int64_t neri_rt_v1_window_error(uint8_t *buffer, int64_t capacity) {
  if (!buffer || capacity <= 0) return 0;
  size_t length = strlen(error);
  if (length > (uint64_t)capacity) length = (size_t)capacity;
  memcpy(buffer, error, length);
  return (int64_t)length;
}
static int key(SDL_Scancode code) {
  if (code >= SDL_SCANCODE_A && code <= SDL_SCANCODE_Z) return code - SDL_SCANCODE_A;
  switch (code) {
    case SDL_SCANCODE_UP: return 26;
    case SDL_SCANCODE_DOWN: return 27;
    case SDL_SCANCODE_LEFT: return 28;
    case SDL_SCANCODE_RIGHT: return 29;
    case SDL_SCANCODE_SPACE: return 30;
    case SDL_SCANCODE_ESCAPE: return 31;
    case SDL_SCANCODE_RETURN: return 32;
    default: return -1;
  }
}
int64_t neri_rt_v1_window_poll(int64_t token) {
  if (!valid(token)) return 1;
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) return 1;
    if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) return 2;
    if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
      int code = key(event.key.scancode);
      if (code >= 0 && !event.key.repeat) return (event.key.down ? 100 : 200) + code;
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
      int code = event.button.button == SDL_BUTTON_LEFT ? 33 :
                 event.button.button == SDL_BUTTON_MIDDLE ? 34 :
                 event.button.button == SDL_BUTTON_RIGHT ? 35 : -1;
      if (code >= 0) return (event.button.down ? 100 : 200) + code;
    }
  }
  return 0;
}
int64_t neri_rt_v1_window_size(int64_t token, int64_t axis) {
  int width, height;
  if (!valid(token) || !SDL_GetRenderOutputSize(renderer, &width, &height)) return 0;
  return axis ? height : width;
}
double neri_rt_v1_window_mouse(int64_t token, int64_t axis) {
  float x, y, output_x, output_y;
  if (!valid(token)) return 0;
  SDL_GetMouseState(&x, &y);
  if (!SDL_RenderCoordinatesFromWindow(renderer, x, y, &output_x, &output_y)) return 0;
  return axis ? output_y : output_x;
}
bool neri_rt_v1_window_color(int64_t token, int64_t red, int64_t green, int64_t blue, int64_t alpha) {
  if (!valid(token)) return fail("Window is closed");
  if (red < 0 || red > 255 || green < 0 || green > 255 || blue < 0 || blue > 255 || alpha < 0 || alpha > 255)
    return fail("Color channels must be between 0 and 255");
  return result(SDL_SetRenderDrawColor(renderer, (Uint8)red, (Uint8)green, (Uint8)blue, (Uint8)alpha));
}
bool neri_rt_v1_window_clear(int64_t token) {
  return valid(token) ? result(SDL_RenderClear(renderer)) : fail("Window is closed");
}
bool neri_rt_v1_window_rect(int64_t token, double x, double y, double width, double height) {
  if (!valid(token)) return fail("Window is closed");
  if (!coordinate(x) || !coordinate(y) || !coordinate(width) || !coordinate(height) || width < 0 || height < 0)
    return fail("Invalid rectangle");
  const SDL_FRect rect = {(float)x, (float)y, (float)width, (float)height};
  return result(SDL_RenderFillRect(renderer, &rect));
}
bool neri_rt_v1_window_line(int64_t token, double x1, double y1, double x2, double y2) {
  if (!valid(token)) return fail("Window is closed");
  if (!coordinate(x1) || !coordinate(y1) || !coordinate(x2) || !coordinate(y2)) return fail("Invalid line");
  return result(SDL_RenderLine(renderer, (float)x1, (float)y1, (float)x2, (float)y2));
}
bool neri_rt_v1_window_text(int64_t token, double x, double y, const uint8_t *text) {
  if (!valid(token)) return fail("Window is closed");
  if (!coordinate(x) || !coordinate(y) || !text) return fail("Invalid text");
  return result(SDL_RenderDebugText(renderer, (float)x, (float)y, (const char *)text));
}
bool neri_rt_v1_window_present(int64_t token) {
  return valid(token) ? result(SDL_RenderPresent(renderer)) : fail("Window is closed");
}
void neri_rt_v1_window_delay(int64_t milliseconds) {
  if (milliseconds > 0 && milliseconds <= 1000) SDL_Delay((Uint32)milliseconds);
}
