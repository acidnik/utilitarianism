# Project-specific agent instructions — Utilitarianism

Pure-C Pebble watchface for the **emery** (Pebble Time 2, 200×228 colour) display.

## Architecture

```
src/c/main.c         All drawing, sensors, orchestration (C)
src/pkjs/index.js    Weather + location relay (phone-side JS)
resources/           PNG icons + PDC weather vectors
```

C does everything on-watch: time, date, battery (rainbow + badge), grid, weather icon/temperature, heart rate, steps, bluetooth / quiet-time, and the daylight bar with hour ticks. The phone (`PKJS`) only handles what the watch cannot reach: **GPS + HTTP** (Open-Meteo weather).

## Weather protocol (C ↔ PKJS)

Messages are AppMessage with auto-generated keys (`message_keys.auto.h` / PKJS global `messageKeys`).

| Key                  | Direction   | Purpose                                    |
|----------------------|-------------|--------------------------------------------|
| `REQUEST_WEATHER`    | C → PKJS   | `0` = geolocate + fetch · `1` = cache only |
| `WEATHER_CODE`       | PKJS → C   | Open-Meteo WMO weather code (int)          |
| `WEATHER_TEMP`       | PKJS → C   | Rounded current temp (°C, int)             |
| `WEATHER_FEELS_LIKE` | PKJS → C   | Rounded apparent temp (°C, int)            |
| `WEATHER_SUNRISE_H/M`| PKJS → C   | Sunrise hour (uint8) / minute (uint8)      |
| `WEATHER_SUNSET_H/M` | PKJS → C   | Sunset hour / minute                       |

### Cadence (all in C, no pkjs-initiated fetches)

- **Initial fetch:** the tick timer fires once immediately on subscribe (`tick_timer_service_subscribe`). `weather_due()` returns true (no success yet). Mode 0 (geolocate).
- **Hour change:** detected in `tick_handler` via `s_last_hour`. Mode 0.
- **15-minute refreshes:** `weather_due()` checks `time(NULL) - s_last_weather_success >= 900`. If true, mode 1 (cached coords — no GPS).
- **Retry on failure:** `s_last_weather_success` is only updated on successful `WEATHER_*` inbox. On failure (no inbox, busy clears via 60s timeout) `weather_due()` stays true and the next minute tick retries.

### Guards

- **`s_weather_busy`** — prevents overlapping triggers while a request is in flight.
- **`s_busy_clear_timer`** (60 s) — failsafe: PKJS swallows HTTP errors and never answers, so without this timer `busy` would stick forever. On timeout `busy` drops and the next tick retries.
- **`weather_due()`** — 15-minute refresh gate, gated only in `tick_handler` (not in `send_weather_request` directly — add it if adding a new fetch path).

## Drawing

### Colors

`GColor8` (union over `a:2, r:2, g:2, b:2`, packed little-endian). Compound literals from `GColorFromRGB()` are NOT compile-time constants for `static const` initializers. Use the `ARGB(r,g,b)` macro:

```c
#define ARGB(r,g,b) ((uint8_t)((0xC0u) | ((r>>6)<<4) | ((g>>6)<<2) | (b>>6)))
static const GColor C_FOO = {.argb = ARGB(128,64,32)};
```

For named palette colours, use the `*ARGB8` literal (e.g. `GColorImperialPurpleARGB8`), not the macro.

### Integer-only math

All geometry uses `int`, never `float`/`double`. Rounding uses `round_div(a,b)` (nearest, half away from zero). The Pebble SDK toolchain does not link soft-float by default; using floats would require extra `-lm` linkage.

### Daylight bar functions

- `time_to_min(hour, minute)` — minutes from midnight.
- `daybar_tick_x(time_min, win_start)` — pixel `x` in [0, W‑1] within the 12‑hour window.
- `draw_daylight_bar(ctx, tm*)` — draws the whole bar (grayD background, yellow daylight segment, green current-time marker, 12 hour‑ticks at `:00` each). Tick colours invert: black on yellow, yellow on dark.

### Text centering

`draw_text_centered(ctx, text, font, color, x0, x1, y)` uses `GTextAlignmentCenter` → no manual width measurement needed.

### Bitmaps (PNG)

Always set `GCompOpSet` compositing for PNG icons (they have transparent backgrounds). Restore to `GCompOpAssign` afterwards.

## PKJS notes

- The runtime lacks `fetch` — use `XMLHttpRequest` with the patched `setRequestHeader`/`getAllResponseHeaders` (workarounds for trailing‑CRLF bugs in the phone app’s proxy).
- `navigator.geolocation.getCurrentPosition` is available via the phone app’s WebView.
- Coordinates are cached in PKJS memory (`lastLat`/`lastLon`). Mode‑1 requests reuse the cache.
- Async code uses `.then()` chains, not `async/await` (webpack may not transpile).

## Pebble SDK reference

The SDK is at `/home/nik/.local/share/pebble-sdk/SDKs/4.17/sdk-core/pebble/`.
Each platform has its own `include/pebble.h` which bundles every public API.
For **emery** (our target):

```
/home/nik/.local/share/pebble-sdk/SDKs/4.17/sdk-core/pebble/emery/include/pebble.h
```

### How to check if an API exists

```bash
grep -n 'function_name' /home/nik/.local/share/pebble-sdk/SDKs/4.17/sdk-core/pebble/emery/include/pebble.h
```

Or for macros, constants, and colour definitions:

```bash
grep -n 'GColorPurple\|RESOURCE_ID' /home/nik/.local/share/pebble-sdk/SDKs/4.17/sdk-core/pebble/emery/include/gcolor_definitions.h
```

### Auto-generated headers (build-time)

| File | Purpose |
|------|---------|
| `build/include/message_keys.auto.h` | `extern uint32_t MESSAGE_KEY_*` for C |
| `build/emery/src/resource_ids.auto.h` | `#define RESOURCE_ID_*` (icons, fonts) |
| `build/js/message_keys.json` | name→id mapping, consumed by PKJS webpack |

After changing `messageKeys` in `package.json`, run `pebble build` once to
regenerate these files before trusting autocomplete or static analysis.

### Font keys

Emery system fonts (from `pebble_fonts.h`):

| JS name | C `FONT_KEY_*` |
|---------|----------------|
| `Roboto-Bold` 49 | `FONT_KEY_ROBOTO_BOLD_SUBSET_49` |
| `Gothic-Bold` 28 | `FONT_KEY_GOTHIC_28_BOLD` |
| `Gothic-Regular` 14 | `FONT_KEY_GOTHIC_14` |

Full list: grep `FONT_KEY_` in the emery `pebble_fonts.h`.

### Build

```bash
pebble build                                 # debug: pebble build --debug
pebble install --emulator emery               # run in QEMU
pebble install --phone <ip>                   # install to phone app
```

## Known issues / quirks

- The linker RWX‑segment warning is a standard Pebble ELF artefact; ignore it.
- `U: pkjs ready` + immediate geolocate is normal — it's the watch's C‑side `tick_handler` firing on subscribe.
- The `init_weather_timer_handler` and `s_init_timer` were removed: the immediate tick (fires on subscribe) does the initial fetch, and all paths go through the single `tick_handler → weather_due → send_weather_request` gate. Do NOT add a separate init timer unless the runtime changes.
