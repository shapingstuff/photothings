// handlers/peopleHandler.js

const fetch = require("node-fetch");

// For people wheel, also a 5 s slideshow
const SLIDE_INTERVAL_MS = 5000;

module.exports = async function peopleHandler(payload, { photoprism }) {
  const { name } = payload;
  console.log("▶️ [peopleHandler] name=", name);

  if (!name) return null;

  const url = `${photoprism}/api/v1/photos?public=true&count=500`
            + `&person=${encodeURIComponent(name)}`;

  let json;
  try {
    const res = await fetch(url);
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    json = await res.json();
  } catch (err) {
    console.error("❌ [peopleHandler] fetch error:", err);
    return null;
  }

  const list = Array.isArray(json) ? json : (json.Photos || []);
  const hashes = list.map(p => p.Hash || p.UID || p.ID).filter(Boolean);

  return {
    hashes,
    intervalMs: SLIDE_INTERVAL_MS
  };
};