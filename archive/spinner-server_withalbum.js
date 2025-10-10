// album-controller-multi.js (simplified GET response)
// Multi-album MQTT controller for spinner wheels
// Responds to GET with fresh publish after delay (no retain)
//
// Usage: NODE_ENV=production node album-controller-multi.js

const mqtt = require("mqtt");

// Node 18+ has global fetch; otherwise try node-fetch
let fetchFn = global.fetch;
if (!fetchFn) {
  try {
    fetchFn = (...args) => require("node-fetch")(...args);
  } catch (e) {
    console.error("No global fetch and node-fetch not installed. Install node-fetch or run Node 18+.");
    process.exit(1);
  }
}

const PHOTOPRISM = process.env.PHOTOPRISM || "http://192.168.68.81:2342";
const MQTT_URL    = process.env.MQTT_URL   || "mqtt://localhost:1883";
const NAV_WILDCARD = "spinner/album/+/nav";
const PHOTO_BASE   = "spinner/album";
const MANIFEST_BASE = "spinner/album";

// behaviour flags
const PER_DEVICE = false;   // simplified: do NOT use per-device topics for now
const PRELOAD    = true;
const ALBUM_TTL_MS = 30 * 60 * 1000;
const GET_RESPONSE_DELAY_MS = 2000; // 2 second delay before responding to GET (gives ESP time to subscribe)

// internal state
const albums = new Map();

// MQTT client
const client = mqtt.connect(MQTT_URL, { reconnectPeriod: 2000 });

// Helpers --------------------------------------------------------------------
function computeAgeString(taken) {
  if (!taken) return "";
  const d = new Date(taken);
  if (Number.isNaN(d.getTime())) return "";
  const now = new Date();
  const diffDays = Math.floor((now - d) / (1000*60*60*24));
  if (diffDays < 14) return `${diffDays}d`;
  if (diffDays < 60) return `${Math.round(diffDays / 7)}w`;
  if (diffDays < 365) {
    const months = Math.floor(diffDays / 30);
    return `${months}m`;
  }
  const years = Math.floor(diffDays / 365);
  const months = Math.floor((diffDays % 365) / 30);
  return months ? `${years}y${months}m` : `${years}y`;
}

async function loadAlbumPhotos(uid) {
  try {
    const url = `${PHOTOPRISM}/api/v1/photos?public=true&count=1000&album=${encodeURIComponent(uid)}`;
    const res = await fetchFn(url);
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    const json = await res.json();
    const list = Array.isArray(json) ? json : (json.Photos || []);
    return list.map(p => ({
      raw: p,
      Hash: p.Hash || p.UID || p.ID || p.id,
      Taken: p.Taken || p.Date || p.CreateDate || p.date || p.taken || null
    })).filter(x => x.Hash);
  } catch (e) {
    console.error(`[album ${uid}] load error:`, e && e.message ? e.message : e);
    return [];
  }
}

async function ensureAlbum(uid) {
  let meta = albums.get(uid);
  const now = Date.now();
  if (!meta || (meta.preloadAt && (now - meta.preloadAt > ALBUM_TTL_MS))) {
    if (!meta) {
      meta = { photos: [], preloadAt: 0, globalIndex: 0, deviceIndex: new Map() };
      albums.set(uid, meta);
    }
    if (PRELOAD) {
      meta.photos = await loadAlbumPhotos(uid);
      meta.preloadAt = Date.now();
      console.log(`[album ${uid}] loaded ${meta.photos.length} photos`);
    } else {
      meta.photos = [];
      meta.preloadAt = Date.now();
    }
  }
  return meta;
}

function pickForIndex(meta, index) {
  if (!meta || !meta.photos || meta.photos.length === 0) return null;
  const n = meta.photos.length;
  const idx = ((index % n) + n) % n;
  return { pick: meta.photos[idx], idx, photosCount: n };
}

// topic helpers
function topicForPhoto(uid) {
  return `${PHOTO_BASE}/${uid}/photo`;
}
function topicForManifest(uid) {
  return `${MANIFEST_BASE}/${uid}/manifest`;
}

