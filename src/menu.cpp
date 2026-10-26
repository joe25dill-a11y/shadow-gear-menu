#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "menu.h"

#include "features.h"
#include "log.h"
#include "settings_store.h"
#include "value_scan.h"

#include <windows.h>
#include <windowsx.h>
#include <objidl.h>
#include <gdiplus.h>
#include <dwmapi.h>

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <memory>
#include <string>
#include <vector>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "dwmapi.lib")

using namespace Gdiplus;

constexpr UINT kShutdownMessage = WM_APP + 1;
constexpr wchar_t kClassName[] = L"SkysShadowMenu";

enum class Page { Player, Equipment, Orcs, World, Settings, Debug };
constexpr int kPageCount = 6;

struct Item {
  Page page;
  FeatureId feature;
  const wchar_t* title;
  const wchar_t* detail;
};

const Item kItems[] = {
    {Page::Player, FeatureId::Health, L"God mode", L"Keeps your health from falling while this is on."},
    {Page::Player, FeatureId::Arrows, L"Infinite arrows", L"Arrow shots stop spending arrows."},
    {Page::Player, FeatureId::Might, L"Infinite might", L"The might meter stays full."},
    {Page::Player, FeatureId::Focus, L"Infinite focus", L"The focus meter stays full."},
    {Page::Player, FeatureId::Wrath, L"Infinite wrath", L"The wrath meter stays full."},
    {Page::Player, FeatureId::Mirian, L"Mirian",
     L"Adds 100000 Mirian on the next Mirian change. Turn it off and on again for another 100000."},
    {Page::Player, FeatureId::None, L"Damage multiplier", L"No published damage-multiplier interface."},
    {Page::Player, FeatureId::None, L"Fly mode", L"No published flight or movement interface."},
    {Page::Player, FeatureId::None, L"XP and level", L"No published experience or level interface."},
    {Page::Player, FeatureId::None, L"Skill points", L"Public skill mods remove purchase locks. They do not grant points."},
    {Page::Player, FeatureId::None, L"Cooldowns", L"No published cooldown interface."},
    {Page::Equipment, FeatureId::ItemChallenges, L"Complete item challenges",
     L"Finishes weapon challenges so secondary upgrades can be bought. Does not change level, rarity, or gems."},
    {Page::Equipment, FeatureId::None, L"Gear level, rarity, and gems",
     L"The Home scan checks published signatures only. It cannot read or edit worn gear."},
    {Page::Orcs, FeatureId::None, L"Captain traits and levels", L"No published captain editor."},
    {Page::Orcs, FeatureId::None, L"Spawn a chosen orc", L"The public spawn mod only raises epic and legendary odds."},
    {Page::Orcs, FeatureId::None, L"Intel reveal", L"The public Uruk Intel mod is separate, and it is not wired in here."},
    {Page::Orcs, FeatureId::None, L"Recruitment", L"No published recruitment interface."},
    {Page::World, FeatureId::MissionTimers, L"Mission timers", L"Stops mission timers while this is on."},
    {Page::World, FeatureId::None, L"Time of day", L"No published time-of-day interface."},
};

const wchar_t* kPageNames[] = {L"Player", L"Equipment", L"Orcs", L"World", L"Settings", L"Debug"};

struct Metrics {
  float scale = 1.f;
  int sidebar = 232;
  int header = 78;
  int status = 46;
  int pad = 20;
  int row_h = 76;
  int row_gap = 8;
  int search_h = 40;
  int cat_h = 46;
  int toggle_w = 46;
  int toggle_h = 24;
  int chip_w = 96;
  int chip_h = 28;
  int button = 34;
};

struct Layout {
  RECT header{};
  RECT sidebar{};
  RECT content{};
  RECT status{};
  RECT search{};
  RECT search_clear{};
  RECT list{};
  RECT close_btn{};
  RECT min_btn{};
  RECT categories[kPageCount]{};
  RECT value_box{};
  RECT whole_chip{};
  RECT decimal_chip{};
  RECT first_btn{};
  RECT next_btn{};
  RECT clear_btn{};
  RECT write_btn{};
  RECT menu_key{};
  RECT scale_chips[3]{};
  RECT accent_chips[3]{};
  RECT reset_btn{};
  RECT scan_btn{};
  std::vector<RECT> rows;
  std::vector<RECT> keys;
  std::vector<RECT> toggles;
  std::vector<int> visible;
};

struct Palette {
  Color canvas;
  Color sidebar;
  Color header;
  Color content;
  Color text;
  Color muted;
  Color line;
  Color accent;
  Color accent_ink;
  Color row;
  Color row_hot;
  Color off_track;
  Color ember;
};

HWND g_hwnd = nullptr;
HANDLE g_menu_thread = nullptr;
Page g_page = Page::Player;
std::wstring g_search;
bool g_search_focus = false;
std::wstring g_value_text;
bool g_value_focus = false;
bool g_value_whole = true;
int g_scroll = 0;
int g_selected = 0;
int g_hover = -1;
int g_binding = -2;
bool g_visible = false;
bool g_quit = false;
bool g_status_error = false;
std::wstring g_status = L"Opening the menu.";
float g_anim[kFeatureCount] = {};
ULONG_PTR g_gdi_token = 0;
bool g_audited = false;
int g_blink = 0;

struct FontSet {
  float scale = -1.f;
  std::unique_ptr<FontFamily> display;
  std::unique_ptr<FontFamily> ui;
  std::unique_ptr<Font> title;
  std::unique_ptr<Font> subtitle;
  std::unique_ptr<Font> category;
  std::unique_ptr<Font> row_title;
  std::unique_ptr<Font> detail;
  std::unique_ptr<Font> caption;
  std::unique_ptr<Font> chip;

  void Clear() {
    title.reset();
    subtitle.reset();
    category.reset();
    row_title.reset();
    detail.reset();
    caption.reset();
    chip.reset();
    display.reset();
    ui.reset();
    scale = -1.f;
  }

  void Ensure(float next) {
    if (title && std::fabs(scale - next) < 0.01f) {
      return;
    }
    Clear();
    scale = next;
    display = std::make_unique<FontFamily>(L"Palatino Linotype");
    if (!display->IsAvailable()) {
      display = std::make_unique<FontFamily>(L"Georgia");
    }
    ui = std::make_unique<FontFamily>(L"Segoe UI");
    const FontFamily* display_family = display->IsAvailable() ? display.get() : ui.get();
    const FontFamily* ui_family = ui->IsAvailable() ? ui.get() : display_family;
    title = std::make_unique<Font>(display_family, 26.f * next, FontStyleBold, UnitPixel);
    subtitle = std::make_unique<Font>(ui_family, 12.f * next, FontStyleRegular, UnitPixel);
    category = std::make_unique<Font>(ui_family, 15.f * next, FontStyleBold, UnitPixel);
    row_title = std::make_unique<Font>(ui_family, 16.f * next, FontStyleBold, UnitPixel);
    detail = std::make_unique<Font>(ui_family, 12.5f * next, FontStyleRegular, UnitPixel);
    caption = std::make_unique<Font>(ui_family, 12.f * next, FontStyleRegular, UnitPixel);
    chip = std::make_unique<Font>(ui_family, 12.f * next, FontStyleBold, UnitPixel);
  }
};

FontSet g_fonts;

int ScalePx(int value, float scale) {
  return static_cast<int>(std::lround(value * scale));
}

Metrics MakeMetrics(HWND hwnd) {
  UINT dpi = 96;
  if (hwnd != nullptr) {
    const UINT window_dpi = GetDpiForWindow(hwnd);
    if (window_dpi != 0) {
      dpi = window_dpi;
    }
  }
  float scale = (static_cast<float>(dpi) / 96.f) * (static_cast<float>(AppSettings().scale_percent) / 100.f);
  scale = std::clamp(scale, 0.8f, 2.4f);
  Metrics metrics;
  metrics.scale = scale;
  metrics.sidebar = ScalePx(232, scale);
  metrics.header = ScalePx(78, scale);
  metrics.status = ScalePx(46, scale);
  metrics.pad = ScalePx(20, scale);
  metrics.row_h = ScalePx(76, scale);
  metrics.row_gap = ScalePx(8, scale);
  metrics.search_h = ScalePx(40, scale);
  metrics.cat_h = ScalePx(46, scale);
  metrics.toggle_w = ScalePx(46, scale);
  metrics.toggle_h = ScalePx(24, scale);
  metrics.chip_w = ScalePx(96, scale);
  metrics.chip_h = ScalePx(28, scale);
  metrics.button = ScalePx(34, scale);
  return metrics;
}

