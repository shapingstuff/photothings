const mqtt = require("mqtt");
const WebSocket = require("ws");
const fetch = require("node-fetch");

const PHOTOPRISM_API = "http://192.168.68.81:2342";
const mqttClient = mqtt.connect("mqtt://localhost:1883");
const ws = new WebSocket("ws://localhost:8080");

let timeline = [];
let lastSentHash = null;

function parseTapeAlbums(albums) {
  return albums
    .filter(a => a.Description && a.Description.startsWith("TAPE|"))
    .map(a => {
      const parts = a.Description.split("|");
      if (parts.length < 7) return null;
      const [_, tapeId, type, title, color, start, end] = parts;
      return {
        tapeId,
        type,
        title,
        color,
        startCM: parseInt(start),
        endCM: parseInt(end),
        uid: a.UID,
        photos: [],
      };
    })
    .filter(Boolean)
    .sort((a, b) => a.startCM - b.startCM);
}

async function fetchTapeTimeline() {
  try {
    const res = await fetch(`${PHOTOPRISM_API}/api/v1/albums?count=100`);
    const albums = await res.json();

    if (!Array.isArray(albums)) {
      console.error("❌ Unexpected album response:", albums);
      return;
    }

    timeline = parseTapeAlbums(albums);

    for (let album of timeline) {
      const res = await fetch(`${PHOTOPRISM_API}/api/v1/photos?album=${album.uid}&public=true&count=200`);
      const data = await res.json();
      if (!Array.isArray(data)) {
        console.warn(`⚠️ Skipping album ${album.title}: photos response not an array`);
        continue;
      }

      album.photos = data
        .map(p => ({
          hash: p.Hash,
          date: new Date(p.TakenAt || p.TakenAtLocal || null)
        }))
        .filter(p => !isNaN(p.date.getTime()))
        .sort((a, b) => a.date - b.date);
    }

    console.log("📚 Timeline loaded with", timeline.length, "albums");
  } catch (err) {
    console.error("❌ Failed to fetch timeline:", err);
  }
}

function getCurrentAlbum(mm) {
  for (let album of timeline) {
    const startMM = album.startCM * 10;
    const endMM = album.endCM * 10;
    if (mm >= startMM && mm <= endMM) {
      return { album, startMM, endMM };
    }
  }
  return null;
}

mqttClient.on("connect", () => {
  console.log("📡 MQTT connected");
  mqttClient.subscribe("tape/position");
  fetchTapeTimeline();
});

mqttClient.on("message", async (topic, message) => {
  if (topic !== "tape/position") return;

  const mm = parseInt(message.toString());
  const match = getCurrentAlbum(mm);
  if (!match) {
    console.log("⚠️ No album at this mm");
    return;
  }

  const { album, startMM, endMM } = match;
  const range = endMM - startMM;
  const relMM = mm - startMM;
  const photoCount = album.photos.length;

  if (photoCount === 0) {
    console.log("⚠️ No photos in album:", album.title);
    return;
  }

  const binSize = range / photoCount;
  const index = Math.min(photoCount - 1, Math.floor(relMM / binSize));
  const photo = album.photos[index];

  if (!photo || photo.hash === lastSentHash) return;
  lastSentHash = photo.hash;

  const imageUrl = `${PHOTOPRISM_API}/api/v1/t/${photo.hash}/public/fit_1920`;
  const payload = {
    type: "image",
    url: imageUrl,
    albumTitle: album.title,
    color: album.color,
    mm,
    cm: Math.floor(mm / 10)
  };

  if (ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify(payload));
  }

  console.log("🖼️ Sent image:", album.title, "→", imageUrl);
  mqttClient.publish("tape/led", JSON.stringify(colorToRGB(album.color)));
});

function colorToRGB(colorName) {
  const colors = {
    red:     { r: 255, g: 0,   b: 0 },
    green:   { r: 0,   g: 255, b: 0 },
    blue:    { r: 0,   g: 0,   b: 255 },
    yellow:  { r: 255, g: 255, b: 0 },
    magenta: { r: 255, g: 0,   b: 255 },
    cyan:    { r: 0,   g: 255, b: 255 },
    white:   { r: 255, g: 255, b: 255 },
    orange:  { r: 255, g: 165, b: 0 },
    black:   { r: 0,   g: 0,   b: 0 }
  };
  return colors[colorName.toLowerCase()] || colors.white;
}