// Publishing -----------------------------------------------------------------
function publishPhotoObject(uid, index) {
  const meta = albums.get(uid);
  if (!meta) return;
  const chosen = pickForIndex(meta, index);
  const topic = topicForPhoto(uid);
  if (!chosen) {
    client.publish(topic, JSON.stringify({ index, photosCount: 0, url: "", date: "", age: "" }), { retain: true }, (err) => {
      if (err) console.error(`[album ${uid}] publish photo error`, err);
      else console.warn(`[album ${uid}] published empty photo -> ${topic}`);
    });
    return;
  }
  const { pick, idx, photosCount } = chosen;
  // ensure we have a date: fallback to 2019 if Photoprism omitted
  const taken = pick.Taken || "2019-01-01T00:00:00Z";
  const age_days = taken ? Math.floor((Date.now() - new Date(taken).getTime())/(1000*60*60*24)) : null;
  const age = taken ? computeAgeString(taken) : "";
  const url = pick.Hash ? `${PHOTOPRISM}/api/v1/t/${pick.Hash}/public/fit_1920` : "";

  const payload = {
    index: idx,
    hash: pick.Hash,
    date: taken,
    age_days,
    age,
    url,
    photosCount
  };

  client.publish(topic, JSON.stringify(payload), { retain: true, qos: 1 }, (err) => {
    if (err) console.error(`[album ${uid}] publish photo error`, err);
    else console.log(`[album ${uid}] published photo -> ${topic} (idx ${idx}/${photosCount})`);
  });
}

// MQTT -----------------------------------------------------------------------
client.on("connect", () => {
  console.log("MQTT connected to", MQTT_URL);
  client.subscribe(NAV_WILDCARD, (err) => {
    if (err) console.error("subscribe error", err);
    else console.log("Subscribed to", NAV_WILDCARD);
  });
});

client.on("error", (err) => console.error("MQTT err", err));

client.on("message", async (topic, buf) => {
  try {
    const m = topic.match(/^spinner\/album\/([^\/]+)\/nav$/);
    if (!m) return;
    const uid = m[1];
    let payload;
    try { payload = JSON.parse(buf.toString()); } catch (e) { 
      console.warn("Bad nav JSON:", buf.toString());
      return;
    }

    const device = null; // simplified: ignore per-device for now
    const cmd = (payload.cmd || "").toString();
    const steps = Math.max(0, parseInt(payload.steps || 1, 10));

    const meta = await ensureAlbum(uid);

    let idx = meta.globalIndex ?? 0;

    if (cmd === "next") idx = idx + (steps || 1);
    else if (cmd === "prev") idx = idx - (steps || 1);
    else if (cmd === "goto" && Number.isInteger(payload.index)) idx = payload.index;
    else if (cmd === "get") {
      console.log(`[album ${uid}] received GET (simplified - global manifest)`);

      // Wait a short time so ESP32 has time to register subscriptions
      console.log(`[album ${uid}] waiting ${GET_RESPONSE_DELAY_MS}ms before responding to GET...`);
      await new Promise(resolve => setTimeout(resolve, GET_RESPONSE_DELAY_MS));

      // ensure photos loaded
      if (!meta.photos || meta.photos.length === 0) {
        meta.photos = await loadAlbumPhotos(uid);
        meta.preloadAt = Date.now();
      }

      // Build manifest: ensure date fallback to 2019 if missing
      const entries = meta.photos.map((p, i) => {
        const taken = p.Taken || "2019-01-01T00:00:00Z";
        return { index: i, hash: p.Hash, date: taken, age: taken ? computeAgeString(taken) : "" };
      });
      const manifest = { length: entries.length, entries };
      const manifestPayload = JSON.stringify(manifest);

      // Publish to simple global topic (spinner/album/<uid>/manifest)
      const simpleTopic = `spinner/album/${uid}/manifest`;
      console.log(`[album ${uid}] publishing manifest -> ${simpleTopic} (len=${entries.length})`);
      client.publish(simpleTopic, manifestPayload, { qos: 1 }, (err) => {
        if (err) console.error(`[album ${uid}] manifest publish error`, err);
        else console.log(`[album ${uid}] manifest published`);
      });

      // also publish current photo (global)
      publishPhotoObject(uid, meta.globalIndex ?? 0);
      return;
    } else {
      console.warn("Unknown cmd:", cmd);
    }

    // save back new index (global)
    meta.globalIndex = idx;

    // lazy load if needed
    if (!meta.photos || meta.photos.length === 0) {
      meta.photos = await loadAlbumPhotos(uid);
      meta.preloadAt = Date.now();
    }

    publishPhotoObject(uid, idx);

  } catch (e) {
    console.error("message handler error:", e && e.stack ? e.stack : e);
  }
});