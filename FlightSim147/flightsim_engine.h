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

// ---------------- aircraft ----------------
// Rear-view silhouettes: each shape is a polygon (n >= 3, up to 8 points)
// or an ellipse (n = -1: cx, cy, rx, ry). Units are screen pixels, +y down.
struct Shape { uint8_t r, g, b; int8_t n; float pt[16]; };

struct Aircraft {
  const char *name;
  float speed, maxTurn, turnResp, bank, clearance, climb, spriteBank;
  const Shape *shapes; int nShapes;
};

#define MIRROR4(r,g,b, x0,y0,x1,y1,x2,y2,x3,y3) \
  {r,g,b,4,{x0,y0,x1,y1,x2,y2,x3,y3}}, {r,g,b,4,{-(x0),y0,-(x1),y1,-(x2),y2,-(x3),y3}}

static const Shape JET[] = {
  MIRROR4(150,158,170, -4,0, -34,6, -34,8, -4,4),        // swept wings
  MIRROR4(120,128,140, -3,-2, -14,0, -14,2, -3,1),       // stabilisers
  {90,98,110, 4, {-1.5f,-3, -1,-17, 1,-17, 1.5f,-3}},    // fin
  {130,138,150, -1, {0,0, 5.5f,5}},                      // fuselage
  {255,120,30, -1, {0,0.5f, 3,3}},                       // afterburner
  {255,230,120, -1, {0,0.5f, 1.6f,1.6f}},
};
static const Shape PROP[] = {
  {235,235,235, 4, {-40,-3, 40,-3, 40,0, -40,0}},        // straight wing
  MIRROR4(200,40,40, -40,-3, -34,-3, -34,0, -40,0),      // red tips
  {225,225,225, 4, {-12,-6, 12,-6, 12,-4, -12,-4}},      // tailplane
  {200,40,40, 4, {-1,-5, -0.5f,-17, 1.5f,-17, 1.5f,-5}}, // fin
  {245,245,245, -1, {0,-2.5f, 4.5f,5}},                  // fuselage
  {60,60,70, -1, {0,-4, 2.5f,1.5f}},                     // canopy
};
static const Shape BIPLANE[] = {
  {225,185,40, 4, {-30,2, 30,2, 30,5, -30,5}},           // lower wing
  {80,60,40, 4, {-19,-10, -17,-10, -17,3, -19,3}},       // struts
  {80,60,40, 4, {17,-10, 19,-10, 19,3, 17,3}},
  {80,60,40, 4, {-6,-10, -4,-10, -4,0, -6,0}},
  {80,60,40, 4, {4,-10, 6,-10, 6,0, 4,0}},
  {245,205,60, 4, {-30,-13, 30,-13, 30,-10, -30,-10}},   // upper wing
  {225,185,40, 4, {-10,-5, 10,-5, 10,-3, -10,-3}},       // tailplane
  {190,50,40, 4, {-1,-5, -0.5f,-16, 1.5f,-16, 1.5f,-5}}, // fin
  {200,60,45, -1, {0,-1, 5,6}},                          // fuselage
};
static const Shape HELI[] = {
  {190,195,200, -1, {0,-15, 40,1.6f}},                   // rotor blur
  {60,60,60, 4, {-1,-15, 1,-15, 1,-9, -1,-9}},           // mast
  {50,50,50, 4, {-10,6, 10,6, 10,7, -10,7}},             // skid bar
  {50,50,50, 4, {-9,3, -7,3, -7,7, -9,7}},
  {50,50,50, 4, {7,3, 9,3, 9,7, 7,7}},
  {40,90,60, -1, {0,-3, 9,7.5f}},                        // body
  {30,70,45, -1, {0,-2, 2.5f,2.5f}},                     // tail boom end-on
  {150,150,150, 4, {4,-8, 5,-8, 5,4, 4,4}},              // tail rotor blur
};
#undef MIRROR4

static const Aircraft AIRCRAFT[] = {
  {"JET",     70, 0.9f, 1.6f, 0.55f, 55, 60, 2.0f, JET,     sizeof(JET) / sizeof(Shape)},
  {"PROP",    34, 0.7f, 1.2f, 0.55f, 38, 40, 2.2f, PROP,    sizeof(PROP) / sizeof(Shape)},
  {"BIPLANE", 24, 1.0f, 1.4f, 0.60f, 28, 30, 2.4f, BIPLANE, sizeof(BIPLANE) / sizeof(Shape)},
  {"HELI",    18, 1.2f, 2.0f, 0.25f, 20, 35, 1.2f, HELI,    sizeof(HELI) / sizeof(Shape)},
};
static const int NUM_AIRCRAFT = sizeof(AIRCRAFT) / sizeof(Aircraft);

