// spinner-server.js (integrated with album controller)
//
// Manages:
// - Slideshow for various RFID interactions
// - Album manifests and navigation (previously album-controller-multi.js)
//
// Usage: node spinner-server.js

const mqtt = require("mqtt");
const fetch = require("node-fetch");

// ───── CONFIG ─────
const PHOTOPRISM_API = "http://192.168.68.81:2342";
const MQTT_URL = "mqtt://localhost:1883";
const SLIDE_TOPIC = "spinner/slideshow";
const NAV_WILDCARD = "spinner/album/+/nav";
const ALBUM_MANIFEST_BASE = "spinner/album";
const ALBUM_PHOTO_BASE = "spinner/album";
const MAX_REFRESH_MS = 30 * 60 * 1000; // 30 minutes

// Default birthdate for age calculations (UK format: 25th April 2019)
const DEFAULT_BIRTHDATE = "2019-04-25";

// Known albums to preload on startup
const KNOWN_ALBUMS = ["at3k2ggmwen1awna", "at3k2guo8gcj8w5m", "otheralbum"];

// ───── Handlers for existing interactions ─────
const dateHandler = require("./handlers/dateHandler");
const photoHandler = require("./handlers/photoHandler");
const peopleHandler = require("./handlers/peopleHandler");
const friendHandler = require("./handlers/friendHandler");
const birthFamHandler = require('./handlers/birthFamHandler');
const cousinsHandler = require('./handlers/cousinsHandler');
const afamilyHandler = require('./handlers/afamilyHandler');
const distanceHandler = require('./handlers/distanceHandler');
const daysHandler = require('./handlers/daysHandler');
const themeAHandler = require('./handlers/themeAHandler');

const handlers = {
  "spinner/date": dateHandler,
  "spinner/date/count": photoHandler,
  "spinner/people": peopleHandler,
  "spinner/friend": friendHandler,
  "spinner/birthfam": birthFamHandler,
  "spinner/cousins": cousinsHandler,
  "spinner/afamily": afamilyHandler,
  "spinner/distance": distanceHandler,
  "spinner/days": daysHandler,
  "spinner/themeA": themeAHandler
};

// ───── Album map for max-count topics ─────
const albumMap = {
  "spinner/date/count": "asvl97q34341yxl8"
};

// ───── Album controller state (from album-controller-multi.js) ─────
const albums = new Map(); // albumUID -> { photos: [], preloadAt, globalIndex }

// ───── Slideshow state ─────
let currentKey = null;
let photoHashes = [];
let slideIndex = 0;
let timerId = null;
let seqCounter = 0;

// ═════════════════════════════════════════════════════════════════════
// ALBUM CONTROLLER FUNCTIONS (simplified - no manifest needed)
// ═════════════════════════════════════════════════════════════════════

function computeAgeString(photoTakenDate) {
  if (!photoTakenDate) return "";
  
  const birthDate = new Date(DEFAULT_BIRTHDATE);
  const takenDate = new Date(photoTakenDate);
  
  if (Number.isNaN(birthDate.getTime()) || Number.isNaN(takenDate.getTime())) return "";
  
  // Calculate age at time photo was taken
  const diffMs = takenDate - birthDate;
  const diffDays = Math.floor(diffMs / (1000 * 60 * 60 * 24));
  
  // Special case: birth day (25th-26th April)
  if (diffDays === 0 || diffDays === 1) {
    return "Newborn";
  }
  
  // Under 1 week: show days (e.g., "3 days")
  if (diffDays < 7) {
    return diffDays === 1 ? "1 day" : `${diffDays} days`;
  }
  
  // 1 week to 8 weeks: show weeks (e.g., "5 weeks")
  if (diffDays < 56) {
    const weeks = Math.floor(diffDays / 7);
    return weeks === 1 ? "1 week" : `${weeks} weeks`;
  }
  
  // 8 weeks to 24 months: show months (e.g., "3 months", "18 months")
  if (diffDays < 730) { // ~24 months
    const months = Math.floor(diffDays / 30.44);
    return months === 1 ? "1 month" : `${months} months`;
  }
  
  // Over 24 months: show years old (e.g., "2 years old", "3 years old")
  const years = Math.floor(diffDays / 365.25);
  return years === 1 ? "1 year old" : `${years} years old`;
}

