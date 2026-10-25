#include "gear_window.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>

#include <cstdio>
#include <string>

namespace {

constexpr int kSlotCount = 5;
constexpr int kIdTitle = 10;
constexpr int kIdSubtitle = 11;
constexpr int kIdList = 100;
constexpr int kIdStatus = 101;
constexpr int kIdSetLevel = 201;
constexpr int kIdSetRarity = 202;
constexpr int kIdSetGem = 203;

constexpr wchar_t kClassName[] = L"ShadowGearMenuWindow";
const wchar_t* kSlotNames[kSlotCount] = {L"Sword", L"Dagger", L"Bow", L"Armor", L"Cloak"};

struct SlotRow {
  wchar_t name[64];
  wchar_t level[16];
  wchar_t rarity[24];
  wchar_t gem[32];
};

HINSTANCE g_instance = nullptr;
HWND g_window = nullptr;
HWND g_list = nullptr;
HWND g_status = nullptr;
HFONT g_title_font = nullptr;
HFONT g_body_font = nullptr;
HBRUSH g_background = nullptr;
SlotRow g_rows[kSlotCount] = {};
bool g_visible = false;

void LogLine(const wchar_t* line) {
  wchar_t path[MAX_PATH] = {};
  HMODULE self = nullptr;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                              GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCWSTR>(&LogLine), &self)) {
    return;
  }
  const DWORD length = GetModuleFileNameW(self, path, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) {
    return;
  }
  wchar_t* slash = wcsrchr(path, L'\\');
  if (slash == nullptr) {
    return;
  }
  wcscpy_s(slash + 1, MAX_PATH - static_cast<size_t>(slash + 1 - path), L"shadow-gear-menu.log");

  const HANDLE file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return;
  }
  char buffer[512] = {};
  const int written = WideCharToMultiByte(CP_UTF8, 0, line, -1, buffer, static_cast<int>(sizeof(buffer) - 2),
                                          nullptr, nullptr);
  if (written > 1) {
    buffer[written - 1] = '\n';
    buffer[written] = '\0';
    DWORD ignored = 0;
    WriteFile(file, buffer, static_cast<DWORD>(written), &ignored, nullptr);
  }
  CloseHandle(file);
}

void SetStatus(const wchar_t* text) {
  if (g_status != nullptr) {
    SetWindowTextW(g_status, text);
  }
}

void FillPlaceholderRows() {
  for (int i = 0; i < kSlotCount; ++i) {
    wcscpy_s(g_rows[i].name, L"—");
    wcscpy_s(g_rows[i].level, L"—");
    wcscpy_s(g_rows[i].rarity, L"—");
    wcscpy_s(g_rows[i].gem, L"—");
  }
}

void RefreshList() {
  if (g_list == nullptr) {
    return;
  }
  ListView_DeleteAllItems(g_list);
  for (int i = 0; i < kSlotCount; ++i) {
    LVITEMW item = {};
    item.mask = LVIF_TEXT;
    item.iItem = i;
    item.pszText = const_cast<wchar_t*>(kSlotNames[i]);
    ListView_InsertItem(g_list, &item);
    ListView_SetItemText(g_list, i, 1, g_rows[i].name);
    ListView_SetItemText(g_list, i, 2, g_rows[i].level);
    ListView_SetItemText(g_list, i, 3, g_rows[i].rarity);
    ListView_SetItemText(g_list, i, 4, g_rows[i].gem);
  }
}

HWND FindGameWindow() {
  HWND found = nullptr;
  EnumWindows(
      [](HWND hwnd, LPARAM param) -> BOOL {
        if (!IsWindowVisible(hwnd)) {
          return TRUE;
        }
        wchar_t title[256] = {};
        GetWindowTextW(hwnd, title, 256);
        if (wcsstr(title, L"Shadow of War") == nullptr) {
          return TRUE;
        }
        *reinterpret_cast<HWND*>(param) = hwnd;
        return FALSE;
      },
      reinterpret_cast<LPARAM>(&found));
  return found;
}

