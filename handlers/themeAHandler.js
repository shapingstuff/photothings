// handlers/themeAHandler.js
// Theme -> album-UID mapping (edit to your album UIDs)
const THEME_TO_ALBUM_UID = {
  "play":   "at33k9cuihfxko38",
  "learn":  "at33k6h7gfufykw8",
  "sleep":  "at33k72z3nzzacq5",
  "read":   "at33k7hz5gxc7z12",
  "run":    "at33k7wyii8a7lwo",
  "ride":   "at33k87nt1l1z0eb",
  "create": "at33k8hodv36h5d1",
  "party":  "at33k8tiytwymm28",
  "eat":    "at33k929z5i6j9o2"
  // add more: "themeLower": "AlbumUID"
};

// slide interval in ms (same as others)
const SLIDE_INTERVAL_MS = 5000;

// helper: create a lowercase lookup map for case-insensitive matching
const lowerMap = Object.fromEntries(
  Object.entries(THEME_TO_ALBUM_UID).map(([k, v]) => [k.toLowerCase(), v])
);

// Handler: expects payload { name: "<Theme>" } (same as friendHandler)
module.exports = async function themeAHandler(payload, { photoprism } = {}) {
  if (!payload) {
    console.warn('[themeAHandler] missing payload');
    return null;
  }

  // Accept either { name: "Play" } or a raw string in payload (robust)
  let name = null;
  if (typeof payload === 'string') {
    name = payload.trim();
  } else if (payload.name) {
    name = String(payload.name).trim();
  } else {
    console.warn('[themeAHandler] payload has no name field');
    return null;
  }

  if (!name) {
    console.warn('[themeAHandler] empty name');
    return null;
  }

  console.log('▶️ [themeAHandler] got name:', name);

  // case-insensitive lookup
  const albumUID = lowerMap[name.toLowerCase()] || null;

  // build URL: prefer album UID, otherwise fall back to person=<name>
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
      console.warn(`⚠️ [themeAHandler] no photos found for ${name} (albumUID=${albumUID || 'N/A'})`);
      return null;
    }

    // Extract photo hashes (adapt if PhotoPrism returns a different field)
    const hashes = list
      .map(p => p.Hash || p.UID || p.ID || p.Id || p.id)
      .filter(Boolean);

    if (!hashes.length) {
      console.warn(`⚠️ [themeAHandler] no usable hash fields for ${name}`);
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
    console.error("❌ [themeAHandler] fetch error:", err);
    return null;
  }
};