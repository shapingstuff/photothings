// handlers/friendHandler.js

const fetch = require("node-fetch");

// 5 seconds per slide
const SLIDE_INTERVAL_MS = 5000;

module.exports = async function friendHandler(payload, { photoprism }) {
  const { name } = payload;
  console.log("▶️ [friendHandler] got name:", name);
  if (!name) return null;

  // Build PhotoPrism URL to fetch photos tagged with this "friend"
  // Adjust the query param to match your library's tagging (e.g. &person= or &tag=)
  const url = `${photoprism}/api/v1/photos?public=true&count=500&person=${encodeURIComponent(name)}`;

  let json;
  try {
    const res = await fetch(url);
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    json = await res.json();
  } catch (err) {
    console.error("❌ [friendHandler] fetch error:", err);
    return null;
  }

  const list = Array.isArray(json) ? json : (json.Photos || []);
  if (!list.length) {
    console.warn(`⚠️ [friendHandler] no photos found for ${name}`);
  }

  // Extract and shuffle hashes
  const hashes = list
    .map(p => p.Hash || p.UID || p.ID)
    .filter(Boolean);

  for (let i = hashes.length - 1; i > 0; i--) {
    const j = Math.floor(Math.random() * (i + 1));
    [hashes[i], hashes[j]] = [hashes[j], hashes[i]];
  }

  // Return the data needed by spinner-server.js:
  return {
    hashes,
    intervalMs: SLIDE_INTERVAL_MS
  };
};