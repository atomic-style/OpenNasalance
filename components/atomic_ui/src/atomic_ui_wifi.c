// Wifi indicator + network/AP configuration screen.
//
// The header indicator is a clickable LV_SYMBOL_WIFI label that recolors
// based on BIT_WIFI_READY. Tapping it opens a modal that lives in
// lv_layer_top (so it overlays whatever the app is showing).
//
// Modal layout follows ~/dev/WIFI.md:
//   - full-screen black backdrop at ~50% opacity
//   - inset window with black bg + thin yellow border
//   - centered "Wifi" title at top
//   - "Station" / "Access Point" mode toggle
//   - STA pane: ssid / dBm / url with Cancel / Forget / Scan / OK
//   - AP pane:  ssid / pass / url with Cancel / OK
//   - "Scan" jumps to a sibling pane that scans, lists APs, and lets the user
//     type a password before saving credentials.

#include "atomic_ui_widgets.h"

#include "atomic_bits.h"
#include "atomic_log.h"
#include "atomic_ui_theme.h"
#include "atomic_wifi.h"
#include "lvgl.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "UI-WIFI";

#define A_UI_WIFI_TICK_MS    1000
#define A_UI_WIFI_RSSI_MS    3000
#define A_UI_WIFI_MAX_APS    16

// 50% transparent black per the spec (#00000088).
#define A_UI_BACKDROP_OPA    LV_OPA_50

// Inset margin as a percentage of screen dim.
#define A_UI_WINDOW_INSET    5

// ---------------------------------------------------------------------------
//  Indicator
// ---------------------------------------------------------------------------
static void wifi_tick(lv_timer_t *timer) {
  lv_obj_t *label = (lv_obj_t *)lv_timer_get_user_data(timer);
  if (!label) return;
  bool ready = a_bits(BIT_WIFI_READY);
  bool waiting = a_bits(BIT_WIFI_WAIT);
  lv_color_t c;
  if (ready) {
    c = a_ui_theme_get()->indicator_color[A_UI_IND_ON];
  } else if (waiting) {
    c = a_ui_theme_get()->indicator_color[A_UI_IND_WARN];
  } else {
    c = a_ui_theme_get()->indicator_color[A_UI_IND_OFF];
  }
  lv_obj_set_style_text_color(label, c, 0);
}

static void wifi_clicked(lv_event_t *e) {
  (void)e;
  info(TAG, "wifi_clicked()");
  a_ui_wifi_menu_open();
}