Palette MakePalette(int accent) {
  Palette palette;
  palette.canvas = Color(255, 14, 11, 10);
  palette.sidebar = Color(255, 24, 18, 16);
  palette.header = Color(255, 18, 14, 12);
  palette.content = Color(255, 34, 26, 22);
  palette.text = Color(255, 242, 232, 214);
  palette.muted = Color(255, 168, 152, 130);
  palette.line = Color(255, 96, 76, 48);
  palette.row = Color(255, 44, 34, 29);
  palette.row_hot = Color(255, 58, 44, 36);
  palette.off_track = Color(255, 66, 54, 48);
  palette.ember = Color(255, 196, 112, 92);
  palette.accent_ink = Color(255, 28, 20, 14);
  if (accent == 1) {
    palette.accent = Color(255, 206, 106, 66);
  } else if (accent == 2) {
    palette.accent = Color(255, 126, 168, 118);
  } else {
    palette.accent = Color(255, 201, 164, 92);
  }
  return palette;
}

RectF ToF(const RECT& rect) {
  return RectF(static_cast<REAL>(rect.left), static_cast<REAL>(rect.top), static_cast<REAL>(rect.right - rect.left),
               static_cast<REAL>(rect.bottom - rect.top));
}

bool Live(const RECT& rect) {
  return rect.right > rect.left && rect.bottom > rect.top;
}

bool Hit(const RECT& rect, POINT point) {
  return Live(rect) && PtInRect(&rect, point);
}

bool Overlaps(const RECT& a, const RECT& b) {
  return a.left < b.right && a.right > b.left && a.top < b.bottom && a.bottom > b.top;
}

std::wstring Lower(std::wstring text) {
  for (wchar_t& ch : text) {
    ch = static_cast<wchar_t>(towlower(ch));
  }
  return text;
}

bool ContainsI(const wchar_t* haystack, const std::wstring& needle) {
  if (needle.empty()) {
    return true;
  }
  return Lower(haystack).find(Lower(needle)) != std::wstring::npos;
}