void CenterOnGame() {
  RECT ours = {};
  GetWindowRect(g_window, &ours);
  const int width = ours.right - ours.left;
  const int height = ours.bottom - ours.top;
  const HWND game = FindGameWindow();
  RECT target = {};
  if (game != nullptr && GetWindowRect(game, &target)) {
    // Prefer the game window.
  } else {
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &target, 0);
  }
  const int x = target.left + ((target.right - target.left) - width) / 2;
  const int y = target.top + ((target.bottom - target.top) - height) / 2;
  SetWindowPos(g_window, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
}

void ShowMenu(bool show) {
  g_visible = show;
  if (show) {
    CenterOnGame();
    ShowWindow(g_window, SW_SHOWNA);
    SetWindowPos(g_window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    LogLine(L"Menu shown");
  } else {
    ShowWindow(g_window, SW_HIDE);
    LogLine(L"Menu hidden");
  }
}

void OnGearAction(const wchar_t* action) {
  wchar_t message[192] = {};
  swprintf_s(message, L"%s is not available yet. Worn gear is not connected.", action);
  SetStatus(message);
  LogLine(message);
}

void Layout(int client_width, int client_height) {
  const int margin = 16;
  const int title_height = 28;
  const int subtitle_height = 36;
  const int button_height = 28;
  const int status_height = 40;
  const int button_top = client_height - margin - status_height - 8 - button_height;
  const int list_top = margin + title_height + 8 + subtitle_height + 8;
  const int list_height = button_top - 12 - list_top;
  const int button_width = 140;
  const HWND title = GetDlgItem(g_window, kIdTitle);
  const HWND subtitle = GetDlgItem(g_window, kIdSubtitle);
  if (title != nullptr) {
    MoveWindow(title, margin, margin, client_width - margin * 2, title_height, TRUE);
  }
  if (subtitle != nullptr) {
    MoveWindow(subtitle, margin, margin + title_height + 4, client_width - margin * 2, subtitle_height, TRUE);
  }
  if (g_list != nullptr) {
    MoveWindow(g_list, margin, list_top, client_width - margin * 2, list_height, TRUE);
  }
  const int buttons_width = button_width * 3 + 16;
  int button_x = margin;
  if (buttons_width < client_width - margin * 2) {
    button_x = margin;
  }
  MoveWindow(GetDlgItem(g_window, kIdSetLevel), button_x, button_top, button_width, button_height, TRUE);
  MoveWindow(GetDlgItem(g_window, kIdSetRarity), button_x + button_width + 8, button_top, button_width,
             button_height, TRUE);
  MoveWindow(GetDlgItem(g_window, kIdSetGem), button_x + (button_width + 8) * 2, button_top, button_width,
             button_height, TRUE);
  if (g_status != nullptr) {
    MoveWindow(g_status, margin, client_height - margin - status_height, client_width - margin * 2, status_height,
               TRUE);
  }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_CREATE: {
      g_title_font = CreateFontW(-20, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
                                 L"Segoe UI");
      g_body_font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
      const HWND title = CreateWindowExW(0, L"STATIC", L"Shadow Gear", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd,
                                          reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdTitle)), g_instance, nullptr);
      const HWND subtitle = CreateWindowExW(
          0, L"STATIC", L"Numpad 1 shows and hides this window. Slots fill in after worn gear can be read.",
          WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdSubtitle)),
          g_instance, nullptr);
      SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(g_title_font), TRUE);
      SendMessageW(subtitle, WM_SETFONT, reinterpret_cast<WPARAM>(g_body_font), TRUE);

      g_list = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL, 0, 0, 0, 0,
                               hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdList)), g_instance, nullptr);
      ListView_SetExtendedListViewStyle(g_list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
      SendMessageW(g_list, WM_SETFONT, reinterpret_cast<WPARAM>(g_body_font), TRUE);
      const wchar_t* columns[] = {L"Slot", L"Name", L"Level", L"Rarity", L"Gem"};
      const int widths[] = {90, 180, 70, 110, 150};
      for (int i = 0; i < 5; ++i) {
        LVCOLUMNW column = {};
        column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        column.pszText = const_cast<wchar_t*>(columns[i]);
        column.cx = widths[i];
        column.iSubItem = i;
        ListView_InsertColumn(g_list, i, &column);
      }
      ListView_SetBkColor(g_list, RGB(24, 24, 24));
      ListView_SetTextBkColor(g_list, RGB(24, 24, 24));
      ListView_SetTextColor(g_list, RGB(235, 235, 235));

      auto make_button = [&](int id, const wchar_t* text) {
        const HWND button = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, hwnd,
                                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_instance, nullptr);
        SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(g_body_font), TRUE);
      };
      make_button(kIdSetLevel, L"Set level");
      make_button(kIdSetRarity, L"Set rarity");
      make_button(kIdSetGem, L"Set gem");

      g_status = CreateWindowExW(0, L"STATIC", L"Worn gear is not connected yet.", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
                                 hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdStatus)), g_instance, nullptr);
      SendMessageW(g_status, WM_SETFONT, reinterpret_cast<WPARAM>(g_body_font), TRUE);
      FillPlaceholderRows();
      RefreshList();
      return 0;
    }
    case WM_SIZE: {
      Layout(LOWORD(lparam), HIWORD(lparam));
      return 0;
    }
    case WM_COMMAND: {
      switch (LOWORD(wparam)) {
        case kIdSetLevel:
          OnGearAction(L"Set level");
          return 0;
        case kIdSetRarity:
          OnGearAction(L"Set rarity");
          return 0;
        case kIdSetGem:
          OnGearAction(L"Set gem");
          return 0;
        default:
          break;
      }
      break;
    }
    case WM_CLOSE:
      ShowMenu(false);
      return 0;
    case WM_CTLCOLORSTATIC: {
      const HDC dc = reinterpret_cast<HDC>(wparam);
      SetTextColor(dc, RGB(235, 235, 235));
      SetBkColor(dc, RGB(24, 24, 24));
      return reinterpret_cast<LRESULT>(g_background);
    }
    case WM_DESTROY:
      if (g_title_font != nullptr) {
        DeleteObject(g_title_font);
      }
      if (g_body_font != nullptr) {
        DeleteObject(g_body_font);
      }
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}

}  // namespace

