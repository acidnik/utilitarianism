// Phone-side PKJS relay:
//  - moddableProxy: forwards fetch() and Location sensor between watch and phone.
//  - health relay: forwards HEALTH_STEPS / HEART_RATE_BPM back to watch JS.
//
// Patches XMLHttpRequest to work around proxy bugs:
//  1. setRequestHeader — reject empty header names (trailing \n → split "")
//  2. getAllResponseHeaders — strip trailing end-of-line markers so split by \r\n
//     doesn't produce empty trailing entries that crash .map() with undefined.trim().

const moddableProxy = require("@moddable/pebbleproxy");

// Workaround 1: ignore empty header names.
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
  // Remove any trailing \r\n sequences (the proxy splits by \r\n, and empty
  // trailing entries crash its .map() with undefined.trim()).
  return raw.replace(/(?:\r\n)+$/g, '').replace(/^\r\n/, '');
};

Pebble.addEventListener('ready', moddableProxy.readyReceived);
Pebble.addEventListener('appmessage', function (e) {
  // Let the proxy handle its own messages (HTTP/WS/Location/ready) first.
  if (moddableProxy.appMessageReceived(e))
    return;

  // Otherwise: relay health keys from watch C back to watch JS.
  if (!e || !e.payload)
    return;

  const relay = {};
  if (e.payload.HEALTH_STEPS !== undefined)
    relay.HEALTH_STEPS = e.payload.HEALTH_STEPS;
  if (e.payload.HEART_RATE_BPM !== undefined)
    relay.HEART_RATE_BPM = e.payload.HEART_RATE_BPM;
  if (e.payload.QUIET_TIME !== undefined)
    relay.QUIET_TIME = e.payload.QUIET_TIME;

  if (Object.keys(relay).length === 0)
    return;

  Pebble.sendAppMessage(
    relay,
    function () { /* forwarded health snapshot */ },
    function (err) { console.log("relay: forward failed " + JSON.stringify(err)); }
  );
});