function formatDateReadable(isoDateString) {
  if (!isoDateString) return "";
  
  const date = new Date(isoDateString);
  if (Number.isNaN(date.getTime())) return "";
  
  const day = date.getDate();
  const months = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sept', 'Oct', 'Nov', 'Dec'];
  const month = months[date.getMonth()];
  const year = date.getFullYear();
  
  // Add ordinal suffix (st, nd, rd, th)
  let suffix = 'th';
  if (day === 1 || day === 21 || day === 31) suffix = 'st';
  else if (day === 2 || day === 22) suffix = 'nd';
  else if (day === 3 || day === 23) suffix = 'rd';
  
  return `${day}${suffix} ${month} ${year}`;
}

async function loadAlbumPhotos(uid) {
  try {
    const url = `${PHOTOPRISM_API}/api/v1/photos?public=true&count=1000&album=${encodeURIComponent(uid)}`;
    const res = await fetch(url);
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    const json = await res.json();
    const list = Array.isArray(json) ? json : (json.Photos || []);
    const photos = list.map(p => ({
      raw: p,
      Hash: p.Hash || p.UID || p.ID || p.id,
      Taken: p.TakenAt || p.TakenAtLocal || p.Taken || p.CreatedAt || p.Date || p.CreateDate || p.date || p.taken || null
    })).filter(x => x.Hash);
    
    // Sort by date (earliest first) - photos with no date go to end
    photos.sort((a, b) => {
      if (!a.Taken) return 1;
      if (!b.Taken) return -1;
      return new Date(a.Taken) - new Date(b.Taken);
    });
    
    console.log(`📸 [album ${uid}] loaded ${photos.length} photos, sorted by date (earliest first)`);
    return photos;
  } catch (e) {
    console.error(`❌ [album ${uid}] load error:`, e && e.message ? e.message : e);
    return [];
  }
}

async function ensureAlbum(uid) {
  let meta = albums.get(uid);
  const now = Date.now();
  
  if (!meta || (meta.preloadAt && (now - meta.preloadAt > MAX_REFRESH_MS))) {
    if (!meta) {
      meta = { photos: [], preloadAt: 0, globalIndex: 0 };
      albums.set(uid, meta);
    }
    
    meta.photos = await loadAlbumPhotos(uid);
    meta.preloadAt = Date.now();
    console.log(`📸 [album ${uid}] loaded ${meta.photos.length} photos`);
  }
  return meta;
}

function pickForIndex(meta, index) {
  if (!meta || !meta.photos || meta.photos.length === 0) return null;
  const n = meta.photos.length;
  const idx = ((index % n) + n) % n;
  return { pick: meta.photos[idx], idx, photosCount: n };
}

