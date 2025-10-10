// module_distance.cpp
// Marquee of waypoint names with symbols placed BETWEEN underscore gap slots.
// Single-line marquee (top), bottom status (time/distance).
// Defensive fixes to avoid crashes on module switching.
// Adds MQTT publish when a waypoint is focused (topic: "distance") and ensures mqttClient.loop() is serviced.

#include "module_distance.h"
#include "shared.h"

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <FastLED.h>
#include <AS5600.h>

#include <Fonts/Roboto_Regular_NEW16pt7b.h>
#include <Fonts/Roboto_Condensed_Medium9pt7b.h>

namespace
{
  // ===== CONFIG =====
  static const bool DEBUG = false; // set true to enable placement debug prints
  bool ENABLE_DISTANCE = true;

  const int MAX_MILES = 500;
  const float MILES_PER_REV = 10.0f;
  const long COUNTS_PER_REV = 4096L;
  const long MAX_COUNTS = (long)(MAX_MILES / MILES_PER_REV * COUNTS_PER_REV);

  // Fonts
  const GFXfont *MARQUEE_FONT = &Roboto_Regular_NEW16pt7b;
  const GFXfont *STATUS_FONT = &Roboto_Condensed_Medium9pt7b;

  // vertical tuning: positive moves things down (pixels)
  int MARQUEE_Y_OFFSET = 12;
  int STATUS_Y_OFFSET = 4;

  // spacing: extra pixels added after each char when laying out marquee
  int MARQUEE_LETTER_SPACING = 2;

  // symbol set & placement tuning
  const char *SYMBOLS = "]^{|}~";
  const int SYMBOL_INTERVAL = 8;
  const int SYMBOL_JITTER = 3;
  const int MIN_SYMBOL_GAP = 3;
  const int SYMBOL_MIN_GAP_PX = 10;

  // colors
  const CRGB WP_COLOUR = CRGB::White;
  const CRGB DEFAULT_COLOUR = CRGB::Grey;

  // MQTT topic
  const char *MQTT_TOPIC = "spinner/distance";

  // ===== WAYPOINTS =====
  struct Waypoint { int mile; const char *name; };
  static Waypoint waypoints[] = {
      {0, "Ovington"},
      {5, "Ovingham"},
      {8, "Throckley"},
      {20, "North Shields"},
      {130, "Dalgety Bay"},
      {135, "North Queensferry"},
      {160, "Glasgow"},
      {182, "Dunoon"}};
  static const int numWP = sizeof(waypoints) / sizeof(waypoints[0]);

  // ===== STATE =====
  String baseMarquee; // base marquee: '_' slots + letters (no symbols)
  int charW = 6, charH = 10, charY = 0;
  long totalCounts = 0;
  uint16_t lastRaw = 0;

  int *waypointPixelOffset = nullptr; // per-waypoint pixel start
  bool active = false;

  int *underscorePixelPos = nullptr;
  int underscoreCount = 0;

  struct SymbolPlacement { int underscoreOrdinal; char sym; };
  SymbolPlacement *symbolPlacements = nullptr;
  int symbolPlacementCount = 0;

  // last waypoint published via MQTT (-1 = none)
  int lastPublishedIdx = -1;
}

// ---------- helpers ----------
static int measureRenderedCharWidth(char c)
{
  display.setFont(MARQUEE_FONT);
  display.setTextSize(1);
  char buf[4] = {c, 0, 0, 0};
  int16_t rx, ry;
  uint16_t rw, rh;
  display.getTextBounds(buf, 0, 0, &rx, &ry, &rw, &rh);
  return (int)rw + MARQUEE_LETTER_SPACING;
}

static void freeOffsets()
{
  if (waypointPixelOffset) { free(waypointPixelOffset); waypointPixelOffset = nullptr; }
  if (underscorePixelPos) { free(underscorePixelPos); underscorePixelPos = nullptr; }
  if (symbolPlacements) { free(symbolPlacements); symbolPlacements = nullptr; }
  symbolPlacementCount = 0;
  underscoreCount = 0;
  lastPublishedIdx = -1;
}

