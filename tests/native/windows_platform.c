#include "neri/runtime_abi.h"

#include <winsock2.h>
#include <windows.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(condition)                                                     \
  do {                                                                       \
    if (!(condition)) {                                                      \
      fprintf(stderr, "windows platform check failed: %s (%s:%d)\n",       \
              #condition, __FILE__, __LINE__);                              \
      ++failures;                                                            \
    }                                                                        \
  } while (0)

static int64_t open_listener(int *port) {
  for (*port = 43000; *port < 43100; ++*port) {
    const int64_t fd = neri_rt_v1_net_open();
    if (fd < 0) return -1;
    if (neri_rt_v1_net_configure(fd) == 0 &&
        neri_rt_v1_net_bind(fd, *port) == 0 &&
        neri_rt_v1_net_listen(fd) == 0)
      return fd;
    neri_rt_v1_net_close(fd);
  }
  return -1;
}

static void check_sockets(void) {
  int port = 0;
  const int64_t listener = open_listener(&port);
  CHECK(listener >= 0);
  if (listener < 0) return;

  const int64_t client = neri_rt_v1_net_open();
  CHECK(client >= 0);
  if (client < 0) {
    neri_rt_v1_net_close(listener);
    return;
  }
  CHECK(neri_rt_v1_net_configure(client) == 0);
  const int64_t connect_result = neri_rt_v1_net_connect(client, port);
  CHECK(connect_result == 0 || connect_result == -2);
  CHECK(neri_rt_v1_net_poll(listener, 0, 1000) > 0);
  const int64_t accepted = neri_rt_v1_net_accept(listener);
  CHECK(accepted >= 0);
  if (accepted >= 0) {
    CHECK(neri_rt_v1_net_configure(accepted) == 0);
    CHECK(neri_rt_v1_net_poll(accepted, 0, 20) == 0);
    uint8_t outgoing[] = {'n', 'e', 'r', 'i'};
    CHECK(neri_rt_v1_net_write(client, outgoing, sizeof(outgoing)) == 4);
    CHECK(neri_rt_v1_net_poll(accepted, 0, 1000) > 0);
    uint8_t incoming[sizeof(outgoing)] = {0};
    CHECK(neri_rt_v1_net_read(accepted, incoming, sizeof(incoming)) == 4);
    CHECK(memcmp(incoming, outgoing, sizeof(outgoing)) == 0);
    neri_rt_v1_net_close(accepted);
  }
  CHECK(neri_rt_v1_net_bind(listener, 0) == -1);
  uint8_t message[128] = {0};
  CHECK(neri_rt_v1_net_error(message, sizeof(message)) > 0);
  neri_rt_v1_net_close(client);
  neri_rt_v1_net_close(listener);
}

static void check_redirected_terminal(void) {
  /* CTest has no interactive console. Redirected handles must fail cleanly
   * and must not alter either stream's state. */
  HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
  HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
  DWORD input_mode = 0, output_mode = 0;
  const BOOL input_console = input && input != INVALID_HANDLE_VALUE &&
                             GetConsoleMode(input, &input_mode);
  const BOOL output_console = output && output != INVALID_HANDLE_VALUE &&
                              GetConsoleMode(output, &output_mode);
  const int64_t token = neri_rt_v1_terminal_open();
  if (!input_console || !output_console) CHECK(token == 0);
  if (token > 0) {
    CHECK(neri_rt_v1_terminal_size(token, 0) > 0);
    CHECK(neri_rt_v1_terminal_size(token, 1) > 0);
    neri_rt_v1_terminal_close(token);
    CHECK(neri_rt_v1_terminal_read(token, 0) == -2);
  }
}

static void check_interactive_terminal(void) {
  const HANDLE old_input = GetStdHandle(STD_INPUT_HANDLE);
  const HANDLE old_output = GetStdHandle(STD_OUTPUT_HANDLE);
  FreeConsole();
  if (!AllocConsole()) {
    CHECK(0);
    SetStdHandle(STD_INPUT_HANDLE, old_input);
    SetStdHandle(STD_OUTPUT_HANDLE, old_output);
    return;
  }
  const HWND window = GetConsoleWindow();
  if (window) ShowWindow(window, SW_HIDE);
  HANDLE input = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                             OPEN_EXISTING, 0, NULL);
  HANDLE output = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                              OPEN_EXISTING, 0, NULL);
  if (input == INVALID_HANDLE_VALUE || output == INVALID_HANDLE_VALUE) {
    CHECK(0);
    if (input != INVALID_HANDLE_VALUE) CloseHandle(input);
    if (output != INVALID_HANDLE_VALUE) CloseHandle(output);
    FreeConsole();
    SetStdHandle(STD_INPUT_HANDLE, old_input);
    SetStdHandle(STD_OUTPUT_HANDLE, old_output);
    return;
  }
  SetStdHandle(STD_INPUT_HANDLE, input);
  SetStdHandle(STD_OUTPUT_HANDLE, output);
  DWORD before_input = 0, before_output = 0, after_input = 0, after_output = 0;
  CHECK(GetConsoleMode(input, &before_input));
  CHECK(GetConsoleMode(output, &before_output));
  const int64_t token = neri_rt_v1_terminal_open();
  CHECK(token > 0);
  if (token > 0) {
    DWORD test_mode = 0;
    CHECK(GetConsoleMode(input, &test_mode));
    CHECK((test_mode & ENABLE_VIRTUAL_TERMINAL_INPUT) == 0);
    INPUT_RECORD records[2] = {0};
    records[0].EventType = KEY_EVENT;
    records[0].Event.KeyEvent.bKeyDown = TRUE;
    records[0].Event.KeyEvent.wRepeatCount = 1;
    records[0].Event.KeyEvent.wVirtualKeyCode = VK_UP;
    records[0].Event.KeyEvent.wVirtualScanCode = 0x48;
    records[1].EventType = KEY_EVENT;
    records[1].Event.KeyEvent.bKeyDown = TRUE;
    records[1].Event.KeyEvent.wRepeatCount = 1;
    records[1].Event.KeyEvent.wVirtualKeyCode = 'w';
    records[1].Event.KeyEvent.uChar.UnicodeChar = L'w';
    DWORD written = 0;
    const BOOL flushed = FlushConsoleInputBuffer(input);
    CHECK(flushed);
    CHECK(WriteConsoleInputW(input, records, 2, &written) && written == 2);
    const int64_t first = neri_rt_v1_terminal_read(token, 1000);
    const int64_t second = neri_rt_v1_terminal_read(token, 1000);
    const int64_t third = neri_rt_v1_terminal_read(token, 1000);
    CHECK(first == 27);
    CHECK(second == 91);
    CHECK(third == 65);
    const int64_t letter = neri_rt_v1_terminal_read(token, 1000);
    CHECK(letter == 'w');
    const int64_t started = neri_rt_v1_clock_milliseconds();
    const int64_t idle = neri_rt_v1_terminal_read(token, 20);
    CHECK(idle == -1);
    const int64_t elapsed = neri_rt_v1_clock_milliseconds() - started;
    CHECK(elapsed >= 15);
    neri_rt_v1_terminal_close(token);
    CHECK(GetConsoleMode(input, &after_input));
    CHECK(GetConsoleMode(output, &after_output));
    CHECK(before_input == after_input);
    CHECK(before_output == after_output);
    CHECK(neri_rt_v1_terminal_read(token, 0) == -2);
  }
  CloseHandle(input);
  CloseHandle(output);
  FreeConsole();
  SetStdHandle(STD_INPUT_HANDLE, old_input);
  SetStdHandle(STD_OUTPUT_HANDLE, old_output);
}

int main(void) {
  check_sockets();
  check_redirected_terminal();
  check_interactive_terminal();
  if (failures) return 1;
  puts("Windows socket and terminal contracts passed");
  return 0;
}
