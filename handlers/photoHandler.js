// handlers/photoHandler.js

const fetch = require("node-fetch");

// Photo handler for absolute counter: returns ALL photo hashes in album
// and logs the number of photos fetched for debugging
module.exports = async function photoHandler(_count, { photoprism }) {
  console.log("▶️ [photoHandler] loading full album");

  // === CONFIG: put your real album UID here ===
  const albumUID = "asvl97q34341yxl8";  // replace with your album UID
  const pageSize = 1000;

  // Fetch photos via album filter
  const url = `${photoprism}/api/v1/photos?album=${encodeURIComponent(albumUID)}` +
              `&public=true&count=${pageSize}`;
  console.log("📡 [photoHandler] GET URL:", url);

  let data;
  try {
    const res = await fetch(url);
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    data = await res.json();
  } catch (err) {
    console.error("❌ [photoHandler] fetch error:", err);
    return { hashes: [], intervalMs: 0 };
  }

  if (!Array.isArray(data)) {
    console.error("❌ [photoHandler] unexpected response, not array:", data);
    return { hashes: [], intervalMs: 0 };
  }

  console.log(`📚 [photoHandler] fetched ${data.length} items from album`);

  // Sort by timestamp ascending
  data.sort((a, b) => {
    const da = new Date(a.TakenAt || a.TakenAtLocal || 0);
    const db = new Date(b.TakenAt || b.TakenAtLocal || 0);
    return da - db;
  });

  // Extract stable IDs
  const hashes = data
    .map(p => p.Hash || p.UID || p.ID)
    .filter(Boolean);

  console.log(`✅ [photoHandler] prepared ${hashes.length} hashes for indexing`);

  // Return the full list—spinner-server will pick by index
  return { hashes, intervalMs: 0 };
};