static void buildBaseMarqueeAndOffsets()
{
  freeOffsets();
  baseMarquee = String();

  // Build baseMarquee: one '_' per mile gap, then names
  int lastMile = 0;
  int charIndex = 0;
  int *nameCharIndex = (int *)malloc(sizeof(int) * numWP);
  if (!nameCharIndex) {
    Serial.println("module_distance: malloc failed (nameCharIndex)");
    return;
  }

  for (int i = 0; i < numWP; ++i) {
    int gap = waypoints[i].mile - lastMile;
    while (gap--) { baseMarquee += '_'; ++charIndex; }
    nameCharIndex[i] = charIndex;
    for (const char *p = waypoints[i].name; *p; ++p) { baseMarquee += *p; ++charIndex; }
    lastMile = waypoints[i].mile;
  }
  int rem = MAX_MILES - lastMile;
  while (rem--) { baseMarquee += '_'; ++charIndex; }

  // allocate waypointPixelOffset
  waypointPixelOffset = (int *)malloc(sizeof(int) * numWP);
  if (!waypointPixelOffset) {
    Serial.println("module_distance: malloc failed (waypointPixelOffset)");
    free(nameCharIndex);
    return;
  }

  display.setFont(MARQUEE_FONT);
  display.setTextSize(1);

  // count underscores
  int totalChars = (int)baseMarquee.length();
  int ucount = 0;
  for (int i = 0; i < totalChars; ++i) if (baseMarquee.charAt(i) == '_') ++ucount;
  underscoreCount = ucount;
  if (underscoreCount > 0) {
    underscorePixelPos = (int *)malloc(sizeof(int) * underscoreCount);
    if (!underscorePixelPos) {
      Serial.println("module_distance: malloc failed (underscorePixelPos)");
      free(nameCharIndex);
      return;
    }
  }

  // sweep chars, sum widths and record offsets/underscore positions
  int px = 0;
  int uidx = 0;
  for (int i = 0; i < totalChars; ++i) {
    for (int w = 0; w < numWP; ++w) {
      if (i == nameCharIndex[w]) waypointPixelOffset[w] = px;
    }
    char c = baseMarquee.charAt(i);
    int w = measureRenderedCharWidth(c);
    if (c == '_') {
      if (underscorePixelPos && uidx < underscoreCount) underscorePixelPos[uidx++] = px;
    }
    px += w;
  }

  free(nameCharIndex);

  // measure charW/charH
  int16_t rx, ry; uint16_t rw, rh;
  display.getTextBounds("_", 0, 0, &rx, &ry, &rw, &rh);
  charW = (int)rw + MARQUEE_LETTER_SPACING;
  charH = (int)rh;
  charY = ((SCREEN_H / 2 - (int)charH) / 2) - ry + MARQUEE_Y_OFFSET;

  if (DEBUG) {
    Serial.printf("buildBase: chars=%d underscores=%d charW=%d charH=%d\n", totalChars, underscoreCount, charW, charH);
  }
}

// check if pixel mid overlaps any waypoint name area (+/- gapPx)
static bool midOverlapsName(int midPx, int gapPx)
{
  if (!waypointPixelOffset) return false;
  display.setFont(MARQUEE_FONT);
  display.setTextSize(1);
  for (int i = 0; i < numWP; ++i) {
    int16_t bx, by; uint16_t bw, bh;
    display.getTextBounds(waypoints[i].name, 0, 0, &bx, &by, &bw, &bh);
    int nameLeft = waypointPixelOffset[i];
    int nameRight = nameLeft + (int)bw;
    if (midPx >= (nameLeft - gapPx) && midPx <= (nameRight + gapPx)) return true;
  }
  return false;
}

