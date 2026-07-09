//
// Utilitarianism — pure-C watchface.
//
// Ported one-to-one from the former Moddable/JS implementation
// (src/embeddedjs/main.js). All drawing, sensors (battery / bluetooth /
// health / quiet time / time) and orchestration now live here in C. The only
// piece kept on the phone side is the PKJS weather+location relay
// (src/pkjs/index.js): the watch sends a REQUEST_WEATHER AppMessage and PKJS
// answers with WEATHER_* keys (Open-Meteo current + daily sunrise/sunset).
//
// Layout coordinates are the same as the JS version and assume the emery
// 200x228 color display (W = 200, H = 228).
//

#include <pebble.h>

// ---- Message keys (auto-generated from package.json messageKeys) ----
// REQUEST_WEATHER (C -> PKJS): 0 = geolocate + fetch, 1 = reuse cached coords.
// WEATHER_* (PKJS -> C): current weather + sun times.

// ---- Screen ----
#define W 200
#define H 228
#define WEATHER_REFRESH_S (15 * 60)   // seconds between successful weather updates

// ---- Fonts (system fonts matching the JS names) ----
//   JS "Roboto-Bold" 49      -> ROBOTO_BOLD_SUBSET_49   (time)
//   JS "Gothic-Bold" 28      -> GOTHIC_28_BOLD           (big / date)
//   JS "Gothic-Regular" 14   -> GOTHIC_14                (small / battery %)
static GFont s_font_time;
static GFont s_font_big;
static GFont s_font_date;
static GFont s_font_small;

// ---- Colors ----
// The hardware palette is 2 bits per channel. GColor8 packs a (a:2, r:2, g:2, b:2)
// byte, so we precompute the opaque (alpha=3) argb bytes as integer constants
// for use in static initializers (compound literals are not constant expressions).
#define ARGB(r, g, b) \
  ((uint8_t)((0xC0u) | (((uint8_t)(r) >> 6) << 4) | (((uint8_t)(g) >> 6) << 2) | ((uint8_t)(b) >> 6)))

static const GColor C_WHITE  = {.argb = 0xFFu};
static const GColor C_BLACK  = {.argb = 0xC0u};
static const GColor C_GREEN  = {.argb = ARGB(63, 214, 63)};
static const GColor C_YELLOW = {.argb = ARGB(255, 224, 0)};
static const GColor C_GRAYD  = {.argb = ARGB(58, 58, 58)};
static const GColor C_DARK   = {.argb = ARGB(34, 34, 34)};

// Rainbow used for the battery bar (left-to-right segments).
static const GColor RAINBOW[6] = {
  {.argb = ARGB(255, 42, 42)},
  {.argb = ARGB(255, 138, 0)},
  {.argb = ARGB(255, 224, 0)},
  {.argb = ARGB(63, 214, 63)},
  {.argb = ARGB(42, 155, 255)},
  {.argb = GColorPurpleARGB8},
};

