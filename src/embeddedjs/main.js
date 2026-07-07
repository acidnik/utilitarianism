import Poco from "commodetto/Poco";
import Location from "embedded:sensor/Location";
import Battery from "embedded:sensor/Battery";
import Message from "pebble/message";

const render = new Poco(screen);
const W = render.width;   // 200
const H = render.height;  // 228

// ---- Fonts ----
// const timeFont = new render.Font("Bitham-Bold", 42);
// const timeFont = new render.Font("Leco-Regular", 42);
const timeFont = new render.Font("Roboto-Bold", 49);
const bigFont  = new render.Font("Gothic-Bold", 28);
const dateFont = new render.Font("Gothic-Bold", 28);
const smallFont = new render.Font("Gothic-Regular", 14);

// ---- Colors ----
const black  = render.makeColor(0, 0, 0);
const white  = render.makeColor(255, 255, 255);
const red    = render.makeColor(255, 42, 42);
const green  = render.makeColor(63, 214, 63);
const yellow = render.makeColor(255, 224, 0);
const grayD  = render.makeColor(58, 58, 58);
const gray   = render.makeColor(150, 150, 150);
const dark   = render.makeColor(34, 34, 34);

const rainbow = [
  render.makeColor(255, 42, 42),
  render.makeColor(255, 138, 0),
  render.makeColor(255, 224, 0),
  render.makeColor(63, 214, 63),
  render.makeColor(42, 155, 255),
  render.makeColor(155, 77, 255),
];

const DAYS = ["SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"];

// ---- Icons (PNG bitmaps + PDC weather icons) ----
// NOTE: confirm the ID/name resolution on first build. If numeric IDs don't
// work, the resource is likely addressed differently in your SDK version —
// see README "Resource IDs" before assuming the icons are broken.
const icHeart  = new Poco.PebbleBitmap(1);
const icSteps  = new Poco.PebbleBitmap(2);
const icSunny  = new Poco.PebbleDrawCommandImage(3);
const icPartly = new Poco.PebbleDrawCommandImage(4);
const icCloudy = new Poco.PebbleDrawCommandImage(5);
const icDrizzle = new Poco.PebbleDrawCommandImage(6);
const icRain    = new Poco.PebbleDrawCommandImage(7);
const icSnow    = new Poco.PebbleDrawCommandImage(8);
const icSleet   = new Poco.PebbleDrawCommandImage(9);
const icFog        = new Poco.PebbleDrawCommandImage(10);
const icQuietTime  = new Poco.PebbleBitmap(11);
const icBluetooth   = new Poco.PebbleBitmap(12);
const icBluetoothOff = new Poco.PebbleBitmap(13);

// Open-Meteo weather_code -> icon mapping
// Codes follow WMO standard: https://open-meteo.com/en/docs
function weatherIcon(code) {
  if (code === 0) return icSunny;                   // Clear sky
  if (code <= 2)  return icPartly;                  // Mainly clear / partly cloudy
  if (code <= 3)  return icCloudy;                  // Overcast
  if (code <= 48) return icFog;                     // Fog / mist / haze
  if (code <= 55) return icDrizzle;                 // Drizzle: light to heavy
  if (code <= 57) return icSleet;                   // Freezing drizzle
  if (code <= 65) return icRain;                    // Rain: slight to heavy
  if (code <= 67) return icSleet;                   // Freezing rain
  if (code <= 77) return icSnow;                    // Snow fall / snow grains
  if (code <= 82) return icDrizzle;                 // Rain showers
  if (code <= 86) return icSnow;                    // Snow showers
  return icRain;                                    // Thunderstorm (95-99)
}

console.log("U: app started");

// ---- Live data (filled by fetchWeather / sensors) ----
let battery     = 1;       // 0..1 fraction, from Battery sensor
let temp        = null;
let feelsLike   = null;
let weatherCode = 0;
let bpm         = -1;
let steps       = -1;
let quietTime   = false;
let isConnected = true;
let sunrise     = null;   // fraction-of-day hours, from Open-Meteo daily
let sunset      = null;

// ---- Battery sensor ----
// onSample fires when the charge state changes. sample() returns
// { percent: 0..100, charging: bool, plugged: bool }.
const batterySensor = new Battery({
  onSample() {
    const s = this.sample();
    battery = s.percent / 100;
    redraw();
  },
});
battery = batterySensor.sample().percent / 100;   // initial value