static void decideSymbolPlacements()
{
  if (symbolPlacements) { free(symbolPlacements); symbolPlacements = nullptr; symbolPlacementCount = 0; }
  if (!underscorePixelPos || underscoreCount == 0) return;

  int approxSymbols = max(1, underscoreCount / max(1, SYMBOL_INTERVAL));
  if (approxSymbols > underscoreCount / max(1, MIN_SYMBOL_GAP)) {
    approxSymbols = max(1, underscoreCount / max(1, MIN_SYMBOL_GAP));
  }

  symbolPlacements = (SymbolPlacement *)malloc(sizeof(SymbolPlacement) * approxSymbols);
  if (!symbolPlacements) {
    Serial.println("module_distance: malloc failed (symbols)");
    return;
  }
  symbolPlacementCount = 0;

  float step = (float)underscoreCount / (float)approxSymbols;

  for (int k = 0; k < approxSymbols; ++k) {
    int desired = (int)round(step * (k + 0.5f));
    int jitter = (SYMBOL_JITTER > 0) ? random(-SYMBOL_JITTER, SYMBOL_JITTER + 1) : 0;
    int ordinal = desired + jitter;
    if (ordinal < 0) ordinal = 0;
    if (ordinal >= underscoreCount) ordinal = underscoreCount - 1;

    // search near ordinal for a valid spot (respect MIN_SYMBOL_GAP and SYMBOL_MIN_GAP_PX)
    int foundOrdinal = -1;
    int searchRadius = max(3, SYMBOL_JITTER * 4);
    for (int r = 0; r <= searchRadius; ++r) {
      int cand[2] = { ordinal - r, ordinal + r };
      for (int ci = 0; ci < 2; ++ci) {
        int candOrd = cand[ci];
        if (candOrd < 0 || candOrd >= underscoreCount) continue;
        bool tooClose = false;
        for (int s = 0; s < symbolPlacementCount; ++s) {
          if (abs(symbolPlacements[s].underscoreOrdinal - candOrd) < MIN_SYMBOL_GAP) { tooClose = true; break; }
        }
        if (tooClose) continue;
        int leftPx = underscorePixelPos[candOrd];
        int rightPx = (candOrd + 1 < underscoreCount) ? underscorePixelPos[candOrd + 1] : (leftPx + charW);
        int mid = (leftPx + rightPx) / 2;
        if (midOverlapsName(mid, SYMBOL_MIN_GAP_PX)) continue;
        foundOrdinal = candOrd;
        break;
      }
      if (foundOrdinal >= 0) break;
    }

    if (foundOrdinal < 0) {
      if (DEBUG) Serial.printf("skip symbol for bucket %d (no safe ordinal)\n", k);
      continue;
    }

    int symIdx = (strlen(SYMBOLS) > 0) ? random(0, (int)strlen(SYMBOLS)) : 0;
    char sym = SYMBOLS[symIdx];
    symbolPlacements[symbolPlacementCount].underscoreOrdinal = foundOrdinal;
    symbolPlacements[symbolPlacementCount].sym = sym;
    ++symbolPlacementCount;
    if (DEBUG) Serial.printf("placed symbol #%d at ordinal=%d sym=%c (bucket %d)\n", symbolPlacementCount - 1, foundOrdinal, sym, k);
  }

  // sort placements
  for (int i = 1; i < symbolPlacementCount; ++i) {
    for (int j = i; j > 0 && symbolPlacements[j - 1].underscoreOrdinal > symbolPlacements[j].underscoreOrdinal; --j) {
      SymbolPlacement tmp = symbolPlacements[j];
      symbolPlacements[j] = symbolPlacements[j - 1];
      symbolPlacements[j - 1] = tmp;
    }
  }

  if (DEBUG) {
    Serial.printf("decideSymbolPlacements: final count=%d\n", symbolPlacementCount);
    for (int s = 0; s < symbolPlacementCount; ++s) {
      Serial.printf("  sym[%d] underscoreOrd=%d sym=%c\n", s, symbolPlacements[s].underscoreOrdinal, symbolPlacements[s].sym);
    }
  }
}

// ----- Module API -----
void module_distance_enable(bool on) { ENABLE_DISTANCE = on; }
bool module_distance_isEnabled() { return ENABLE_DISTANCE; }

void module_distance_setup()
{
  randomSeed(analogRead(0) ^ millis());
  lastRaw = 4095 - as5600.readAngle();
  totalCounts = 0;
  display.setTextWrap(false);
  buildBaseMarqueeAndOffsets();
  decideSymbolPlacements();
  display.clearDisplay();
  display.display();
  if (DEBUG) {
    Serial.println("module_distance: setup complete");
    Serial.print("baseMarquee: "); Serial.println(baseMarquee);
  }
}

