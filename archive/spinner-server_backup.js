// spinner-server.js (streamlined: MQTT-only)
//
// Usage: configure PHOTOPRISM_API and MQTT_URL, then run `node spinner-server.js`
// Ensure your MQTT broker supports websockets if the browser uses mqtt-over-ws;
// spinner-server uses plain MQTT (tcp) to publish retained slide messages.

const mqtt      = require("mqtt");
const fetch     = require("node-fetch"); // keep this if you run Node < 18; otherwise global fetch is fine

// ───── CONFIG ─────
const PHOTOPRISM_API  = "http://192.168.68.81:2342";   // Photoprism base URL
const MQTT_URL        = "mqtt://localhost:1883";       // broker (change to mqtt://slideshow:1883 if you added hosts)
const SLIDE_TOPIC     = "spinner/slideshow";
const MAX_REFRESH_MS  = 30 * 60 * 1000; // 30 minutes

// ───── Handlers (same as before) ─────
const dateHandler    = require("./handlers/dateHandler");
const photoHandler   = require("./handlers/photoHandler");
const peopleHandler  = require("./handlers/peopleHandler");
const friendHandler  = require("./handlers/friendHandler");
const birthFamHandler = require('./handlers/birthFamHandler');
const cousinsHandler = require('./handlers/cousinsHandler');
const afamilyHandler = require('./handlers/afamilyHandler');
const distanceHandler = require('./handlers/distanceHandler');
const daysHandler = require('./handlers/daysHandler');
const themeAHandler = require('./handlers/themeAHandler');

const handlers = {
  "spinner/date":        dateHandler,
  "spinner/date/count":  photoHandler,
  "spinner/people":      peopleHandler,
  "spinner/friend":      friendHandler,
  "spinner/birthfam":    birthFamHandler,
  "spinner/cousins":     cousinsHandler,
  "spinner/afamily":     afamilyHandler,
  "spinner/distance":    distanceHandler,
  "spinner/days":        daysHandler,
  "spinner/themeA":      themeAHandler
};

// ───── Album map for max-count topics (same) ─────
const albumMap = {
  "spinner/date/count": "asvl97q34341yxl8"
};

// ───── State ─────
let currentKey  = null;
let photoHashes = [];
let slideIndex  = 0;
let timerId     = null;

// Sequence counter for published slides (monotonic)
let seqCounter = 0;

// ───── Helper: broadcast an image via MQTT only (retain + seq + ts) ─────
function broadcastSlide(hash, key) {
  const url   = `${PHOTOPRISM_API}/api/v1/t/${hash}/public/fit_1920`;
  const slide = {
    type: "image",
    url,
    key,
    seq: ++seqCounter,
    ts: Date.now()
  };

  const payload = JSON.stringify(slide);
  // publish retained so new clients immediately see the current slide
  mqttClient.publish(SLIDE_TOPIC, payload, { retain: true }, (err) => {
    if (err) {
      console.error("❌ MQTT publish error:", err);
    } else {
      console.log("🖼️ Slide (published, retained):", slide);
    }
  });
}

// ───── Helper: slideshow runner ─────
function startSlideshow(intervalMs, key) {
  if (timerId) clearInterval(timerId);
  if (!photoHashes || photoHashes.length === 0) {
    console.warn("⚠️ startSlideshow called with empty photoHashes");
    return;
  }
  slideIndex = 0;
  broadcastSlide(photoHashes[slideIndex % photoHashes.length], key);
  slideIndex++;
  if (intervalMs > 0) {
    timerId = setInterval(() => {
      broadcastSlide(photoHashes[slideIndex % photoHashes.length], key);
      slideIndex++;
    }, intervalMs);
  }
}