// ---------------- camera / autopilot ----------------
struct Plane {
  float x = 0, y = 0, alt = 150, heading = 0;
  int type = 1;                    // index into AIRCRAFT
  float turnRate = 0, turnTarget = 0, turnTimer = 0;
  float roll = 0;                  // bank as horizon slope
  float vs = 0;                    // vertical speed
  float horizon = H * 0.38f;       // screen y of horizon (pitch)
  uint32_t rng = 12345;
};

static inline float frand(uint32_t &s) { s = s * 1664525u + 1013904223u; return (s >> 8) / 16777216.0f; }

// optional manual inputs in -1..1; pass manual=false for full autopilot
static void updatePlane(Plane &p, float dt, bool manual = false, float stickX = 0, float stickY = 0) {
  const Aircraft &a = AIRCRAFT[p.type];
  if (manual) {
    p.turnTarget = -stickX * a.maxTurn;
  } else {
    p.turnTimer -= dt;
    if (p.turnTimer <= 0) {
      p.turnTarget = (frand(p.rng) - 0.5f) * 1.3f * a.maxTurn;
      if (frand(p.rng) < 0.3f) p.turnTarget = 0;
      p.turnTimer = 2.5f + frand(p.rng) * 4.0f;
    }
  }
  p.turnRate += (p.turnTarget - p.turnRate) * (1 - expf(-dt * a.turnResp));
  p.heading += p.turnRate * dt;
  p.roll = p.turnRate * a.bank;

  float fx = -sinf(p.heading), fy = -cosf(p.heading);
  p.x += fx * a.speed * dt; p.y += fy * a.speed * dt;
  p.x = fmodf(p.x + MAP * 64.0f, (float)MAP); p.y = fmodf(p.y + MAP * 64.0f, (float)MAP);

  // terrain-following: look ahead (further for faster aircraft), keep clearance
  int ahead = 0;
  float look = a.speed * 4.0f;
  for (int k = 0; k <= 10; k++) {
    float d = look * k / 10;
    int g = groundAt(p.x + fx * d, p.y + fy * d);
    if (g > ahead) ahead = g;
  }
  float target = ahead + a.clearance;
  if (manual) target += stickY * 60.0f;
  float wantVs = (target - p.alt) * 0.9f;
  wantVs = wantVs > a.climb ? a.climb : (wantVs < -a.climb * 0.6f ? -a.climb * 0.6f : wantVs);
  p.vs += (wantVs - p.vs) * (1 - expf(-dt * 2.0f));
  p.alt += p.vs * dt;
  int g0 = groundAt(p.x, p.y);
  if (p.alt < g0 + 8) { p.alt = g0 + 8; if (p.vs < 0) p.vs = 0; }
  float wantHz = H * 0.38f + p.vs * 0.9f;
  p.horizon += (wantHz - p.horizon) * (1 - expf(-dt * 3.0f));
}

// ---------------- renderer ----------------
struct Camera { float x, y, alt, heading, roll, horizon; };

// chase camera: behind and above the aircraft, rolling less than the aircraft
static const float CHASE_BACK = 22.0f, CHASE_UP = 7.0f;
static Camera cameraFor(const Plane &p, bool chase) {
  Camera c{p.x, p.y, p.alt, p.heading, p.roll, p.horizon};
  if (chase) {
    c.x = p.x + sinf(p.heading) * CHASE_BACK;
    c.y = p.y + cosf(p.heading) * CHASE_BACK;
    c.alt = p.alt + CHASE_UP;
    int g = groundAt(c.x, c.y);
    if (c.alt < g + 6) c.alt = g + 6;
    c.roll = p.roll * 0.4f;
  }
  return c;
}

