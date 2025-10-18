#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "xcb/xproto.h"

namespace nyla {

extern bool wm_bar_dirty;

//

void InitializeWM();
void ProcessWMEvents(
    const bool& is_running, uint16_t modifier,
    std::vector<std::pair<
        xcb_keycode_t,
        std::variant<void (*)(xcb_timestamp_t timestamp), void (*)()>>>
        keybinds);

void ProcessWM();
void UpdateBar();

std::string DumpClients();
std::string GetActiveClientBarText();

void ManageClientsStartup();

void CloseActive();

void ToggleZoom();
void ToggleFollow();

void MoveLocalNext(xcb_timestamp_t time);
void MoveLocalPrev(xcb_timestamp_t time);

void MoveStackNext(xcb_timestamp_t time);
void MoveStackPrev(xcb_timestamp_t time);

void NextLayout();

}  // namespace nyla