static const char *DAYS[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

// ---- Icons (PNG bitmaps) ----
static GBitmap *s_bm_heart;
static GBitmap *s_bm_steps;
static GBitmap *s_bm_quiet;
static GBitmap *s_bm_bt;
static GBitmap *s_bm_bt_off;

// ---- Weather PDC icon (built on demand from the current weather code) ----
static GDrawCommandImage *s_dci_weather;
static int16_t s_dci_code = -1;

// ---- Live data ----
static Window *s_window;
static Layer *s_layer;

static uint8_t s_charge = 100;       // 0..100
static bool s_connected = true;
static int32_t s_steps = -1;         // -1 == unknown -> "--"
static int32_t s_bpm = -1;

static int32_t s_temp = 0;
static int32_t s_feels = 0;
static int32_t s_weather_code = 0;
static bool s_have_temp = false;     // temp === null in JS
static bool s_have_feels = false;    // feelsLike === null in JS
static bool s_have_sun = false;      // sunrise/sunset !== null in JS
static uint8_t s_sunrise_h, s_sunrise_m;
static uint8_t s_sunset_h, s_sunset_m;

static int s_last_hour = -1;          // for hourchange detection
static time_t s_last_weather_success = 0;  // unix time of last WEATHER_* inbox; 0 = never
static bool s_weather_busy = false;        // request in flight (guard against overlapping triggers)
static AppTimer *s_busy_clear_timer;       // failsafe: drop busy if PKJS never answers (e.g. HTTP error)
static bool s_first_fetch_done = false;    // initial geolocate fired once; later refreshes are cached

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Round a / b to the nearest integer (half away from zero).
static int round_div(int a, int b) {
  if (a >= 0) return (a + b / 2) / b;
  return (a - b / 2) / b;
}

static int clamp_int(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static void fill_rect(GContext *ctx, GColor color, int x, int y, int w, int h) {
  graphics_context_set_fill_color(ctx, color);
  graphics_fill_rect(ctx, GRect(x, y, w, h), 0, GCornersAll);
}

// Horizontally centered text within [x0, x1], top at y.
static void draw_text_centered(GContext *ctx, const char *text, GFont font,
                               GColor color, int x0, int x1, int y) {
  graphics_context_set_text_color(ctx, color);
  graphics_draw_text(ctx, text, font, GRect(x0, y, x1 - x0, H - y),
                     GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}

// Draw a palettized PNG bitmap at its native size with transparency.
static void draw_bitmap(GContext *ctx, GBitmap *bm, int x, int y) {
  if (!bm) return;
  GRect b = gbitmap_get_bounds(bm);
  graphics_context_set_compositing_mode(ctx, GCompOpSet);
  graphics_draw_bitmap_in_rect(ctx, bm, GRect(x, y, b.size.w, b.size.h));
  graphics_context_set_compositing_mode(ctx, GCompOpAssign);
}

// Open-Meteo weather_code -> icon resource (mirrors weatherIcon() in the JS).
static uint32_t weather_icon_resource(int code) {
  if (code == 0) return RESOURCE_ID_IC_SUNNY;       // Clear sky
  if (code <= 2)  return RESOURCE_ID_IC_PARTLY;     // Mainly clear / partly cloudy
  if (code <= 3)  return RESOURCE_ID_IC_CLOUDY;     // Overcast
  if (code <= 48) return RESOURCE_ID_IC_FOG;         // Fog / mist / haze
  if (code <= 55) return RESOURCE_ID_IC_DRIZZLE;     // Drizzle: light to heavy
  if (code <= 57) return RESOURCE_ID_IC_SLEET;        // Freezing drizzle
  if (code <= 65) return RESOURCE_ID_IC_RAIN;         // Rain: slight to heavy
  if (code <= 67) return RESOURCE_ID_IC_SLEET;        // Freezing rain
  if (code <= 77) return RESOURCE_ID_IC_SNOW;         // Snow fall / snow grains
  if (code <= 82) return RESOURCE_ID_IC_DRIZZLE;      // Rain showers
  if (code <= 86) return RESOURCE_ID_IC_SNOW;         // Snow showers
  return RESOURCE_ID_IC_RAIN;                        // Thunderstorm (95-99)
}

static void ensure_weather_dci(void) {
  if (s_dci_weather && s_dci_code == s_weather_code) return;
  if (s_dci_weather) {
    gdraw_command_image_destroy(s_dci_weather);
    s_dci_weather = NULL;
  }
  s_dci_code = s_weather_code;
  uint32_t res = weather_icon_resource(s_weather_code);
  if (res) s_dci_weather = gdraw_command_image_create_with_resource(res);
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

static void layer_update_proc(Layer *layer, GContext *ctx) {
  (void)layer;

  // Background
  graphics_context_set_fill_color(ctx, C_BLACK);
  graphics_fill_rect(ctx, GRect(0, 0, W, H), 0, GCornersAll);

  // BATTERY rainbow bar
  const int fillW = (W * s_charge + 50) / 100;
  for (int i = 0; i < 6; i++) {
    int sx = round_div(W * i, 6);
    int sw = round_div(W * (i + 1), 6) - sx;
    if (sx < fillW) {
      int cw = (sw < fillW - sx) ? sw : (fillW - sx);
      fill_rect(ctx, RAINBOW[i], sx, 0, cw, 6);
    }
  }
  if (fillW < W) fill_rect(ctx, C_DARK, fillW, 0, W - fillW, 6);

  // Battery badge (terminal + body + outline)
  const int badgeW = 32, badgeH = 16;
  const int badgeX = W - badgeW;
  const int badgeY = (s_charge > 80) ? 7 : 0;

  const int nubX = badgeX - 3;
  const int nubY = badgeY + (badgeH - 6) / 2;
  fill_rect(ctx, C_WHITE, nubX, nubY, 4, 6);
  fill_rect(ctx, C_BLACK, nubX + 1, nubY + 1, 2, 4);
  fill_rect(ctx, C_BLACK, badgeX, badgeY, badgeW, badgeH);
  fill_rect(ctx, C_WHITE, badgeX, badgeY, badgeW, 1);
  fill_rect(ctx, C_WHITE, badgeX, badgeY + badgeH - 1, badgeW, 1);
  fill_rect(ctx, C_WHITE, badgeX, badgeY, 1, badgeH);
  fill_rect(ctx, C_WHITE, badgeX + badgeW - 1, badgeY, 1, badgeH);

  char pctbuf[8];
  snprintf(pctbuf, sizeof(pctbuf), "%d", s_charge);
  draw_text_centered(ctx, pctbuf, s_font_small, C_WHITE, badgeX, badgeX + badgeW, badgeY - 1);

  // TIME
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  char tbuf[8];
  snprintf(tbuf, sizeof(tbuf), "%02d:%02d", t->tm_hour, t->tm_min);
  draw_text_centered(ctx, tbuf, s_font_time, C_WHITE, 0, W, 20);

  // GRID lines
  fill_rect(ctx, C_WHITE, 6, 86, W - 12, 2);
  fill_rect(ctx, C_WHITE, 6, 141, W - 12, 2);
  fill_rect(ctx, C_WHITE, 99, 88, 2, 53);          // top-row divider
  const int col1 = 6, col2 = 72, col3 = 138, colR = 194;
  fill_rect(ctx, C_WHITE, col2, 143, 2, 66);       // bottom-row dividers
  fill_rect(ctx, C_WHITE, col3, 143, 2, 66);

  // Cell 1: DATE (day-of-week + day-of-month)
  const int wday = t->tm_wday;   // 0 = Sunday
  const bool weekend = (wday == 0 || wday == 6);
  char dbuf[12];
  snprintf(dbuf, sizeof(dbuf), "%s %02d", DAYS[wday], t->tm_mday);
  draw_text_centered(ctx, dbuf, s_font_date, weekend ? C_GREEN : C_WHITE, 6, 99, 95);

  // Cell 2: WEATHER
  ensure_weather_dci();
  if (s_dci_weather) gdraw_command_image_draw(ctx, s_dci_weather, GPoint(108, 92));
  char tempbuf[16];
  if (!s_have_temp) {
    snprintf(tempbuf, sizeof(tempbuf), "--");
  } else {
    const int show = s_have_feels ? (int)s_feels : (int)s_temp;
    snprintf(tempbuf, sizeof(tempbuf), "%s%d\xC2\xB0", show >= 0 ? "+" : "", show);
  }
  draw_text_centered(ctx, tempbuf, s_font_big, C_WHITE, 150, W, 100);

  // Cell 3: HEART (left bottom)
  const int heartX = col1 + ((col2 - col1) - 40) / 2 + 7;
  draw_bitmap(ctx, s_bm_heart, heartX, 147);
  char hb[16];
  if (s_bpm < 0) snprintf(hb, sizeof(hb), "--");
  else snprintf(hb, sizeof(hb), "%d", (int)s_bpm);
  draw_text_centered(ctx, hb, s_font_big, C_WHITE, col1, col2, 170);

  // Cell 4: STEPS (middle bottom)
  const int stepsX = col2 + ((col3 - col2) - 29) / 2;
  draw_bitmap(ctx, s_bm_steps, stepsX, 147);
  char sb[16];
  if (s_steps < 0) snprintf(sb, sizeof(sb), "--");
  else snprintf(sb, sizeof(sb), "%d", (int)s_steps);
  draw_text_centered(ctx, sb, s_font_big, C_WHITE, col2, col3, 170);

  // Cell 5: Bluetooth + Quiet Time (right bottom)
  const int center5 = (col3 + colR) / 2;
  draw_bitmap(ctx, s_connected ? s_bm_bt : s_bm_bt_off, center5 - 8, 147);
  if (quiet_time_is_active()) draw_bitmap(ctx, s_bm_quiet, center5 - 9, 182);

  // DAYLIGHT BAR — 12-hour window centered on astronomical noon
  //   AM: [midnight, noon]  PM: [noon, midnight]  (astronomical)
  if (s_have_sun) {
    const int sr = s_sunrise_h * 60 + s_sunrise_m;   // minutes from midnight
    const int ss = s_sunset_h * 60 + s_sunset_m;
    const int cur = t->tm_hour * 60 + t->tm_min;
    const int noon = (sr + ss) / 2;

    const bool isAM = (cur < noon);
    const int winStart = isAM ? (noon - 12 * 60) : noon;
    const int winLen = 12 * 60;

    fill_rect(ctx, C_GRAYD, 0, 210, W, 14);
    const int sx = clamp_int(round_div((sr - winStart) * W, winLen), 0, W);
    const int ex = clamp_int(round_div((ss - winStart) * W, winLen), 0, W);
    fill_rect(ctx, C_YELLOW, sx, 210, ex - sx, 14);

    const int markX = round_div((cur - winStart) * W, winLen);
    const int markClamped = clamp_int(markX, 0, W - 4);
    fill_rect(ctx, C_GREEN, markClamped, 206, 4, 22);
  }
}

// ---------------------------------------------------------------------------
// Weather requests (C -> PKJS)
// ---------------------------------------------------------------------------

static void busy_clear_handler(void *data) {
  (void)data;
  s_busy_clear_timer = NULL;
  s_weather_busy = false;
  // PKJS never answered (HTTP error swallowed on the phone side): drop
  // busy so the next tick re-evaluates the refresh threshold and retries.
}

static void send_weather_request(uint8_t mode) {
  // Guard against overlapping triggers (init timer + tick firing close
  // together, or several ticks arriving while a request is in flight).
  if (s_weather_busy) return;
  DictionaryIterator *iter = NULL;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK) return;
  dict_write_uint8(iter, MESSAGE_KEY_REQUEST_WEATHER, mode);
  if (app_message_outbox_send() != APP_MSG_OK) return;
  s_weather_busy = true;
  s_first_fetch_done = true;
  // Failsafe: if PKJS hits an HTTP/network error it logs but never
  // answers, so without this the busy flag would stick. 60s is well
  // under the 15-min refresh window, so a dropped request is retried
  // on the next minute tick rather than held for the full window.
  if (s_busy_clear_timer) app_timer_cancel(s_busy_clear_timer);
  s_busy_clear_timer = app_timer_register(60000, busy_clear_handler, NULL);
}

// ---------------------------------------------------------------------------
// AppMessage inbox (PKJS -> C): weather data
// ---------------------------------------------------------------------------

static void inbox_received_handler(DictionaryIterator *iter, void *context) {
  (void)context;
  Tuple *tuple;

  if ((tuple = dict_find(iter, MESSAGE_KEY_WEATHER_CODE))) {
    s_weather_code = (int32_t)tuple->value->int32;
  }
  if ((tuple = dict_find(iter, MESSAGE_KEY_WEATHER_TEMP))) {
    s_temp = (int32_t)tuple->value->int32;
    s_have_temp = true;
  }
  if ((tuple = dict_find(iter, MESSAGE_KEY_WEATHER_FEELS_LIKE))) {
    s_feels = (int32_t)tuple->value->int32;
    s_have_feels = true;
  }
  if ((tuple = dict_find(iter, MESSAGE_KEY_WEATHER_SUNRISE_H))) {
    s_sunrise_h = tuple->value->uint8;
    s_have_sun = true;
  }
  if ((tuple = dict_find(iter, MESSAGE_KEY_WEATHER_SUNRISE_M))) {
    s_sunrise_m = tuple->value->uint8;
  }
  if ((tuple = dict_find(iter, MESSAGE_KEY_WEATHER_SUNSET_H))) {
    s_sunset_h = tuple->value->uint8;
  }
  if ((tuple = dict_find(iter, MESSAGE_KEY_WEATHER_SUNSET_M))) {
    s_sunset_m = tuple->value->uint8;
  }

  // Success: stamp the refresh clock, clear the in-flight guard.
  s_last_weather_success = time(NULL);
  if (s_busy_clear_timer) { app_timer_cancel(s_busy_clear_timer); s_busy_clear_timer = NULL; }
  s_weather_busy = false;
  layer_mark_dirty(s_layer);
}

static void inbox_dropped_handler(AppMessageResult reason, void *context) {
  (void)context;
  (void)reason;
  // Incoming weather dict was damaged in transit: behave like a
  // no-answer timeout so the next tick retries.
  if (s_busy_clear_timer) { app_timer_cancel(s_busy_clear_timer); s_busy_clear_timer = NULL; }
  s_weather_busy = false;
}

// ---------------------------------------------------------------------------
// Service callbacks
// ---------------------------------------------------------------------------

static void battery_handler(BatteryChargeState charge) {
  s_charge = charge.charge_percent;
  layer_mark_dirty(s_layer);
}

static void connection_handler(bool connected) {
  s_connected = connected;
  layer_mark_dirty(s_layer);
}

static void health_handler(HealthEventType event, void *context) {
  (void)context;
  if (event == HealthEventHeartRateUpdate ||
      event == HealthEventMovementUpdate ||
      event == HealthEventSignificantUpdate) {
    s_steps = (int32_t)health_service_sum_today(HealthMetricStepCount);
    s_bpm = (int32_t)health_service_peek_current_value(HealthMetricHeartRateBPM);
    layer_mark_dirty(s_layer);
  }
}

static bool weather_due(void) {
  // Refresh threshold is measured from the last successful WEATHER_* inbox.
  // On a failed request s_last_weather_success is NOT moved, so this returns
  // true again and the next minute tick retries ("retry on failure") without
  // any explicit retry timer.
  if (s_last_weather_success == 0) return true;   // never succeeded yet
  return (time(NULL) - s_last_weather_success) >= WEATHER_REFRESH_S;
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  (void)units_changed;
  layer_mark_dirty(s_layer);

  // hourchange -> force a fresh GPS fix (once per hour, like the JS version).
  if (tick_time->tm_hour != s_last_hour) {
    s_last_hour = tick_time->tm_hour;
    if (weather_due()) send_weather_request(0);
    return;
  }

  // Minute tick: refresh from cache if the 15-min threshold has elapsed.
  if (weather_due()) send_weather_request(s_first_fetch_done ? 1 : 0);
}

// ---------------------------------------------------------------------------
// Window lifecycle
// ---------------------------------------------------------------------------

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);
  bounds.origin = GPointZero;
  s_layer = layer_create(bounds);
  layer_set_update_proc(s_layer, layer_update_proc);
  layer_add_child(root, s_layer);

  // Fonts
  s_font_time = fonts_get_system_font(FONT_KEY_ROBOTO_BOLD_SUBSET_49);
  s_font_big  = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
  s_font_date = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
  s_font_small = fonts_get_system_font(FONT_KEY_GOTHIC_14);

  // Icons (PNG bitmaps)
  s_bm_heart   = gbitmap_create_with_resource(RESOURCE_ID_IC_HEART);
  s_bm_steps   = gbitmap_create_with_resource(RESOURCE_ID_IC_STEPS);
  s_bm_quiet   = gbitmap_create_with_resource(RESOURCE_ID_IC_QUIET_TIME);
  s_bm_bt      = gbitmap_create_with_resource(RESOURCE_ID_IC_BLUETOOTH);
  s_bm_bt_off  = gbitmap_create_with_resource(RESOURCE_ID_IC_BLUETOOTH_OFF);

  // Seed s_last_hour so the first minute tick isn't mistaken for an hour
  // change (would otherwise duplicate the init_timer's initial fetch).
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  s_last_hour = t->tm_hour;

  // Initial sensor reads
  s_charge = battery_state_service_peek().charge_percent;
  s_connected = connection_service_peek_pebble_app_connection();

  // Service subscriptions
  battery_state_service_subscribe(battery_handler);
  connection_service_subscribe((ConnectionHandlers) {
    .pebble_app_connection_handler = connection_handler,
    .pebblekit_connection_handler = NULL,
  });
  health_service_events_subscribe(health_handler, NULL);
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);

  // AppMessage (weather relay with PKJS)
  app_message_open(64, 64);
  app_message_register_inbox_received(inbox_received_handler);
  app_message_register_inbox_dropped(inbox_dropped_handler);

  // Initial fetch: tick_timer_service_subscribe fires once immediately on
  // subscribe in this runtime, which performs the first fetch via the single
  // gate (weather_due() true since success==0; mode 0 since first_fetch_done
  // is false). All refreshes — initial, hour-change, 15-min — go through
  // tick_handler -> weather_due -> send_weather_request, so they dedupe via
  // weather_due() and the busy guard. No separate init timer.
}

static void window_unload(Window *window) {
  (void)window;
  if (s_busy_clear_timer) { app_timer_cancel(s_busy_clear_timer); s_busy_clear_timer = NULL; }

  tick_timer_service_unsubscribe();
  health_service_events_unsubscribe();
  battery_state_service_unsubscribe();
  connection_service_unsubscribe();
  app_message_deregister_callbacks();

  gbitmap_destroy(s_bm_heart);
  gbitmap_destroy(s_bm_steps);
  gbitmap_destroy(s_bm_quiet);
  gbitmap_destroy(s_bm_bt);
  gbitmap_destroy(s_bm_bt_off);
  if (s_dci_weather) { gdraw_command_image_destroy(s_dci_weather); s_dci_weather = NULL; }

  layer_destroy(s_layer);
  s_layer = NULL;
}

int main(void) {
  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);

  app_event_loop();

  window_destroy(s_window);
}
