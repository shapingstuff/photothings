// handlers/distanceHandler.js
// Map of distance (km) -> place name (edit if you change the distances)
const DISTANCE_TO_PLACE = [
  { d: 0,   name: "Ovington" },
  { d: 5,   name: "Ovingham" },
  { d: 8,   name: "Throckley" },
  { d: 20,  name: "North Shields" },
  { d: 130, name: "Dalgety Bay" },
  { d: 135, name: "North Queensferry" },
  { d: 160, name: "Glasgow" },
  { d: 182, name: "Dunoon" }
];

// Name -> album-UID mapping (edit these to the real album UIDs)
const NAME_TO_ALBUM_UID = {
  "Ovington":           "at2ubbfig4n6qq44",
  "Ovingham":           "at2ubbxkr73hu4zy",
  "Throckley":          "at2ubca6t87v8zgl",
  "North Shields":      "at2ubco476nxf5cs",
  "Dalgety Bay":        "at2ubdgnbyg94c5x",
  "North Queensferry":  "at2ubdtwct1arqh8",
  "Glasgow":            "at2ubedggsfb4alv",
  "Dunoon":             "at2ubepju1gljscs"
  // add or edit as required
};

// seconds per slide (keep same as others)
const SLIDE_INTERVAL_MS = 5000;

// How far away (km) we'll accept a "nearest" match if an exact distance isn't found.
// Set to 0 for strict exact match only.
const TOLERANCE_KM = 3;

function findPlaceByDistance(distKm) {
  // exact match first
  for (const e of DISTANCE_TO_PLACE) {
    if (e.d === distKm) return e.name;
  }
  // nearest within tolerance
  let best = null;
  let bestDiff = Infinity;
  for (const e of DISTANCE_TO_PLACE) {
    const diff = Math.abs(e.d - distKm);
    if (diff < bestDiff) {
      bestDiff = diff;
      best = e;
    }
  }
  if (best && bestDiff <= TOLERANCE_KM) return best.name;
  return null;
}

// NOTE: this expects `fetch(...)` to be available (via global fetch or the fetch shim)
// The spinner-server bootstrap we gave earlier should ensure fetch exists.
module.exports = async function distanceHandler(payload, { photoprism } = {}) {
  if (payload === undefined || payload === null) {
    console.warn('[distanceHandler] empty payload');
    return null;
  }

  // Accept number payload (from spinner/distance/count) or object { distance: N } or { name: "Place" }
  let distance = null;
  let name = null;

  if (typeof payload === 'number') {
    distance = payload;
  } else if (typeof payload === 'string' && payload.trim() !== '') {
    // try parse numeric string
    const maybeNum = Number(payload);
    if (!Number.isNaN(maybeNum)) distance = maybeNum;
    else name = payload.trim();
  } else if (typeof payload === 'object') {
    if (payload.hasOwnProperty('distance') && typeof payload.distance === 'number') {
      distance = payload.distance;
    } else if (payload.hasOwnProperty('name') && payload.name) {
      name = String(payload.name).trim();
    }
  }

  if (distance !== null && name === null) {
    name = findPlaceByDistance(distance);
    console.log(`[distanceHandler] distance=${distance} -> place=${name || 'NONE'}`);
  } else {
    console.log(`[distanceHandler] got name=${name}, distance=${distance !== null ? distance : 'N/A'}`);
  }

  if (!name) {
    console.warn('[distanceHandler] no place mapping for payload; ignoring');
    return null;
  }

  // Prefer album UID mapping; if missing, fall back to person=<name> query
  const albumUID = NAME_TO_ALBUM_UID.hasOwnProperty(name) ? NAME_TO_ALBUM_UID[name] : null;

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
      console.warn(`⚠️ [distanceHandler] no photos found for ${name} (albumUID=${albumUID || 'N/A'})`);
      return null;
    }

    const hashes = list
      .map(p => p.Hash || p.UID || p.ID || p.Id || p.id)
      .filter(Boolean);

    if (!hashes.length) {
      console.warn(`⚠️ [distanceHandler] no usable hash fields for ${name}`);
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
    console.error("❌ [distanceHandler] fetch error:", err);
    return null;
  }
};