// ---- Health relay (steps + heart rate from C via PKJS) ----
// C-side health_relay.c reads HealthService and sends HEALTH_STEPS /
// HEART_RATE_BPM over AppMessage; PKJS forwards them back here.
const healthMessage = new Message({
  keys: ["HEALTH_STEPS", "HEART_RATE_BPM", "QUIET_TIME"],
  onReadable() {
    const data = healthMessage.read();
    console.log("U: health msg");
    if (data.has("HEALTH_STEPS"))   steps = data.get("HEALTH_STEPS");
    if (data.has("HEART_RATE_BPM")) bpm   = data.get("HEART_RATE_BPM");
    if (data.has("QUIET_TIME"))    quietTime = data.get("QUIET_TIME") === 1;
    redraw();
  },
});

// ---- Connection monitoring ----
isConnected = watch.connected.app;
watch.addEventListener("connected", function () {
  isConnected = watch.connected.app;
  redraw();
});

function textCentered(str, font, color, x0, x1, y) {
  const w = render.getTextWidth(str, font);
  render.drawText(str, font, color, x0 + ((x1 - x0) - w) / 2, y);
}

function draw(event) {
  const now = event.date;
  render.begin();
  render.fillRectangle(black, 0, 0, W, H);

  // BATTERY rainbow
  const segW = W / 6;
  const fillW = Math.round(W * battery);
  for (let i = 0; i < 6; i++) {
    const sx = Math.round(i * segW);
    const sw = Math.round((i + 1) * segW) - sx;
    if (sx < fillW) render.fillRectangle(rainbow[i], sx, 0, Math.min(sw, fillW - sx), 6);
  }
  if (fillW < W) render.fillRectangle(dark, fillW, 0, W - fillW, 6);

  // >80%: below the bar (so it doesn't hide rainbow); ≤80%: on the bar line

  const badgeW = 32, badgeH = 16;
  const badgeX = W - badgeW;
  const badgeY = battery > 0.8 ? 7 : 0;

  // Nub — small rectangle between bar and badge (battery terminal)
  const nubX = badgeX - 3;
  const nubY = badgeY + (badgeH - 6) / 2;
  render.fillRectangle(white, nubX, nubY, 4, 6);
  render.fillRectangle(black, nubX + 1, nubY + 1, 2, 4);
  render.fillRectangle(black, badgeX, badgeY, badgeW, badgeH);
  render.fillRectangle(white, badgeX, badgeY, badgeW, 1);
  render.fillRectangle(white, badgeX, badgeY + badgeH - 1, badgeW, 1);
  render.fillRectangle(white, badgeX, badgeY, 1, badgeH);
  render.fillRectangle(white, badgeX + badgeW - 1, badgeY, 1, badgeH);
  const pct = Math.round(battery * 100);
  textCentered(String(pct), smallFont, white, badgeX, badgeX + badgeW, badgeY + 1);

  // TIME
  const hh = String(now.getHours()).padStart(2, "0");
  const mm = String(now.getMinutes()).padStart(2, "0");
  textCentered(hh + ":" + mm, timeFont, white, 0, W, 28);

  // GRID lines
  render.fillRectangle(white, 6, 86, W - 12, 2);
  render.fillRectangle(white, 6, 141, W - 12, 2);
  // Top row divider (between date and weather)
  render.fillRectangle(white, 99, 88, 2, 53);
  // Bottom row dividers (3 cells)
  const col1 = 6, col2 = 72, col3 = 138, colR = 194;
  render.fillRectangle(white, col2, 143, 2, 66);
  render.fillRectangle(white, col3, 143, 2, 66);

  // Cell 1: DATE (day-of-week + day-of-month in one line)
  const weekend = (now.getDay() === 0 || now.getDay() === 6);
  const dateStr = DAYS[now.getDay()] + " " + String(now.getDate()).padStart(2, "0");
  textCentered(dateStr, dateFont, weekend ? green : white, 6, 99, 100);

  // Cell 2: WEATHER
  render.drawDCI(weatherIcon(weatherCode), 108, 92);
  let tStr;
  if (temp === null) {
    tStr = "--";
  } else {
    const show = feelsLike !== null ? feelsLike : temp;
    tStr = (show >= 0 ? "+" : "") + show + "\u00B0";
  }
  textCentered(tStr, bigFont, white, 150, W, 100);

  // Cell 3: HEART (left bottom)
  const heartX = col1 + ((col2 - col1) - 40) / 2 + 7;
  render.drawBitmap(icHeart, heartX, 147);
  textCentered(bpm < 0 ? "--" : String(bpm), bigFont, white, col1, col2, 175);

  // Cell 4: STEPS (middle bottom)
  const stepsX = col2 + ((col3 - col2) - 29) / 2;
  render.drawBitmap(icSteps, stepsX, 147);
  const stepStr = steps < 0 ? "--" : String(steps);
  textCentered(stepStr, bigFont, white, col2, col3, 175);

  // Cell 5: Bluetooth + Quiet Time (right bottom)
  const center5 = (col3 + colR) / 2;
  render.drawBitmap(isConnected ? icBluetooth : icBluetoothOff, center5 - 8, 147);
  if (quietTime) {
    render.drawBitmap(icQuietTime, center5 - 9, 182);
  }

  // DAYLIGHT BAR — 12-hour window centered on astronomical noon
  // AM: [midnight, noon]  PM: [noon, midnight]  (astronomical)
  if (sunrise !== null && sunset !== null) {
    const nowH = now.getHours() + now.getMinutes() / 60;
    const noon = (sunrise + sunset) / 2;
    const isAM = nowH < noon;
    const winStart = isAM ? noon - 12 : noon;
    const winLen = 12;
    render.fillRectangle(grayD, 0, 210, W, 14);
    const sx = Math.max(0, Math.min(W, Math.round((sunrise - winStart) / winLen * W)));
    const ex = Math.max(0, Math.min(W, Math.round((sunset  - winStart) / winLen * W)));
    render.fillRectangle(yellow, sx, 210, ex - sx, 14);
    const markX = Math.round((nowH - winStart) / winLen * W);
    render.fillRectangle(green, Math.max(0, Math.min(W - 4, markX)), 206, 4, 22);
  }

  render.end();
}

