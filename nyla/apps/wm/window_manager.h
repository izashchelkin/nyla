#pragma once

#include <cstdint>
#include <variant>
#include <vector>

#include "xcb/xproto.h"

namespace nyla
{

using KeybindHandler = std::variant<void (*)(xcb_timestamp_t timestamp), void (*)()>;
using Keybind = std::tuple<xcb_keycode_t, int, KeybindHandler>;

//

void InitializeWM();
void ProcessWMEvents(const bool &isRunning, uint16_t modifier, std::vector<Keybind> keybinds);

void ProcessWM();
void UpdateBackground();

void ManageClientsStartup();

void CloseActive();

void ToggleZoom();
void ToggleFollow();

void MoveLocalNext(xcb_timestamp_t time);
void MoveLocalPrev(xcb_timestamp_t time);

void MoveStackNext(xcb_timestamp_t time);
void MoveStackPrev(xcb_timestamp_t time);

void NextLayout();

} // namespace nyla