bool HostIsGame() {
  wchar_t path[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  const wchar_t* name = wcsrchr(path, L'\\');
  name = name != nullptr ? name + 1 : path;
  return _wcsicmp(name, L"ShadowOfWar.exe") == 0;
}

std::wstring KeyName(int key) {
  if (key == 0) {
    return L"Unbound";
  }
  const UINT scan = MapVirtualKeyW(static_cast<UINT>(key), MAPVK_VK_TO_VSC);
  LONG param = static_cast<LONG>(scan << 16);
  switch (key) {
    case VK_LEFT:
    case VK_RIGHT:
    case VK_UP:
    case VK_DOWN:
    case VK_PRIOR:
    case VK_NEXT:
    case VK_HOME:
    case VK_END:
    case VK_INSERT:
    case VK_DELETE:
    case VK_DIVIDE:
    case VK_NUMLOCK:
      param |= 1 << 24;
      break;
    default:
      break;
  }
  wchar_t buffer[64] = {};
  if (GetKeyNameTextW(param, buffer, 64) > 0) {
    return buffer;
  }
  swprintf_s(buffer, L"Key %d", key);
  return buffer;
}

bool ItemAvailable(const Item& item) {
  return item.feature != FeatureId::None && FeaturesReady(item.feature);
}

const wchar_t* ItemDetail(const Item& item) {
  if (item.feature != FeatureId::None && !FeaturesReady(item.feature)) {
    const wchar_t* reason = FeatureReason(item.feature);
    if (reason != nullptr && reason[0] != L'\0') {
      return reason;
    }
  }
  return item.detail;
}

bool ShowFeatureList() {
  if (!g_search.empty()) {
    return true;
  }
  return g_page == Page::Player || g_page == Page::Equipment || g_page == Page::Orcs || g_page == Page::World;
}

std::vector<int> VisibleItems() {
  std::vector<int> visible;
  for (int i = 0; i < static_cast<int>(sizeof(kItems) / sizeof(kItems[0])); ++i) {
    const Item& item = kItems[i];
    if (!g_search.empty()) {
      if (ContainsI(item.title, g_search) || ContainsI(item.detail, g_search) ||
          ContainsI(kPageNames[static_cast<int>(item.page)], g_search)) {
        visible.push_back(i);
      }
    } else if (item.page == g_page) {
      visible.push_back(i);
    }
  }
  return visible;
}

void SetStatus(const std::wstring& text, bool error) {
  g_status = text;
  g_status_error = error;
  if (g_hwnd != nullptr) {
    InvalidateRect(g_hwnd, nullptr, FALSE);
  }
}

void FillRound(Graphics& graphics, const Color& color, const RectF& rect, REAL radius) {
  if (rect.Width <= 1.f || rect.Height <= 1.f) {
    return;
  }
  REAL rad = std::min(radius, std::min(rect.Width, rect.Height) / 2.f);
  GraphicsPath path;
  const REAL d = rad * 2.f;
  path.AddArc(rect.X, rect.Y, d, d, 180.f, 90.f);
  path.AddArc(rect.X + rect.Width - d, rect.Y, d, d, 270.f, 90.f);
  path.AddArc(rect.X + rect.Width - d, rect.Y + rect.Height - d, d, d, 0.f, 90.f);
  path.AddArc(rect.X, rect.Y + rect.Height - d, d, d, 90.f, 90.f);
  path.CloseFigure();
  SolidBrush brush(color);
  graphics.FillPath(&brush, &path);
}

void StrokeRound(Graphics& graphics, const Color& color, const RectF& rect, REAL radius, REAL width) {
  if (rect.Width <= 1.f || rect.Height <= 1.f) {
    return;
  }
  REAL rad = std::min(radius, std::min(rect.Width, rect.Height) / 2.f);
  GraphicsPath path;
  const REAL d = rad * 2.f;
  path.AddArc(rect.X, rect.Y, d, d, 180.f, 90.f);
  path.AddArc(rect.X + rect.Width - d, rect.Y, d, d, 270.f, 90.f);
  path.AddArc(rect.X + rect.Width - d, rect.Y + rect.Height - d, d, d, 0.f, 90.f);
  path.AddArc(rect.X, rect.Y + rect.Height - d, d, d, 90.f, 90.f);
  path.CloseFigure();
  Pen pen(color, width);
  graphics.DrawPath(&pen, &path);
}

void DrawLabel(Graphics& graphics, const wchar_t* text, Font* font, const RectF& rect, const Color& color,
               StringAlignment align, bool wrap) {
  if (text == nullptr || font == nullptr || rect.Width < 2.f || rect.Height < 2.f) {
    return;
  }
  SolidBrush brush(color);
  StringFormat format;
  format.SetAlignment(align);
  format.SetLineAlignment(wrap ? StringAlignmentNear : StringAlignmentCenter);
  format.SetTrimming(StringTrimmingEllipsisCharacter);
  if (!wrap) {
    format.SetFormatFlags(StringFormatFlagsNoWrap);
  }
  graphics.DrawString(text, -1, font, rect, &format, &brush);
}

void DrawIcon(Graphics& graphics, Page page, const RectF& rect, const Color& color) {
  Pen pen(color, 1.6f);
  pen.SetStartCap(LineCapRound);
  pen.SetEndCap(LineCapRound);
  const REAL cx = rect.X + rect.Width / 2.f;
  const REAL cy = rect.Y + rect.Height / 2.f;
  if (page == Page::Player) {
    graphics.DrawEllipse(&pen, cx - 4.f, cy - 7.f, 8.f, 8.f);
    graphics.DrawArc(&pen, cx - 8.f, cy - 1.f, 16.f, 12.f, 200.f, 140.f);
  } else if (page == Page::Equipment) {
    const PointF diamond[] = {PointF(cx, cy - 8.f), PointF(cx + 7.f, cy), PointF(cx, cy + 8.f), PointF(cx - 7.f, cy)};
    graphics.DrawPolygon(&pen, diamond, 4);
  } else if (page == Page::Orcs) {
    graphics.DrawLine(&pen, cx - 8.f, cy + 4.f, cx - 4.f, cy - 7.f);
    graphics.DrawLine(&pen, cx - 4.f, cy - 7.f, cx, cy + 1.f);
    graphics.DrawLine(&pen, cx, cy + 1.f, cx + 4.f, cy - 7.f);
    graphics.DrawLine(&pen, cx + 4.f, cy - 7.f, cx + 8.f, cy + 4.f);
  } else if (page == Page::World) {
    graphics.DrawEllipse(&pen, cx - 7.f, cy - 7.f, 14.f, 14.f);
    graphics.DrawLine(&pen, cx - 7.f, cy, cx + 7.f, cy);
    graphics.DrawArc(&pen, cx - 3.f, cy - 7.f, 6.f, 14.f, 270.f, 180.f);
  } else if (page == Page::Settings) {
    graphics.DrawEllipse(&pen, cx - 3.f, cy - 3.f, 6.f, 6.f);
    graphics.DrawEllipse(&pen, cx - 7.f, cy - 7.f, 14.f, 14.f);
  } else {
    graphics.DrawEllipse(&pen, cx - 6.f, cy - 6.f, 10.f, 10.f);
    graphics.DrawLine(&pen, cx + 2.f, cy + 2.f, cx + 7.f, cy + 7.f);
  }
}

void DrawToggle(Graphics& graphics, const RECT& rect, float amount, const Palette& palette) {
  const RectF bounds = ToF(rect);
  const float t = std::clamp(amount, 0.f, 1.f);
  const BYTE r = static_cast<BYTE>(std::lround(palette.off_track.GetR() + (palette.accent.GetR() - palette.off_track.GetR()) * t));
  const BYTE g = static_cast<BYTE>(std::lround(palette.off_track.GetG() + (palette.accent.GetG() - palette.off_track.GetG()) * t));
  const BYTE b = static_cast<BYTE>(std::lround(palette.off_track.GetB() + (palette.accent.GetB() - palette.off_track.GetB()) * t));
  FillRound(graphics, Color(255, r, g, b), bounds, bounds.Height / 2.f);
  const REAL knob = bounds.Height - 6.f;
  const REAL x = bounds.X + 3.f + t * (bounds.Width - knob - 6.f);
  SolidBrush brush(Color(255, 248, 242, 232));
  graphics.FillEllipse(&brush, x, bounds.Y + 3.f, knob, knob);
}

void DrawChip(Graphics& graphics, const RECT& rect, const wchar_t* text, Font* font, const Palette& palette, bool hot) {
  const RectF bounds = ToF(rect);
  FillRound(graphics, hot ? palette.row_hot : palette.row, bounds, 8.f);
  StrokeRound(graphics, hot ? palette.accent : palette.line, bounds, 8.f, 1.f);
  DrawLabel(graphics, text, font, bounds, hot ? palette.accent : palette.text, StringAlignmentCenter, false);
}

Layout BuildLayout(HWND hwnd) {
  RECT client = {};
  GetClientRect(hwnd, &client);
  const int width = std::max(1L, client.right);
  const int height = std::max(1L, client.bottom);
  const Metrics metrics = MakeMetrics(hwnd);
  Layout layout;
  layout.header = {0, 0, width, metrics.header};
  layout.status = {0, std::max(metrics.header, height - metrics.status), width, height};
  layout.sidebar = {0, metrics.header, std::min(metrics.sidebar, width / 2), layout.status.top};
  layout.content = {layout.sidebar.right, metrics.header, width, layout.status.top};
  const int button_y = (metrics.header - metrics.button) / 2;
  layout.close_btn = {width - metrics.pad - metrics.button, button_y, width - metrics.pad, button_y + metrics.button};
  layout.min_btn = {layout.close_btn.left - ScalePx(8, metrics.scale) - metrics.button, button_y,
                    layout.close_btn.left - ScalePx(8, metrics.scale), button_y + metrics.button};

  int y = layout.sidebar.top + ScalePx(14, metrics.scale);
  int pitch = metrics.cat_h + ScalePx(6, metrics.scale);
  const int available = layout.sidebar.bottom - y - ScalePx(8, metrics.scale);
  if (pitch * kPageCount > available && available > 0) {
    pitch = std::max(ScalePx(36, metrics.scale), available / kPageCount);
  }
  const int cat_h = std::min(metrics.cat_h, std::max(ScalePx(32, metrics.scale), pitch - ScalePx(4, metrics.scale)));
  for (int i = 0; i < kPageCount; ++i) {
    layout.categories[i] = {ScalePx(12, metrics.scale), y, layout.sidebar.right - ScalePx(12, metrics.scale), y + cat_h};
    y += pitch;
  }

  layout.search = {layout.content.left + metrics.pad, layout.content.top + metrics.pad,
                   std::max(layout.content.left + metrics.pad + 40, layout.content.right - metrics.pad),
                   layout.content.top + metrics.pad + metrics.search_h};
  if (!g_search.empty()) {
    layout.search_clear = {layout.search.right - metrics.search_h, layout.search.top, layout.search.right, layout.search.bottom};
  }
  layout.list = {layout.search.left, layout.search.bottom + ScalePx(14, metrics.scale), layout.search.right,
                 std::max(layout.search.bottom + ScalePx(14, metrics.scale), layout.content.bottom - metrics.pad)};

  if (ShowFeatureList()) {
    layout.visible = VisibleItems();
    const int row_pitch = metrics.row_h + metrics.row_gap;
    const int view = std::max(0L, layout.list.bottom - layout.list.top);
    const int span = static_cast<int>(layout.visible.size()) * row_pitch;
    const int max_scroll = std::max(0, span - view);
    g_scroll = std::clamp(g_scroll, 0, max_scroll);
    layout.rows.resize(layout.visible.size());
    layout.keys.resize(layout.visible.size());
    layout.toggles.resize(layout.visible.size());
    int row_y = layout.list.top - g_scroll;
    for (size_t i = 0; i < layout.visible.size(); ++i) {
      RECT row = {layout.list.left, row_y, layout.list.right, row_y + metrics.row_h};
      layout.rows[i] = row;
      const int toggle_y = row.top + (metrics.row_h - metrics.toggle_h) / 2;
      RECT toggle = {row.right - ScalePx(16, metrics.scale) - metrics.toggle_w, toggle_y,
                     row.right - ScalePx(16, metrics.scale), toggle_y + metrics.toggle_h};
      const Item& item = kItems[layout.visible[i]];
      if (ItemAvailable(item)) {
        layout.toggles[i] = toggle;
        const int chip_y = row.top + (metrics.row_h - metrics.chip_h) / 2;
        layout.keys[i] = {toggle.left - ScalePx(10, metrics.scale) - metrics.chip_w, chip_y, toggle.left - ScalePx(10, metrics.scale),
                          chip_y + metrics.chip_h};
      } else {
        layout.toggles[i] = {toggle.left - ScalePx(70, metrics.scale), toggle_y, toggle.right, toggle_y + metrics.toggle_h};
        layout.keys[i] = {};
      }
      row_y += row_pitch;
    }
  } else if (g_search.empty() && g_page == Page::Settings) {
    const int row_h = ScalePx(64, metrics.scale);
    int row_y = layout.list.top;
    layout.menu_key = {layout.list.right - metrics.chip_w - ScalePx(8, metrics.scale), row_y + ScalePx(16, metrics.scale),
                       layout.list.right - ScalePx(8, metrics.scale), row_y + ScalePx(16, metrics.scale) + metrics.chip_h};
    row_y += row_h;
    const int chip_w = ScalePx(84, metrics.scale);
    for (int i = 0; i < 3; ++i) {
      const int left = layout.list.right - (3 - i) * (chip_w + ScalePx(8, metrics.scale));
      layout.scale_chips[i] = {left, row_y + ScalePx(16, metrics.scale), left + chip_w,
                               row_y + ScalePx(16, metrics.scale) + metrics.chip_h};
    }
    row_y += row_h;
    for (int i = 0; i < 3; ++i) {
      const int left = layout.list.right - (3 - i) * (chip_w + ScalePx(8, metrics.scale));
      layout.accent_chips[i] = {left, row_y + ScalePx(16, metrics.scale), left + chip_w,
                                row_y + ScalePx(16, metrics.scale) + metrics.chip_h};
    }
    row_y += row_h + ScalePx(8, metrics.scale);
    layout.reset_btn = {layout.list.left, row_y, layout.list.left + ScalePx(220, metrics.scale), row_y + ScalePx(40, metrics.scale)};
    layout.scan_btn = {layout.reset_btn.right + ScalePx(12, metrics.scale), row_y,
                       layout.reset_btn.right + ScalePx(192, metrics.scale), row_y + ScalePx(40, metrics.scale)};
  } else if (g_search.empty() && g_page == Page::Debug) {
    int row_y = layout.list.top;
    const int box_h = ScalePx(40, metrics.scale);
    layout.value_box = {layout.list.left, row_y, layout.list.left + ScalePx(280, metrics.scale), row_y + box_h};
    const int chip_w = ScalePx(120, metrics.scale);
    layout.whole_chip = {layout.value_box.right + ScalePx(12, metrics.scale), row_y + ScalePx(6, metrics.scale),
                         layout.value_box.right + ScalePx(12, metrics.scale) + chip_w, row_y + ScalePx(6, metrics.scale) + metrics.chip_h};
    layout.decimal_chip = {layout.whole_chip.right + ScalePx(8, metrics.scale), layout.whole_chip.top,
                           layout.whole_chip.right + ScalePx(8, metrics.scale) + chip_w, layout.whole_chip.bottom};
    row_y += box_h + ScalePx(16, metrics.scale);
    const int btn_w = ScalePx(150, metrics.scale);
    const int btn_h = ScalePx(40, metrics.scale);
    layout.first_btn = {layout.list.left, row_y, layout.list.left + btn_w, row_y + btn_h};
    layout.next_btn = {layout.first_btn.right + ScalePx(10, metrics.scale), row_y, layout.first_btn.right + ScalePx(10, metrics.scale) + btn_w,
                       row_y + btn_h};
    layout.clear_btn = {layout.next_btn.right + ScalePx(10, metrics.scale), row_y, layout.next_btn.right + ScalePx(10, metrics.scale) + btn_w,
                        row_y + btn_h};
    row_y += btn_h + ScalePx(12, metrics.scale);
    layout.write_btn = {layout.list.left, row_y, layout.list.left + ScalePx(220, metrics.scale), row_y + btn_h};
  }
  return layout;
}

void Audit(const Layout& layout) {
  if (g_audited) {
    return;
  }
  g_audited = true;
  const wchar_t* problem = nullptr;
  if (Overlaps(layout.search, layout.list)) {
    problem = L"layout overlap: search and list";
  } else if (Overlaps(layout.sidebar, layout.content)) {
    problem = L"layout overlap: sidebar and content";
  } else if (Overlaps(layout.status, layout.content)) {
    problem = L"layout overlap: status and content";
  } else if (Overlaps(layout.header, layout.sidebar)) {
    problem = L"layout overlap: header and sidebar";
  }
  for (size_t i = 0; problem == nullptr && i < layout.rows.size(); ++i) {
    if (Live(layout.toggles[i]) && !Overlaps(layout.toggles[i], layout.rows[i]) &&
        (layout.toggles[i].left < layout.rows[i].left || layout.toggles[i].right > layout.rows[i].right)) {
      problem = L"layout overlap: toggle outside its row";
    }
    if (Live(layout.keys[i]) && layout.keys[i].right > layout.toggles[i].left) {
      problem = L"layout overlap: hotkey chip and toggle";
    }
    for (size_t j = i + 1; j < layout.rows.size(); ++j) {
      if (Overlaps(layout.rows[i], layout.rows[j])) {
        problem = L"layout overlap: feature rows";
        break;
      }
    }
  }
  LogLine(problem != nullptr ? problem : L"layout ok");
}

void DrawFeatureList(Graphics& graphics, const Layout& layout, const Metrics& metrics, const Palette& palette) {
  graphics.SetClip(ToF(layout.list));
  if (layout.visible.empty()) {
    DrawLabel(graphics, L"No features match that search.", g_fonts.row_title.get(), ToF(layout.list), palette.muted,
              StringAlignmentCenter, false);
  }
  for (size_t i = 0; i < layout.visible.size(); ++i) {
    if (!Overlaps(layout.rows[i], layout.list)) {
      continue;
    }
    const Item& item = kItems[layout.visible[i]];
    const bool available = ItemAvailable(item);
    const bool hot = g_hover == layout.visible[i] || g_selected == layout.visible[i];
    FillRound(graphics, hot ? palette.row_hot : palette.row, ToF(layout.rows[i]), 12.f);
    if (g_selected == layout.visible[i]) {
      SolidBrush bar(palette.accent);
      graphics.FillRectangle(&bar, static_cast<REAL>(layout.rows[i].left), static_cast<REAL>(layout.rows[i].top + 12), 3.f,
                             static_cast<REAL>(metrics.row_h - 24));
    }
    const int control_left = Live(layout.keys[i]) ? layout.keys[i].left : layout.toggles[i].left;
    RectF title_rect(static_cast<REAL>(layout.rows[i].left + ScalePx(16, metrics.scale)),
                     static_cast<REAL>(layout.rows[i].top + ScalePx(12, metrics.scale)),
                     static_cast<REAL>(std::max(20, static_cast<int>(control_left - layout.rows[i].left) - ScalePx(28, metrics.scale))),
                     static_cast<REAL>(ScalePx(22, metrics.scale)));
    RectF detail_rect(title_rect.X, title_rect.GetBottom() + 1.f, title_rect.Width,
                      static_cast<REAL>(ScalePx(30, metrics.scale)));
    std::wstring title = item.title;
    if (!g_search.empty()) {
      title += L"   ";
      title += kPageNames[static_cast<int>(item.page)];
    }
    DrawLabel(graphics, title.c_str(), g_fonts.row_title.get(), title_rect, available ? palette.text : palette.muted,
              StringAlignmentNear, false);
    DrawLabel(graphics, ItemDetail(item), g_fonts.detail.get(), detail_rect, palette.muted, StringAlignmentNear, true);
    if (available) {
      const int index = static_cast<int>(item.feature);
      const bool binding = g_binding == index;
      DrawChip(graphics, layout.keys[i], binding ? L"Press key" : KeyName(AppSettings().hotkey[index]).c_str(),
               g_fonts.chip.get(), palette, binding);
      DrawToggle(graphics, layout.toggles[i], g_anim[index], palette);
    } else {
      DrawChip(graphics, layout.toggles[i], L"Unavailable", g_fonts.caption.get(), palette, false);
    }
  }
  graphics.ResetClip();
}

void DrawSettings(Graphics& graphics, const Layout& layout, const Metrics& metrics, const Palette& palette) {
  const Settings& settings = AppSettings();
  auto label = [&](const wchar_t* text, const RECT& anchor) {
    RectF rect(static_cast<REAL>(layout.list.left), static_cast<REAL>(anchor.top - ScalePx(6, metrics.scale)),
               static_cast<REAL>(std::max(40, static_cast<int>(anchor.left - layout.list.left) - 12)),
               static_cast<REAL>(anchor.bottom - anchor.top + ScalePx(12, metrics.scale)));
    DrawLabel(graphics, text, g_fonts.row_title.get(), rect, palette.text, StringAlignmentNear, false);
  };
  label(L"Menu hotkey", layout.menu_key);
  DrawChip(graphics, layout.menu_key, g_binding == -1 ? L"Press key" : KeyName(settings.menu_key).c_str(), g_fonts.chip.get(),
           palette, g_binding == -1);
  const wchar_t* scales[] = {L"100%", L"125%", L"150%"};
  const int scale_values[] = {100, 125, 150};
  label(L"Size", layout.scale_chips[0]);
  for (int i = 0; i < 3; ++i) {
    DrawChip(graphics, layout.scale_chips[i], scales[i], g_fonts.chip.get(), palette, settings.scale_percent == scale_values[i]);
  }
  const wchar_t* accents[] = {L"Gold", L"Ember", L"Elven"};
  label(L"Accent", layout.accent_chips[0]);
  for (int i = 0; i < 3; ++i) {
    DrawChip(graphics, layout.accent_chips[i], accents[i], g_fonts.chip.get(), palette, settings.accent == i);
  }
  FillRound(graphics, palette.accent, ToF(layout.reset_btn), 10.f);
  DrawLabel(graphics, L"Reset to defaults", g_fonts.chip.get(), ToF(layout.reset_btn), palette.accent_ink, StringAlignmentCenter,
            false);
  FillRound(graphics, palette.row, ToF(layout.scan_btn), 10.f);
  DrawLabel(graphics, L"Scan game", g_fonts.chip.get(), ToF(layout.scan_btn), palette.accent, StringAlignmentCenter, false);
  RectF note(static_cast<REAL>(layout.list.left), static_cast<REAL>(layout.reset_btn.bottom + ScalePx(18, metrics.scale)),
             static_cast<REAL>(layout.list.right - layout.list.left), static_cast<REAL>(ScalePx(64, metrics.scale)));
  DrawLabel(graphics,
            L"Numpad 1 opens and closes this menu. Home scans and leaves the menu open. Preferences are saved beside the plugin in shadow-gear-menu.ini.",
            g_fonts.detail.get(), note, palette.muted, StringAlignmentNear, true);
}

void DrawDebug(Graphics& graphics, const Layout& layout, const Metrics& metrics, const Palette& palette) {
  StrokeRound(graphics, g_value_focus ? palette.accent : palette.line, ToF(layout.value_box), 10.f, 1.f);
  RectF value_text(static_cast<REAL>(layout.value_box.left + 12), static_cast<REAL>(layout.value_box.top),
                   static_cast<REAL>(layout.value_box.right - layout.value_box.left - 24),
                   static_cast<REAL>(layout.value_box.bottom - layout.value_box.top));
  if (g_value_text.empty()) {
    DrawLabel(graphics, L"Number from the game", g_fonts.detail.get(), value_text, palette.muted, StringAlignmentNear, false);
  } else {
    DrawLabel(graphics, g_value_text.c_str(), g_fonts.detail.get(), value_text, palette.text, StringAlignmentNear, false);
  }
  DrawChip(graphics, layout.whole_chip, L"Whole", g_fonts.chip.get(), palette, g_value_whole);
  DrawChip(graphics, layout.decimal_chip, L"Decimal", g_fonts.chip.get(), palette, !g_value_whole);
  FillRound(graphics, palette.accent, ToF(layout.first_btn), 10.f);
  DrawLabel(graphics, L"First scan", g_fonts.chip.get(), ToF(layout.first_btn), palette.accent_ink, StringAlignmentCenter, false);
  FillRound(graphics, palette.row, ToF(layout.next_btn), 10.f);
  DrawLabel(graphics, L"Next scan", g_fonts.chip.get(), ToF(layout.next_btn), palette.accent, StringAlignmentCenter, false);
  FillRound(graphics, palette.row, ToF(layout.clear_btn), 10.f);
  DrawLabel(graphics, L"Clear", g_fonts.chip.get(), ToF(layout.clear_btn), palette.text, StringAlignmentCenter, false);
  const bool can_write = ValueScanActive() && ValueScanCount() > 0 && ValueScanCount() <= 8;
  FillRound(graphics, can_write ? palette.accent : palette.row, ToF(layout.write_btn), 10.f);
  DrawLabel(graphics, L"Write number", g_fonts.chip.get(), ToF(layout.write_btn), can_write ? palette.accent_ink : palette.muted,
            StringAlignmentCenter, false);
  RectF note(static_cast<REAL>(layout.list.left), static_cast<REAL>(layout.write_btn.bottom + ScalePx(18, metrics.scale)),
             static_cast<REAL>(layout.list.right - layout.list.left), static_cast<REAL>(ScalePx(150, metrics.scale)));
  DrawLabel(graphics,
            L"Type a number you can see, such as XP. First scan stores every copy. Change that number in the game, type the new number, then Next scan. Write unlocks at 8 copies or fewer. Fly mode and captain editing are not one number. Gear level can be tried when the level on screen is the number you type.",
            g_fonts.detail.get(), note, palette.muted, StringAlignmentNear, true);
}

void PaintMenu(Graphics& graphics, HWND hwnd, int width, int height) {
  const Metrics metrics = MakeMetrics(hwnd);
  g_fonts.Ensure(metrics.scale);
  const Palette palette = MakePalette(AppSettings().accent);
  const Layout layout = BuildLayout(hwnd);
  Audit(layout);

  SolidBrush canvas(palette.canvas);
  graphics.FillRectangle(&canvas, 0, 0, width, height);
  SolidBrush sidebar(palette.sidebar);
  graphics.FillRectangle(&sidebar, ToF(layout.sidebar));
  SolidBrush header(palette.header);
  graphics.FillRectangle(&header, ToF(layout.header));
  SolidBrush content(palette.content);
  graphics.FillRectangle(&content, ToF(layout.content));
  SolidBrush status_brush(palette.header);
  graphics.FillRectangle(&status_brush, ToF(layout.status));

  Pen frame(palette.line, 1.f);
  graphics.DrawRectangle(&frame, 0, 0, width - 1, height - 1);
  Pen rule(palette.accent, 2.f);
  graphics.DrawLine(&rule, 0.f, static_cast<REAL>(layout.header.bottom), static_cast<REAL>(width),
                    static_cast<REAL>(layout.header.bottom));
  Pen hairline(palette.line, 1.f);
  graphics.DrawLine(&hairline, static_cast<REAL>(layout.sidebar.right), static_cast<REAL>(layout.sidebar.top),
                    static_cast<REAL>(layout.sidebar.right), static_cast<REAL>(layout.sidebar.bottom));
  graphics.DrawLine(&hairline, 0.f, static_cast<REAL>(layout.status.top), static_cast<REAL>(width),
                    static_cast<REAL>(layout.status.top));

  const REAL title_right = static_cast<REAL>(layout.min_btn.left - ScalePx(16, metrics.scale));
  RectF title_rect(static_cast<REAL>(metrics.pad), static_cast<REAL>(ScalePx(16, metrics.scale)),
                   std::max(40.f, title_right - metrics.pad), static_cast<REAL>(ScalePx(32, metrics.scale)));
  RectF sub_rect(title_rect.X, title_rect.GetBottom() - 2.f, title_rect.Width, static_cast<REAL>(ScalePx(18, metrics.scale)));
  DrawLabel(graphics, L"SKY'S SHADOW", g_fonts.title.get(), title_rect, palette.text, StringAlignmentNear, false);
  DrawLabel(graphics, L"MIDDLE-EARTH MENU", g_fonts.subtitle.get(), sub_rect, palette.accent, StringAlignmentNear, false);

  auto header_button = [&](const RECT& rect, bool close_button) {
    FillRound(graphics, palette.row, ToF(rect), 8.f);
    Pen pen(palette.text, 1.5f);
    const REAL cx = (rect.left + rect.right) / 2.f;
    const REAL cy = (rect.top + rect.bottom) / 2.f;
    if (close_button) {
      graphics.DrawLine(&pen, cx - 5.f, cy - 5.f, cx + 5.f, cy + 5.f);
      graphics.DrawLine(&pen, cx + 5.f, cy - 5.f, cx - 5.f, cy + 5.f);
    } else {
      graphics.DrawLine(&pen, cx - 6.f, cy + 4.f, cx + 6.f, cy + 4.f);
    }
  };
  header_button(layout.min_btn, false);
  header_button(layout.close_btn, true);

  for (int i = 0; i < kPageCount; ++i) {
    const Page page = static_cast<Page>(i);
    const bool selected = g_page == page && g_search.empty();
    if (selected) {
      FillRound(graphics, palette.row, ToF(layout.categories[i]), 10.f);
      SolidBrush bar(palette.accent);
      graphics.FillRectangle(&bar, static_cast<REAL>(layout.categories[i].left), static_cast<REAL>(layout.categories[i].top + 8),
                             3.f, static_cast<REAL>(layout.categories[i].bottom - layout.categories[i].top - 16));
    }
    RectF icon(static_cast<REAL>(layout.categories[i].left + ScalePx(14, metrics.scale)),
               static_cast<REAL>(layout.categories[i].top + (layout.categories[i].bottom - layout.categories[i].top - 18) / 2),
               18.f, 18.f);
    DrawIcon(graphics, page, icon, selected ? palette.accent : palette.muted);
    RectF label(icon.X + 28.f, static_cast<REAL>(layout.categories[i].top),
                static_cast<REAL>(layout.categories[i].right - icon.X - 34.f),
                static_cast<REAL>(layout.categories[i].bottom - layout.categories[i].top));
    DrawLabel(graphics, kPageNames[i], g_fonts.category.get(), label, selected ? palette.text : palette.muted,
              StringAlignmentNear, false);
  }

  FillRound(graphics, palette.canvas, ToF(layout.search), 10.f);
  StrokeRound(graphics, g_search_focus ? palette.accent : palette.line, ToF(layout.search), 10.f, 1.f);
  RectF search_text(static_cast<REAL>(layout.search.left + ScalePx(14, metrics.scale)), static_cast<REAL>(layout.search.top),
                    static_cast<REAL>(layout.search.right - layout.search.left - ScalePx(48, metrics.scale)),
                    static_cast<REAL>(layout.search.bottom - layout.search.top));
  if (g_search.empty()) {
    DrawLabel(graphics, L"Search features", g_fonts.detail.get(), search_text, palette.muted, StringAlignmentNear, false);
  } else {
    DrawLabel(graphics, g_search.c_str(), g_fonts.detail.get(), search_text, palette.text, StringAlignmentNear, false);
    if (g_search_focus && g_blink != 0 && g_fonts.detail) {
      RectF measured;
      StringFormat format(StringFormat::GenericTypographic());
      graphics.MeasureString(g_search.c_str(), -1, g_fonts.detail.get(), search_text, &format, &measured);
      Pen caret(palette.accent, 1.f);
      const REAL caret_x = search_text.X + measured.Width + 1.f;
      graphics.DrawLine(&caret, caret_x, search_text.Y + 10.f, caret_x, search_text.GetBottom() - 10.f);
    }
  }
  if (Live(layout.search_clear)) {
    DrawLabel(graphics, L"Clear", g_fonts.caption.get(), ToF(layout.search_clear), palette.accent, StringAlignmentCenter, false);
  }

  if (ShowFeatureList()) {
    DrawFeatureList(graphics, layout, metrics, palette);
  } else if (g_page == Page::Debug && g_search.empty()) {
    DrawDebug(graphics, layout, metrics, palette);
  } else {
    DrawSettings(graphics, layout, metrics, palette);
  }

  RectF status_rect(static_cast<REAL>(metrics.pad), static_cast<REAL>(layout.status.top),
                    static_cast<REAL>(width - metrics.pad * 2 - ScalePx(280, metrics.scale)),
                    static_cast<REAL>(layout.status.bottom - layout.status.top));
  DrawLabel(graphics, g_status.c_str(), g_fonts.caption.get(), status_rect, g_status_error ? palette.ember : palette.muted,
            StringAlignmentNear, false);
  RectF hint(static_cast<REAL>(width - metrics.pad - ScalePx(270, metrics.scale)), status_rect.Y,
             static_cast<REAL>(ScalePx(270, metrics.scale)), status_rect.Height);
  DrawLabel(graphics, L"Tab  category     Space  toggle     Esc  close", g_fonts.caption.get(), hint, palette.line,
            StringAlignmentFar, false);
}

void ApplySavedFeatures() {
  Settings& settings = AppSettings();
  bool any = false;
  for (int i = 0; i < kFeatureCount; ++i) {
    g_anim[i] = 0.f;
    if (!settings.enabled[i]) {
      continue;
    }
    any = true;
    std::wstring error;
    if (!SetFeatureEnabled(static_cast<FeatureId>(i), true, error)) {
      settings.enabled[i] = false;
      SetStatus(error, true);
    } else {
      g_anim[i] = 1.f;
    }
  }
  if (any) {
    settings.Save();
  }
}

void ShowMenu(bool show);
HWND FindGameWindow();

void RunPublishedScan(bool open_menu) {
  ScanFeatures();
  ApplySavedFeatures();
  LogLine(FeatureScanSummary().c_str());
  if (!g_status_error) {
    const bool missing = !InPreview() && !FeaturesReady(FeatureId::Health) && !FeaturesReady(FeatureId::Arrows) &&
                         !FeaturesReady(FeatureId::MissionTimers) && !FeaturesReady(FeatureId::ItemChallenges);
    SetStatus(FeatureScanSummary(), missing);
  }
  if (open_menu) {
    ShowMenu(true);
  } else if (g_hwnd != nullptr) {
    InvalidateRect(g_hwnd, nullptr, FALSE);
  }
}

void ToggleFeature(FeatureId id) {
  if (id == FeatureId::None || !FeaturesReady(id)) {
    return;
  }
  const int index = static_cast<int>(id);
  std::wstring error;
  const bool next = !FeatureEnabled(id);
  if (!SetFeatureEnabled(id, next, error)) {
    SetStatus(error, true);
    return;
  }
  AppSettings().enabled[index] = next;
  AppSettings().Save();
  std::wstring text = FeatureName(id);
  text += next ? L" is on." : L" is off.";
  if (InPreview()) {
    text += L" Preview only, so the game is unchanged.";
  }
  SetStatus(text, false);
}

void SelectVisible(const Layout& layout, int delta) {
  if (layout.visible.empty()) {
    return;
  }
  int position = 0;
  for (size_t i = 0; i < layout.visible.size(); ++i) {
    if (layout.visible[i] == g_selected) {
      position = static_cast<int>(i);
      break;
    }
  }
  position = std::clamp(position + delta, 0, static_cast<int>(layout.visible.size()) - 1);
  g_selected = layout.visible[position];
  const Metrics metrics = MakeMetrics(g_hwnd);
  const int row_pitch = metrics.row_h + metrics.row_gap;
  const int top = position * row_pitch;
  const int view = std::max(1L, layout.list.bottom - layout.list.top);
  if (top < g_scroll) {
    g_scroll = top;
  } else if (top + metrics.row_h > g_scroll + view) {
    g_scroll = top + metrics.row_h - view;
  }
}

void AssignKey(int key) {
  Settings& settings = AppSettings();
  if (key == VK_ESCAPE) {
    g_binding = -2;
    SetStatus(L"Hotkey unchanged.", false);
    return;
  }
  if (key == VK_BACK) {
    if (g_binding == -1) {
      settings.menu_key = VK_NUMPAD1;
    } else if (g_binding >= 0) {
      settings.hotkey[g_binding] = 0;
    }
    g_binding = -2;
    settings.Save();
    SetStatus(L"Hotkey cleared.", false);
    return;
  }
  if (key == VK_SHIFT || key == VK_CONTROL || key == VK_MENU || (key >= VK_LBUTTON && key <= VK_XBUTTON2)) {
    return;
  }
  if (g_binding == -1) {
    for (int i = 0; i < kFeatureCount; ++i) {
      if (settings.hotkey[i] == key) {
        settings.hotkey[i] = 0;
      }
    }
    settings.menu_key = key;
    g_binding = -2;
    settings.Save();
    SetStatus(L"Menu hotkey updated.", false);
    return;
  }
  if (g_binding >= 0) {
    if (key == settings.menu_key) {
      SetStatus(L"That key already opens the menu.", true);
      return;
    }
    for (int i = 0; i < kFeatureCount; ++i) {
      if (i != g_binding && settings.hotkey[i] == key) {
        settings.hotkey[i] = 0;
      }
    }
    settings.hotkey[g_binding] = key;
    g_binding = -2;
    settings.Save();
    SetStatus(L"Feature hotkey updated.", false);
  }
}

void OnClick(HWND hwnd, POINT point) {
  const Layout layout = BuildLayout(hwnd);
  if (Hit(layout.close_btn, point)) {
    ShowWindow(hwnd, SW_HIDE);
    g_visible = false;
    LogLine(L"Menu hidden");
    return;
  }
  if (Hit(layout.min_btn, point)) {
    ShowWindow(hwnd, SW_MINIMIZE);
    return;
  }
  for (int i = 0; i < kPageCount; ++i) {
    if (Hit(layout.categories[i], point)) {
      g_page = static_cast<Page>(i);
      g_search.clear();
      g_search_focus = false;
      g_value_focus = false;
      g_scroll = 0;
      InvalidateRect(hwnd, nullptr, FALSE);
      return;
    }
  }
  if (Hit(layout.search_clear, point)) {
    g_search.clear();
    g_scroll = 0;
    InvalidateRect(hwnd, nullptr, FALSE);
    return;
  }
  if (Hit(layout.search, point)) {
    g_search_focus = true;
    InvalidateRect(hwnd, nullptr, FALSE);
    return;
  }
  g_search_focus = false;
  if (Hit(layout.menu_key, point)) {
    g_binding = -1;
    SetStatus(L"Press a key for the menu. Esc cancels. Backspace clears.", false);
    return;
  }
  const int scales[] = {100, 125, 150};
  for (int i = 0; i < 3; ++i) {
    if (Hit(layout.scale_chips[i], point)) {
      AppSettings().scale_percent = scales[i];
      AppSettings().Save();
      SetStatus(L"Size updated.", false);
      return;
    }
    if (Hit(layout.accent_chips[i], point)) {
      AppSettings().accent = i;
      AppSettings().Save();
      SetStatus(L"Accent updated.", false);
      return;
    }
  }
  if (g_search.empty() && g_page == Page::Debug) {
    if (Hit(layout.value_box, point)) {
      g_value_focus = true;
      g_search_focus = false;
      InvalidateRect(hwnd, nullptr, FALSE);
      return;
    }
    g_value_focus = false;
    if (Hit(layout.whole_chip, point)) {
      g_value_whole = true;
      InvalidateRect(hwnd, nullptr, FALSE);
      return;
    }
    if (Hit(layout.decimal_chip, point)) {
      g_value_whole = false;
      InvalidateRect(hwnd, nullptr, FALSE);
      return;
    }
    if (Hit(layout.clear_btn, point)) {
      ValueScanClear();
      SetStatus(L"Value search cleared.", false);
      return;
    }
    if (Hit(layout.first_btn, point) || Hit(layout.next_btn, point) || Hit(layout.write_btn, point)) {
      SetStatus(L"Searching the game. This menu will sit still until the search finishes.", false);
      UpdateWindow(hwnd);
      std::wstring error;
      bool ok = false;
      if (Hit(layout.first_btn, point)) {
        ok = ValueScanFirst(g_value_text, g_value_whole, error);
      } else if (Hit(layout.next_btn, point)) {
        ok = ValueScanNext(g_value_text, g_value_whole, error);
      } else {
        ok = ValueScanWrite(g_value_text, g_value_whole, error);
      }
      if (!ok) {
        SetStatus(error, true);
        return;
      }
      if (Hit(layout.write_btn, point)) {
        SetStatus(L"Wrote that number into the remaining copies. Check the game.", false);
      } else if (ValueScanCount() == 0) {
        SetStatus(L"No copies of that number are left. Clear and start again.", true);
      } else if (ValueScanCount() <= 8) {
        std::wstring text = std::to_wstring(ValueScanCount());
        text += L" copies left. Write number is unlocked.";
        SetStatus(text, false);
      } else {
        std::wstring text = std::to_wstring(ValueScanCount());
        text += L" copies. Change the number in game, type the new one, then Next scan.";
        SetStatus(text, false);
      }
      return;
    }
  }
  if (Hit(layout.scan_btn, point) && g_search.empty() && g_page == Page::Settings) {
    RunPublishedScan(false);
    SetStatus(g_status_error ? g_status : FeatureScanSummary(), g_status_error);
    return;
  }
  if (Hit(layout.reset_btn, point) && g_search.empty() && g_page == Page::Settings) {
    RestoreAllFeatures();
    for (float& value : g_anim) {
      value = 0.f;
    }
    AppSettings().Reset();
    g_binding = -2;
    SetStatus(L"Defaults restored.", false);
    return;
  }
  for (size_t i = 0; i < layout.visible.size(); ++i) {
    if (!Hit(layout.rows[i], point) || !Overlaps(layout.rows[i], layout.list)) {
      continue;
    }
    const Item& item = kItems[layout.visible[i]];
    g_selected = layout.visible[i];
    if (Hit(layout.keys[i], point) && ItemAvailable(item)) {
      g_binding = static_cast<int>(item.feature);
      SetStatus(L"Press a key. Esc cancels. Backspace clears.", false);
      return;
    }
    if (!ItemAvailable(item)) {
      SetStatus(ItemDetail(item), false);
      return;
    }
    ToggleFeature(item.feature);
    return;
  }
  InvalidateRect(hwnd, nullptr, FALSE);
}

void ShowMenu(bool show) {
  g_visible = show;
  if (g_hwnd == nullptr) {
    return;
  }
  if (show) {
    ShowWindow(g_hwnd, InPreview() ? SW_SHOW : SW_SHOWNOACTIVATE);
    LogLine(L"Menu shown");
  } else {
    ShowWindow(g_hwnd, SW_HIDE);
    LogLine(L"Menu hidden");
  }
}

void PollKeys() {
  PollMirianGrant();
  PollPlayerCapture();
  auto down = [](int key) { return key != 0 && (GetAsyncKeyState(key) & 0x8000) != 0; };
  static bool menu_down = false;
  static bool home_down = false;
  static bool feature_down[kFeatureCount] = {};
  if (g_binding != -2) {
    menu_down = down(AppSettings().menu_key);
    home_down = down(VK_HOME);
    return;
  }
  const int menu_key = AppSettings().menu_key;
  const bool menu_now = down(menu_key);
  static DWORD last_menu_toggle = 0;
  if (menu_now && !menu_down && GetTickCount() - last_menu_toggle > 400) {
    last_menu_toggle = GetTickCount();
    ShowMenu(!g_visible);
  }
  menu_down = menu_now;
  const bool home_now = down(VK_HOME);
  if (home_now && !home_down && AppSettings().menu_key != VK_HOME) {
    RunPublishedScan(!g_visible);
  }
  home_down = home_now;
  if (g_visible) {
    return;
  }
  for (int i = 0; i < kFeatureCount; ++i) {
    const bool now = down(AppSettings().hotkey[i]);
    if (now && !feature_down[i] && FeaturesReady(static_cast<FeatureId>(i))) {
      ToggleFeature(static_cast<FeatureId>(i));
    }
    feature_down[i] = now;
  }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_ERASEBKGND:
      return 1;
    case WM_MOUSEACTIVATE:
      return MA_NOACTIVATE;
    case WM_NCCALCSIZE:
      if (wparam) {
        if (IsZoomed(hwnd)) {
          MONITORINFO info = {sizeof(info)};
          if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &info)) {
            reinterpret_cast<NCCALCSIZE_PARAMS*>(lparam)->rgrc[0] = info.rcWork;
          }
        }
        return 0;
      }
      break;
    case WM_NCHITTEST: {
      POINT point = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      ScreenToClient(hwnd, &point);
      RECT client = {};
      GetClientRect(hwnd, &client);
      const int border = 6;
      if (point.y < border && point.x < border) return HTTOPLEFT;
      if (point.y < border && point.x > client.right - border) return HTTOPRIGHT;
      if (point.y > client.bottom - border && point.x < border) return HTBOTTOMLEFT;
      if (point.y > client.bottom - border && point.x > client.right - border) return HTBOTTOMRIGHT;
      if (point.y < border) return HTTOP;
      if (point.y > client.bottom - border) return HTBOTTOM;
      if (point.x < border) return HTLEFT;
      if (point.x > client.right - border) return HTRIGHT;
      const Layout layout = BuildLayout(hwnd);
      if (Hit(layout.close_btn, point) || Hit(layout.min_btn, point)) {
        return HTCLIENT;
      }
      if (point.y < MakeMetrics(hwnd).header) {
        return HTCAPTION;
      }
      return HTCLIENT;
    }
    case WM_GETMINMAXINFO: {
      auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
      const Metrics metrics = MakeMetrics(hwnd);
      info->ptMinTrackSize.x = std::max(860, metrics.sidebar + 560);
      info->ptMinTrackSize.y = std::max(560, metrics.header + metrics.status + 420);
      return 0;
    }
    case WM_DPICHANGED: {
      const RECT* suggested = reinterpret_cast<RECT*>(lparam);
      SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                   suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    }
    case WM_SIZE:
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    case WM_MOUSEMOVE: {
      const Layout layout = BuildLayout(hwnd);
      POINT point = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      int hover = -1;
      for (size_t i = 0; i < layout.visible.size(); ++i) {
        if (Hit(layout.rows[i], point) && Overlaps(layout.rows[i], layout.list)) {
          hover = layout.visible[i];
          break;
        }
      }
      if (hover != g_hover) {
        g_hover = hover;
        if (hover >= 0) {
          g_status = ItemDetail(kItems[hover]);
          g_status_error = !ItemAvailable(kItems[hover]);
        }
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      return 0;
    }
    case WM_LBUTTONDOWN:
      OnClick(hwnd, POINT{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
      return 0;
    case WM_MOUSEWHEEL:
      g_scroll -= GET_WHEEL_DELTA_WPARAM(wparam) / 120 * MakeMetrics(hwnd).row_h;
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    case WM_CHAR:
      if (g_binding == -2 && g_value_focus) {
        if (wparam == 8) {
          if (!g_value_text.empty()) {
            g_value_text.pop_back();
          }
        } else if ((wparam >= L'0' && wparam <= L'9') || wparam == L'.' || wparam == L'-') {
          if (g_value_text.size() < 16 && !(wparam == L'-' && !g_value_text.empty()) &&
              !(wparam == L'.' && g_value_text.find(L'.') != std::wstring::npos)) {
            g_value_text.push_back(static_cast<wchar_t>(wparam));
          }
        } else {
          return 0;
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      if (g_binding != -2 || !g_search_focus) {
        return 0;
      }
      if (wparam == 8) {
        if (!g_search.empty()) {
          g_search.pop_back();
        }
      } else if (wparam >= 32 && g_search.size() < 40) {
        g_search.push_back(static_cast<wchar_t>(wparam));
      } else {
        return 0;
      }
      g_scroll = 0;
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    case WM_KEYDOWN:
      if (g_binding != -2) {
        AssignKey(static_cast<int>(wparam));
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      if (wparam == 'F' && (GetKeyState(VK_CONTROL) & 0x8000)) {
        g_search_focus = true;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      if (wparam == VK_ESCAPE) {
        ShowMenu(false);
        return 0;
      }
      if (wparam == VK_TAB) {
        int page = static_cast<int>(g_page) + ((GetKeyState(VK_SHIFT) & 0x8000) ? -1 : 1);
        if (page < 0) page = kPageCount - 1;
        if (page >= kPageCount) page = 0;
        g_page = static_cast<Page>(page);
        g_scroll = 0;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      if (wparam == VK_LEFT || wparam == VK_RIGHT) {
        int page = static_cast<int>(g_page) + (wparam == VK_RIGHT ? 1 : -1);
        if (page < 0) page = kPageCount - 1;
        if (page >= kPageCount) page = 0;
        g_page = static_cast<Page>(page);
        g_search.clear();
        g_scroll = 0;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      if (wparam == VK_UP || wparam == VK_DOWN) {
        SelectVisible(BuildLayout(hwnd), wparam == VK_DOWN ? 1 : -1);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      if (g_value_focus && (wparam == VK_SPACE || wparam == VK_RETURN)) {
        g_value_focus = false;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      if ((wparam == VK_SPACE || wparam == VK_RETURN) && !(g_search_focus && wparam == VK_SPACE)) {
        if (g_search_focus && wparam == VK_RETURN) {
          g_search_focus = false;
          InvalidateRect(hwnd, nullptr, FALSE);
          return 0;
        }
        const Layout layout = BuildLayout(hwnd);
        for (int index : layout.visible) {
          if (index == g_selected && ItemAvailable(kItems[index])) {
            ToggleFeature(kItems[index].feature);
            break;
          }
        }
        return 0;
      }
      break;
    case WM_TIMER: {
      bool dirty = false;
      for (int i = 0; i < kFeatureCount; ++i) {
        const float target = FeatureEnabled(static_cast<FeatureId>(i)) ? 1.f : 0.f;
        if (std::fabs(g_anim[i] - target) > 0.01f) {
          g_anim[i] += (target - g_anim[i]) * 0.35f;
          dirty = true;
        } else {
          g_anim[i] = target;
        }
      }
      const int blink = static_cast<int>((GetTickCount() / 500) % 2);
      if (g_search_focus && blink != g_blink) {
        g_blink = blink;
        dirty = true;
      }
      if (dirty) {
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      return 0;
    }
    case WM_CLOSE:
      ShowMenu(false);
      return 0;
    case kShutdownMessage:
      RestoreAllFeatures();
      DestroyWindow(hwnd);
      return 0;
    case WM_PAINT: {
      PAINTSTRUCT paint = {};
      HDC dc = BeginPaint(hwnd, &paint);
      RECT client = {};
      GetClientRect(hwnd, &client);
      const int width = std::max(1L, client.right);
      const int height = std::max(1L, client.bottom);
      HDC memory = CreateCompatibleDC(dc);
      HBITMAP bitmap = CreateCompatibleBitmap(dc, width, height);
      HGDIOBJ old = SelectObject(memory, bitmap);
      {
        Graphics graphics(memory);
        graphics.SetSmoothingMode(SmoothingModeAntiAlias);
        graphics.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
        PaintMenu(graphics, hwnd, width, height);
      }
      BitBlt(dc, 0, 0, width, height, memory, 0, 0, SRCCOPY);
      SelectObject(memory, old);
      DeleteObject(bitmap);
      DeleteDC(memory);
      EndPaint(hwnd, &paint);
      return 0;
    }
    case WM_DESTROY:
      KillTimer(hwnd, 1);
      g_fonts.Clear();
      PostQuitMessage(0);
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
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

HMODULE ThisModule() {
  HMODULE self = nullptr;
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                     reinterpret_cast<LPCWSTR>(&ThisModule), &self);
  return self != nullptr ? self : GetModuleHandleW(nullptr);
}

void RequestMenuShutdown() {
  g_quit = true;
  if (g_hwnd != nullptr) {
    PostMessageW(g_hwnd, kShutdownMessage, 0, 0);
  }
  if (g_menu_thread != nullptr) {
    WaitForSingleObject(g_menu_thread, 3000);
  }
}

void RunShadowMenu() {
  g_menu_thread = OpenThread(SYNCHRONIZE, FALSE, GetCurrentThreadId());
  LogLine(L"Sky's Shadow menu loaded");
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  AppSettings().Load();
  RunPublishedScan(false);
  if (!g_status_error && !InPreview() && !HostIsGame()) {
    SetStatus(L"This menu is not running inside Shadow of War. Numpad 1 opens it. Home scans.", true);
  }

  GdiplusStartupInput input;
  if (GdiplusStartup(&g_gdi_token, &input, nullptr) != Ok) {
    LogLine(L"GDI+ failed to start");
    return;
  }

  WNDCLASSW window_class = {};
  window_class.lpfnWndProc = WindowProc;
  window_class.hInstance = ThisModule();
  window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  window_class.lpszClassName = kClassName;
  if (RegisterClassW(&window_class) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    LogLine(L"Failed to register the menu window");
    GdiplusShutdown(g_gdi_token);
    return;
  }

  RECT work = {};
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
  HWND game = FindGameWindow();
  if (game != nullptr) {
    RECT game_rect = {};
    if (GetWindowRect(game, &game_rect)) {
      work = game_rect;
    }
  }
  int width = 1120;
  int height = 760;
  const int work_w = work.right - work.left;
  const int work_h = work.bottom - work.top;
  if (work_w < width + 40) width = std::max(860, work_w - 40);
  if (work_h < height + 40) height = std::max(560, work_h - 40);
  const int x = work.left + std::max(0, (work_w - width) / 2);
  const int y = work.top + std::max(0, (work_h - height) / 2);

  const bool in_game = HostIsGame();
  g_hwnd = CreateWindowExW(in_game ? (WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE) : WS_EX_APPWINDOW, kClassName,
                           L"Sky's Shadow", WS_POPUP | WS_THICKFRAME, x, y, width, height, in_game ? game : nullptr,
                           nullptr, ThisModule(), nullptr);
  if (g_hwnd == nullptr) {
    LogLine(L"Failed to create the menu window");
    GdiplusShutdown(g_gdi_token);
    return;
  }
  const int corner = 2;
  DwmSetWindowAttribute(g_hwnd, 33, &corner, sizeof(corner));
  SetTimer(g_hwnd, 1, 16, nullptr);
  if (InPreview()) {
    ShowMenu(true);
  } else {
    LogLine(L"Menu is hidden. Numpad 1 opens it. Home scans and leaves it open.");
  }

  while (!g_quit) {
    DWORD wait = MsgWaitForMultipleObjects(0, nullptr, FALSE, 15, QS_ALLINPUT);
    (void)wait;
    MSG message = {};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      if (message.message == WM_QUIT) {
        g_quit = true;
        break;
      }
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    if (g_quit) {
      break;
    }
    PollKeys();
  }

  RestoreAllFeatures();
  g_fonts.Clear();
  if (g_gdi_token != 0) {
    GdiplusShutdown(g_gdi_token);
    g_gdi_token = 0;
  }
  LogLine(L"Sky's Shadow menu stopped");
  CloseLogConsole();
}
