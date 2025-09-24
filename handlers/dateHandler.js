// handlers/dateHandler.js

const fetch = require("node-fetch");

// For date wheel, we want a 5 s slideshow
const SLIDE_INTERVAL_MS = 5000;

module.exports = async function dateHandler(payload, { photoprism }) {
  const { month, year } = payload;
  console.log("▶️ [dateHandler]", payload);

  const url = `${photoprism}/api/v1/photos?public=true&count=500`
            + `&year=${year}&month=${month}`;

  let json;
  try {
    const res = await fetch(url);
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    json = await res.json();
  } catch (err) {
    console.error("❌ [dateHandler] fetch error:", err);
    return null;
  }

  const list = Array.isArray(json) ? json : (json.Photos || []);
  const hashes = list.map(p => p.Hash || p.UID || p.ID).filter(Boolean);

  return {
    hashes,
    intervalMs: SLIDE_INTERVAL_MS
  };
};