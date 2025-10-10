// handlers/friendHandler.js
// Name -> album-UID mapping (edit these to your album UIDs)
const NAME_TO_ALBUM_UID = {
  "Bronn": "at2u39jekkjepob1",
  "School": "at2u39t6peve5k3f",
  "Seth": "at2u3816zsbnekek",
  "Bo": "at2u398e63qfpjjb",
  "Esta": "at2u37og15surzdv",
  "Asha": "at2u32z1a54xvnz2"
  // add more: "Name": "AlbumUID",
};

// 5 seconds per slide
const SLIDE_INTERVAL_MS = 5000;

// NOTE: this file expects a working `fetch(...)` in the runtime. If your Node version
// does not expose global fetch, ensure spinner-server bootstrap provides it (or install node-fetch).
module.exports = async function friendHandler(payload, { photoprism } = {}) {
  if (!payload || !payload.name) {
    console.warn('[friendHandler] missing payload.name');
    return null;
  }

  const name = String(payload.name).trim();
  console.log("▶️ [friendHandler] got name:", name);

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
      console.warn(`⚠️ [friendHandler] no photos found for ${name} (albumUID=${albumUID || 'N/A'})`);
      return null;
    }

    // Extract photo hashes (adapt if your PhotoPrism returns a different field)
    const hashes = list
      .map(p => p.Hash || p.UID || p.ID || p.Id || p.id)
      .filter(Boolean);

    if (!hashes.length) {
      console.warn(`⚠️ [friendHandler] no usable hash fields for ${name}`);
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
    console.error("❌ [friendHandler] fetch error:", err);
    return null;
  }
};