# utilitarianism

A Pebble watchface written in **pure C** using the Pebble SDK (target: emery,
the 200×228 color display).

## Layout

The watch renders everything itself in C (time, date, battery rainbow + badge,
grid, weather icon/temperature, heart rate, steps, bluetooth/quiet-time, and a
daylight/sun-position bar). The only piece kept on the phone side is the PKJS
relay, which does what the watch cannot reach directly: **GPS + network**.

```
src/c/main.c         Watch-side: all drawing + sensors + orchestration
src/pkjs/index.js    Phone-side: weather + location relay (Open-Meteo)
resources/            Icons (PNG bitmaps + PDC weather vectors)
package.json          Project metadata, message keys, resources
wscript               Build rules — usually no need to edit
```

### Weather / location flow

The watch sends a `REQUEST_WEATHER` AppMessage and PKJS answers with the
`WEATHER_*` keys (current temperature, apparent temperature, weather code, and
today's sunrise/sunset):

| key (C ↔ PKJS)        | direction   | meaning                                       |
|----------------------|-------------|-----------------------------------------------|
| `REQUEST_WEATHER`    | C → PKJS   | `0` = geolocate + fetch · `1` = cached refresh |
| `WEATHER_CODE`       | PKJS → C   | Open-Meteo WMO weather code                    |
| `WEATHER_TEMP`       | PKJS → C   | rounded current temperature (°C)              |
| `WEATHER_FEELS_LIKE` | PKJS → C   | rounded apparent temperature (°C)             |
| `WEATHER_SUNRISE_H/M`| PKJS → C   | sunrise hour / minute                          |
| `WEATHER_SUNSET_H/M` | PKJS → C   | sunset hour / minute                           |

Cadence matches the original JS version: an initial geolocate + fetch on launch,
a fresh geolocate on each hour change, and a cached-coordinate refresh every 15
minutes (no GPS re-request).

## Building & running

```sh
pebble build                          # build for the target platforms
pebble install --emulator emery       # install on the emery emulator
pebble install --phone <ip>           # install to a paired phone
```

`pebble build --debug` enables the JS debugger for the PKJS bundle.

## Target platforms

`targetPlatforms` in `package.json` controls which watches you build for. This
project targets **emery** (Pebble Time 2). Add `basalt`, `chalk`, etc. for
older hardware, but the layout coordinates are tuned for the 200×228 emery
display.

## Documentation

Full SDK docs, tutorials, and API reference: <https://developer.repebble.com>