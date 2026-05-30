// Copyright (C) 2026 Atomic Style, LLC
// SPDX-License-Identifier: GPL-3.0-or-later
//
// atomic_ui — opinionated thin kit on top of LVGL.
//   - Themes are C structs (one active per build); see atomic_ui_theme.h.
//   - Widgets are factory functions taking config structs; see
//     atomic_ui_widgets.h.

#pragma once

#include "esp_err.h"
#include "atomic_ui_theme.h"
#include "atomic_ui_widgets.h"

#ifdef __cplusplus
extern "C" {
#endif

// Bring up LVGL screen + show splash + status. Blocks until LVGL is ready.
// `project` is stored and used as the initial header bar title; pass NULL or
// "" for no title. Typical call site is `atomic_ui(DEV_PROJECT)`.
esp_err_t atomic_ui(const char *project);

// Hide splash + status (used after init completes).
esp_err_t a_ui_clear(void);

// Update the bottom status label. Safe to call from any task.
esp_err_t a_ui_status(const char *text);

// Update the header bar's left-justified project label. Safe to call from
// any task; passing NULL or "" hides it.
void a_ui_project(const char *project);

// Header bar that sits at the top of the active screen. Created automatically
// during atomic_ui() and present whenever atomic_lvgl is in the build.
// Returns NULL until atomic_ui() has run. Add status items as children of
// a_ui_header_right_slot(a_ui_header()).
lv_obj_t *a_ui_header(void);
int32_t   a_ui_header_h(void);

#ifdef __cplusplus
}
#endif