// ---- Weather + sun times via Open-Meteo ----
function hmsToFrac(iso) {
  // iso like "2026-06-29T05:12" -> 5.2 hours
  const t = iso.split("T")[1];
  const [h, m] = t.split(":").map(Number);
  return h + m / 60;
}

// HTTP GET via device.network.http.io (bypasses fetch()/Headers which can crash
// on response headers without ":"). Caches the client per origin like the SDK fetch.
let httpClient;
function httpGet(host, port, path) {
  return new Promise((resolve, reject) => {
    if (!httpClient) {
      httpClient = new device.network.http.io({
        ...device.network.http,
        host,
        port,
        onError() { reject(new Error("HTTP connection error")); },
      });
    }
    let body = [];
    httpClient.request({
      method: "GET",
      path,
      headers: new Map([["Accept", "application/json"]]),
      onHeaders(status, headers, statusText) {
        if (status >= 400) reject(new Error(status + " " + statusText));
      },
      onReadable(count) {
        const chunk = this.read();
        if (chunk) body.push(chunk);
      },
      onDone(error) {
        if (error) reject(new Error(error));
        else if (body.length) {
          const bytes = new Uint8Array(body.reduce((n, b) => n + b.byteLength, 0));
          let offset = 0;
          for (const b of body) { bytes.set(new Uint8Array(b), offset); offset += b.byteLength; }
          resolve(String.fromArrayBuffer(bytes.buffer));
        } else reject(new Error("empty response"));
      },
    });
  });
}

let weatherBusy = false;
async function fetchWeather(lat, lon) {
  if (weatherBusy) return;
  weatherBusy = true;
  try {
    const path = "/v1/forecast?latitude=" + lat + "&longitude=" + lon +
      "&current=temperature_2m,apparent_temperature,weather_code&daily=sunrise,sunset&timezone=auto";
    console.log("weather: fetching " + path);
    const text = await httpGet("api.open-meteo.com", 80, path);
    const data = JSON.parse(text);
    temp        = Math.round(data.current.temperature_2m);
    feelsLike   = Math.round(data.current.apparent_temperature);
    weatherCode = data.current.weather_code;
    sunrise     = hmsToFrac(data.daily.sunrise[0]);
    sunset      = hmsToFrac(data.daily.sunset[0]);
    console.log("weather: temp=" + temp + " code=" + weatherCode);
    redraw();
  } catch (e) {
    console.log("weather error: " + e);
  } finally {
    weatherBusy = false;
  }
}

function requestLocation() {
  console.log("location: requesting");
  try {
    const loc = new Location({
      onSample() {
        const s = this.sample();
        console.log("location: got " + s.latitude + ", " + s.longitude);
        lastLat = s.latitude;
        lastLon = s.longitude;
        this.close();
        fetchWeather(lastLat, lastLon);
      },
      onError(e) {
        console.log("location error: " + e);
      },
    });
  } catch (e) {
    console.log("location construct error: " + e);
  }
}

// redraw helper using current time
function redraw() { draw({ date: new Date() }); }

// Cache last known coordinates so weather can be refreshed without re-requesting GPS.
let lastLat = null;
let lastLon = null;

watch.addEventListener("minutechange", function (e) { console.log("U: mc"); draw(e); });
watch.addEventListener("hourchange", function (e) { console.log("U: hc"); requestLocation(); });  // fires immediately too

// Refresh weather every 15 minutes using cached coordinates (no GPS re-request).
Timer.repeat(function () {
  if (lastLat !== null && lastLon !== null) {
    console.log("U: 15min weather refresh");
    fetchWeather(lastLat, lastLon);
  }
}, 15 * 60 * 1000);