void module_distance_activate()
{
  // Ensure marquee/offsets exist before using them
  if (!waypointPixelOffset || !underscorePixelPos) {
    if (DEBUG) Serial.println("module_distance: rebuilding marquee in activate()");
    buildBaseMarqueeAndOffsets();
  }

  // (re)decide symbol placements now that offsets exist
  decideSymbolPlacements();

  active = true;
  lastPublishedIdx = -1; // reset publish state on activation
  if (leds && NUM_PIXELS > 0) { leds[0] = DEFAULT_COLOUR; FastLED.show(); }
  if (DEBUG) Serial.println("module_distance: activated");
}

void module_distance_deactivate()
{
  active = false;
  freeOffsets();
  lastPublishedIdx = -1;
  if (leds && NUM_PIXELS > 0) { leds[0] = CRGB::Black; FastLED.show(); }
  if (DEBUG) Serial.println("module_distance: deactivated");
}

void module_distance_loop()
{
  if (!ENABLE_DISTANCE || !active) return;

  // ensure mqtt client processed (important to keep connection alive)
  if (mqttClient.connected()) mqttClient.loop();

  // protect against missing offsets
  if (!waypointPixelOffset || !underscorePixelPos) {
    if (DEBUG) Serial.println("module_distance: missing offsets in loop(), attempting rebuild");
    buildBaseMarqueeAndOffsets();
    decideSymbolPlacements();
    if (!waypointPixelOffset || !underscorePixelPos) {
      if (DEBUG) Serial.println("module_distance: rebuild failed, skipping loop iteration");
      delay(50);
      return;
    }
  }

  // 1) read & invert encoder
  uint16_t raw = as5600.readAngle();
  uint16_t inv = 4095 - raw;

  // 2) signed delta wrap aware
  int32_t diff = int32_t(inv) - int32_t(lastRaw);
  if (diff > 2048) diff -= 4096;
  if (diff < -2048) diff += 4096;
  lastRaw = inv;

  // 3) accumulate & clamp
  totalCounts += diff;
  if (totalCounts < 0) totalCounts = 0;
  if (totalCounts > MAX_COUNTS) totalCounts = MAX_COUNTS;

  // 4) compute miles & ETA
  float milesF = (totalCounts * MILES_PER_REV) / (float)COUNTS_PER_REV;
  int miles = int(milesF + 0.5f);
  if (miles < 0) miles = 0;
  if (miles > MAX_MILES) miles = MAX_MILES;
  int totalMin = int((milesF / 60.0f) * 60.0f + 0.5f);
  if (totalMin < 0) totalMin = 0;
  int hr = totalMin / 60;
  int mn = totalMin % 60;

  // 5) compute scroll offset
  int16_t scrollX = miles * charW;

  // 6) detect waypoint visible for LED and compute focused wp for MQTT
  bool anyVis = false;
  for (int i = 0; i < numWP; ++i) {
    int16_t nameX = waypointPixelOffset[i] - scrollX;
    int16_t bx, by; uint16_t bw, bh;
    display.setFont(MARQUEE_FONT);
    display.getTextBounds(waypoints[i].name, 0, 0, &bx, &by, &bw, &bh);
    if ((nameX + (int)bw > 0) && (nameX < SCREEN_W)) { anyVis = true; break; }
  }

  bool destVis = false;
  {
    int i = numWP - 1;
    int16_t nameX = waypointPixelOffset[i] - scrollX;
    int16_t bx, by; uint16_t bw, bh;
    display.setFont(MARQUEE_FONT);
    display.getTextBounds(waypoints[i].name, 0, 0, &bx, &by, &bw, &bh);
    if ((nameX + (int)bw > 0) && (nameX < SCREEN_W)) destVis = true;
  }

  // ---------- Find focused waypoint (closest center to screen center) ----------
  int centerX = SCREEN_W / 2;
  int bestIdx = -1;
  int bestDist = INT32_MAX;
  display.setFont(MARQUEE_FONT);
  for (int i = 0; i < numWP; ++i) {
    int16_t bx, by; uint16_t bw, bh;
    display.getTextBounds(waypoints[i].name, 0, 0, &bx, &by, &bw, &bh);
    int wpLeft = waypointPixelOffset[i] - scrollX;
    int wpCenter = wpLeft + (int)bw / 2;
    int d = abs(wpCenter - centerX);
    if (d < bestDist) { bestDist = d; bestIdx = i; }
  }
  const int FOCUS_THRESHOLD_PX = charW * 3; // tweakable
  bool focused = (bestIdx >= 0 && bestDist <= FOCUS_THRESHOLD_PX);

  // Always print focused waypoint to serial so you can see it in monitor
  if (focused) {
    Serial.printf("Focused waypoint: idx=%d name=%s distPx=%d\n", bestIdx, waypoints[bestIdx].name, bestDist);
  } else {
    // Print when nothing focused (helps debugging)
    if (DEBUG) Serial.println("No focused waypoint");
  }

  // MQTT: publish when focused waypoint changes
  if (focused && bestIdx != lastPublishedIdx) {
    lastPublishedIdx = bestIdx;
    // prepare payload
    char payload[80];
    snprintf(payload, sizeof(payload), "{\"name\":\"%s\",\"mile\":%d}", waypoints[bestIdx].name, waypoints[bestIdx].mile);
    if (mqttClient.connected()) {
      bool ok = mqttClient.publish(MQTT_TOPIC, payload);
      Serial.print("MQTT publish ");
      Serial.print(ok ? "OK: " : "FAILED: ");
      Serial.println(payload);
    } else {
      Serial.print("MQTT skipped (not connected): ");
      Serial.println(payload);
    }
  } else if (!focused) {
    // clear lastPublishedIdx so it will publish again when a wp re-enters focus
    lastPublishedIdx = -1;
  }

  // 7) LED color
  if (destVis) leds[0] = WP_COLOUR;
  else if (anyVis) leds[0] = WP_COLOUR;
  else leds[0] = DEFAULT_COLOUR;
  FastLED.show();

  // 8) draw
  display.clearDisplay();
  display.setFont(MARQUEE_FONT);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  int totalChars = (int)baseMarquee.length();
  int px = 0;
  for (int i = 0; i < totalChars; ++i) {
    char c = baseMarquee.charAt(i);
    int w = measureRenderedCharWidth(c);
    int drawX = px - scrollX;
    if (!(drawX + w <= 0 || drawX >= SCREEN_W)) {
      char buf[2] = {c, 0};
      display.setCursor(drawX, charY);
      display.print(buf);
    }
    px += w;
  }

  // draw symbols
  for (int s = 0; s < symbolPlacementCount; ++s) {
    int ord = symbolPlacements[s].underscoreOrdinal;
    char sym = symbolPlacements[s].sym;
    if (ord < 0 || ord >= underscoreCount) continue;
    int leftPx = underscorePixelPos[ord];
    int rightPx = (ord + 1 < underscoreCount) ? underscorePixelPos[ord + 1] : (leftPx + charW);
    int mid = (leftPx + rightPx) / 2;
    char bufSym[2] = {sym, 0};
    int16_t bx, by; uint16_t bw, bh;
    display.getTextBounds(bufSym, 0, 0, &bx, &by, &bw, &bh);
    int symX = mid - ((int)bw / 2);
    int drawX = symX - scrollX;
    if (!(drawX + (int)bw <= 0 || drawX >= SCREEN_W)) {
      display.setCursor(drawX, charY);
      display.print(bufSym);
    }
  }

  // bottom status
  display.setFont(STATUS_FONT);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  char statusBuf[32];
  snprintf(statusBuf, sizeof(statusBuf), "%dh%02dm (%dmi)", hr, mn, miles);
  int16_t sx, sy; uint16_t sw, sh;
  display.getTextBounds(statusBuf, 0, 0, &sx, &sy, &sw, &sh);
  display.setCursor((SCREEN_W - (int)sw) / 2 - sx,
                    SCREEN_H / 2 + ((SCREEN_H / 2 - (int)sh) / 2) - sy + STATUS_Y_OFFSET);
  display.print(statusBuf);

  display.display();

  delay(10);
}