#include "nyla/wm/window_manager.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <iterator>
#include <limits>
#include <span>
#include <variant>
#include <vector>

#include "absl/cleanup/cleanup.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_join.h"
#include "nyla/commons/rect.h"
#include "nyla/layout/layout.h"
#include "nyla/wm/palette.h"
#include "nyla/x11/error.h"
#include "nyla/x11/wm_hints.h"
#include "nyla/x11/x11.h"
#include "xcb/xcb.h"
#include "xcb/xproto.h"

namespace nyla {

template <typename Key, typename Val>
using Map = absl::flat_hash_map<Key, Val>;

struct WindowStack {
  LayoutType layout_type;
  bool zoom;

  std::vector<xcb_window_t> windows;
  xcb_window_t active_window;
};

struct Client {
  Rect rect;
  uint32_t border_width;
  std::string name;

  bool wm_hints_input;
  bool wm_take_focus;
  bool wm_delete_window;

  uint32_t max_width;
  uint32_t max_height;

  bool urgent;
  bool wants_configure_notify;

  xcb_window_t transient_for;
  std::vector<xcb_window_t> subwindows;

  Map<xcb_atom_t, xcb_get_property_cookie_t> property_cookies;
};

template <typename Sink>
void AbslStringify(Sink& sink, const Client& c) {
  absl::Format(&sink, "Window{ rect=%v, input=%v, take_focus=%v }", c.rect,
               c.wm_hints_input, c.wm_take_focus);
}

static uint32_t wm_bar_height = 20;
static xcb_window_t wm_bar_window;
static xcb_gcontext_t wm_bar_gc;
bool wm_bar_dirty;
bool wm_bar_hidden;

static bool wm_layout_dirty;
static bool wm_follow;
static bool wm_border_dirty;

static Map<xcb_window_t, Client> wm_clients;
static std::vector<xcb_window_t> wm_pending_clients;

static Map<xcb_atom_t,
           void (*)(xcb_window_t, Client&, xcb_get_property_reply_t*)>
    wm_property_change_handlers;

static std::vector<WindowStack> wm_stacks;
static size_t wm_active_stack_idx;

//

static WindowStack& GetActiveStack() {
  CHECK_LT(wm_active_stack_idx, wm_stacks.size());
  return wm_stacks.at(wm_active_stack_idx);
}

static void Handle_WM_Hints(xcb_window_t client_window, Client& client,
                            xcb_get_property_reply_t* reply) {
  WM_Hints wm_hints = [&reply] {
    if (!reply || xcb_get_property_value_length(reply) != sizeof(WM_Hints))
      return WM_Hints{};

    return *static_cast<WM_Hints*>(xcb_get_property_value(reply));
  }();

  Initialize(wm_hints);

  client.wm_hints_input = wm_hints.input;

  // if (wm_hints.urgent() && !client.urgent) indicator?
  client.urgent = wm_hints.urgent();
}

static void Handle_WM_Normal_Hints(xcb_window_t client_window, Client& client,
                                   xcb_get_property_reply_t* reply) {
  WM_Normal_Hints wm_normal_hints = [&reply] {
    if (!reply ||
        xcb_get_property_value_length(reply) != sizeof(WM_Normal_Hints))
      return WM_Normal_Hints{};

    return *static_cast<WM_Normal_Hints*>(xcb_get_property_value(reply));
  }();

  Initialize(wm_normal_hints);
  // LOG(INFO) << client_window << " " << wm_normal_hints;

  client.max_width = wm_normal_hints.max_width;
  client.max_height = wm_normal_hints.max_height;
}

static void Handle_WM_Name(xcb_window_t client_window, Client& client,
                           xcb_get_property_reply_t* reply) {
  if (!reply) {
    LOG(ERROR) << "property fetch error";
    return;
  }

  client.name = {static_cast<char*>(xcb_get_property_value(reply)),
                 static_cast<size_t>(xcb_get_property_value_length(reply))};
  // LOG(INFO) << client_window << " name=" << client.name;
}

static void Handle_WM_Protocols(xcb_window_t client_window, Client& client,
                                xcb_get_property_reply_t* reply) {
  if (!reply) return;
  if (reply->type != XCB_ATOM_ATOM) return;

  client.wm_delete_window = false;
  client.wm_take_focus = false;

  auto wm_protocols = std::span{
      static_cast<xcb_atom_t*>(xcb_get_property_value(reply)),
      xcb_get_property_value_length(reply) / sizeof(xcb_atom_t),
  };
  // LOG(INFO) << client_window << " " << absl::StrJoin(wm_protocols, ", ");

  for (xcb_atom_t atom : wm_protocols) {
    if (atom == x11.atoms.wm_delete_window) {
      client.wm_delete_window = true;
      continue;
    }
    if (atom == x11.atoms.wm_take_focus) {
      client.wm_take_focus = true;
      continue;
    }
  }
}

static void Handle_WM_Transient_For(xcb_window_t client_window, Client& client,
                                    xcb_get_property_reply_t* reply) {
  if (!reply || !reply->length) return;
  if (reply->type != XCB_ATOM_WINDOW) return;

  if (client.transient_for != 0) return;

  client.transient_for =
      *reinterpret_cast<xcb_window_t*>(xcb_get_property_value(reply));
}

void InitializeWM() {
  wm_stacks.resize(9);

  wm_property_change_handlers.try_emplace(XCB_ATOM_WM_HINTS, Handle_WM_Hints);
  wm_property_change_handlers.try_emplace(XCB_ATOM_WM_NORMAL_HINTS,
                                          Handle_WM_Normal_Hints);
  wm_property_change_handlers.try_emplace(XCB_ATOM_WM_NAME, Handle_WM_Name);
  wm_property_change_handlers.try_emplace(x11.atoms.wm_protocols,
                                          Handle_WM_Protocols);
  wm_property_change_handlers.try_emplace(XCB_ATOM_WM_TRANSIENT_FOR,
                                          Handle_WM_Transient_For);

  wm_bar_window = xcb_generate_id(x11.conn);

  if (xcb_request_check(
          x11.conn,
          xcb_create_window_checked(
              x11.conn, XCB_COPY_FROM_PARENT, wm_bar_window, x11.screen->root,
              0, 0, x11.screen->width_in_pixels, wm_bar_height, 0,
              XCB_WINDOW_CLASS_INPUT_OUTPUT, x11.screen->root_visual,
              XCB_CW_BACK_PIXEL | XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK,
              (uint32_t[]){
                  x11.screen->black_pixel,
                  true,
                  XCB_EVENT_MASK_EXPOSURE,
              }))) {
    LOG(ERROR) << "could not create bar window";
  }

  xcb_map_window(x11.conn, wm_bar_window);

  constexpr const char* kFontName = "fixed";

  xcb_font_t font = xcb_generate_id(x11.conn);

  if (xcb_request_check(x11.conn,
                        xcb_open_font_checked(x11.conn, font, strlen(kFontName),
                                              kFontName))) {
    LOG(ERROR) << "could not create bar font";
  }

  wm_bar_gc = xcb_generate_id(x11.conn);
  if (xcb_request_check(x11.conn,
                        xcb_create_gc_checked(
                            x11.conn, wm_bar_gc, wm_bar_window,
                            XCB_GC_FOREGROUND | XCB_GC_BACKGROUND | XCB_GC_FONT,
                            (uint32_t[]){x11.screen->white_pixel,
                                         x11.screen->black_pixel, font}))) {
    LOG(ERROR) << "could not create bar GC";
  }
}

static void ClearZoom(WindowStack& stack) {
  if (!stack.zoom) return;

  stack.zoom = false;
  wm_layout_dirty = true;
}

static void ApplyBorder(xcb_connection_t* conn, xcb_window_t window,
                        Color color) {
  if (!window) return;
  xcb_change_window_attributes(conn, window, XCB_CW_BORDER_PIXEL, &color);
}

static void Activate(const WindowStack& stack, xcb_timestamp_t time) {
  if (!stack.active_window) goto revert_to_root;

  if (auto it = wm_clients.find(stack.active_window); it != wm_clients.end()) {
    wm_border_dirty = true;

    const auto& client = it->second;

    xcb_window_t immediate_focus =
        client.wm_hints_input ? stack.active_window : x11.screen->root;

    xcb_set_input_focus(x11.conn, XCB_INPUT_FOCUS_NONE, immediate_focus, time);

    if (client.wm_take_focus) {
      X11_Send_WM_Take_Focus(stack.active_window, time);
    }

    return;
  }

revert_to_root:
  xcb_set_input_focus(x11.conn, XCB_INPUT_FOCUS_NONE, x11.screen->root, time);
}

static void Activate(WindowStack& stack, xcb_window_t client_window,
                     xcb_timestamp_t time) {
  if (stack.active_window != client_window) {
    ApplyBorder(x11.conn, stack.active_window, Color::kNone);
    stack.active_window = client_window;
    wm_bar_dirty = true;
  }

  Activate(stack, time);
}

static void CheckFocusTheft() {
  auto reply = xcb_get_input_focus_reply(
      x11.conn, xcb_get_input_focus(x11.conn), nullptr);
  xcb_window_t focused_window = reply->focus;
  free(reply);

  const WindowStack& stack = GetActiveStack();
  if (stack.active_window == focused_window) return;

  if (focused_window == x11.screen->root) return;
  if (!focused_window) return;

  if (!wm_clients.contains(focused_window)) {
    for (;;) {
      xcb_query_tree_reply_t* reply = xcb_query_tree_reply(
          x11.conn, xcb_query_tree(x11.conn, focused_window), nullptr);

      if (!reply) {
        Activate(stack, XCB_CURRENT_TIME);
        return;
      }

      xcb_window_t parent = reply->parent;
      free(reply);

      if (!parent || parent == x11.screen->root) break;
      focused_window = parent;
    }
  }

  if (stack.active_window == focused_window) return;

  auto it = wm_clients.find(focused_window);
  if (it == wm_clients.end()) {
    Activate(stack, XCB_CURRENT_TIME);
    return;
  }

  const auto& [_, client] = *it;
  if (client.transient_for) {
    if (client.transient_for == stack.active_window) {
      return;
    }

    if (auto it = wm_clients.find(stack.active_window);
        it != wm_clients.end() &&
        client.transient_for == it->second.transient_for) {
      return;
    }
  }

  Activate(stack, XCB_CURRENT_TIME);
}

static void FetchClientProperty(xcb_window_t client_window, Client& client,
                                xcb_atom_t property) {
  if (!wm_property_change_handlers.contains(property)) return;

  auto cookie = xcb_get_property_unchecked(
      x11.conn, false, client_window, property, XCB_ATOM_ANY, 0,
      std::numeric_limits<uint32_t>::max());

  auto it = client.property_cookies.find(property);
  if (it == client.property_cookies.end()) {
    client.property_cookies.try_emplace(property, cookie);
  }
}

void ManageClient(xcb_window_t client_window) {
  if (auto [it, inserted] = wm_clients.try_emplace(client_window, Client{});
      inserted) {
    xcb_change_window_attributes(
        x11.conn, client_window, XCB_CW_EVENT_MASK,
        (uint32_t[]){
            XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_FOCUS_CHANGE |
                XCB_EVENT_MASK_ENTER_WINDOW,
        });

    for (auto& [property, _] : wm_property_change_handlers) {
      FetchClientProperty(client_window, it->second, property);
    }

    wm_pending_clients.emplace_back(client_window);
  }
}

void ManageClientsStartup() {
  xcb_query_tree_reply_t* tree_reply = xcb_query_tree_reply(
      x11.conn, xcb_query_tree(x11.conn, x11.screen->root), nullptr);
  if (!tree_reply) return;

  std::span<xcb_window_t> children = {
      xcb_query_tree_children(tree_reply),
      static_cast<size_t>(xcb_query_tree_children_length(tree_reply))};

  for (xcb_window_t client_window : children) {
    xcb_get_window_attributes_reply_t* attr_reply =
        xcb_get_window_attributes_reply(
            x11.conn, xcb_get_window_attributes(x11.conn, client_window),
            nullptr);
    if (!attr_reply) continue;
    absl::Cleanup attr_reply_freer = [attr_reply] { free(attr_reply); };

    if (attr_reply->override_redirect) continue;
    if (attr_reply->map_state == XCB_MAP_STATE_UNMAPPED) continue;

    ManageClient(client_window);
  }

  free(tree_reply);
}

void UnmanageClient(xcb_window_t window) {
  auto it = wm_clients.find(window);
  if (it == wm_clients.end()) return;

  auto& client = it->second;

  for (auto& [_, cookie] : client.property_cookies)
    xcb_discard_reply(x11.conn, cookie.sequence);

  if (client.transient_for) {
    CHECK(client.subwindows.empty());
    auto& subwindows = wm_clients.at(client.transient_for).subwindows;
    auto it = std::ranges::find(subwindows, window);
    CHECK(it != subwindows.end());
    subwindows.erase(it);
  } else {
    for (xcb_window_t subwindow : client.subwindows) {
      wm_clients.at(subwindow).transient_for = 0;
    }
  }

  wm_clients.erase(it);

  for (size_t istack = 0; istack < wm_stacks.size(); ++istack) {
    WindowStack& stack = wm_stacks.at(istack);

    auto it = std::ranges::find_if(
        stack.windows, [window](xcb_window_t elem) { return elem == window; });
    if (it == stack.windows.end()) continue;

    stack.zoom = false;
    stack.windows.erase(it);
    wm_layout_dirty = true;

    if (stack.active_window == window) {
      stack.active_window = 0;
      wm_follow = false;

      if (istack == wm_active_stack_idx) {
        Activate(stack, stack.windows.empty() ? 0 : stack.windows.front(),
                 XCB_CURRENT_TIME);
      }
    }
    return;
  }
}

static void ConfigureClientIfNeeded(xcb_connection_t* conn,
                                    xcb_window_t client_window, Client& client,
                                    const Rect& new_rect,
                                    uint32_t new_border_width) {
  uint16_t mask = 0;
  std::vector<uint32_t> values;
  bool anything_changed = false;
  bool size_changed = false;

  if (new_rect.x() != client.rect.x()) {
    anything_changed = true;
    mask |= XCB_CONFIG_WINDOW_X;
    values.emplace_back(new_rect.x());
  }

  if (new_rect.y() != client.rect.y()) {
    anything_changed = true;
    mask |= XCB_CONFIG_WINDOW_Y;
    values.emplace_back(new_rect.y());
  }

  if (new_rect.width() != client.rect.width()) {
    anything_changed = true;
    size_changed = true;
    mask |= XCB_CONFIG_WINDOW_WIDTH;
    values.emplace_back(new_rect.width());
  }

  if (new_rect.height() != client.rect.height()) {
    anything_changed = true;
    size_changed = true;
    mask |= XCB_CONFIG_WINDOW_HEIGHT;
    values.emplace_back(new_rect.height());
  }

  if (new_border_width != client.border_width) {
    anything_changed = true;
    size_changed = true;
    mask |= XCB_CONFIG_WINDOW_BORDER_WIDTH;
    values.emplace_back(new_border_width);
  }

  if (anything_changed) {
    xcb_configure_window(conn, client_window, mask, values.data());

    client.wants_configure_notify = !size_changed;
    client.rect = new_rect;
    client.border_width = new_border_width;
  }
}

static void MoveStack(xcb_timestamp_t time, auto compute_idx) {
  size_t iold = wm_active_stack_idx;
  size_t inew = compute_idx(iold + wm_stacks.size()) % wm_stacks.size();

  if (iold == inew) return;

  wm_bar_dirty = true;

  WindowStack& oldstack = GetActiveStack();
  wm_active_stack_idx = inew;
  WindowStack& newstack = GetActiveStack();

  if (wm_follow) {
    if (oldstack.active_window) {
      newstack.active_window = oldstack.active_window;
      newstack.windows.emplace_back(oldstack.active_window);

      newstack.zoom = false;
      oldstack.zoom = false;

      auto it = std::ranges::find(oldstack.windows, oldstack.active_window);
      CHECK_NE(it, oldstack.windows.end());
      oldstack.windows.erase(it);

      if (oldstack.windows.empty())
        oldstack.active_window = 0;
      else
        oldstack.active_window = oldstack.windows.at(0);
    }
  } else {
    ApplyBorder(x11.conn, oldstack.active_window, Color::kNone);
    Activate(newstack, newstack.active_window, time);
  }

  wm_layout_dirty = true;
}

void MoveStackNext(xcb_timestamp_t time) {
  MoveStack(time, [](auto idx) { return idx + 1; });
}

void MoveStackPrev(xcb_timestamp_t time) {
  MoveStack(time, [](auto idx) { return idx - 1; });
}

static void MoveLocal(xcb_timestamp_t time, auto compute_idx) {
  WindowStack& stack = GetActiveStack();
  ClearZoom(stack);

  if (stack.windows.empty()) return;

  if (stack.active_window && stack.windows.size() < 2) {
    return;
  }

  if (stack.active_window) {
    auto it = std::ranges::find(stack.windows, stack.active_window);
    CHECK_NE(it, stack.windows.end());

    size_t iold = std::distance(stack.windows.begin(), it);
    size_t inew =
        compute_idx(iold + stack.windows.size()) % stack.windows.size();

    if (iold == inew) return;

    if (wm_follow) {
      std::iter_swap(stack.windows.begin() + iold,
                     stack.windows.begin() + inew);
      wm_layout_dirty = true;
    } else {
      Activate(stack, stack.windows.at(inew), time);
    }
  } else {
    if (!wm_follow) {
      Activate(stack, stack.windows.front(), time);
    }
  }
}

void MoveLocalNext(xcb_timestamp_t time) {
  MoveLocal(time, [](auto idx) { return idx + 1; });
}

void MoveLocalPrev(xcb_timestamp_t time) {
  MoveLocal(time, [](auto idx) { return idx - 1; });
}

void NextLayout() {
  WindowStack& stack = GetActiveStack();
  CycleLayoutType(stack.layout_type);
  wm_layout_dirty = true;
  ClearZoom(stack);
}

std::string GetActiveClientBarText() {
  const WindowStack& stack = GetActiveStack();
  xcb_window_t active_window = stack.active_window;
  if (!active_window || !wm_clients.contains(active_window))
    return "nyla: no active client";

  return wm_clients.at(active_window).name;
}

void CloseActive() {
  WindowStack& stack = GetActiveStack();
  if (!stack.active_window) return;

  static absl::Time last = absl::InfinitePast();
  if (absl::Now() - last >= absl::Milliseconds(100)) {
    X11_Send_WM_Delete_Window(stack.active_window);
  }
  last = absl::Now();
}

void ToggleZoom() {
  WindowStack& stack = GetActiveStack();
  stack.zoom ^= 1;
  wm_bar_dirty = true;
  wm_layout_dirty = true;
  wm_border_dirty = true;
}

void ToggleFollow() {
  WindowStack& stack = GetActiveStack();
  if (!stack.active_window) {
    wm_follow = false;
    return;
  }

  wm_follow ^= 1;
  if (!wm_follow) ClearZoom(stack);

  wm_border_dirty = true;
}

//

void ProcessWM() {
  for (auto& [client_window, client] : wm_clients) {
    for (auto& [property, cookie] : client.property_cookies) {
      xcb_get_property_reply_t* reply =
          xcb_get_property_reply(x11.conn, cookie, nullptr);
      if (!reply) continue;

      auto handler_it = wm_property_change_handlers.find(property);
      if (handler_it == wm_property_change_handlers.end()) {
        LOG(ERROR) << "missing property change handler " << property;
        continue;
      }

      handler_it->second(client_window, client, reply);
    }
    client.property_cookies.clear();
  }

  WindowStack& stack = GetActiveStack();

  if (!wm_pending_clients.empty()) {
    for (xcb_window_t client_window : wm_pending_clients) {
      auto it = wm_clients.find(client_window);
      if (it == wm_clients.end()) continue;

      auto& [_, client] = *it;
      if (client.transient_for) {
        bool found = false;
        for (int i = 0; i < 10; ++i) {
          auto it = wm_clients.find(client.transient_for);
          if (it == wm_clients.end()) break;

          xcb_window_t next_transient = it->second.transient_for;
          if (!next_transient) {
            found = true;
            break;
          }
          client.transient_for = next_transient;
        }
        if (!found) client.transient_for = 0;
      }

      if (!client.transient_for || client.transient_for != stack.active_window)
        ClearZoom(stack);
    }

    bool activated = false;
    for (xcb_window_t client_window : wm_pending_clients) {
      const auto& client = wm_clients.at(client_window);
      if (client.transient_for) {
        Client& parent = wm_clients.at(client.transient_for);
        parent.subwindows.push_back(client_window);
      } else {
        stack.windows.emplace_back(client_window);

        if (!activated) {
          Activate(stack, client_window, XCB_CURRENT_TIME);
          activated = true;
        }
      }
    }

    wm_pending_clients.clear();
    wm_layout_dirty = true;
  }

  if (wm_border_dirty) {
    Color color = [&stack] {
      if (wm_follow) return Color::kActiveFollow;
      if (stack.zoom || stack.windows.size() < 2) return Color::kNone;
      return Color::kActive;
    }();
    ApplyBorder(x11.conn, stack.active_window, color);

    wm_border_dirty = false;
  }

  if (wm_layout_dirty) {
    Rect screen_rect =
        Rect(x11.screen->width_in_pixels, x11.screen->height_in_pixels);
    if (!stack.zoom)
      screen_rect = TryApplyMarginTop(screen_rect, wm_bar_height);

    auto hide = [](xcb_window_t client_window, Client& client) {
      ConfigureClientIfNeeded(
          x11.conn, client_window, client,
          Rect{x11.screen->width_in_pixels, x11.screen->height_in_pixels,
               client.rect.width(), client.rect.height()},
          client.border_width);
    };

    auto hide_all = [hide](xcb_window_t client_window, Client& client) {
      hide(client_window, client);
      for (xcb_window_t subwindow : client.subwindows)
        hide(subwindow, wm_clients.at(subwindow));
    };

    auto configure_windows = [](Rect bounding_rect,
                                std::span<const xcb_window_t> windows,
                                LayoutType layout_type, auto visitor) {
      std::vector<Rect> layout =
          ComputeLayout(bounding_rect, windows.size(), 2, layout_type);
      CHECK_EQ(layout.size(), windows.size());

      for (auto [rect, client_window] :
           std::ranges::views::zip(layout, windows)) {
        Client& client = wm_clients.at(client_window);

        auto center = [](uint32_t max, uint32_t& w, int32_t& x) {
          if (max) {
            uint32_t tmp = std::min(max, w);
            x += (w - tmp) / 2;
            w = tmp;
          }
        };
        center(client.max_width, rect.width(), rect.x());
        center(client.max_height, rect.height(), rect.y());

        ConfigureClientIfNeeded(x11.conn, client_window, client, rect, 2);

        visitor(client);
      }
    };

    auto configure_subwindows = [configure_windows](const Client& client) {
      configure_windows(TryApplyMargin(client.rect, 20), client.subwindows,
                        LayoutType::kRows, [](Client& client) {});
    };

    if (stack.zoom) {
      for (xcb_window_t client_window : stack.windows) {
        auto& client = wm_clients.at(client_window);

        if (client_window != stack.active_window) {
          hide_all(client_window, client);
        } else {
          ConfigureClientIfNeeded(x11.conn, client_window, client, screen_rect,
                                  wm_follow ? 2 : 0);

          configure_subwindows(client);
        }
      }
    } else {
      configure_windows(screen_rect, stack.windows, stack.layout_type,
                        configure_subwindows);
    }

    for (size_t istack = 0; istack < wm_stacks.size(); ++istack) {
      if (istack != wm_active_stack_idx) {
        for (xcb_window_t client_window : wm_stacks[istack].windows)
          hide_all(client_window, wm_clients.at(client_window));
      }
    }

    wm_layout_dirty = false;
  }

  for (auto& [client_window, client] : wm_clients) {
    if (client.wants_configure_notify) {
      X11_SendConfigureNotify(client_window, x11.screen->root, client.rect.x(),
                              client.rect.y(), client.rect.width(),
                              client.rect.height(), 2);
      client.wants_configure_notify = false;
    }
  }
}

void ProcessWMEvents(
    const bool& is_running, uint16_t modifier,
    std::vector<std::pair<
        xcb_keycode_t,
        std::variant<void (*)(xcb_timestamp_t timestamp), void (*)()>>>
        keybinds) {
  while (is_running) {
    xcb_generic_event_t* event = xcb_poll_for_event(x11.conn);
    if (!event) break;
    absl::Cleanup event_freer = [event] { free(event); };

    bool is_synthethic = event->response_type & 0x80;
    uint8_t event_type = event->response_type & 0x7F;

    WindowStack& stack = GetActiveStack();

    if (is_synthethic && event_type != XCB_CLIENT_MESSAGE) {
      // continue;
    }

    switch (event_type) {
      case XCB_KEY_PRESS: {
        auto keypress = reinterpret_cast<xcb_key_press_event_t*>(event);
        if (keypress->state == modifier) {
          for (const auto& [keycode, fn] : keybinds) {
            if (keycode == keypress->detail) {
              if (std::holds_alternative<void (*)()>(fn)) {
                std::get<void (*)()>(fn)();
              } else if (std::holds_alternative<void (*)(xcb_timestamp_t time)>(
                             fn)) {
                std::get<void (*)(xcb_timestamp_t time)>(fn)(keypress->time);
              } else {
                CHECK(false);
              }
              break;
            }
          }
        }
        break;
      }
      case XCB_PROPERTY_NOTIFY: {
        auto propertynotify =
            reinterpret_cast<xcb_property_notify_event_t*>(event);

        xcb_window_t client_window = propertynotify->window;
        auto it = wm_clients.find(client_window);
        if (it != wm_clients.end()) {
          FetchClientProperty(client_window, it->second, propertynotify->atom);
        }
        break;
      }
      case XCB_CONFIGURE_REQUEST: {
        auto configurerequest =
            reinterpret_cast<xcb_configure_request_event_t*>(event);
        auto it = wm_clients.find(configurerequest->window);
        if (it != wm_clients.end()) {
          it->second.wants_configure_notify = true;
        }
        break;
      }
      case XCB_MAP_REQUEST: {
        xcb_map_window(
            x11.conn,
            reinterpret_cast<xcb_map_request_event_t*>(event)->window);
        break;
      }
      case XCB_MAP_NOTIFY: {
        auto mapnotify = reinterpret_cast<xcb_map_notify_event_t*>(event);
        if (!mapnotify->override_redirect) {
          xcb_window_t window =
              reinterpret_cast<xcb_map_notify_event_t*>(event)->window;
          ManageClient(window);
        }
        break;
      }
      case XCB_MAPPING_NOTIFY: {
        // auto mappingnotify =
        //     reinterpret_cast<xcb_mapping_notify_event_t*>(event);
        LOG(INFO) << "mapping notify";
        break;
      }
      case XCB_UNMAP_NOTIFY: {
        UnmanageClient(
            reinterpret_cast<xcb_unmap_notify_event_t*>(event)->window);
        break;
      }
      case XCB_DESTROY_NOTIFY: {
        UnmanageClient(
            reinterpret_cast<xcb_destroy_notify_event_t*>(event)->window);
        break;
      }
      case XCB_FOCUS_IN: {
        auto focusin = reinterpret_cast<xcb_focus_in_event_t*>(event);
        if (focusin->mode == XCB_NOTIFY_MODE_NORMAL) CheckFocusTheft();
        break;
      }
      case XCB_EXPOSE: {
        auto expose = reinterpret_cast<xcb_expose_event_t*>(event);
        if (expose->window == wm_bar_window) wm_bar_dirty = true;
        break;
      }
      case XCB_ENTER_NOTIFY: {
        auto enternotify = reinterpret_cast<xcb_enter_notify_event_t*>(event);
        if (enternotify->event)
          Activate(stack, enternotify->event, enternotify->time);
        break;
      }
      case 0: {
        auto error = reinterpret_cast<xcb_generic_error_t*>(event);
        LOG(ERROR) << "xcb error: "
                   << static_cast<X11ErrorCode>(error->error_code)
                   << " sequence: " << error->sequence;
        break;
      }
    }
  }
}

void UpdateBar() {
  wm_bar_dirty = false;

  const WindowStack& stack = GetActiveStack();

  if (stack.zoom) {
    if (!wm_bar_hidden) {
      xcb_unmap_window(x11.conn, wm_bar_window);
      wm_bar_hidden = true;
    }

    return;
  }

  if (wm_bar_hidden) {
    xcb_map_window(x11.conn, wm_bar_window);
    wm_bar_hidden = false;
  }

  const std::string& active_client_name =
      stack.active_window ? wm_clients.at(stack.active_window).name
                          : ("nylawm " + std::to_string(wm_active_stack_idx));

  double load_avg[3];
  getloadavg(load_avg, std::size(load_avg));

  std::string bar_text = absl::StrFormat(
      "%.2f, %.2f, %.2f %s %v", load_avg[0], load_avg[1], load_avg[2],
      absl::FormatTime("%H:%M:%S %d.%m.%Y", absl::Now(), absl::LocalTimeZone()),
      active_client_name);

  xcb_clear_area(x11.conn, 0, wm_bar_window, 0, 0, x11.screen->width_in_pixels,
                 wm_bar_height);
  xcb_image_text_8(x11.conn, bar_text.size(), wm_bar_window, wm_bar_gc, 8, 16,
                   bar_text.data());
}

std::string DumpClients() {
  std::string out;

  const WindowStack& stack = GetActiveStack();

  absl::StrAppendFormat(&out, "active window = %x\n\n", stack.active_window);

  for (const auto& [client_window, client] : wm_clients) {
    std::string_view indent = [&client]() {
      if (client.transient_for) return "  T  ";
      if (!client.subwindows.empty()) return "  S  ";
      return "";
    }();

    std::string transient_for_name;
    if (client.transient_for) {
      auto it = wm_clients.find(client.transient_for);
      if (it == wm_clients.end()) {
        transient_for_name = "invalid " + std::to_string(client.transient_for);
      } else {
        transient_for_name =
            it->second.name + " " + std::to_string(client.transient_for);
      }
    } else {
      transient_for_name = "none";
    }

    absl::StrAppendFormat(&out,
                          "%swindow=%x\n%sname=%v\n%srect=%v\n%swm_"
                          "transient_for=%v\n%sinput=%v\n"
                          "%swm_take_focus=%v\n%swm_delete_window=%v\n%"
                          "ssubwindows=%v\n%smax_dimensions=%vx%v\n\n",
                          indent, client_window, indent, client.name, indent,
                          client.rect, indent, transient_for_name, indent,
                          client.wm_hints_input, indent, client.wm_take_focus,
                          indent, client.wm_delete_window, indent,
                          absl::StrJoin(client.subwindows, ", "), indent,
                          client.max_width, client.max_height);
  }
  return out;
}

}  // namespace nyla