// ───── Fetch & publish max‐index for one album (retained) ─────
async function refreshMaxForTopic(topic, albumUID) {
  try {
    const url = `${PHOTOPRISM_API}/api/v1/photos?album=${encodeURIComponent(albumUID)}&public=true&count=1000`;
    const res = await fetch(url);
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    const list = await res.json();
    if (!Array.isArray(list)) throw new Error("Expected array");
    const maxIdx = list.length - 1;
    const maxTopic = `${topic}/max`;
    mqttClient.publish(maxTopic, String(maxIdx), { retain: true }, (err) => {
      if (err) console.error("❌ publish max error:", err);
      else console.log(`📏 [refresh] published ${maxTopic} = ${maxIdx}`);
    });
  } catch (err) {
    console.error(`❌ [refresh] error for ${topic}:`, err);
  }
}

// ───── Periodic refresh of all max‐index topics ─────
function refreshAllMax() {
  for (const [topic, albumUID] of Object.entries(albumMap)) {
    refreshMaxForTopic(topic, albumUID);
  }
}

// ───── MQTT setup ─────
const mqttClient = mqtt.connect(MQTT_URL, { reconnectPeriod: 2000 });

mqttClient.on("connect", () => {
  console.log("📡 MQTT connected");
  // Subscribe to control topics
  mqttClient.subscribe(Object.keys(handlers), err => {
    if (err) console.error("❌ Subscribe error:", err);
    else console.log("✅ Subscribed to handler topics");
  });

  // Subscribe to each max‐topic (to echo cached max data if needed)
  mqttClient.subscribe(Object.keys(albumMap).map(t => `${t}/max`), err => {
    if (err) console.error("❌ Subscribe max error:", err);
    else console.log("✅ Subscribed to album max topics");
  });

  // Do an immediate refresh + schedule periodic
  refreshAllMax();
  setInterval(refreshAllMax, MAX_REFRESH_MS);
});

mqttClient.on("reconnect", () => console.log("📡 MQTT reconnecting..."));
mqttClient.on("error", (err) => console.error("❌ MQTT error:", err));
mqttClient.on("close", () => console.warn("⚠️ MQTT closed"));

// ───── Incoming messages ─────
mqttClient.on("message", async (topic, buf) => {
  const msg = buf.toString().trim();

  // Ignore max topics here (they are informational)
  if (topic.endsWith("/max")) {
    console.log(`📏 MQTT [${topic}]: ${msg}`);
    return;
  }

  console.log(`📥 MQTT [${topic}]:`, msg);
  const handler = handlers[topic];
  if (!handler) {
    return console.warn("⚠️ No handler for topic:", topic);
  }

  // Parse payload: integer for *count*, JSON otherwise
  let payload;
  try {
    payload = topic.endsWith("/count") ? parseInt(msg, 10) : JSON.parse(msg);
  } catch (e) {
    return console.error("❌ Payload parse error:", e);
  }

  // Deduplicate: ignore identical consecutive triggers
  const key = `${topic}:${msg}`;
  if (key === currentKey) {
    console.log("↩️ Duplicate, ignoring");
    return;
  }
  currentKey = key;

  // Clear any running slideshow
  if (timerId) {
    clearInterval(timerId);
    timerId = null;
  }

  // Invoke handler
  let result;
  try {
    result = await handler(payload, { photoprism: PHOTOPRISM_API });
  } catch (e) {
    return console.error(`❌ Handler error [${topic}]:`, e);
  }

  if (!result || !Array.isArray(result.hashes) || result.hashes.length === 0) {
    return console.warn(`⚠️ No photos returned for ${topic}`, payload);
  }

  // Set the current list and dispatch
  photoHashes = result.hashes;

  // Dispatch single shot vs slideshow
  if (result.intervalMs === 0 && topic.endsWith("/count")) {
    const idx = Math.min(Math.max(0, payload), photoHashes.length - 1);
    broadcastSlide(photoHashes[idx], key);
  } else {
    startSlideshow(result.intervalMs, key);
  }
});

console.log("🚀 spinner-server running (MQTT-only)");
process.stdin.resume();