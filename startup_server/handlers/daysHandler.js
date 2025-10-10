// handlers/daysHandler.js
// Handles payloads like:
//   { days_ago: 0, date: "2025-09-19", photoprism_q: 'taken:"2025-09-19"' }

const SLIDE_INTERVAL_MS = 5000;
const MAX_COUNT = 1000; // how many photos to request (adjust as needed)

// helper: get YYYY-MM-DD from Date
function ymdFromDate(d) {
  const yyyy = d.getFullYear();
  const mm = String(d.getMonth() + 1).padStart(2, '0');
  const dd = String(d.getDate()).padStart(2, '0');
  return `${yyyy}-${mm}-${dd}`;
}

// compute date string from days_ago (0 = today)
function dateFromDaysAgo(daysAgo) {
  const now = new Date();
  // create date at local midnight then subtract days
  const localMidnight = new Date(now.getFullYear(), now.getMonth(), now.getDate());
  localMidnight.setDate(localMidnight.getDate() - (Number(daysAgo) || 0));
  return ymdFromDate(localMidnight);
}

module.exports = async function daysHandler(payload, { photoprism } = {}) {
  if (!payload) {
    console.warn('[daysHandler] empty payload');
    return null;
  }

  // prefer explicit photoprism_q if provided
  let q = null;

  if (payload.photoprism_q) {
    q = String(payload.photoprism_q).trim();
  } else if (payload.date) {
    // assume payload.date is "YYYY-MM-DD"
    q = `taken:${String(payload.date).trim()}`;
  } else if (payload.days_ago !== undefined && payload.days_ago !== null) {
    const dstr = dateFromDaysAgo(payload.days_ago);
    q = `taken:${dstr}`;
  } else {
    // last fallback: try to accept payload directly as a date-like string
    if (typeof payload === 'string' && payload.trim().length >= 8) {
      q = `taken:${payload.trim()}`;
    }
  }

  if (!q) {
    console.warn('[daysHandler] no photoprism_q/date/days_ago provided in payload:', payload);
    return null;
  }

  console.log(`[daysHandler] query -> ${q}`);

  // Build PhotoPrism URL (we use the `q` parameter)
  const url = `${photoprism}/api/v1/photos?public=true&count=${MAX_COUNT}&q=${encodeURIComponent(q)}`;

  try {
    const res = await fetch(url);
    if (!res.ok) {
      throw new Error(`HTTP ${res.status} ${res.statusText}`);
    }
    const json = await res.json();
    const list = Array.isArray(json) ? json : (json.Photos || []);

    if (!list || list.length === 0) {
      console.warn(`[daysHandler] no photos found for query: ${q}`);
      return null;
    }

    // Extract hashes / IDs
    const hashes = list
      .map(p => p.Hash || p.UID || p.ID || p.Id || p.id)
      .filter(Boolean);

    if (!hashes.length) {
      console.warn(`[daysHandler] no usable hash fields in results for query: ${q}`);
      return null;
    }

    // Shuffle (Fisher-Yates)
    for (let i = hashes.length - 1; i > 0; i--) {
      const j = Math.floor(Math.random() * (i + 1));
      [hashes[i], hashes[j]] = [hashes[j], hashes[i]];
    }

    return {
      hashes,
      intervalMs: SLIDE_INTERVAL_MS
    };
  } catch (err) {
    console.error('[daysHandler] fetch error:', err);
    return null;
  }
};