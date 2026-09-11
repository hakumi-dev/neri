#include "neri/runtime_abi.h"
#include "../runtime/terminal.h"
#include <windows.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static HANDLE input, output;
static DWORD saved_input, saved_output;
static int active, registered;
static volatile LONG interrupted;
static int64_t generation;
int neri_terminal_active(void) { return active; }
static unsigned char pending[16];
static size_t pending_count;
static BOOL WINAPI interrupt_session(DWORD event) {
  if (event != CTRL_C_EVENT && event != CTRL_BREAK_EVENT) return FALSE;
  InterlockedExchange(&interrupted, 1);
  return TRUE;
}
void neri_terminal_restore(void) {
  if (!active) return;
  const char reset[] = "\033[0m\033[?25h\033[?1049l";
  DWORD written;
  WriteFile(output, reset, sizeof(reset) - 1, &written, NULL);
  SetConsoleMode(input, saved_input);
  SetConsoleMode(output, saved_output);
  SetConsoleCtrlHandler(interrupt_session, FALSE);
  pending_count = 0;
  active = 0;
}
int64_t neri_rt_v1_terminal_open(void) {
  if (active || neri_interrupt_active() || generation == INT64_MAX) return 0;
  input = GetStdHandle(STD_INPUT_HANDLE);
  output = GetStdHandle(STD_OUTPUT_HANDLE);
  if (!GetConsoleMode(input, &saved_input) || !GetConsoleMode(output, &saved_output)) return 0;
  if (!registered) {
    if (atexit(neri_terminal_restore)) return 0;
    registered = 1;
  }
  if (!SetConsoleMode(output, saved_output | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) return 0;
  // ReadConsoleInputW supplies key events; this adapter performs VT encoding.
  if (!SetConsoleMode(input, (saved_input & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_QUICK_EDIT_MODE | ENABLE_VIRTUAL_TERMINAL_INPUT)) | ENABLE_EXTENDED_FLAGS)) {
    SetConsoleMode(output, saved_output); return 0;
  }
  if (!SetConsoleCtrlHandler(interrupt_session, TRUE)) {
    SetConsoleMode(input, saved_input); SetConsoleMode(output, saved_output); return 0;
  }
  active = 1;
  pending_count = 0;
  InterlockedExchange(&interrupted, 0);
  ++generation;
  const char enter[] = "\033[?1049h\033[?25l\033[2J\033[H";
  DWORD written;
  WriteFile(output, enter, sizeof(enter) - 1, &written, NULL);
  return generation;
}
void neri_rt_v1_terminal_close(int64_t token) {
  if (active && token == generation) neri_terminal_restore();
}
int64_t neri_rt_v1_terminal_read(int64_t token, int64_t timeout) {
  if (!active || token != generation || interrupted || timeout < 0 || timeout > 60000) return -2;
  if (pending_count) {
    const int64_t result = pending[0];
    memmove(pending, pending + 1, --pending_count);
    return result;
  }
  const ULONGLONG deadline = GetTickCount64() + (ULONGLONG)timeout;
  do {
    DWORD wait = WaitForSingleObject(input, timeout > 10 ? 10 : (DWORD)timeout);
    if (interrupted) return -2;
    if (wait == WAIT_FAILED) return -2;
    if (wait == WAIT_OBJECT_0) {
      INPUT_RECORD record;
      DWORD count;
      if (!ReadConsoleInputW(input, &record, 1, &count) || count != 1) return -2;
      if (record.EventType == KEY_EVENT && record.Event.KeyEvent.bKeyDown) {
        const KEY_EVENT_RECORD key = record.Event.KeyEvent;
        switch (key.wVirtualKeyCode) {
          case VK_UP:    pending[0] = 27; pending[1] = 91; pending[2] = 65; pending_count = 3; break;
          case VK_DOWN:  pending[0] = 27; pending[1] = 91; pending[2] = 66; pending_count = 3; break;
          case VK_RIGHT: pending[0] = 27; pending[1] = 91; pending[2] = 67; pending_count = 3; break;
          case VK_LEFT:  pending[0] = 27; pending[1] = 91; pending[2] = 68; pending_count = 3; break;
          default:
            if (key.uChar.UnicodeChar) {
              char utf8[4];
              const int length = WideCharToMultiByte(CP_UTF8, 0, &key.uChar.UnicodeChar, 1, utf8, sizeof(utf8), NULL, NULL);
              if (length > 0) {
                memcpy(pending, utf8, (size_t)length);
                pending_count = (size_t)length;
              }
            }
            break;
        }
        if (pending_count) {
          const int64_t result = pending[0];
          memmove(pending, pending + 1, --pending_count);
          return result;
        }
      }
    }
  } while (GetTickCount64() < deadline);
  return -1;
}
int64_t neri_rt_v1_terminal_size(int64_t token, int64_t rows) {
  CONSOLE_SCREEN_BUFFER_INFO info;
  if (!active || token != generation || !GetConsoleScreenBufferInfo(output, &info)) return 0;
  return rows ? info.srWindow.Bottom - info.srWindow.Top + 1 : info.srWindow.Right - info.srWindow.Left + 1;
}
int64_t neri_rt_v1_clock_milliseconds(void) { return (int64_t)GetTickCount64(); }
