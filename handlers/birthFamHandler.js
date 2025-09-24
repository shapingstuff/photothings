// handlers/birthFamHandler.js
// Name -> album-UID mapping (edit these to your album UIDs)
const NAME_TO_ALBUM_UID = {
  "Peter": "at2u5p5lqxdwceoi",
  "Gillian": "at2u5pi5g7s6q7r5",
  "Mia": "at2u4b9k8wxixf3u",
  "Joey": "at2u5npkb5vd3cn5",
  "Cian": "uat2u5o64ty38d3ou",
  "Shannon": "at2u5orh77y2hn0i"
};

// 5 seconds per slide (same as friendHandler)
const SLIDE_INTERVAL_MS = 5000;

// NOTE: requires a working `fetch(...)` in the runtime.
module.exports = async function birthFamHandler(payload, { photoprism } = {}) {
  if (!payload || !payload.name) {
    console.warn('[birthFamHandler] missing payload.name');
    return null;
  }

  const name = String(payload.name).trim();
  console.log("▶️ [birthFamHandler] got name:", name);

  // Determine whether we have an explicit album UID for this name
  const albumUID = NAME_TO_ALBUM_UID.hasOwnProperty(name) ? NAME_TO_ALBUM_UID[name] : null;

  // Build URL depending on whether we have an album UID or fall back to person query
  const url = albumUID
    ? `${photoprism}/api/v1/photos?public=true&count=1000&album=${encodeURIComponent(albumUID)}`
    : `${photoprism}/api/v1/photos?public=true&count=500&person=${encodeURIComponent(name)}`;

  try {
    const res = await fetch(url);
    if (!res.ok) {
      throw new Error(`HTTP ${res.status} ${res.statusText}`);
    }
    const json = await res.json();
    const list = Array.isArray(json) ? json : (json.Photos || []);

    if (!list || list.length === 0) {
      console.warn(`⚠️ [birthFamHandler] no photos found for ${name} (albumUID=${albumUID || 'N/A'})`);
      return null;
    }

    // Extract photo hashes (adapt if your PhotoPrism returns a different field)
    const hashes = list
      .map(p => p.Hash || p.UID || p.ID || p.Id || p.id)
      .filter(Boolean);

    if (!hashes.length) {
      console.warn(`⚠️ [birthFamHandler] no usable hash fields for ${name}`);
      return null;
    }

    // Shuffle (Fisher–Yates)
    for (let i = hashes.length - 1; i > 0; i--) {
      const j = Math.floor(Math.random() * (i + 1));
      [hashes[i], hashes[j]] = [hashes[j], hashes[i]];
    }

    return {
      hashes,
      intervalMs: SLIDE_INTERVAL_MS
    };
  } catch (err) {
    console.error("❌ [birthFamHandler] fetch error:", err);
    return null;
  }
};