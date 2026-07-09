//
// Phone-side PKJS for the pure-C watchface "Utilitarianism".
//
// The watch renders everything itself; PKJS only does what C cannot reach from
// the watch: GPS + network. On a REQUEST_WEATHER AppMessage from the watch it
// fetches the current weather + today's sunrise/sunset from Open-Meteo and
// answers with the WEATHER_* keys used by src/c/main.c.
//
//   REQUEST_WEATHER == 0  -> force a fresh GPS fix, then fetch (hour change)
//   REQUEST_WEATHER == 1  -> reuse cached coordinates (15-minute refresh);
//                            if no cache yet, do nothing (matches the JS timer
//                            guard `if (lastLat !== null && lastLon !== null)`)
//
// Coordinates are cached in memory so the periodic refresh doesn't re-request
// GPS, exactly like the previous watch-side `lastLat/lastLon` cache.
//
// Networking uses XMLHttpRequest: the PKJS runtime has no fetch(). The
// setRequestHeader / getAllResponseHeaders patches below work around proxy
// quirks (empty header names and trailing CRLF crashing the splitter),
// carried over from the former pebbleproxy setup.

let lastLat = null;
let lastLon = null;
let weatherBusy = false;

// Workaround 1: ignore empty header names (trailing \n -> split "").
const _origSRH = XMLHttpRequest.prototype.setRequestHeader;
XMLHttpRequest.prototype.setRequestHeader = function (name, value) {
  if (!name) return;
  return _origSRH.call(this, name, value);
};

// Workaround 2: strip trailing line endings from getAllResponseHeaders output.
const _origGARH = XMLHttpRequest.prototype.getAllResponseHeaders;
XMLHttpRequest.prototype.getAllResponseHeaders = function () {
  const raw = _origGARH.call(this);
  if (!raw) return '';
  return raw.replace(/(?:\r\n)+$/g, '').replace(/^\r\n/, '');
};

// "2026-06-29T05:12" -> [hour, minute] numbers.
function hmsParts(iso) {
  const t = iso.split("T")[1];
  const [h, m] = t.split(":").map(Number);
  return [h, m];
}

function sendWeather(temp, feelsLike, code, sunrise, sunset) {
  const [srH, srM] = hmsParts(sunrise);
  const [ssH, ssM] = hmsParts(sunset);
  const msg = {
    WEATHER_CODE: code,
    WEATHER_TEMP: temp,
    WEATHER_FEELS_LIKE: feelsLike,
    WEATHER_SUNRISE_H: srH,
    WEATHER_SUNRISE_M: srM,
    WEATHER_SUNSET_H: ssH,
    WEATHER_SUNSET_M: ssM,
  };
  console.log("weather: temp=" + temp + " feels=" + feelsLike + " code=" + code);
  Pebble.sendAppMessage(
    msg,
    function () { /* forwarded */ },
    function (err) { console.log("relay: weather send failed " + JSON.stringify(err)); }
  );
}

function fetchWeather(lat, lon) {
  const url =
    "https://api.open-meteo.com/v1/forecast?latitude=" + lat + "&longitude=" + lon +
    "&current=temperature_2m,apparent_temperature,weather_code" +
    "&daily=sunrise,sunset&timezone=auto";
  console.log("weather: fetching " + url);
  return new Promise(function (resolve, reject) {
    const xhr = new XMLHttpRequest();
    xhr.open("GET", url, true);
    xhr.setRequestHeader("Accept", "application/json");
    xhr.onload = function () {
      if (xhr.status >= 200 && xhr.status < 300) {
        try {
          resolve(JSON.parse(xhr.responseText));
        } catch (e) {
          reject(new Error("weather parse error: " + e));
        }
      } else {
        reject(new Error("HTTP " + xhr.status));
      }
    };
    xhr.onerror = function () { reject(new Error("weather network error")); };
    xhr.ontimeout = function () { reject(new Error("weather timeout")); };
    xhr.timeout = 30000;
    xhr.send();
  })
    .then(function (data) {
      const temp = Math.round(data.current.temperature_2m);
      const feelsLike = Math.round(data.current.apparent_temperature);
      const code = data.current.weather_code;
      const sunrise = data.daily.sunrise[0];
      const sunset = data.daily.sunset[0];
      sendWeather(temp, feelsLike, code, sunrise, sunset);
    })
    .catch(function (e) {
      console.log("weather error: " + e);
    });
}

// Resolves the coordinates to use. forceGeolocate=true always fixes GPS;
// otherwise the cache is returned immediately if available.
function getCoords(forceGeolocate) {
  if (!forceGeolocate && lastLat !== null && lastLon !== null) {
    return Promise.resolve({ lat: lastLat, lon: lastLon });
  }
  return new Promise(function (resolve, reject) {
    console.log("location: requesting");
    try {
      navigator.geolocation.getCurrentPosition(
        function (pos) {
          const c = pos.coords;
          lastLat = c.latitude;
          lastLon = c.longitude;
          console.log("location: got " + lastLat + ", " + lastLon);
          resolve({ lat: lastLat, lon: lastLon });
        },
        function (err) {
          reject(new Error("location error: " + (err && err.message)));
        },
        { enableHighAccuracy: false, timeout: 30000, maximumAge: 600000 }
      );
    } catch (e) {
      reject(new Error("location construct error: " + e));
    }
  });
}

function requestWeather(mode) {
  if (weatherBusy) return;
  weatherBusy = true;

  let coordsP;
  if (mode === 1) {
    // Periodic refresh: cached coords only, no GPS re-request. If no cache yet,
    // skip until the next hour-change geolocate fills it.
    if (lastLat === null || lastLon === null) {
      weatherBusy = false;
      return;
    }
    coordsP = Promise.resolve({ lat: lastLat, lon: lastLon });
  } else {
    // Hour change / initial: geolocate, then fetch.
    coordsP = getCoords(true);
  }

  coordsP
    .then(function (c) { return fetchWeather(c.lat, c.lon); })
    .catch(function (e) { console.log("" + e); })
    .then(function () { weatherBusy = false; });
}

Pebble.addEventListener("ready", function () {
  console.log("U: pkjs ready");
  // No initial fetch here: the watch (C) drives the weather cadence (an
  // initial fetch ~5s after launch, then hour-change + 15-min refreshes)
  // and sends REQUEST_WEATHER. PKJS is a dumb network responder.
});

Pebble.addEventListener("appmessage", function (e) {
  if (!e || !e.payload) return;
  if (e.payload.REQUEST_WEATHER !== undefined) {
    requestWeather(e.payload.REQUEST_WEATHER);
  }
});