// handlers/afamilyHandler.js
// Name -> album-UID mapping (edit these to your album UIDs)
const NAME_TO_ALBUM_UID = {
  "Maddie": "at2u9s6nirbtumo6",
  "Bob":   "uid-for-bob-album-here",
  "Eve":   "uid-for-eve-album-here"
  // add more: "Name": "AlbumUID",
};

// slide interval in ms
const SLIDE_INTERVAL_MS = 5000;

// Handler: expects payload { name: "<Name>" }
module.exports = async function afamilyHandler(payload, { photoprism } = {}) {
  if (!payload || !payload.name) {
    console.warn('[afamilyHandler] missing payload.name');
    return null;
  }

  const name = String(payload.name).trim();
  console.log("▶️ [afamilyHandler] got name:", name);

  // lookup UID (exact match). If you want case-insensitive, see note below.
  const albumUID = Object.prototype.hasOwnProperty.call(NAME_TO_ALBUM_UID, name)
    ? NAME_TO_ALBUM_UID[name]
    : null;

  // build URL: prefer album UID, otherwise fall back to person query
  const url = albumUID
    ? `${photoprism}/api/v1/photos?public=true&count=1000&album=${encodeURIComponent(albumUID)}`
    : `${photoprism}/api/v1/photos?public=true&count=500&person=${encodeURIComponent(name)}`;

  try {
    const res = await fetch(url);
    if (!res.ok) throw new Error(`HTTP ${res.status} ${res.statusText}`);
    const json = await res.json();
    const list = Array.isArray(json) ? json : (json.Photos || []);

    if (!list || list.length === 0) {
      console.warn(`⚠️ [afamilyHandler] no photos found for ${name} (albumUID=${albumUID || 'N/A'})`);
      return null;
    }

    const hashes = list
      .map(p => p.Hash || p.UID || p.ID || p.Id || p.id)
      .filter(Boolean);

    if (!hashes.length) {
      console.warn(`⚠️ [afamilyHandler] no usable hash fields for ${name}`);
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
    console.error("❌ [afamilyHandler] fetch error:", err);
    return null;
  }
};