function publishPhotoObject(uid, index) {
  const meta = albums.get(uid);
  if (!meta) return;
  
  const chosen = pickForIndex(meta, index);
  const albumPhotoTopic = `${ALBUM_PHOTO_BASE}/${uid}/photo`;
  
  if (!chosen) {
    const emptyPayload = JSON.stringify({ 
      index, 
      photosCount: 0, 
      date: "", 
      age: "" 
    });
    mqttClient.publish(albumPhotoTopic, emptyPayload, { qos: 0 }, (err) => {
      if (err) console.error(`❌ [album ${uid}] photo publish error`, err);
    });
    return;
  }
  
  const { pick, idx, photosCount } = chosen;
  const taken = pick.Taken || "";
  const age = taken ? computeAgeString(taken) : "";
  const formattedDate = taken ? formatDateReadable(taken) : "";
  const url = pick.Hash ? `${PHOTOPRISM_API}/api/v1/t/${pick.Hash}/public/fit_1920` : "";

  // Minimal photo data for ESP32
  const photoPayload = {
    index: idx,
    photosCount,
    date: formattedDate,
    age
  };

  // Slideshow data for frame display
  const slideshowPayload = {
    type: "image",
    url,
    key: `album-${uid}-${idx}`,
    seq: ++seqCounter,
    ts: Date.now()
  };

  // Publish to album topic (for ESP32) - SMALL payload
  mqttClient.publish(albumPhotoTopic, JSON.stringify(photoPayload), { qos: 0 }, (err) => {
    if (err) console.error(`❌ [album ${uid}] photo topic publish error`, err);
  });

  // Publish to slideshow topic (for frame display)
  mqttClient.publish(SLIDE_TOPIC, JSON.stringify(slideshowPayload), { retain: true }, (err) => {
    if (err) console.error(`❌ [album ${uid}] slideshow publish error`, err);
    else console.log(`🖼️  [album ${uid}] published photo idx ${idx}/${photosCount} to BOTH topics`);
  });
}

// Handle album navigation commands
async function handleAlbumNav(uid, payload) {
  // Stop slideshow immediately when album navigation starts
  if (timerId) {
    clearInterval(timerId);
    timerId = null;
    console.log("🛑 Stopped slideshow for album navigation");
  }
  
  const cmd = (payload.cmd || "").toString();
  const steps = Math.max(0, parseInt(payload.steps || 1, 10));

  const meta = await ensureAlbum(uid);
  let idx = meta.globalIndex ?? 0;

  if (cmd === "next") {
    idx = idx + (steps || 1);
  } else if (cmd === "prev") {
    idx = idx - (steps || 1);
  } else if (cmd === "goto" && Number.isInteger(payload.index)) {
    idx = payload.index;
  } else if (cmd === "get") {
    console.log(`📥 [album ${uid}] received GET command`);
    
    // Ensure photos loaded
    if (!meta.photos || meta.photos.length === 0) {
      meta.photos = await loadAlbumPhotos(uid);
      meta.preloadAt = Date.now();
    }
    
    // Just publish current photo (no manifest needed)
    publishPhotoObject(uid, meta.globalIndex ?? 0);
    return;
  } else {
    console.warn(`⚠️  [album ${uid}] unknown cmd: ${cmd}`);
    return;
  }

  // Save new index
  meta.globalIndex = idx;

  // Lazy load if needed
  if (!meta.photos || meta.photos.length === 0) {
    meta.photos = await loadAlbumPhotos(uid);
    meta.preloadAt = Date.now();
  }

  publishPhotoObject(uid, idx);
}

// ═════════════════════════════════════════════════════════════════════
// SLIDESHOW FUNCTIONS (existing)
// ═════════════════════════════════════════════════════════════════════

function broadcastSlide(hash, key) {
  const url = `${PHOTOPRISM_API}/api/v1/t/${hash}/public/fit_1920`;
  const slide = {
    type: "image",
    url,
    key,
    seq: ++seqCounter,
    ts: Date.now()
  };

  mqttClient.publish(SLIDE_TOPIC, JSON.stringify(slide), { retain: true }, (err) => {
    if (err) console.error("❌ Slideshow publish error:", err);
    else console.log("🖼️  Slideshow slide published:", slide.key);
  });
}

