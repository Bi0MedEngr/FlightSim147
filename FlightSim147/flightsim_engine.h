// flightsim_engine.h — procedural voxel-terrain flight sim core
// Pure C++ (no Arduino deps) so it can be tested on a PC too.
#pragma once
#include <stdint.h>
#include <math.h>
#include <string.h>

namespace fs {

// ---------------- configuration ----------------
static const int W = 320;          // render width  (landscape)
static const int H = 172;          // render height
static const int MAP_BITS = 8;     // 256x256 wrapping world
static const int MAP = 1 << MAP_BITS;
static const int MAP_MASK = MAP - 1;
static const float DIST = 520.0f;  // view distance (map units)
static const float HSCALE = 85.0f; // vertical projection scale
static const int WATER = 70;       // water level (0..255)
static const int FOG_LEVELS = 16;

static inline uint16_t rgb565(int r, int g, int b) {
  r = r < 0 ? 0 : (r > 255 ? 255 : r);
  g = g < 0 ? 0 : (g > 255 ? 255 : g);
  b = b < 0 ? 0 : (b > 255 ? 255 : b);
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// ---------------- world data ----------------
static uint8_t hmap[MAP * MAP];   // terrain height
static uint8_t cmap[MAP * MAP];   // material*32 + shade
static uint16_t fogLUT[FOG_LEVELS][256];
static uint16_t skyLUT[256];
static const int SKY_R = 120, SKY_G = 170, SKY_B = 225; // horizon haze colour

static inline uint32_t hash3(uint32_t x, uint32_t y, uint32_t s) {
  uint32_t h = x * 374761393u + y * 668265263u + s * 2246822519u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}
static inline float lattice(int x, int y, int period, uint32_t seed) {
  x &= (period - 1); y &= (period - 1);
  return (hash3(x, y, seed) & 0xFFFF) / 65535.0f;
}
static inline float smooth(float t) { return t * t * (3 - 2 * t); }

// periodic value noise, sampled at map coords, `cells` lattice cells across the map
static float vnoise(int px, int py, int cells, uint32_t seed) {
  float cs = (float)MAP / cells;
  float fx = px / cs, fy = py / cs;
  int ix = (int)floorf(fx), iy = (int)floorf(fy);
  float tx = smooth(fx - ix), ty = smooth(fy - iy);
  float a = lattice(ix, iy, cells, seed), b = lattice(ix + 1, iy, cells, seed);
  float c = lattice(ix, iy + 1, cells, seed), d = lattice(ix + 1, iy + 1, cells, seed);
  return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * ty;
}

// materials
enum { M_DEEP, M_SHALLOW, M_SAND, M_GRASS, M_FOREST, M_ROCK, M_SNOW, M_DRYGRASS };
static const uint8_t MAT_RGB[8][3] = {
  { 20,  60, 140}, { 40, 110, 180}, {214, 196, 140}, { 86, 150,  60},
  { 40,  95,  45}, {120, 110, 100}, {240, 245, 250}, {150, 150,  70},
};

static void buildPalettes() {
  for (int f = 0; f < FOG_LEVELS; f++) {
    float fa = (float)f / (FOG_LEVELS - 1);
    fa = fa * fa * 0.9f;
    for (int i = 0; i < 256; i++) {
      int m = (i >> 5) & 7, s = i & 31;
      float br = 0.45f + s * (0.85f / 31.0f);
      float r = MAT_RGB[m][0] * br, g = MAT_RGB[m][1] * br, b = MAT_RGB[m][2] * br;
      r += (SKY_R - r) * fa; g += (SKY_G - g) * fa; b += (SKY_B - b) * fa;
      fogLUT[f][i] = rgb565((int)r, (int)g, (int)b);
    }
  }
  // sky gradient: index 0 = far above horizon (deep blue), 255 = at horizon
  for (int i = 0; i < 256; i++) {
    float t = i / 255.0f; t = t * t;
    skyLUT[i] = rgb565((int)(35 + (SKY_R + 40 - 35) * t),
                       (int)(80 + (SKY_G + 35 - 80) * t),
                       (int)(170 + (SKY_B + 25 - 170) * t));
  }
}

static void generateWorld(uint32_t seed) {
  static const int cells[5] = {4, 8, 16, 32, 64};
  static const float amp[5] = {1.0f, 0.5f, 0.25f, 0.12f, 0.06f};
  float total = 0; for (int o = 0; o < 5; o++) total += amp[o];
  // raw heights
  for (int y = 0; y < MAP; y++)
    for (int x = 0; x < MAP; x++) {
      float n = 0;
      for (int o = 0; o < 5; o++) n += vnoise(x, y, cells[o], seed + o * 101) * amp[o];
      n /= total;                       // ~0..1, centred ~0.5
      n = (n - 0.25f) / 0.6f;           // stretch contrast
      n = n < 0 ? 0 : (n > 1 ? 1 : n);
      n = powf(n, 1.7f);                // flat valleys, sharp peaks
      hmap[y * MAP + x] = (uint8_t)(n * 225);
    }
  // colour + lighting (light from the -x,-y direction)
  for (int y = 0; y < MAP; y++)
    for (int x = 0; x < MAP; x++) {
      int h = hmap[y * MAP + x];
      int hn = hmap[((y + 1) & MAP_MASK) * MAP + ((x + 1) & MAP_MASK)];
      uint32_t r = hash3(x, y, seed ^ 0xABCD);
      int jit = (int)(r & 15) - 8;
      int m;
      if (h < WATER - 18) m = M_DEEP;
      else if (h < WATER) m = M_SHALLOW;
      else if (h < WATER + 5 + (jit >> 2)) m = M_SAND;
      else if (h < 120 + jit) m = ((r >> 8) & 7) == 0 ? M_DRYGRASS : M_GRASS;
      else if (h < 165 + jit) m = M_FOREST;
      else if (h < 190 + jit) m = M_ROCK;
      else m = M_SNOW;
      int shade;
      if (m <= M_SHALLOW) {
        shade = 18 + ((r >> 4) & 3);
      } else {
        shade = 18 + (h - hn) * 2 + ((r >> 12) & 3) - 1;
        shade = shade < 0 ? 0 : (shade > 31 ? 31 : shade);
      }
      cmap[y * MAP + x] = (uint8_t)(m * 32 + shade);
    }
  // flatten water surface
  for (int i = 0; i < MAP * MAP; i++) if (hmap[i] < WATER) hmap[i] = WATER;
}

static inline int groundAt(float x, float y) {
  return hmap[(((int)floorf(y)) & MAP_MASK) * MAP + (((int)floorf(x)) & MAP_MASK)];
}

// ---------------- camera / autopilot ----------------
struct Plane {
  float x = 0, y = 0, alt = 150, heading = 0;
  float speed = 38;          // map units / s
  float turnRate = 0, turnTarget = 0, turnTimer = 0;
  float roll = 0;            // horizon slope (pixels per pixel)
  float vs = 0;              // vertical speed
  float horizon = H * 0.38f; // screen y of horizon
  uint32_t rng = 12345;
};

static inline float frand(uint32_t &s) { s = s * 1664525u + 1013904223u; return (s >> 8) / 16777216.0f; }

// optional manual inputs in -1..1; pass manual=false for full autopilot
static void updatePlane(Plane &p, float dt, bool manual = false, float stickX = 0, float stickY = 0) {
  if (manual) {
    p.turnTarget = -stickX * 0.9f;
  } else {
    p.turnTimer -= dt;
    if (p.turnTimer <= 0) {
      p.turnTarget = (frand(p.rng) - 0.5f) * 1.2f;
      if (frand(p.rng) < 0.3f) p.turnTarget = 0;
      p.turnTimer = 2.5f + frand(p.rng) * 4.0f;
    }
  }
  p.turnRate += (p.turnTarget - p.turnRate) * (1 - expf(-dt * 1.2f));
  p.heading += p.turnRate * dt;
  p.roll = p.turnRate * 0.55f;   // bank into the turn

  float fx = -sinf(p.heading), fy = -cosf(p.heading);
  p.x += fx * p.speed * dt; p.y += fy * p.speed * dt;
  p.x = fmodf(p.x + MAP * 64.0f, (float)MAP); p.y = fmodf(p.y + MAP * 64.0f, (float)MAP);

  // terrain-following: look ahead, keep clearance
  int ahead = 0;
  for (int d = 0; d <= 160; d += 16) {
    int g = groundAt(p.x + fx * d, p.y + fy * d);
    if (g > ahead) ahead = g;
  }
  float target = ahead + 45.0f;
  if (manual) target += stickY * 60.0f;
  float wantVs = (target - p.alt) * 0.9f;
  wantVs = wantVs > 45 ? 45 : (wantVs < -25 ? -25 : wantVs);
  p.vs += (wantVs - p.vs) * (1 - expf(-dt * 2.0f));
  p.alt += p.vs * dt;
  int g0 = groundAt(p.x, p.y);
  if (p.alt < g0 + 10) { p.alt = g0 + 10; if (p.vs < 0) p.vs = 0; }
  // pitch: climbing pushes the horizon down the screen
  float wantHz = H * 0.38f + p.vs * 0.9f;
  p.horizon += (wantHz - p.horizon) * (1 - expf(-dt * 3.0f));
}

// ---------------- renderer ----------------
// fb: W*H RGB565, row-major
static void render(const Plane &p, uint16_t *fb) {
  static int16_t ybuf[W];
  for (int i = 0; i < W; i++) ybuf[i] = H;

  const float sinA = sinf(p.heading), cosA = cosf(p.heading);
  const float half = W * 0.5f;
  // per-column roll offset, fixed point *256
  static int32_t rollOff[W];
  for (int i = 0; i < W; i++) rollOff[i] = (int32_t)(p.roll * (i - half) * 256.0f);
  const int32_t hz256 = (int32_t)(p.horizon * 256.0f);

  float z = 1.0f, dz = 0.6f;
  while (z < DIST) {
    float plx = -cosA * z - sinA * z + p.x;
    float ply =  sinA * z - cosA * z + p.y;
    float prx =  cosA * z - sinA * z + p.x;
    float pry = -sinA * z - cosA * z + p.y;
    int32_t fx = (int32_t)(plx * 65536.0f), fy = (int32_t)(ply * 65536.0f);
    int32_t dx = (int32_t)((prx - plx) * 65536.0f / W);
    int32_t dy = (int32_t)((pry - ply) * 65536.0f / W);
    int32_t inv = (int32_t)(HSCALE / z * 256.0f);   // *256
    int32_t camH = (int32_t)p.alt;
    int fog = 0;
    if (z > DIST * 0.3f) {
      fog = (int)((z - DIST * 0.3f) / (DIST * 0.7f) * (FOG_LEVELS - 1));
      if (fog > FOG_LEVELS - 1) fog = FOG_LEVELS - 1;
    }
    const uint16_t *pal = fogLUT[fog];
    for (int i = 0; i < W; i++) {
      int idx = (((fy >> 16) & MAP_MASK) << MAP_BITS) | ((fx >> 16) & MAP_MASK);
      int32_t ys = ((camH - hmap[idx]) * inv + hz256 + rollOff[i]) >> 8;
      if (ys < 0) ys = 0;
      if (ys < ybuf[i]) {
        uint16_t c = pal[cmap[idx]];
        uint16_t *dst = fb + ys * W + i;
        for (int y = ys; y < ybuf[i]; y++) { *dst = c; dst += W; }
        ybuf[i] = (int16_t)ys;
      }
      fx += dx; fy += dy;
    }
    z += dz;
    dz += 0.012f;
  }
  // sky in remaining space above terrain, tilted with roll
  for (int i = 0; i < W; i++) {
    int hx = (hz256 + rollOff[i]) >> 8;
    uint16_t *dst = fb + i;
    for (int y = 0; y < ybuf[i]; y++) {
      int k = 255 - (hx - y) * 2;
      k = k < 0 ? 0 : (k > 255 ? 255 : k);
      *dst = skyLUT[k]; dst += W;
    }
  }
}

// minimal HUD drawn straight into the framebuffer: centre reticle
static void drawReticle(uint16_t *fb) {
  const uint16_t c = rgb565(120, 255, 120);
  int cx = W / 2, cy = (int)(H * 0.38f);
  for (int d = 4; d <= 12; d++) {
    fb[cy * W + cx - d] = c; fb[cy * W + cx + d] = c;
  }
  for (int d = 3; d <= 6; d++) fb[(cy + d) * W + cx] = c;
}

} // namespace fs