lv_obj_t *a_ui_wifi_create(lv_obj_t *parent) {
  lv_obj_t *label = lv_label_create(parent);
  lv_obj_set_style_text_font(label, a_ui_theme_role_font(A_UI_ROLE_STATUS), 0);
  lv_label_set_text(label, LV_SYMBOL_WIFI);
  lv_obj_add_flag(label, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(label, 6);
  lv_obj_add_event_cb(label, wifi_clicked, LV_EVENT_CLICKED, NULL);
  lv_timer_create(wifi_tick, A_UI_WIFI_TICK_MS, label);
  return label;
}

// ---------------------------------------------------------------------------
//  Modal state
// ---------------------------------------------------------------------------
//
// All widgets live under s_modal in lv_layer_top so the modal overlays the
// active screen without swapping it.

static lv_obj_t *s_modal;
static lv_obj_t *s_window;     // landing window (mode toggle + info + actions)
static lv_obj_t *s_pane_sta;
static lv_obj_t *s_pane_ap;
static lv_obj_t *s_btn_sta;    // mode toggle
static lv_obj_t *s_btn_ap;
static lv_obj_t *s_lbl_sta_ssid;
static lv_obj_t *s_lbl_sta_dbm;
static lv_obj_t *s_lbl_sta_url;
static lv_obj_t *s_lbl_ap_ssid;
static lv_obj_t *s_lbl_ap_pass;
static lv_obj_t *s_lbl_ap_url;
static lv_obj_t *s_lbl_status; // top-right inline status
static lv_obj_t *s_btn_forget; // STA-only
static lv_obj_t *s_btn_scan;   // STA-only

static lv_obj_t *s_scan_window; // scan subscreen
static lv_obj_t *s_scan_list;
static lv_obj_t *s_scan_pwd;
static lv_obj_t *s_scan_kb;
static lv_obj_t *s_scan_status;
static char      s_scan_ssid[A_WIFI_SSID_MAX];

static lv_timer_t *s_refresh_timer; // 3 s url/dBm refresh

static a_wifi_mode_t s_pending_mode; // mode the user has selected on the toggle
static a_wifi_mode_t s_initial_mode; // mode at modal open (for Cancel revert)

// ---------------------------------------------------------------------------
//  Helpers
// ---------------------------------------------------------------------------

static void set_status(const char *fmt, ...) {
  if (!s_lbl_status) return;
  char buf[80];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  lv_label_set_text(s_lbl_status, buf);
}

// Apply theme bg + thin yellow border + black background per the spec.
static void style_window(lv_obj_t *w) {
  lv_obj_remove_style_all(w);
  lv_obj_set_style_bg_color(w, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(w, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(w, a_ui_theme_get()->warn, 0);
  lv_obj_set_style_border_width(w, 1, 0);
  lv_obj_set_style_radius(w, 0, 0);
  lv_obj_set_style_pad_all(w, 6, 0);
  lv_obj_remove_flag(w, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *make_info_label(lv_obj_t *parent, const char *txt) {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, a_ui_theme_role_font(A_UI_ROLE_STATUS), 0);
  lv_obj_set_style_text_color(l, a_ui_theme_role_color(A_UI_ROLE_BODY), 0);
  lv_label_set_text(l, txt);
  return l;
}

static void mode_button_set_active(lv_obj_t *btn, bool active) {
  // Inverted color scheme per the spec: off = black bg + white text,
  // on = white bg + black text.
  lv_color_t bg_off = lv_color_black();
  lv_color_t bg_on  = lv_color_white();
  lv_color_t fg_off = lv_color_white();
  lv_color_t fg_on  = lv_color_black();
  lv_obj_set_style_bg_color(btn, active ? bg_on : bg_off, 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(btn, a_ui_theme_get()->warn, 0);
  lv_obj_set_style_border_width(btn, 1, 0);
  lv_obj_t *lbl = lv_obj_get_child(btn, 0);
  if (lbl) lv_obj_set_style_text_color(lbl, active ? fg_on : fg_off, 0);
}

static void show_pane_for_mode(a_wifi_mode_t mode) {
  if (mode == A_WIFI_MODE_AP) {
    lv_obj_remove_flag(s_pane_ap, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_pane_sta, LV_OBJ_FLAG_HIDDEN);
    if (s_btn_forget) lv_obj_add_flag(s_btn_forget, LV_OBJ_FLAG_HIDDEN);
    if (s_btn_scan)   lv_obj_add_flag(s_btn_scan,   LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_remove_flag(s_pane_sta, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_pane_ap, LV_OBJ_FLAG_HIDDEN);
    if (s_btn_forget) lv_obj_remove_flag(s_btn_forget, LV_OBJ_FLAG_HIDDEN);
    if (s_btn_scan)   lv_obj_remove_flag(s_btn_scan,   LV_OBJ_FLAG_HIDDEN);
  }
  mode_button_set_active(s_btn_sta, mode == A_WIFI_MODE_STA);
  mode_button_set_active(s_btn_ap,  mode == A_WIFI_MODE_AP);
}

// Pull live values into the visible pane.
static void refresh_panes(void) {
  char ssid[A_WIFI_SSID_MAX] = {0};
  char url[40] = {0};
  a_wifi_get_active_ssid(ssid, sizeof ssid);
  a_wifi_get_url(url, sizeof url);

  char buf[A_WIFI_PASS_MAX + 16];
  if (s_pending_mode == A_WIFI_MODE_AP) {
    char pass[A_WIFI_PASS_MAX] = {0};
    char ap_ssid[A_WIFI_SSID_MAX] = {0};
    a_wifi_load_ap_credentials(ap_ssid, pass);
    snprintf(buf, sizeof buf, "ssid: %s",
             ssid[0] ? ssid : ap_ssid);
    lv_label_set_text(s_lbl_ap_ssid, buf);
    snprintf(buf, sizeof buf, "pass: %s", pass[0] ? pass : "(open)");
    lv_label_set_text(s_lbl_ap_pass, buf);
    snprintf(buf, sizeof buf, "url:  %s", url);
    lv_label_set_text(s_lbl_ap_url, buf);
  } else {
    snprintf(buf, sizeof buf, "ssid: %s", ssid[0] ? ssid : "(none)");
    lv_label_set_text(s_lbl_sta_ssid, buf);

    int8_t rssi = 0;
    if (a_wifi_get_rssi(&rssi) == ESP_OK) {
      snprintf(buf, sizeof buf, "dBm:  %d", rssi);
    } else {
      snprintf(buf, sizeof buf, "dBm:  --");
    }
    lv_label_set_text(s_lbl_sta_dbm, buf);
    snprintf(buf, sizeof buf, "url:  %s", url);
    lv_label_set_text(s_lbl_sta_url, buf);
  }
}

static void refresh_tick(lv_timer_t *t) {
  (void)t;
  refresh_panes();
}

// ---------------------------------------------------------------------------
//  Mode toggle handlers
//
// The toggle buttons apply the mode change immediately — no need to wait for
// OK. Tapping "Station" disconnects from any current AP and brings up STA
// using saved credentials; tapping "Access Point" tears down STA, starts the
// SoftAP with the project's name + default password, and brings up DHCPS.
// The 3-second refresh timer pulls fresh ssid/pass/url into the visible pane
// after each change.
// ---------------------------------------------------------------------------
static void apply_mode_now(a_wifi_mode_t mode) {
  if (mode == s_pending_mode) {
    show_pane_for_mode(mode);
    return;
  }
  s_pending_mode = mode;
  show_pane_for_mode(mode);
  set_status(mode == A_WIFI_MODE_AP ? "Starting AP..." : "Switching to STA...");
  esp_err_t r = a_wifi_apply_mode(mode);
  if (r != ESP_OK) {
    set_status("apply: %s", esp_err_to_name(r));
  } else {
    set_status(mode == A_WIFI_MODE_AP ? "AP up" : "STA");
  }
  refresh_panes();
}

static void mode_sta_clicked(lv_event_t *e) {
  (void)e;
  apply_mode_now(A_WIFI_MODE_STA);
}
static void mode_ap_clicked(lv_event_t *e) {
  (void)e;
  apply_mode_now(A_WIFI_MODE_AP);
}

// ---------------------------------------------------------------------------
//  Modal lifecycle
// ---------------------------------------------------------------------------
static void modal_close(void) {
  if (s_refresh_timer) {
    lv_timer_delete(s_refresh_timer);
    s_refresh_timer = NULL;
  }
  if (s_modal) {
    lv_obj_delete(s_modal);
    s_modal = NULL;
  }
  s_window = NULL;
  s_pane_sta = s_pane_ap = NULL;
  s_btn_sta = s_btn_ap = NULL;
  s_lbl_sta_ssid = s_lbl_sta_dbm = s_lbl_sta_url = NULL;
  s_lbl_ap_ssid = s_lbl_ap_pass = s_lbl_ap_url = NULL;
  s_lbl_status = NULL;
  s_scan_window = NULL;
  s_scan_list = s_scan_pwd = s_scan_kb = s_scan_status = NULL;
  s_scan_ssid[0] = '\0';
}

// ---------------------------------------------------------------------------
//  Scan subscreen
// ---------------------------------------------------------------------------
static void scan_close(void) {
  if (!s_scan_window) return;
  lv_obj_delete(s_scan_window);
  s_scan_window = NULL;
  s_scan_list = s_scan_pwd = s_scan_kb = s_scan_status = NULL;
  s_scan_ssid[0] = '\0';
  if (s_window) lv_obj_remove_flag(s_window, LV_OBJ_FLAG_HIDDEN);
  refresh_panes();
}

static void scan_set_status(const char *fmt, ...) {
  if (!s_scan_status) return;
  char buf[80];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  lv_label_set_text(s_scan_status, buf);
}

static void scan_list_btn_clicked(lv_event_t *e) {
  lv_obj_t *btn = lv_event_get_target_obj(e);
  const char *txt = lv_list_get_button_text(s_scan_list, btn);
  if (!txt) return;
  strncpy(s_scan_ssid, txt, sizeof s_scan_ssid - 1);
  s_scan_ssid[sizeof s_scan_ssid - 1] = '\0';
  scan_set_status("Selected: %s", s_scan_ssid);
  if (s_scan_pwd) lv_obj_remove_flag(s_scan_pwd, LV_OBJ_FLAG_HIDDEN);
  if (s_scan_kb) {
    lv_obj_remove_flag(s_scan_kb, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(s_scan_kb, s_scan_pwd);
  }
}

static void scan_populate(void) {
  if (!s_scan_list) return;
  lv_obj_clean(s_scan_list);
  static a_wifi_ap_t aps[A_UI_WIFI_MAX_APS];
  uint16_t n = 0;
  scan_set_status("Scanning...");
  esp_err_t r = a_wifi_scan(aps, A_UI_WIFI_MAX_APS, &n);
  if (r != ESP_OK) {
    scan_set_status("Scan failed: %s", esp_err_to_name(r));
    return;
  }
  if (n == 0) {
    scan_set_status("No networks found");
    return;
  }
  for (uint16_t i = 0; i < n; i++) {
    lv_obj_t *btn =
        lv_list_add_button(s_scan_list, LV_SYMBOL_WIFI, aps[i].ssid);
    lv_obj_add_event_cb(btn, scan_list_btn_clicked, LV_EVENT_CLICKED, NULL);
  }
  scan_set_status("%u networks", (unsigned)n);
}

static void scan_btn_save_cb(lv_event_t *e) {
  (void)e;
  if (!s_scan_ssid[0]) {
    scan_set_status("Pick a network first");
    return;
  }
  const char *pwd = s_scan_pwd ? lv_textarea_get_text(s_scan_pwd) : "";
  scan_set_status("Saving %s...", s_scan_ssid);
  esp_err_t r = a_wifi_apply_credentials(s_scan_ssid, pwd ? pwd : "");
  if (r == ESP_OK) {
    info(TAG, "saved & connecting to %s", s_scan_ssid);
    scan_close();
  } else {
    scan_set_status("Save failed: %s", esp_err_to_name(r));
  }
}

static void scan_btn_cancel_cb(lv_event_t *e) {
  (void)e;
  scan_close();
}

static void scan_open(void) {
  if (s_scan_window) return;
  if (s_window) lv_obj_add_flag(s_window, LV_OBJ_FLAG_HIDDEN);

  s_scan_window = lv_obj_create(s_modal);
  style_window(s_scan_window);
  lv_obj_set_size(s_scan_window, LV_PCT(100 - 2 * A_UI_WINDOW_INSET),
                  LV_PCT(100 - 2 * A_UI_WINDOW_INSET));
  lv_obj_center(s_scan_window);

  lv_obj_t *title = lv_label_create(s_scan_window);
  lv_obj_set_style_text_font(title, a_ui_theme_role_font(A_UI_ROLE_HEADER), 0);
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_label_set_text(title, "Wifi");
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

  s_scan_status = lv_label_create(s_scan_window);
  lv_obj_set_style_text_font(s_scan_status,
                             a_ui_theme_role_font(A_UI_ROLE_STATUS), 0);
  lv_obj_set_style_text_color(s_scan_status, lv_color_white(), 0);
  lv_label_set_text(s_scan_status, "");
  lv_obj_align(s_scan_status, LV_ALIGN_TOP_LEFT, 0, 0);

  s_scan_list = lv_list_create(s_scan_window);
  lv_obj_set_size(s_scan_list, LV_PCT(55), LV_PCT(70));
  lv_obj_align(s_scan_list, LV_ALIGN_LEFT_MID, 0, 8);

  s_scan_pwd = lv_textarea_create(s_scan_window);
  lv_textarea_set_one_line(s_scan_pwd, true);
  lv_textarea_set_placeholder_text(s_scan_pwd, "password");
  lv_textarea_set_password_mode(s_scan_pwd, true);
  lv_obj_set_size(s_scan_pwd, LV_PCT(40), 24);
  lv_obj_align(s_scan_pwd, LV_ALIGN_TOP_RIGHT, 0, 22);
  lv_obj_add_flag(s_scan_pwd, LV_OBJ_FLAG_HIDDEN);

  s_scan_kb = lv_keyboard_create(s_scan_window);
  lv_obj_set_size(s_scan_kb, LV_PCT(40), LV_PCT(50));
  lv_obj_align(s_scan_kb, LV_ALIGN_BOTTOM_RIGHT, 0, -28);
  lv_obj_add_flag(s_scan_kb, LV_OBJ_FLAG_HIDDEN);

  // Action buttons (bottom-right, standardized).
  static const struct {
    const char   *label;
    lv_event_cb_t cb;
    int32_t       x_off;
  } btns[] = {
      {"Cancel", scan_btn_cancel_cb, -110},
      {"Save",   scan_btn_save_cb,   -50},
  };
  for (size_t i = 0; i < sizeof btns / sizeof btns[0]; i++) {
    lv_obj_t *b = a_ui_button_create(&(a_ui_button_cfg_t){
        .parent   = s_scan_window,
        .text     = btns[i].label,
        .w        = 56,
        .h        = 22,
        .on_click = btns[i].cb,
    });
    lv_obj_align(b, LV_ALIGN_BOTTOM_RIGHT, btns[i].x_off, 0);
  }

  s_scan_ssid[0] = '\0';
  scan_populate();
}

// ---------------------------------------------------------------------------
//  Landing window action handlers
// ---------------------------------------------------------------------------
// Mode changes are applied live by the toggle handlers, so OK and Cancel
// have the same effect: close the modal.
static void btn_ok_cb(lv_event_t *e) {
  (void)e;
  modal_close();
}

static void btn_cancel_cb(lv_event_t *e) {
  (void)e;
  modal_close();
}

static void btn_forget_cb(lv_event_t *e) {
  (void)e;
  esp_err_t r = a_wifi_forget();
  set_status(r == ESP_OK ? "Forgot creds" : "Forget: %s", esp_err_to_name(r));
  refresh_panes();
}

static void btn_scan_cb(lv_event_t *e) {
  (void)e;
  scan_open();
}

// ---------------------------------------------------------------------------
//  Open
// ---------------------------------------------------------------------------
void a_ui_wifi_menu_open(void) {
  if (s_modal) return;

  s_initial_mode = a_wifi_get_mode_pref();
  s_pending_mode = s_initial_mode;

  // Backdrop covers the whole screen with translucent black.
  s_modal = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_modal);
  lv_obj_set_size(s_modal, LV_PCT(100), LV_PCT(100));
  lv_obj_set_pos(s_modal, 0, 0);
  lv_obj_set_style_bg_color(s_modal, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(s_modal, A_UI_BACKDROP_OPA, 0);
  lv_obj_remove_flag(s_modal, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s_modal, LV_OBJ_FLAG_CLICKABLE); // swallow taps to backdrop

  s_window = lv_obj_create(s_modal);
  style_window(s_window);
  lv_obj_set_size(s_window, LV_PCT(100 - 2 * A_UI_WINDOW_INSET),
                  LV_PCT(100 - 2 * A_UI_WINDOW_INSET));
  lv_obj_center(s_window);

  // Title centered top.
  lv_obj_t *title = lv_label_create(s_window);
  lv_obj_set_style_text_font(title, a_ui_theme_role_font(A_UI_ROLE_HEADER), 0);
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_label_set_text(title, "Wifi");
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

  // Inline status (top-left).
  s_lbl_status = lv_label_create(s_window);
  lv_obj_set_style_text_font(s_lbl_status,
                             a_ui_theme_role_font(A_UI_ROLE_STATUS), 0);
  lv_obj_set_style_text_color(s_lbl_status, lv_color_white(), 0);
  lv_label_set_text(s_lbl_status, "");
  lv_obj_align(s_lbl_status, LV_ALIGN_TOP_LEFT, 0, 0);

  // Mode toggle (two square buttons, side-by-side, centered).
  s_btn_sta = a_ui_button_create(&(a_ui_button_cfg_t){
      .parent = s_window, .text = "Station", .w = 80, .h = 26,
      .on_click = mode_sta_clicked,
  });
  lv_obj_align(s_btn_sta, LV_ALIGN_TOP_MID, -42, 22);

  s_btn_ap = a_ui_button_create(&(a_ui_button_cfg_t){
      .parent = s_window, .text = "Access Point", .w = 80, .h = 26,
      .on_click = mode_ap_clicked,
  });
  lv_obj_align(s_btn_ap, LV_ALIGN_TOP_MID, 42, 22);

  // STA pane.
  s_pane_sta = lv_obj_create(s_window);
  lv_obj_remove_style_all(s_pane_sta);
  lv_obj_set_size(s_pane_sta, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_align(s_pane_sta, LV_ALIGN_LEFT_MID, 0, 8);
  lv_obj_set_flex_flow(s_pane_sta, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(s_pane_sta, 2, 0);
  lv_obj_remove_flag(s_pane_sta, LV_OBJ_FLAG_SCROLLABLE);
  s_lbl_sta_ssid = make_info_label(s_pane_sta, "ssid: ...");
  s_lbl_sta_dbm  = make_info_label(s_pane_sta, "dBm:  ...");
  s_lbl_sta_url  = make_info_label(s_pane_sta, "url:  ...");

  // AP pane (initially hidden).
  s_pane_ap = lv_obj_create(s_window);
  lv_obj_remove_style_all(s_pane_ap);
  lv_obj_set_size(s_pane_ap, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_align(s_pane_ap, LV_ALIGN_LEFT_MID, 0, 8);
  lv_obj_set_flex_flow(s_pane_ap, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(s_pane_ap, 2, 0);
  lv_obj_remove_flag(s_pane_ap, LV_OBJ_FLAG_SCROLLABLE);
  s_lbl_ap_ssid = make_info_label(s_pane_ap, "ssid: ...");
  s_lbl_ap_pass = make_info_label(s_pane_ap, "pass: ...");
  s_lbl_ap_url  = make_info_label(s_pane_ap, "url:  ...");

  // Action buttons in bottom-right. STA gets cancel/forget/scan/ok; AP gets
  // cancel/ok. Both panes share the cancel + ok buttons by parenting them to
  // the window itself; mode-specific ones hang off their pane.
  lv_obj_t *btn_cancel = a_ui_button_create(&(a_ui_button_cfg_t){
      .parent = s_window, .text = "Cancel", .w = 56, .h = 22,
      .on_click = btn_cancel_cb,
  });
  lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_RIGHT, -178, 0);

  s_btn_forget = a_ui_button_create(&(a_ui_button_cfg_t){
      .parent = s_window, .text = "Forget", .w = 56, .h = 22,
      .on_click = btn_forget_cb,
  });
  lv_obj_align(s_btn_forget, LV_ALIGN_BOTTOM_RIGHT, -118, 0);

  s_btn_scan = a_ui_button_create(&(a_ui_button_cfg_t){
      .parent = s_window, .text = "Scan", .w = 56, .h = 22,
      .on_click = btn_scan_cb,
  });
  lv_obj_align(s_btn_scan, LV_ALIGN_BOTTOM_RIGHT, -58, 0);

  lv_obj_t *btn_ok = a_ui_button_create(&(a_ui_button_cfg_t){
      .parent = s_window, .text = "OK", .w = 50, .h = 22,
      .on_click = btn_ok_cb,
  });
  lv_obj_align(btn_ok, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

  show_pane_for_mode(s_pending_mode);
  refresh_panes();
  s_refresh_timer = lv_timer_create(refresh_tick, A_UI_WIFI_RSSI_MS, NULL);
}
