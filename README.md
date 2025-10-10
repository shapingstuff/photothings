This is the correct path: /home/admin/Photothings

There is a seperate startup_server so the other service can be used for devleopment whilst still having a working version

Here’s a single, copy-pasteable README.md file (one code block). Copy the entire block below into your repo as README.md.

# Spinner — Server & Slideshow Startup / Maintenance

This repository contains the server pieces for the **Spinner** project (slideshow + album controller).  
This README collects startup/service information, troubleshooting commands and example systemd unit files so you can install, run and maintain the services on your Linux device (e.g. Raspberry Pi).

---

## Quick summary

Two services typically run as systemd units:

- `slideshow.service` — runs the Python slideshow viewer (browser kiosk or Python-based image server).  
- `spinner-server.service` — runs the Node.js spinner server (album controller + slideshow publisher).

Defaults used by the code in this repo:
- MQTT broker (TCP): `192.168.68.80:1883`  
- MQTT websocket (for browser slideshow): `192.168.68.80:9001` (if used)  
- PhotoPrism API: `http://192.168.68.81:2342`  
- Node script: `spinner-server.js` (Node 18+ recommended)  
- Paths (examples): `/home/pi/slideshow`, `/home/pi/spinner-server`

Adjust values to match your environment.

---

## Files & locations (example)

- `/home/pi/slideshow/` — slideshow HTML/Python app that subscribes `spinner/slideshow` (websocket/WS or similar).
- `/home/pi/spinner-server/` — Node.js server that publishes slideshow messages and handles album navigation.
- Systemd units (if installed):
  - `/etc/systemd/system/slideshow.service`
  - `/etc/systemd/system/spinner-server.service`

---

## Example systemd unit files

Paste these files to `/etc/systemd/system/` and edit paths/user as required.

### `/etc/systemd/system/slideshow.service`

[Unit]
Description=Spinner Slideshow (Python)
After=network.target

[Service]
Type=simple
User=pi
WorkingDirectory=/home/pi/slideshow
ExecStart=/usr/bin/python3 /home/pi/slideshow/slideshow.py
Restart=on-failure
RestartSec=5
Environment=PYTHONUNBUFFERED=1
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target

### `/etc/systemd/system/spinner-server.service`

[Unit]
Description=Spinner Server (Node)
After=network.target

[Service]
Type=simple
User=pi
WorkingDirectory=/home/pi/spinner-server
ExecStart=/usr/bin/node /home/pi/spinner-server/spinner-server.js
Restart=on-failure
RestartSec=5
Environment=NODE_ENV=production
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target

Notes:
- Replace `/usr/bin/node` or `/usr/bin/python3` with actual paths if different.
- If you need environment variables (e.g. `PHOTOPRISM`, `MQTT_URL`), either add `Environment="PHOTOPRISM=..."` lines or use `EnvironmentFile=/etc/default/spinner-server`.

---

## Install & enable services

1. Copy the unit files into `/etc/systemd/system/` (use `sudo`).
2. Reload systemd and enable/start the services:
```bash
sudo systemctl daemon-reload
sudo systemctl enable --now slideshow.service
sudo systemctl enable --now spinner-server.service

--now starts the service immediately and enables it on boot.

⸻

Check status & logs

Check status:

sudo systemctl status spinner-server.service
sudo systemctl status slideshow.service

Follow logs live:

sudo journalctl -u spinner-server.service -f
sudo journalctl -u slideshow.service -f

Show last 200 lines:

sudo journalctl -u spinner-server.service -n 200 --no-pager


⸻

Manually run (debugging)

Run directly to see stdout:

# Node server
cd /home/pi/spinner-server
/usr/bin/node spinner-server.js

# Python slideshow
cd /home/pi/slideshow
/usr/bin/python3 slideshow.py

This helps surface environment/path issues.

⸻

MQTT topics and message formats

Make sure firmware and server agree on topics exactly:
	•	spinner/slideshow
	•	Published by server for frame display (retained). Payload:

{ "type": "image", "url": "http://...", "key": "album-<uid>-idx", "seq": 123, "ts": 1680000000000 }


	•	spinner/album/<ALBUM_ID>/nav
	•	Subscribed by server, published by devices to navigate. Examples:

{"device":"<id>","cmd":"next","steps":1}
{"cmd":"get"}


	•	spinner/album/<ALBUM_ID>/photo (or /photo/<device> optional)
	•	Published by server with metadata for ESP32:

{
  "index": 3,
  "hash": "<hash>",
  "date": "2025-06-27T14:22:06Z",
  "age_days": 98,
  "age": "3m",
  "url": "http://.../fit_1920",
  "photosCount": 15
}



Important:
	•	Exact topic strings matter. If server publishes global /photo but device subscribes to /photo/<deviceId>, messages may be missed. Choose one convention (global or per-device) and keep firmware & server consistent.
	•	Use retain: true on slideshow topic so the display shows the last slide immediately after (server should set retained for the frame).

⸻

Race conditions & subscribe timing

When device subscribes then immediately publishes GET, the server’s reply might arrive before broker processes the subscription. Solutions:
	•	Add a short delay after subscribe before sending GET (e.g., 200–1000 ms).
	•	Server can introduce a small delay before responding to GET (we used ~2000 ms in some scripts while debugging).
	•	Prefer retained publishes for current state (so a newly subscribed device immediately receives the retained message).

⸻

ESP32 / Arduino buffer notes
	•	Arduino PubSubClient and ArduinoJson buffers are limited (~4KB). Avoid publishing very large manifests (or split them).
	•	Using simple /photo messages with age/date + index is compact and reliable. Then firmware can map index → URL when needed (or just show date/age).

⸻

Troubleshooting checklist
	1.	Verify the exact topic the ESP subscribed to and the exact topic the server publishes to (watch case and trailing slashes).
	2.	Use mosquitto_sub on the Pi to observe messages:

mosquitto_sub -h 192.168.68.80 -t 'spinner/#' -v


	3.	If ESP doesn’t get messages but broker shows them:
	•	Confirm ESP remains connected (broker client list).
	•	Confirm QoS and retained flags align with expectations.
	4.	If GET/subscribe race is suspected: add delay between subscribe and GET (in device code).
	5.	If large JSON fails: test with a small manual mosquitto_pub message; if that arrives but server’s does not, compare message size/flags and topic exactness.
	6.	Check journal logs for services if they fail/restart.

⸻

Git: quick push to existing repo

If you already have a remote set up:

git add .
git commit -m "Add spinner server + service README"
git push origin main

Replace main with your branch name if different. If no origin remote:

git remote add origin git@github.com:YOURUSER/YOURREPO.git
git push -u origin main


⸻

Will the services restart on failure and at boot?

Yes — with the recommended unit files:
	•	Restart=on-failure restarts the service if it exits with an error.
	•	Enabling the service (systemctl enable) ensures it will start at boot.
	•	Logs are captured by systemd and viewable by journalctl.

If you prefer unconditional restart, change Restart=on-failure to Restart=always.

⸻