/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APP_DASHBOARD_XEX_METADATA_H_
#define XENIA_APP_DASHBOARD_XEX_METADATA_H_

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace xe {
namespace app {
namespace dashboard {

// Metadata pulled out of an XEX2 executable without loading it into guest
// memory. The XDBF (SPA) resource embedded in the image provides the localized
// title name, the title icon and the achievement table.
struct XexMetadata {
  bool valid = false;
  uint32_t title_id = 0;
  uint32_t media_id = 0;
  uint32_t version = 0;
  uint32_t base_version = 0;
  uint8_t disc_number = 0;
  uint8_t disc_count = 0;
  std::string title_name;
  std::vector<uint8_t> icon_png;
  uint32_t achievement_count = 0;
  uint32_t total_gamerscore = 0;
};

// Parses the plaintext XEX2 headers (title ID, media ID, version) and, when
// possible, decrypts and decompresses the image to read the SPA resource.
// Returns false if the buffer is not an XEX2 file. Header-only data is still
// returned (valid = true) when the image itself cannot be decoded.
bool ReadXexMetadata(std::span<const uint8_t> xex_data, XexMetadata* out);

}  // namespace dashboard
}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_DASHBOARD_XEX_METADATA_H_
