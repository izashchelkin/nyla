#pragma once

#include <cstdint>

#include "nyla/protocols/atoms.h"
#include "nyla/wm/keyboard.h"
#include "xcb/xcb.h"
#include "xcb/xproto.h"

namespace nyla {

extern xcb_connection_t* wm_conn;
extern xcb_screen_t wm_screen;
extern X11Atoms atoms;

extern bool wm_bar_dirty;

//

void InitializeWM();
void ProcessWMEvents(const bool& is_running, uint16_t modifier,
                     std::span<Keybind> keybinds);
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