// fb: W*H RGB565, row-major
static void render(const Camera &cam, uint16_t *fb) {
  static int16_t ybuf[W];
  for (int i = 0; i < W; i++) ybuf[i] = H;

  const float sinA = sinf(cam.heading), cosA = cosf(cam.heading);
  const float half = W * 0.5f;
  static int32_t rollOff[W];
  for (int i = 0; i < W; i++) rollOff[i] = (int32_t)(cam.roll * (i - half) * 256.0f);
  const int32_t hz256 = (int32_t)(cam.horizon * 256.0f);
  const int32_t camH = (int32_t)cam.alt;

  float z = 1.0f, dz = 0.6f;
  while (z < DIST) {
    float plx = -cosA * z - sinA * z + cam.x;
    float ply =  sinA * z - cosA * z + cam.y;
    float prx =  cosA * z - sinA * z + cam.x;
    float pry = -sinA * z - cosA * z + cam.y;
    int32_t fx = (int32_t)(plx * 65536.0f), fy = (int32_t)(ply * 65536.0f);
    int32_t dx = (int32_t)((prx - plx) * 65536.0f / W);
    int32_t dy = (int32_t)((pry - ply) * 65536.0f / W);
    int32_t inv = (int32_t)(HSCALE / z * 256.0f);
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

// scanline polygon fill (convex or concave, up to 24 points)
static void fillPoly(uint16_t *fb, const float *xs, const float *ys, int n, uint16_t c) {
  float miny = ys[0], maxy = ys[0];
  for (int i = 1; i < n; i++) { if (ys[i] < miny) miny = ys[i]; if (ys[i] > maxy) maxy = ys[i]; }
  int y0 = (int)ceilf(miny - 0.5f), y1 = (int)floorf(maxy - 0.5f);
  y0 = y0 < 0 ? 0 : y0; y1 = y1 > H - 1 ? H - 1 : y1;
  for (int y = y0; y <= y1; y++) {
    float sy = y + 0.5f, nx[24]; int k = 0;
    for (int i = 0, j = n - 1; i < n; j = i++) {
      if ((ys[i] <= sy) != (ys[j] <= sy))
        nx[k++] = xs[i] + (sy - ys[i]) * (xs[j] - xs[i]) / (ys[j] - ys[i]);
    }
    for (int a = 1; a < k; a++) { float t = nx[a]; int b = a - 1; while (b >= 0 && nx[b] > t) { nx[b + 1] = nx[b]; b--; } nx[b + 1] = t; }
    for (int a = 0; a + 1 < k; a += 2) {
      int xa = (int)ceilf(nx[a] - 0.5f), xb = (int)floorf(nx[a + 1] - 0.5f);
      xa = xa < 0 ? 0 : xa; xb = xb > W - 1 ? W - 1 : xb;
      uint16_t *d = fb + y * W;
      for (int x = xa; x <= xb; x++) d[x] = c;
    }
  }
}

// draw the aircraft for the chase view
static void drawAircraft(uint16_t *fb, const Plane &p, const Camera &cam) {
  const Aircraft &a = AIRCRAFT[p.type];
  float ang = -atanf(p.roll) * a.spriteBank;              // right turn → clockwise bank
  float ca = cosf(ang), sa = sinf(ang);
  float cx = W * 0.5f;
  float cy = cam.horizon + (cam.alt - p.alt) * HSCALE / CHASE_BACK - p.vs * 0.15f;
  for (int s = 0; s < a.nShapes; s++) {
    const Shape &sh = a.shapes[s];
    float xs[24], ys[24]; int n;
    if (sh.n < 0) {       // ellipse
      n = 16;
      for (int i = 0; i < n; i++) {
        float t = i * 6.2831853f / n;
        float lx = sh.pt[0] + cosf(t) * sh.pt[2], ly = sh.pt[1] + sinf(t) * sh.pt[3];
        xs[i] = cx + lx * ca - ly * sa; ys[i] = cy + lx * sa + ly * ca;
      }
    } else {
      n = sh.n;
      for (int i = 0; i < n; i++) {
        float lx = sh.pt[i * 2], ly = sh.pt[i * 2 + 1];
        xs[i] = cx + lx * ca - ly * sa; ys[i] = cy + lx * sa + ly * ca;
      }
    }
    fillPoly(fb, xs, ys, n, rgb565(sh.r, sh.g, sh.b));
  }
}

// cockpit-view centre reticle
static void drawReticle(uint16_t *fb, const Camera &cam) {
  const uint16_t c = rgb565(120, 255, 120);
  int cx = W / 2, cy = (int)(H * 0.38f);
  (void)cam;
  for (int d = 4; d <= 12; d++) {
    fb[cy * W + cx - d] = c; fb[cy * W + cx + d] = c;
  }
  for (int d = 3; d <= 6; d++) fb[(cy + d) * W + cx] = c;
}

// convenience: full frame
static void drawFrame(const Plane &p, bool chase, uint16_t *fb) {
  Camera cam = cameraFor(p, chase);
  render(cam, fb);
  if (chase) drawAircraft(fb, p, cam);
  else drawReticle(fb, cam);
}

} // namespace fs
