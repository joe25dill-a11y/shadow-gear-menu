#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <string>

namespace {

std::wstring LogPath() {
  wchar_t path[MAX_PATH] = {};
  HMODULE self = nullptr;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCWSTR>(&LogPath), &self)) {
    return L"shadow-gear-menu.log";
  }
  const DWORD length = GetModuleFileNameW(self, path, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) {
    return L"shadow-gear-menu.log";
  }
  wchar_t* slash = wcsrchr(path, L'\\');
  if (slash == nullptr) {
    return L"shadow-gear-menu.log";
  }
  wcscpy_s(slash + 1, MAX_PATH - static_cast<size_t>(slash + 1 - path), L"shadow-gear-menu.log");
  return path;
}

void EnsureConsole() {
  static bool ready = false;
  if (ready) {
    return;
  }
  ready = true;
  const HWND game = GetForegroundWindow();
  if (!AllocConsole()) {
    return;
  }
  SetConsoleTitleW(L"Sky's Shadow log");
  HWND console = GetConsoleWindow();
  if (console != nullptr) {
    HMENU menu = GetSystemMenu(console, FALSE);
    if (menu != nullptr) {
      DeleteMenu(menu, SC_CLOSE, MF_BYCOMMAND);
    }
    SetWindowPos(console, HWND_BOTTOM, 30, 30, 720, 420, SWP_NOACTIVATE);
    ShowWindow(console, SW_SHOWNOACTIVATE);
  }
  const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
  const wchar_t* header =
      L"Sky's Shadow log\r\n"
      L"This window stays behind the game.\r\n"
      L"Numpad 1 opens the menu and leaves it open.\r\n"
      L"Home rechecks the switches already in the menu.\r\n\r\n";
  if (out != nullptr && out != INVALID_HANDLE_VALUE) {
    DWORD written = 0;
    WriteConsoleW(out, header, static_cast<DWORD>(wcslen(header)), &written, nullptr);
  }
  if (game != nullptr && game != console) {
    SetForegroundWindow(game);
  }
}

void WriteConsoleLine(const wchar_t* line) {
  EnsureConsole();
  const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
  if (out == nullptr || out == INVALID_HANDLE_VALUE) {
    return;
  }
  DWORD written = 0;
  WriteConsoleW(out, line, static_cast<DWORD>(wcslen(line)), &written, nullptr);
  WriteConsoleW(out, L"\r\n", 2, &written, nullptr);
}

}  // namespace

void CloseLogConsole() {}

void LogLine(const wchar_t* line) {
  if (line == nullptr) {
    return;
  }
  WriteConsoleLine(line);
  const std::wstring path = LogPath();
  const HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return;
  }
  char buffer[1024] = {};
  const int written =
      WideCharToMultiByte(CP_UTF8, 0, line, -1, buffer, static_cast<int>(sizeof(buffer) - 2), nullptr, nullptr);
  if (written > 1) {
    buffer[written - 1] = '\n';
    DWORD ignored = 0;
    WriteFile(file, buffer, static_cast<DWORD>(written), &ignored, nullptr);
  }
  CloseHandle(file);
}

void Logf(const wchar_t* fmt, ...) {
  wchar_t buffer[512] = {};
  va_list args;
  va_start(args, fmt);
  vswprintf_s(buffer, fmt, args);
  va_end(args);
  LogLine(buffer);
}
