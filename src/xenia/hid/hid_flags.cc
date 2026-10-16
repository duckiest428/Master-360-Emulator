/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/hid/hid_flags.h"

DEFINE_bool(guide_button, true, "Forward guide button presses to guest.",
            "HID");

DEFINE_bool(dashboard_guide, true,
            "Open the Xbox Guide overlay when the Guide button is pressed. "
            "While enabled the Guide button is consumed by the system and not "
            "reported to titles, matching real hardware. Requires guide_button.",
            "HID");