function startSlideshow(intervalMs, key) {
  if (timerId) clearInterval(timerId);
  if (!photoHashes || photoHashes.length === 0) {
    console.warn("⚠️  startSlideshow called with empty photoHashes");
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

function refreshAllMax() {
  for (const [topic, albumUID] of Object.entries(albumMap)) {
    refreshMaxForTopic(topic, albumUID);
  }
}

// ═════════════════════════════════════════════════════════════════════
// MQTT SETUP
// ═════════════════════════════════════════════════════════════════════

const mqttClient = mqtt.connect(MQTT_URL, { reconnectPeriod: 2000 });

mqttClient.on("connect", async () => {
  console.log("📡 MQTT connected");
  
  // Subscribe to handler topics (existing interactions)
  mqttClient.subscribe(Object.keys(handlers), err => {
    if (err) console.error("❌ Subscribe error:", err);
    else console.log("✅ Subscribed to handler topics");
  });

  // Subscribe to album navigation wildcard
  mqttClient.subscribe(NAV_WILDCARD, err => {
    if (err) console.error("❌ Subscribe nav error:", err);
    else console.log("✅ Subscribed to album nav topics");
  });

  // Subscribe to max topics
  mqttClient.subscribe(Object.keys(albumMap).map(t => `${t}/max`), err => {
    if (err) console.error("❌ Subscribe max error:", err);
    else console.log("✅ Subscribed to album max topics");
  });

  // Preload all known albums so photos are ready
  console.log("🔄 Preloading known albums...");
  for (const uid of KNOWN_ALBUMS) {
    await ensureAlbum(uid);
  }
  console.log("✅ All albums preloaded");

  // Immediate refresh + periodic for max topics
  refreshAllMax();
  setInterval(refreshAllMax, MAX_REFRESH_MS);
  
  console.log("⏰ Albums will refresh every 30 minutes");
});

mqttClient.on("reconnect", () => console.log("📡 MQTT reconnecting..."));
mqttClient.on("error", (err) => console.error("❌ MQTT error:", err));
mqttClient.on("close", () => console.warn("⚠️  MQTT closed"));

// ═════════════════════════════════════════════════════════════════════
// INCOMING MESSAGES
// ═════════════════════════════════════════════════════════════════════

mqttClient.on("message", async (topic, buf) => {
  const msg = buf.toString().trim();

  // ─── Handle album navigation ───
  if (topic.match(/^spinner\/album\/([^\/]+)\/nav$/)) {
    const match = topic.match(/^spinner\/album\/([^\/]+)\/nav$/);
    const uid = match[1];
    
    let payload;
    try {
      payload = JSON.parse(msg);
    } catch (e) {
      console.warn("⚠️  Bad nav JSON:", msg);
      return;
    }
    
    await handleAlbumNav(uid, payload);
    return;
  }

  // ─── Ignore max topics (informational) ───
  if (topic.endsWith("/max")) {
    console.log(`📏 MQTT [${topic}]: ${msg}`);
    return;
  }

  // ─── Handle existing slideshow interactions ───
  console.log(`📥 MQTT [${topic}]:`, msg);
  const handler = handlers[topic];
  if (!handler) {
    return console.warn("⚠️  No handler for topic:", topic);
  }

  let payload;
  try {
    payload = topic.endsWith("/count") ? parseInt(msg, 10) : JSON.parse(msg);
  } catch (e) {
    return console.error("❌ Payload parse error:", e);
  }

  // Deduplicate
  const key = `${topic}:${msg}`;
  if (key === currentKey) {
    console.log("↩️  Duplicate, ignoring");
    return;
  }
  currentKey = key;

  if (timerId) {
    clearInterval(timerId);
    timerId = null;
  }

  let result;
  try {
    result = await handler(payload, { photoprism: PHOTOPRISM_API });
  } catch (e) {
    return console.error(`❌ Handler error [${topic}]:`, e);
  }

  if (!result || !Array.isArray(result.hashes) || result.hashes.length === 0) {
    return console.warn(`⚠️  No photos returned for ${topic}`, payload);
  }

  photoHashes = result.hashes;

  if (result.intervalMs === 0 && topic.endsWith("/count")) {
    const idx = Math.min(Math.max(0, payload), photoHashes.length - 1);
    broadcastSlide(photoHashes[idx], key);
  } else {
    startSlideshow(result.intervalMs, key);
  }
});

console.log("🚀 spinner-server running (slideshow + album controller integrated)");
process.stdin.resume();