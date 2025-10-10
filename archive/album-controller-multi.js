// album-controller-multi.js (simplified GET + publish directly to slideshow)
// Multi-album MQTT controller for spinner wheels
// Responds to GET with fresh publish after delay (no retain) and forwards slide to spinner/slideshow
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
const SLIDE_TOPIC  = "spinner/slideshow"; // publish slides here directly

// behaviour flags
const PER_DEVICE = true;
const PRELOAD    = true;
const ALBUM_TTL_MS = 30 * 60 * 1000;
const GET_RESPONSE_DELAY_MS = 2000; // 2s delay before responding to GET (give device time to subscribe)

// internal state
const albums = new Map(); // albumUID -> { photos:[], preloadAt, globalIndex, deviceIndex: Map }
let seqCounter = 0;

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
function topicForPhoto(uid, device) {
  if (PER_DEVICE && device) return `${PHOTO_BASE}/${uid}/photo/${device}`;
  return `${PHOTO_BASE}/${uid}/photo`;
}
function topicForManifest(uid, device) {
  if (PER_DEVICE && device) return `${MANIFEST_BASE}/${uid}/manifest/${device}`;
  return `${MANIFEST_BASE}/${uid}/manifest`;
}

// normalize url/hash -> full photoprism URL
function photoUrlFromHashOrMaybeUrl(maybe) {
  if (!maybe) return "";
  if (typeof maybe !== "string") return "";
  if (maybe.startsWith("http://") || maybe.startsWith("https://")) return maybe;
  // assume hash
  return `${PHOTOPRISM}/api/v1/t/${maybe}/public/fit_1920`;
}

// publish a slide payload to spinner/slideshow (retained)
function broadcastSlide(maybeUrlOrHash, uid, idx, device = null, date = "", age = "") {
  const url = photoUrlFromHashOrMaybeUrl(maybeUrlOrHash);
  if (!url) return;
  const keyParts = [`album:${uid}`];
  if (typeof idx !== "undefined" && idx !== null) keyParts.push(`idx:${idx}`);
  if (device) keyParts.push(`dev:${device}`);
  const key = keyParts.join(":");
  const slide = {
    type: "image",
    url,
    key,
    seq: ++seqCounter,
    ts: Date.now(),
    album: uid,
    index: idx,
    device: device || null,
    date: date || "",
    age: age || ""
  };
  const payload = JSON.stringify(slide);
  client.publish(SLIDE_TOPIC, payload, { retain: true }, (err) => {
    if (err) console.error(`[album ${uid}] slideshow publish error`, err);
    else console.log(`[album ${uid}] broadcast to slideshow -> ${SLIDE_TOPIC} (idx ${idx})`);
  });
}

// Publishing -----------------------------------------------------------------
function publishPhotoObject(uid, index, device = null) {
  const meta = albums.get(uid);
  if (!meta) return;
  const chosen = pickForIndex(meta, index);
  const topic = topicForPhoto(uid, device);
  if (!chosen) {
    client.publish(topic, JSON.stringify({ index, photosCount: 0, url: "", date: "", age: "" }), { retain: true, qos: 1 }, (err) => {
      if (err) console.error(`[album ${uid}] publish photo error`, err);
      else console.warn(`[album ${uid}] published empty photo -> ${topic}`);
    });
    return;
  }
  const { pick, idx, photosCount } = chosen;
  const taken = pick.Taken || "";
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
    else {
      console.log(`[album ${uid}] published photo -> ${topic} (idx ${idx}/${photosCount})`);
      // FORWARD to slideshow immediately so slideshow sees the image without needing spinner-server
      broadcastSlide(url || pick.Hash, uid, idx, device, taken, age);
    }
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

    const device = payload.device || null;
    const cmd = (payload.cmd || "").toString();
    const steps = Math.max(0, parseInt(payload.steps || 1, 10));

    const meta = await ensureAlbum(uid);

    let idx = (PER_DEVICE && device) ? (meta.deviceIndex.get(device) ?? (meta.globalIndex ?? 0)) : (meta.globalIndex ?? 0);

    if (cmd === "next") idx = idx + (steps || 1);
    else if (cmd === "prev") idx = idx - (steps || 1);
    else if (cmd === "goto" && Number.isInteger(payload.index)) idx = payload.index;
    else if (cmd === "get") {
      console.log(`[album ${uid}] received GET (simplified - responding to simple manifest + slideshow)`);

      // wait so device has time to subscribe to manifest/photo topics
      console.log(`[album ${uid}] waiting ${GET_RESPONSE_DELAY_MS}ms before responding to GET...`);
      await new Promise(resolve => setTimeout(resolve, GET_RESPONSE_DELAY_MS));

      // ensure photos loaded
      if (!meta.photos || meta.photos.length === 0) {
        meta.photos = await loadAlbumPhotos(uid);
        meta.preloadAt = Date.now();
      }

      // Build manifest
      const entries = meta.photos.map((p, i) => {
        const taken = p.Taken || "";
        return { index: i, hash: p.Hash, date: taken, age: taken ? computeAgeString(taken) : "" };
      });
      const manifest = { length: entries.length, entries };
      const manifestPayload = JSON.stringify(manifest);

      // Publish ONLY to simple global manifest topic (no device ID)
      const simpleTopic = `spinner/album/${uid}/manifest`;
      console.log(`[album ${uid}] publishing manifest to: ${simpleTopic}`);
      client.publish(simpleTopic, manifestPayload, { qos: 1 }, (err) => {
        if (err) {
          console.error(`[album ${uid}] ❌ ERROR publishing manifest:`, err);
        } else {
          console.log(`[album ${uid}] ✅ SUCCESS published manifest (${entries.length} entries)`);
        }
      });

      // publish current photo (no device)
      publishPhotoObject(uid, meta.globalIndex ?? 0, null);
      return;
    } else {
      console.warn("Unknown cmd:", cmd);
    }

    // save back new index
    if (PER_DEVICE && device) meta.deviceIndex.set(device, idx);
    else meta.globalIndex = idx;

    // lazy load if needed
    if (!meta.photos || meta.photos.length === 0) {
      meta.photos = await loadAlbumPhotos(uid);
      meta.preloadAt = Date.now();
    }

    publishPhotoObject(uid, idx, device || null);

  } catch (e) {
    console.error("message handler error:", e && e.stack ? e.stack : e);
  }
});