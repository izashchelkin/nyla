#pragma once

#include "absl/cleanup/cleanup.h"
#include "absl/log/log.h"
#include "xcb/xcb.h"
#include "xcb/xproto.h"

namespace nyla {

xcb_get_property_reply_t* FetchPropertyInternal(xcb_connection_t* conn,
                                                xcb_window_t window,
                                                const xcb_atom_t property,
                                                const xcb_atom_t type,
                                                const uint32_t long_length);

template <typename PropertyStruct>
PropertyStruct FetchPropertyStruct(xcb_connection_t* conn, xcb_window_t window,
                                   const xcb_atom_t property,
                                   const xcb_atom_t type) {
  static_assert((sizeof(PropertyStruct) % 4) == 0);

  auto reply = FetchPropertyInternal(conn, window, property, type,
                                     sizeof(PropertyStruct) >> 2);
  absl::Cleanup reply_freer = [reply] { free(reply); };

  if (xcb_get_property_value_length(reply) != sizeof(PropertyStruct) ||
      reply->bytes_after != 0) {
    LOG(FATAL) << "FetchPropertyStruct: invalid reply length";
  }
  return *static_cast<PropertyStruct*>(xcb_get_property_value(reply));
}

template <typename Element>
std::vector<Element> FetchPropertyList(xcb_connection_t* conn,
                                       xcb_window_t window,
                                       const xcb_atom_t property,
                                       const xcb_atom_t type) {
  auto reply = FetchPropertyInternal(conn, window, property, type,
                                     std::numeric_limits<uint32_t>::max());
  absl::Cleanup reply_freer = [reply] { free(reply); };

  return std::vector<Element>(
      static_cast<Element*>(xcb_get_property_value(reply)),
      static_cast<Element*>(xcb_get_property_value(reply)) +
          (xcb_get_property_value_length(reply) / sizeof(Element)));
}

}  // namespace nyla