void RunGearWindow() {
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                     reinterpret_cast<LPCWSTR>(&RunGearWindow), &g_instance);
  LogLine(L"shadow-gear-menu loaded");

  INITCOMMONCONTROLSEX controls = {};
  controls.dwSize = sizeof(controls);
  controls.dwICC = ICC_LISTVIEW_CLASSES;
  InitCommonControlsEx(&controls);

  g_background = CreateSolidBrush(RGB(24, 24, 24));

  WNDCLASSW window_class = {};
  window_class.lpfnWndProc = WindowProc;
  window_class.hInstance = g_instance;
  window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  window_class.hbrBackground = g_background;
  window_class.lpszClassName = kClassName;
  if (RegisterClassW(&window_class) == 0) {
    LogLine(L"Failed to register window class");
    return;
  }

  g_window = CreateWindowExW(WS_EX_APPWINDOW, kClassName, L"Shadow Gear", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                             CW_USEDEFAULT, 720, 460, nullptr, nullptr, g_instance, nullptr);
  if (g_window == nullptr) {
    LogLine(L"Failed to create window");
    return;
  }

  for (int i = 0; i < 200 && FindGameWindow() == nullptr; ++i) {
    Sleep(100);
  }
  ShowMenu(true);

  bool insert_was_down = false;
  MSG message = {};
  for (;;) {
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      if (message.message == WM_QUIT) {
        return;
      }
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }

    const bool insert_down = (GetAsyncKeyState(VK_NUMPAD1) & 0x8000) != 0;
    if (insert_down && !insert_was_down) {
      ShowMenu(!g_visible);
    }
    insert_was_down = insert_down;
    Sleep(10);
  }
}
