// OrbitView - preset-based camera for X-Plane 12 (Windows).
//
// Drives the camera from per-aircraft lists of cockpit and exterior views.
// Cockpit views write the pilot's head datarefs (aircraft-relative, so full
// precision); exterior views control the camera via XPLMControlCamera.
//   A / D              previous / next view in the current category
//   End                toggle cockpit <-> exterior
//   Right-drag         look / orbit
//   Mouse wheel, = / - zoom (in the cockpit this is X-Plane's own zoom)
//   Right-drag + wheel cockpit: OrbitView's own wider, finer zoom (per view)
//   Shift+W/S          move forward / back (exterior: closer / farther)
//   Shift+A/D          move left / right
//   Shift+E/Q          move up / down
//   Shift+Space        reset the current view to its saved position
//   Ctrl+Shift+Space   add the current camera as a new view
//   Right stick        look / orbit (axes auto-detected from X-Plane's
//                      "View left/right" and "View up/down" assignments)
//   F12                cinematic camera: slow drifting shots of different
//                      parts of the aircraft, with the occasional flyby
//                      (A / D skip a shot, F12 or End leave it)
//   Right-drag + middle click  raise / lower binoculars (wheel = magnification,
//                      2-50x, separate from the normal zoom; compass at the bottom)
// Views are managed in the Plugins > OrbitView > Views window and saved to
// presets\<aircraft>.acf.txt. Every action is also an orbitview/* command.
// Camera effects (G sway, turbulence, ground) are set on the Preferences page
// and are off by default.

#define _USE_MATH_DEFINES
#include <windows.h>
#include <windowsx.h>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <GL/gl.h>

#include "XPLMCamera.h"
#include "XPLMDataAccess.h"
#include "XPLMDisplay.h"
#include "XPLMGraphics.h"
#include "XPLMMenus.h"
#include "XPLMPlanes.h"
#include "XPLMPlugin.h"
#include "XPLMProcessing.h"
#include "XPLMScenery.h"
#include "XPLMUtilities.h"
#include "XPStandardWidgets.h"
#include "XPWidgets.h"

// ---------------------------------------------------------------------------
// Config

struct Config {
    int   keyboard = 1;              // install the key sniffer
    int   mouse_pan = 1;             // right-drag to look
    float mouse_sensitivity = 0.15f; // degrees per pixel
    int   invert_mouse_y = 0;
    float wheel_step = 0.12f;        // zoom per wheel notch (log scale)
    int   stick_x_axis = -1;         // raw X-Plane axis index, -1 = auto-detect
    int   stick_y_axis = -1;
    float stick_deadzone = 0.12f;
    float stick_rate = 120.0f;       // degrees per second at full deflection
    int   invert_stick_y = 0;
    float move_speed = 0.6f;         // m/s in the cockpit; scaled by distance outside
    float smoothing = 6.0f;          // view transition speed (1/s), 0 = cut
    float input_smoothing = 15.0f;   // how fast the camera catches up with mouse/keys/wheel (1/s), 0 = raw
    float move_accel = 6.0f;         // Shift+WASDQE ramp up/down speed (1/s), 0 = instant
    float heading_follow = 2.5f;     // exterior heading follow speed (1/s)
    int   effects = 0;               // camera effects master switch (Preferences page)
    int   effects_exterior = 1;      // also shake exterior views
    float fx_gforce = 1.0f;          // effect strengths, 1 = default, 0..2
    float fx_turbulence = 1.0f;
    float fx_ground = 1.0f;
    float cinematic_shot_time = 7.0f; // seconds per cinematic shot (varies +-15%)
    float cinematic_fade = 0.4f;      // dip to black between shots (s), 0 = hard cut
    int   cinematic_flyby = 1;        // include flybys when the aircraft is moving
    float binocular_zoom = 6.0f;      // binocular magnification when first raised (2..50)
    float cinematic_distance = 1.0f;  // drift-shot distance multiplier
};

static Config g_cfg;
static std::string g_pluginDir;      // ...\Resources\plugins\OrbitView\ (with separator)

static void Log(const char* fmt, ...) {
    char buf[512] = "OrbitView: ";
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf + 11, sizeof(buf) - 12, fmt, ap);
    va_end(ap);
    strcat_s(buf, "\n");
    XPLMDebugString(buf);
}

static void LoadConfig() {
    g_cfg = Config();
    FILE* f = nullptr;
    if (fopen_s(&f, (g_pluginDir + "config.ini").c_str(), "r") != 0 || !f) {
        Log("no config.ini, using defaults");
        return;
    }
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char key[64];
        float v;
        if (line[0] == '#' || sscanf_s(line, " %63[^= ] = %f", key, (unsigned)sizeof(key), &v) != 2) continue;
        std::string k = key;
        if      (k == "keyboard")           g_cfg.keyboard = (int)v;
        else if (k == "mouse_pan")          g_cfg.mouse_pan = (int)v;
        else if (k == "mouse_sensitivity")  g_cfg.mouse_sensitivity = v;
        else if (k == "invert_mouse_y")     g_cfg.invert_mouse_y = (int)v;
        else if (k == "wheel_step")         g_cfg.wheel_step = v;
        else if (k == "stick_x_axis")       g_cfg.stick_x_axis = (int)v;
        else if (k == "stick_y_axis")       g_cfg.stick_y_axis = (int)v;
        else if (k == "stick_deadzone")     g_cfg.stick_deadzone = v;
        else if (k == "stick_rate")         g_cfg.stick_rate = v;
        else if (k == "invert_stick_y")     g_cfg.invert_stick_y = (int)v;
        else if (k == "move_speed")         g_cfg.move_speed = v;
        else if (k == "smoothing")          g_cfg.smoothing = v;
        else if (k == "input_smoothing")    g_cfg.input_smoothing = v;
        else if (k == "move_accel")         g_cfg.move_accel = v;
        else if (k == "heading_follow")     g_cfg.heading_follow = v;
        else if (k == "effects")            g_cfg.effects = (int)v;
        else if (k == "effects_exterior")   g_cfg.effects_exterior = (int)v;
        else if (k == "fx_gforce")          g_cfg.fx_gforce = v;
        else if (k == "fx_turbulence")      g_cfg.fx_turbulence = v;
        else if (k == "fx_ground")          g_cfg.fx_ground = v;
        else if (k == "cinematic_shot_time") g_cfg.cinematic_shot_time = v;
        else if (k == "cinematic_fade")     g_cfg.cinematic_fade = v;
        else if (k == "cinematic_flyby")    g_cfg.cinematic_flyby = (int)v;
        else if (k == "cinematic_distance") g_cfg.cinematic_distance = v;
        else if (k == "binocular_zoom")     g_cfg.binocular_zoom = v;
        else Log("unknown config key '%s'", key);
    }
    fclose(f);
}

// Writes the Preferences-page settings back into config.ini, keeping the rest
// of the file (comments included) as it is.
static void SavePrefs() {
    const struct { const char* key; float v; } vals[] = {
        {"effects", (float)g_cfg.effects}, {"effects_exterior", (float)g_cfg.effects_exterior},
        {"fx_gforce", g_cfg.fx_gforce}, {"fx_turbulence", g_cfg.fx_turbulence}, {"fx_ground", g_cfg.fx_ground},
    };
    const int n = (int)(sizeof(vals) / sizeof(vals[0]));
    std::string path = g_pluginDir + "config.ini";
    std::vector<std::string> lines;
    FILE* f = nullptr;
    char line[256];
    if (fopen_s(&f, path.c_str(), "r") == 0 && f) {
        while (fgets(line, sizeof(line), f)) lines.push_back(line);
        fclose(f);
    }
    bool found[n] = {};
    for (std::string& l : lines) {
        char key[64];
        float v;
        if (l[0] == '#' || sscanf_s(l.c_str(), " %63[^= ] = %f", key, (unsigned)sizeof(key), &v) != 2) continue;
        for (int i = 0; i < n; ++i)
            if (strcmp(key, vals[i].key) == 0) {
                snprintf(line, sizeof(line), "%s = %g\n", key, vals[i].v);
                l = line;
                found[i] = true;
            }
    }
    bool header = false;
    for (int i = 0; i < n; ++i) {
        if (found[i]) continue;
        if (!header) {
            if (!lines.empty() && lines.back().back() != '\n') lines.back() += "\n";
            lines.push_back("\n# Camera effects (set on the Preferences page).\n");
            header = true;
        }
        snprintf(line, sizeof(line), "%s = %g\n", vals[i].key, vals[i].v);
        lines.push_back(line);
    }
    if (fopen_s(&f, path.c_str(), "w") != 0 || !f) { Log("cannot write %s", path.c_str()); return; }
    for (const std::string& l : lines) fputs(l.c_str(), f);
    fclose(f);
}

// ---------------------------------------------------------------------------
// Math. X-Plane local coords: +x east, +y up, -z north. Heading/pitch/roll use
// the same convention for the aircraft and the camera.

static const double D2R = M_PI / 180.0, R2D = 180.0 / M_PI;

struct V3 { double x, y, z; };
static V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
static V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static V3 operator*(V3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }

static V3 RotX(V3 v, double a) { double c = cos(a), s = sin(a); return {v.x, c * v.y - s * v.z, s * v.y + c * v.z}; }
static V3 RotY(V3 v, double a) { double c = cos(a), s = sin(a); return {c * v.x + s * v.z, v.y, -s * v.x + c * v.z}; }
static V3 RotZ(V3 v, double a) { double c = cos(a), s = sin(a); return {c * v.x - s * v.y, s * v.x + c * v.y, v.z}; }

// Body -> world for heading/pitch/roll in degrees.
static V3 Orient(V3 v, double h, double p, double r) {
    return RotY(RotX(RotZ(v, -r * D2R), p * D2R), -h * D2R);
}

static float Wrap180(float a) {
    while (a > 180.f) a -= 360.f;
    while (a < -180.f) a += 360.f;
    return a;
}
static float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// ---------------------------------------------------------------------------
// Views

// Cockpit: x,y,z = eye relative to CG in aircraft axes; yaw/pitch relative to
//          the aircraft; zoom = camera zoom factor.
// Exterior: x,y,z = pivot offset from CG in heading-aligned axes (x right,
//          y up, z aft); yaw relative to aircraft heading (0 = behind);
//          pitch absolute; dist = orbit radius.
struct View { float x, y, z, yaw, pitch, dist, zoom; };

// "saved" is what the view returns to (and what is written to disk); "live"
// is the saved position plus whatever the user has moved since.
struct Preset { char name[32]; View saved, live; };

enum { COCKPIT = 0, EXTERIOR = 1 };
static const int kMaxViews = 12;
static const char* kCategory[2] = {"Cockpit", "Exterior"};

static std::vector<Preset> g_presets[2];
static int  g_mode = COCKPIT;
static int  g_idx[2] = {0, 0};
static View g_cur;                   // smoothed state actually rendered
static float g_hdgSmooth = 0.f;
static bool g_viewsReady = false;

static Preset& Current() { return g_presets[g_mode][g_idx[g_mode]]; }

// ---------------------------------------------------------------------------
// Datarefs

static XPLMDataRef dr_lx, dr_ly, dr_lz, dr_psi;
static XPLMDataRef dr_isExternal, dr_vr;
static XPLMDataRef dr_headX, dr_headY, dr_headZ, dr_headPsi, dr_headThe;
static XPLMDataRef dr_peX, dr_peY, dr_peZ, dr_sizeX, dr_sizeZ;
static XPLMDataRef dr_axisValues, dr_axisAssign;
static XPLMDataRef dr_gloadCam;
static XPLMDataRef dr_ax, dr_ay, dr_az, dr_vy, dr_the, dr_phi, dr_gs, dr_onGround, dr_paused;
static XPLMDataRef dr_vx, dr_vz, dr_fov, dr_magPsi;
static int g_savedGloadCam = -1;     // user's G-loaded camera setting while we hold it off

static XPLMProbeRef g_probe = nullptr;

// ---------------------------------------------------------------------------
// Input state

enum { MV_FWD, MV_BACK, MV_LEFT, MV_RIGHT, MV_UP, MV_DOWN, MV_COUNT };
static bool g_moveKey[MV_COUNT], g_moveCmd[MV_COUNT];
static const int kMoveVk[MV_COUNT] = {'W', 'S', 'A', 'D', 'E', 'Q'};
static float g_moveVel[3];           // ramped forward / right / up command, -1..1
static bool g_transition = false;    // gliding to a newly selected view
static float g_zoomCmd = 0.f;        // held zoom rate: >0 in, <0 out

static double g_mouseDx = 0, g_mouseDy = 0, g_wheel = 0;
static bool  g_rDrag = false;
static POINT g_lastPt;

static int g_stickX = -1, g_stickY = -1; // resolved raw axis indices
static bool g_stickArmed = false;    // stick seen centred since its hardware appeared

static bool g_enabled = true;
static bool g_running = false;       // enabled, views built, not in VR
static bool g_inControl = false;     // we own the camera (exterior views)
static bool g_cine = false;          // cinematic camera running (always in EXTERIOR mode)

// Binoculars: magnify the current view; the wheel sets the magnification and
// looking around slows down to match. Zoom and (outside) look offsets glide.
static bool  g_bino = false;
static bool  g_binoToggleReq = false;  // set by the window procedure, handled in the flight loop
static float g_binoZoom = 0.f;         // target magnification, 0 = take it from config.ini
static float g_binoZoomCur = 1.f;      // rendered magnification
static float g_binoYaw = 0.f, g_binoPitch = 0.f;       // exterior look offset, target
static float g_binoYawCur = 0.f, g_binoPitchCur = 0.f; // exterior look offset, rendered
static float g_savedFov = -1.f;        // user's field of view while cockpit binoculars narrow it
static const float kBinoMin = 2.f, kBinoMax = 50.f;
static float g_viewHdg = 0.f;          // true heading the rendered view looks along (for the compass)
static double g_lastViewCmdTime = -10.0;
static double g_initAt = -1.0;       // time at which to (re)build views for a new aircraft

static double Now() {
    using namespace std::chrono;
    static const auto t0 = steady_clock::now();
    return duration<double>(steady_clock::now() - t0).count();
}

// ---------------------------------------------------------------------------
// Toast window: shows the view name for a moment after switching.

static XPLMWindowID g_toast = nullptr;
static char  g_toastText[64] = "";
static double g_toastUntil = 0;

static void DrawToast(XPLMWindowID w, void*) {
    int l, t, r, b;
    XPLMGetWindowGeometry(w, &l, &t, &r, &b);
    XPLMDrawTranslucentDarkBox(l, t, r, b);
    float white[3] = {1, 1, 1};
    int textW = (int)XPLMMeasureString(xplmFont_Proportional, g_toastText, (int)strlen(g_toastText));
    XPLMDrawString(white, (l + r - textW) / 2, (t + b) / 2 - 4, g_toastText, nullptr, xplmFont_Proportional);
}
static int ToastClick(XPLMWindowID, int, int, XPLMMouseStatus, void*) { return 1; }
static void ToastKey(XPLMWindowID, char, XPLMKeyFlags, char, void*, int) {}
static XPLMCursorStatus ToastCursor(XPLMWindowID, int, int, void*) { return xplm_CursorDefault; }
static int ToastWheel(XPLMWindowID, int, int, int, int, void*) { return 0; }

static void ShowToastText(const char* text) {
    strcpy_s(g_toastText, text);
    int l, t, r, b;
    XPLMGetScreenBoundsGlobal(&l, &t, &r, &b);
    int cx = (l + r) / 2;
    XPLMSetWindowGeometry(g_toast, cx - 130, t - 60, cx + 130, t - 90);
    XPLMSetWindowIsVisible(g_toast, 1);
    g_toastUntil = Now() + 1.5;
}

// Binocular compass: a heading tape along the bottom of the screen showing
// the magnetic heading the view looks along, plus the magnification.
static XPLMWindowID g_compass = nullptr;

static float g_viewHdgForCompass() {   // magnetic
    return g_viewHdg + (dr_magPsi ? XPLMGetDataf(dr_magPsi) - XPLMGetDataf(dr_psi) : 0.f);
}

static void DrawCompassText(int x, int y, const char* s, float* color) {
    int w = (int)XPLMMeasureString(xplmFont_Proportional, s, (int)strlen(s));
    XPLMDrawString(color, x - w / 2, y, (char*)s, nullptr, xplmFont_Proportional);
}

static void DrawCompass(XPLMWindowID w, void*) {
    int l, t, r, b;
    XPLMGetWindowGeometry(w, &l, &t, &r, &b);
    XPLMDrawTranslucentDarkBox(l, t, r, b);
    float hdg = fmodf(g_viewHdgForCompass() + 720.f, 360.f);
    const float half = 30.f;                       // degrees each side of centre
    float ppd = (r - l - 24) / (2.f * half);
    int cx = (l + r) / 2, tickTop = b + 30;

    XPLMSetGraphicsState(0, 0, 0, 0, 1, 0, 0);
    glColor4f(1.f, 1.f, 1.f, 0.9f);
    glBegin(GL_LINES);
    for (int d = (int)floorf((hdg - half) / 5.f) * 5; d <= hdg + half; d += 5) {
        float x = cx + (d - hdg) * ppd;
        if (x < l + 8 || x > r - 8) continue;
        float len = ((d % 10) + 10) % 10 == 0 ? 8.f : 4.f;
        glVertex2f(x, (float)tickTop);
        glVertex2f(x, tickTop - len);
    }
    glEnd();
    glColor4f(1.f, 0.85f, 0.2f, 1.f);              // lubber line
    glBegin(GL_LINES);
    glVertex2f((float)cx, (float)tickTop + 4.f);
    glVertex2f((float)cx, (float)tickTop - 12.f);
    glEnd();

    float white[3] = {1, 1, 1}, amber[3] = {1.f, 0.85f, 0.2f};
    for (int d = (int)floorf((hdg - half) / 30.f) * 30; d <= hdg + half; d += 30) {
        float x = cx + (d - hdg) * ppd;
        if (x < l + 14 || x > r - 14) continue;
        int n = ((d % 360) + 360) % 360;
        char lab[8];
        if (n % 90 == 0) snprintf(lab, sizeof(lab), "%c", "NESW"[n / 90]);
        else snprintf(lab, sizeof(lab), "%03d", n);
        DrawCompassText((int)x, b + 6, lab, white);
    }
    char buf[16];
    snprintf(buf, sizeof(buf), "%03d", ((int)lroundf(hdg) + 359) % 360 + 1);
    DrawCompassText(cx, t - 14, buf, amber);
    snprintf(buf, sizeof(buf), "%.1fx", g_binoZoomCur);
    XPLMDrawString(white, r - 8 - (int)XPLMMeasureString(xplmFont_Proportional, buf, (int)strlen(buf)), t - 14,
                   buf, nullptr, xplmFont_Proportional);
}

static void ShowCompass(bool show) {
    if (!g_compass || (XPLMGetWindowIsVisible(g_compass) != 0) == show) return;
    if (show) {
        int l, t, r, b;
        XPLMGetScreenBoundsGlobal(&l, &t, &r, &b);
        int cx = (l + r) / 2;
        XPLMSetWindowGeometry(g_compass, cx - 180, b + 100, cx + 180, b + 50);
    }
    XPLMSetWindowIsVisible(g_compass, show);
}

static void ShowToast() {
    char text[64];
    snprintf(text, sizeof(text), "%s: %s", kCategory[g_mode], Current().name);
    ShowToastText(text);
}

// ---------------------------------------------------------------------------
// Presets on disk

static std::string AircraftPresetPath() {
    char file[512] = "", path[1024] = "";
    XPLMGetNthAircraftModel(0, file, path);
    return g_pluginDir + "presets\\" + file + ".txt";
}

static void SavePresets() {
    CreateDirectoryA((g_pluginDir + "presets").c_str(), nullptr);
    std::string p = AircraftPresetPath();
    FILE* f = nullptr;
    if (fopen_s(&f, p.c_str(), "w") != 0 || !f) { Log("cannot write %s", p.c_str()); return; }
    fprintf(f, "OrbitView 2\n# category x y z yaw pitch dist zoom name\n");
    for (int m = 0; m < 2; ++m)
        for (const Preset& pr : g_presets[m]) {
            const View& v = pr.saved;
            fprintf(f, "%c %f %f %f %f %f %f %f %s\n", m == COCKPIT ? 'C' : 'E',
                    v.x, v.y, v.z, v.yaw, v.pitch, v.dist, v.zoom, pr.name);
        }
    fclose(f);
    Log("saved views to %s", p.c_str());
}

// Replaces a category's list only if the file has at least one view for it.
static void LoadPresets() {
    std::string p = AircraftPresetPath();
    FILE* f = nullptr;
    if (fopen_s(&f, p.c_str(), "r") != 0 || !f) return;
    std::vector<Preset> loaded[2];
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char c;
        Preset pr = {};
        View& v = pr.saved;
        if (sscanf_s(line, "%c %f %f %f %f %f %f %f %31[^\r\n]", &c, 1, &v.x, &v.y, &v.z, &v.yaw,
                     &v.pitch, &v.dist, &v.zoom, pr.name, (unsigned)sizeof(pr.name)) != 9) continue;
        int m = c == 'C' ? COCKPIT : (c == 'E' ? EXTERIOR : -1);
        if (m < 0 || (int)loaded[m].size() >= kMaxViews) continue;
        pr.live = pr.saved;
        loaded[m].push_back(pr);
    }
    fclose(f);
    for (int m = 0; m < 2; ++m)
        if (!loaded[m].empty()) g_presets[m] = loaded[m];
    Log("loaded views from %s", p.c_str());
}

// ---------------------------------------------------------------------------
// Factory views, built from the aircraft's eye point and size.

static void AddFactory(int m, const char* name, View v) {
    Preset pr = {};
    strcpy_s(pr.name, name);
    pr.saved = pr.live = v;
    g_presets[m].push_back(pr);
}

static void BuildFactoryViews() {
    float ex = XPLMGetDataf(dr_headX), ey = XPLMGetDataf(dr_headY), ez = XPLMGetDataf(dr_headZ);
    float the0 = XPLMGetDataf(dr_headThe);
    if (fabsf(ex) + fabsf(ey) + fabsf(ez) < 0.01f) {
        ex = XPLMGetDataf(dr_peX); ey = XPLMGetDataf(dr_peY); ez = XPLMGetDataf(dr_peZ);
        the0 = 0.f;
    }
    // +1 = centre console is to the right of the pilot.
    float side = ex > 0.05f ? -1.f : 1.f;
    float size = fmaxf(XPLMGetDataf(dr_sizeX), XPLMGetDataf(dr_sizeZ));
    float D = size > 0.f ? Clamp(2.5f * size, 15.f, 120.f) : 30.f;

    g_presets[COCKPIT].clear();
    AddFactory(COCKPIT, "Pilot",       {ex,        ey, ez,         0.f,          the0, 0.f, 1.f});
    AddFactory(COCKPIT, "Copilot",     {-ex,       ey, ez,         0.f,          the0, 0.f, 1.f});
    AddFactory(COCKPIT, "Pedestal",    {ex * 0.5f, ey, ez,         25.f * side, -50.f, 0.f, 1.f});
    AddFactory(COCKPIT, "Overhead",    {ex * 0.5f, ey, ez + 0.15f, 0.f,          70.f, 0.f, 1.f});
    AddFactory(COCKPIT, "Side window", {ex,        ey, ez,        -85.f * side,  -5.f, 0.f, 1.f});

    g_presets[EXTERIOR].clear();
    AddFactory(EXTERIOR, "Chase",         {0, 0, 0,    0.f,  -8.f, D,         1.f});
    AddFactory(EXTERIOR, "Close chase",   {0, 0, 0,    0.f,  -5.f, D * 0.55f, 1.f});
    AddFactory(EXTERIOR, "Front quarter", {0, 0, 0, -145.f,  -6.f, D * 0.8f,  1.f});
    AddFactory(EXTERIOR, "Side",          {0, 0, 0,   90.f,  -4.f, D * 0.8f,  1.f});
    AddFactory(EXTERIOR, "Top",           {0, 0, 0,    0.f, -75.f, D * 1.4f,  1.f});
    AddFactory(EXTERIOR, "Low rear",      {0, 0, 0,   20.f,   4.f, D * 0.7f,  1.f});

    Log("factory views (eye %.2f %.2f %.2f, exterior radius %.0f m)", ex, ey, ez, D);
}

static void InitViews() {
    BuildFactoryViews();
    LoadPresets();
    g_idx[COCKPIT] = g_idx[EXTERIOR] = 0;
    g_cur = Current().live;
    g_hdgSmooth = XPLMGetDataf(dr_psi);
    g_viewsReady = true;
}

// ---------------------------------------------------------------------------
// View switching and editing

static void ApplyViewType() {
    XPLMCommandOnce(XPLMFindCommand(g_mode == COCKPIT ? "sim/view/3d_cockpit_cmnd_look" : "sim/view/chase"));
    g_lastViewCmdTime = Now();
}

static int CameraCB(XPLMCameraPosition_t* out, int losingControl, void*);

// Exterior views own the camera; cockpit views leave it to X-Plane.
static void UpdateCameraOwnership() {
    if (g_running && g_mode == EXTERIOR) {
        if (!g_inControl && !XPLMIsCameraBeingControlled(nullptr)) {
            XPLMControlCamera(xplm_ControlCameraForever, CameraCB, nullptr);
            g_inControl = true;
        }
    } else if (g_inControl) {
        XPLMDontControlCamera();
        g_inControl = false;
    }
}

static void EndBinoculars();

// Jump (no glide) to the current category's selected view.
static void EnterMode(int m) {
    EndBinoculars();
    g_mode = m;
    Current().live = Current().saved;
    g_cur = Current().live;
    g_hdgSmooth = XPLMGetDataf(dr_psi);
    UpdateCameraOwnership();
}

static void SetMode(int m) {
    EnterMode(m);
    ApplyViewType();
    ShowToast();
}

static void SelectView(int i) {
    g_bino = false;                  // lower the binoculars; the zoom glides out
    g_binoYaw = g_binoPitch = 0.f;
    g_idx[g_mode] = i;
    Current().live = Current().saved;
    g_transition = true;
    ShowToast();
}

static void StepView(int dir) {
    int n = (int)g_presets[g_mode].size();
    SelectView((g_idx[g_mode] + dir + n) % n);
}

static void ResetView() {
    Current().live = Current().saved;
    g_transition = true;
    ShowToastText("View reset");
}

static void AddView(const char* name) {
    std::vector<Preset>& list = g_presets[g_mode];
    if ((int)list.size() >= kMaxViews) { ShowToastText("View list is full (12)"); return; }
    Preset pr = {};
    if (name && *name) strcpy_s(pr.name, name);
    else snprintf(pr.name, sizeof(pr.name), "Custom %d", (int)list.size() + 1);
    pr.saved = pr.live = Current().live;
    list.insert(list.begin() + g_idx[g_mode] + 1, pr);
    g_idx[g_mode] += 1;
    SavePresets();
    char text[64];
    snprintf(text, sizeof(text), "Added view: %s", pr.name);
    ShowToastText(text);
}

static void SaveCurrentView() {
    Current().saved = Current().live;
    SavePresets();
    char text[64];
    snprintf(text, sizeof(text), "Saved view: %s", Current().name);
    ShowToastText(text);
}

static void RenameCurrentView(const char* name) {
    if (!name || !*name) return;
    strcpy_s(Current().name, name);
    SavePresets();
}

static void DeleteCurrentView() {
    std::vector<Preset>& list = g_presets[g_mode];
    if (list.size() <= 1) { ShowToastText("Can't delete the last view"); return; }
    list.erase(list.begin() + g_idx[g_mode]);
    if (g_idx[g_mode] >= (int)list.size()) g_idx[g_mode] = (int)list.size() - 1;
    Current().live = Current().saved;
    SavePresets();
    ShowToast();
}

static void RestoreFactoryViews() {
    BuildFactoryViews();
    g_idx[COCKPIT] = g_idx[EXTERIOR] = 0;
    SavePresets();
    ShowToastText("Factory views restored");
}

static void ClearHeldInput() {
    memset(g_moveKey, 0, sizeof(g_moveKey));
    memset(g_moveVel, 0, sizeof(g_moveVel));
    g_rDrag = false;
}

// X-Plane's G-loaded internal camera shoves the pilot's head around; since we
// rewrite the head every frame it never settles and just jerks the view.
static void HoldGloadCamOff(bool hold) {
    if (!dr_gloadCam) return;
    if (hold && g_savedGloadCam < 0) {
        g_savedGloadCam = XPLMGetDatai(dr_gloadCam);
        XPLMSetDatai(dr_gloadCam, 0);
    } else if (!hold && g_savedGloadCam >= 0) {
        XPLMSetDatai(dr_gloadCam, g_savedGloadCam);
        g_savedGloadCam = -1;
    }
}

// ---------------------------------------------------------------------------
// Binoculars

static void ShowBinoZoom() {
    char text[64];
    snprintf(text, sizeof(text), "Binoculars %.1fx", g_binoZoom);
    ShowToastText(text);
}

// The field of view is a saved X-Plane setting. While OrbitView has changed
// it, fov_restore.txt holds the user's own value, so that if X-Plane quits
// (it may save its prefs before plugins are stopped) or crashes, the value is
// put back on the next start.
static std::string FovRestorePath() { return g_pluginDir + "fov_restore.txt"; }

static void RestoreFov() {
    if (g_savedFov < 0.f) return;
    if (dr_fov) XPLMSetDataf(dr_fov, g_savedFov);
    g_savedFov = -1.f;
    DeleteFileA(FovRestorePath().c_str());
}

static void RecoverFovAfterRestart() {
    FILE* f = nullptr;
    if (fopen_s(&f, FovRestorePath().c_str(), "r") != 0 || !f) return;
    float v = 0.f;
    if (fscanf_s(f, "%f", &v) == 1 && v >= 20.f && v <= 160.f && dr_fov) {
        XPLMSetDataf(dr_fov, v);
        Log("restored field of view to %.1f deg", v);
    }
    fclose(f);
    DeleteFileA(FovRestorePath().c_str());
}

// Drops the binoculars at once (view category changes, cinematic, shutdown).
static void EndBinoculars() {
    g_bino = false;
    g_binoZoomCur = 1.f;
    g_binoYaw = g_binoPitch = g_binoYawCur = g_binoPitchCur = 0.f;
    RestoreFov();
}

static void ToggleBinoculars() {
    g_bino = !g_bino;
    if (!g_bino) {   // lowered: zoom and look offset glide back
        g_binoYaw = g_binoPitch = 0.f;
        ShowToastText("Binoculars off");
        return;
    }
    if (g_binoZoom <= 0.f) g_binoZoom = Clamp(g_cfg.binocular_zoom, kBinoMin, kBinoMax);
    ShowBinoZoom();
}

static void SmoothBinoculars(float dt) {
    float target = g_bino ? g_binoZoom : 1.f;
    float a = 1.f - expf(-6.f * dt);   // zoom glides in log space, so every step feels the same
    g_binoZoomCur = expf(logf(g_binoZoomCur) + (logf(target) - logf(g_binoZoomCur)) * a);
    if (!g_bino && g_binoZoomCur < 1.002f) g_binoZoomCur = 1.f;
    float b = g_cfg.input_smoothing > 0.f ? 1.f - expf(-g_cfg.input_smoothing * dt) : 1.f;
    g_binoYawCur = Wrap180(g_binoYawCur + Wrap180(g_binoYaw - g_binoYawCur) * b);
    g_binoPitchCur += (g_binoPitch - g_binoPitchCur) * b;
}

// Cockpit zoom (right-drag + wheel, per view) and binoculars both work by
// changing X-Plane's field of view; the user's own value is put back as soon
// as the combined zoom is back to 1x.
static void ApplyCockpitFov() {
    if (!dr_fov) return;
    float zoom = g_cur.zoom * g_binoZoomCur;
    if (fabsf(zoom - 1.f) < 0.001f) { RestoreFov(); return; }
    if (g_savedFov < 0.f) {
        g_savedFov = XPLMGetDataf(dr_fov);
        FILE* f = nullptr;
        if (fopen_s(&f, FovRestorePath().c_str(), "w") == 0 && f) {
            fprintf(f, "%f\n", g_savedFov);
            fclose(f);
        }
    }
    float half = (float)atan(tan(g_savedFov * 0.5 * D2R) / zoom) * (float)R2D;
    XPLMSetDataf(dr_fov, fminf(2.f * half, 150.f));
}

static void StopCinematic(bool restoreView);

static void StopRunning() {
    EndBinoculars();
    StopCinematic(false);
    g_running = false;
    UpdateCameraOwnership();
    ClearHeldInput();
    HoldGloadCamOff(false);
}

// ---------------------------------------------------------------------------
// Per-frame camera

static float StickDeflection(int axis, float* values) {
    if (axis < 0 || axis >= 500) return 0.f;
    float d = (values[axis] - 0.5f) * 2.f;   // raw axis values are 0..1
    float dz = g_cfg.stick_deadzone;
    if (fabsf(d) < dz) return 0.f;
    return (d > 0 ? d - dz : d + dz) / (1.f - dz);
}

static void ProcessInput(float dt) {
    View& t = Current().live;

    // All input moves the target; Smooth() eases the rendered camera after it,
    // which hides the uneven timing of mouse messages and key events.

    // Look / orbit. An axis stays assigned after its pad is unplugged and then
    // reads 0, i.e. full deflection, which spun the camera. So the stick is
    // used only once it has been seen centred, and dropped again when every
    // assigned axis reads exactly 0 (no device; a round-gated stick can't
    // reach that corner). sim/joystick/joy_mapped_axis_avail can't be used:
    // it reads 0 even with the pad connected.
    float sx = 0.f, sy = 0.f;
    if (g_stickX >= 0 || g_stickY >= 0) {
        float values[500];
        XPLMGetDatavf(dr_axisValues, values, 0, 500);
        bool noDevice = (g_stickX < 0 || values[g_stickX] == 0.f) &&
                        (g_stickY < 0 || values[g_stickY] == 0.f);
        sx = StickDeflection(g_stickX, values);
        sy = -StickDeflection(g_stickY, values);   // stick up reads low
        if (g_cfg.invert_stick_y) sy = -sy;
        if (noDevice) {
            if (g_stickArmed) Log("stick lost");
            g_stickArmed = false;
        } else if (!g_stickArmed && sx == 0.f && sy == 0.f) {
            g_stickArmed = true;
            Log("stick armed");
        }
        if (!g_stickArmed) sx = sy = 0.f;
    }
    float my = (float)g_mouseDy * (g_cfg.invert_mouse_y ? 1.f : -1.f);
    float dyaw = (float)g_mouseDx * g_cfg.mouse_sensitivity + sx * g_cfg.stick_rate * dt;
    float dpitch = my * g_cfg.mouse_sensitivity + sy * g_cfg.stick_rate * dt;
    g_mouseDx = g_mouseDy = 0;

    // Binoculars: looking around slows with the magnification. Outside, the
    // camera stays put and only the look direction turns (no orbit).
    if (g_binoZoomCur > 1.f) { dyaw /= g_binoZoomCur; dpitch /= g_binoZoomCur; }
    float pitchMin = -89.f, pitchMax = g_mode == COCKPIT ? 89.f : 60.f;
    if (g_bino && g_mode == EXTERIOR) {
        g_binoYaw = Wrap180(g_binoYaw + dyaw);
        g_binoPitch = Clamp(g_binoPitch + dpitch, -80.f, 80.f);
    } else {
        t.yaw = Wrap180(t.yaw + dyaw);
        t.pitch = Clamp(t.pitch + dpitch, pitchMin, pitchMax);
    }

    // Translation. Keys stay held while Windows says they're down, so missed
    // or auto-repeat up/down events can't make movement stutter.
    for (int i = 0; i < MV_COUNT; ++i)
        if (g_moveKey[i] && !(GetAsyncKeyState(kMoveVk[i]) & 0x8000)) g_moveKey[i] = false;
    auto held = [](int i) { return (g_moveKey[i] || g_moveCmd[i]) ? 1.f : 0.f; };
    float want[3] = {held(MV_FWD) - held(MV_BACK), held(MV_RIGHT) - held(MV_LEFT), held(MV_UP) - held(MV_DOWN)};
    float ramp = g_cfg.move_accel > 0.f ? 1.f - expf(-g_cfg.move_accel * dt) : 1.f;
    for (int i = 0; i < 3; ++i) {
        g_moveVel[i] += (want[i] - g_moveVel[i]) * ramp;
        if (fabsf(g_moveVel[i]) < 0.001f && want[i] == 0.f) g_moveVel[i] = 0.f;
    }
    float fw = g_moveVel[0], rt = g_moveVel[1], up = g_moveVel[2];
    double yr = t.yaw * D2R;
    V3 fwdV = {sin(yr), 0, -cos(yr)}, rightV = {cos(yr), 0, sin(yr)};
    if (fw || rt || up) {
        V3 d;
        if (g_mode == COCKPIT) {
            d = (fwdV * fw + rightV * rt + V3{0, 1, 0} * up) * (g_cfg.move_speed * dt);
        } else {
            float speed = g_cfg.move_speed * fmaxf(5.f, t.dist) * 0.5f;
            d = (rightV * rt + V3{0, 1, 0} * up) * (speed * dt);
            t.dist = Clamp(t.dist - fw * speed * dt, 3.f, 3000.f);
        }
        t.x += (float)d.x; t.y += (float)d.y; t.z += (float)d.z;
    }

    // Exterior zoom: held commands and the mouse wheel. (Cockpit zoom is
    // X-Plane's own.)
    // With binoculars up, the wheel (and outside, = / -) sets their own
    // magnification instead, about 1.25x per notch.
    if (g_bino) {
        float zb = g_zoomCmd * dt * 1.2f + (float)g_wheel * 0.22f;
        g_wheel = 0;
        if (zb != 0.f) {
            g_binoZoom = Clamp(g_binoZoom * expf(zb), kBinoMin, kBinoMax);
            ShowBinoZoom();
        }
        return;
    }
    float z = g_zoomCmd * dt * 1.2f + (float)g_wheel * g_cfg.wheel_step;
    g_wheel = 0;
    if (z != 0.f && g_mode == EXTERIOR) t.dist = Clamp(t.dist * expf(-z), 3.f, 3000.f);
    // Cockpit: the wheel only gets here while right-dragging (the plain wheel
    // stays X-Plane's, so it can turn knobs). Finer steps, and well past
    // X-Plane's own zoom-out limit.
    if (z != 0.f && g_mode == COCKPIT) {
        t.zoom = Clamp(t.zoom * expf(z * 0.67f), 0.4f, 3.f);
        char text[32];
        snprintf(text, sizeof(text), "Zoom %.2fx", t.zoom);
        ShowToastText(text);
    }
}

static void Smooth(float dt) {
    const View& t = Current().live;
    float rate = g_transition ? g_cfg.smoothing : g_cfg.input_smoothing;
    float a = rate > 0.f ? 1.f - expf(-rate * dt) : 1.f;
    g_cur.x += (t.x - g_cur.x) * a;
    g_cur.y += (t.y - g_cur.y) * a;
    g_cur.z += (t.z - g_cur.z) * a;
    g_cur.yaw = Wrap180(g_cur.yaw + Wrap180(t.yaw - g_cur.yaw) * a);
    g_cur.pitch += (t.pitch - g_cur.pitch) * a;
    g_cur.dist += (t.dist - g_cur.dist) * a;
    g_cur.zoom += (t.zoom - g_cur.zoom) * a;
    if (g_transition && fabsf(Wrap180(t.yaw - g_cur.yaw)) < 0.5f && fabsf(t.pitch - g_cur.pitch) < 0.5f &&
        fabsf(t.x - g_cur.x) + fabsf(t.y - g_cur.y) + fabsf(t.z - g_cur.z) < 0.02f &&
        fabsf(t.dist - g_cur.dist) < 0.01f * t.dist + 0.05f)
        g_transition = false;

    float b = g_cfg.heading_follow > 0.f ? 1.f - expf(-g_cfg.heading_follow * dt) : 1.f;
    g_hdgSmooth = Wrap180(g_hdgSmooth + Wrap180(XPLMGetDataf(dr_psi) - g_hdgSmooth) * b);
    SmoothBinoculars(dt);
}

// ---------------------------------------------------------------------------
// Camera effects: G-force sway, turbulence jolts, runway rumble and touchdown
// bumps. They're an offset added on top of the rendered view each frame and
// never folded into it, so they can't make the view drift.

struct Spring { V3 x = {0, 0, 0}, v = {0, 0, 0}; };

// Damped spring pulled toward target, integrated in small steps.
static void StepSpring(Spring& s, V3 target, double w, double zeta, float dt) {
    int n = (int)ceilf(dt * 240.f);
    double h = dt / (n > 0 ? n : 1);
    for (int i = 0; i < n; ++i) {
        V3 a = (target - s.x) * (w * w) - s.v * (2.0 * zeta * w);
        s.v = s.v + a * h;
        s.x = s.x + s.v * h;
    }
}

static V3 ClampV(V3 v, double m) {
    return {fmax(-m, fmin(m, v.x)), fmax(-m, fmin(m, v.y)), fmax(-m, fmin(m, v.z))};
}

static Spring g_fxSway, g_fxJolt;    // slow G sway; quick jolts (turbulence, touchdown)
static V3 g_fxForceLP = {0, 0, 0};
static float g_fxRumbleT = 0.f, g_fxPrevVy = 0.f;
static bool g_fxWasOnGround = true;
static V3 g_fxHead = {0, 0, 0};      // cockpit head offset, aircraft axes (x right, y up, z aft), m
static float g_fxPitch = 0.f, g_fxYaw = 0.f;       // cockpit, degrees
static float g_fxExtPitch = 0.f, g_fxExtYaw = 0.f; // exterior, degrees

static void ResetEffects() {
    g_fxSway = g_fxJolt = Spring();
    g_fxForceLP = g_fxHead = {0, 0, 0};
    g_fxPitch = g_fxYaw = g_fxExtPitch = g_fxExtYaw = 0.f;
    g_fxWasOnGround = true;
}

static void UpdateEffects(float dt) {
    if (!g_cfg.effects || !g_running) { ResetEffects(); return; }
    if (XPLMGetDatai(dr_paused) || dt <= 0.f) return;   // hold still while paused

    // What the seat pushes the pilot with, in aircraft axes, less the usual 1 g.
    const double G = 9.81;
    V3 f = {XPLMGetDataf(dr_ax), XPLMGetDataf(dr_ay) + G, XPLMGetDataf(dr_az)};
    V3 b = RotZ(RotX(RotY(f, XPLMGetDataf(dr_psi) * D2R), -XPLMGetDataf(dr_the) * D2R), XPLMGetDataf(dr_phi) * D2R);
    b.y -= G;
    b = ClampV(b, 3.0 * G);          // ignore teleports and crashes

    // G sway: the head lags behind acceleration, sinks under G, leans in turns.
    V3 sway = ClampV({-0.004 * b.x, -0.004 * b.y, -0.005 * b.z}, 0.10) * g_cfg.fx_gforce;
    StepSpring(g_fxSway, sway, 5.0, 0.6, dt);

    // Turbulence: the quick part of the acceleration, airborne only (on the
    // ground the gear's own jitter would make it buzz).
    float lp = 1.f - expf(-3.f * dt);
    g_fxForceLP = g_fxForceLP + (b - g_fxForceLP) * lp;
    V3 hp = b - g_fxForceLP;
    bool onGround = XPLMGetDatai(dr_onGround) != 0;
    V3 jolt = onGround ? V3{0, 0, 0} : ClampV({-0.003 * hp.x, -0.004 * hp.y, -0.002 * hp.z}, 0.05) * g_cfg.fx_turbulence;

    // Touchdown: a downward kick scaled by the sink rate just before contact.
    if (onGround && !g_fxWasOnGround)
        g_fxJolt.v.y -= 0.12 * Clamp(-g_fxPrevVy, 0.f, 6.f) * g_cfg.fx_ground;
    g_fxWasOnGround = onGround;
    g_fxPrevVy = XPLMGetDataf(dr_vy);
    StepSpring(g_fxJolt, jolt, 14.0, 0.35, dt);

    // Runway rumble, growing with ground speed.
    float rumY = 0.f, rumP = 0.f;
    if (onGround) {
        g_fxRumbleT = fmodf(g_fxRumbleT + dt, 1000.f);
        float k = Clamp(XPLMGetDataf(dr_gs) / 35.f, 0.f, 1.f) * g_cfg.fx_ground;
        float t = g_fxRumbleT * 2.f * (float)M_PI;
        rumY = 0.0015f * k * (0.5f * sinf(t * 7.3f) + 0.3f * sinf(t * 11.9f + 1.3f) + 0.2f * sinf(t * 17.1f + 2.1f));
        rumP = 0.05f * k * (0.5f * sinf(t * 6.1f + 0.7f) + 0.5f * sinf(t * 13.7f + 2.9f));
    }

    g_fxHead = g_fxSway.x + g_fxJolt.x + V3{0, rumY, 0};
    g_fxPitch = (float)(20.0 * g_fxSway.x.y + 30.0 * g_fxJolt.x.y) + rumP;
    g_fxYaw = (float)(15.0 * g_fxJolt.x.x);
    // Outside, centimetres don't show, so jolts and rumble become small angles.
    g_fxExtPitch = g_cfg.effects_exterior ? (float)(30.0 * g_fxJolt.x.y) + 2.f * rumP : 0.f;
    g_fxExtYaw = g_cfg.effects_exterior ? (float)(20.0 * g_fxJolt.x.x) : 0.f;
}

// ---------------------------------------------------------------------------
// Cinematic camera: a shuffled run of exterior shots of different parts of the
// aircraft, each drifting slowly for a few seconds, with a dip to black between
// them. Now and then, when the aircraft is moving, a flyby: the camera waits
// beside the flight path ahead and tracks the aircraft as it passes.

// Pivot in body axes (x right, y up, z aft) as fractions of the half-span (x),
// the aircraft radius (y) and the half-length (z); eye = pivot on the eye point.
// The camera drifts from (yaw0, pitch0, dist0) to (yaw1, pitch1, dist1) over
// the shot: yaw relative to heading (0 = behind, 90 = left side, 180 = in
// front), pitch absolute (negative = looking down), dist in aircraft radii.
// Each shot is randomly mirrored left/right.
struct Shot {
    float px, py, pz;
    int   eye;
    float yaw0, yaw1, pitch0, pitch1, dist0, dist1;
};
static const Shot kShots[] = {
    // Close-ups aim part-way between the feature and the middle, so the rest
    // of the aircraft stays in frame.
    {0.f,    0.f,  -0.6f,  0,  150.f,  165.f,  -4.f,  -7.f, 1.00f, 0.85f},  // nose, front quarter, pushing in
    {0.f,    0.f,   0.f,   1, -155.f, -140.f, -14.f,  -9.f, 0.75f, 0.70f},  // flight deck windows
    {-0.25f, -0.05f, -0.1f, 0, 125.f,  142.f,   2.f,  -1.f, 0.80f, 0.70f},  // engine / inner wing from ahead
    {-0.4f,  0.f,   0.05f, 0,   30.f,   50.f, -22.f, -16.f, 0.75f, 0.80f},  // over the wing
    {0.6f,   0.f,   0.1f,  0,  -35.f,  -15.f,  -4.f,  -1.f, 0.60f, 0.70f},  // wingtip
    {0.f,    0.15f, 0.6f,  0,   35.f,   15.f,  -8.f, -14.f, 0.70f, 0.75f},  // tail
    {0.f,   -0.1f, -0.1f,  0, -115.f,  -95.f,  14.f,  10.f, 0.95f, 0.90f},  // belly and gear from below (airborne only)
    {0.f,    0.f,   0.f,   0,   82.f,   98.f,  -2.f,  -3.f, 1.70f, 1.60f},  // side profile dolly
    {0.f,    0.f,   0.f,   0,   12.f,   -8.f, -26.f, -18.f, 2.00f, 2.40f},  // high rear, pulling back
    {0.f,    0.f,   0.f,   0,  172.f,  158.f,   7.f,   3.f, 1.90f, 1.60f},  // low front, looking up
    {0.f,    0.05f, -0.3f, 0,   10.f,   16.f, -11.f,  -9.f, 1.35f, 1.25f},  // along the fuselage from the tail
};
static const int kShotCount = (int)(sizeof(kShots) / sizeof(kShots[0]));

static int    g_cineReturn = EXTERIOR; // category to go back to afterwards
static int    g_cineShot = 0;          // index into kShots, -1 = flyby
static float  g_cineSide = 1.f;        // +1 / -1: shot mirrored left/right
static double g_cineT = 0, g_cineLen = 7; // time into the shot (paused time excluded) and its length
static float  g_cineHdg = 0.f;         // smoothed heading the drift shots are framed from
static bool   g_cineGround = false;    // aircraft was on the ground when the shot started
static int    g_cineBag[kShotCount], g_cineBagPos = kShotCount, g_cineSinceFlyby = 0;
static double g_flyLat, g_flyLon, g_flyAlt; // flyby camera; world coords so a local-origin shift can't move it
static float  g_fade = 0.f;            // black overlay opacity, 0..1

static float Rand01() {
    static uint32_t s = 0;
    if (!s) s = (uint32_t)GetTickCount64() | 1u;
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return (s >> 8) * (1.f / 16777216.f);
}

// Half-span, half-length and a radius used to scale distances. The radius has
// a floor so close-ups of light aircraft don't end up a couple of metres away.
static void AircraftSize(float* S, float* L, float* R) {
    *S = XPLMGetDataf(dr_sizeX);
    *L = XPLMGetDataf(dr_sizeZ);
    if (*S <= 0.f) *S = 10.f;
    if (*L <= 0.f) *L = 10.f;
    *R = Clamp(fmaxf(*S, *L), 12.f, 45.f);
}

static bool FlybyPossible() { return XPLMGetDataf(dr_gs) > 25.f; }

// Places the camera beside the point the aircraft will reach in a few
// seconds, offset sideways (more at higher speed, so it doesn't whip past),
// usually a little below so it looks up as the aircraft goes over.
static void StartFlyby() {
    const double lead = 4.5;           // seconds until the aircraft is abeam
    double vx = XPLMGetDataf(dr_vx), vy = XPLMGetDataf(dr_vy), vz = XPLMGetDataf(dr_vz);
    double hs = fmax(sqrt(vx * vx + vz * vz), 1.0);
    float S, L, R;
    AircraftSize(&S, &L, &R);
    double off = Clamp(fmaxf(1.4f * S, 0.6f * (float)hs), 15.f, 150.f) * (Rand01() < 0.5f ? -1.0 : 1.0);
    double up = Rand01() < 0.65f ? -0.25 * fabs(off) : 0.15 * fabs(off);
    V3 acf = {XPLMGetDatad(dr_lx), XPLMGetDatad(dr_ly), XPLMGetDatad(dr_lz)};
    V3 pos = acf + V3{vx, vy, vz} * lead + V3{-vz / hs, 0, vx / hs} * off + V3{0, up, 0};
    XPLMProbeInfo_t info = {sizeof(info)};
    if (XPLMProbeTerrainXYZ(g_probe, (float)pos.x, (float)pos.y, (float)pos.z, &info) == xplm_ProbeHitTerrain &&
        pos.y < info.locationY + 2.0)
        pos.y = info.locationY + 2.0;
    XPLMLocalToWorld(pos.x, pos.y, pos.z, &g_flyLat, &g_flyLon, &g_flyAlt);
    g_cineShot = -1;
    g_cineSinceFlyby = 0;
    g_cineLen = lead + 4.5;
}

static void StartShot(bool allowFlyby) {
    g_cineT = 0;
    g_cineHdg = XPLMGetDataf(dr_psi);
    if (allowFlyby && g_cfg.cinematic_flyby && FlybyPossible() &&
        (g_cineSinceFlyby >= 5 || (g_cineSinceFlyby >= 3 && Rand01() < 0.5f))) {
        StartFlyby();
        return;
    }
    ++g_cineSinceFlyby;
    // On the ground, shots from below would put the camera in the grass.
    g_cineGround = XPLMGetDatai(dr_onGround) != 0;
    for (int tries = 0; tries <= kShotCount; ++tries) {
        if (g_cineBagPos >= kShotCount) {   // reshuffle, never repeating the last shot straight away
            int last = g_cineShot;
            for (int i = 0; i < kShotCount; ++i) g_cineBag[i] = i;
            for (int i = kShotCount - 1; i > 0; --i) {
                int j = (int)(Rand01() * (i + 1));
                int tmp = g_cineBag[i]; g_cineBag[i] = g_cineBag[j]; g_cineBag[j] = tmp;
            }
            if (g_cineBag[0] == last) { g_cineBag[0] = g_cineBag[1]; g_cineBag[1] = last; }
            g_cineBagPos = 0;
        }
        g_cineShot = g_cineBag[g_cineBagPos++];
        if (!g_cineGround || kShots[g_cineShot].pitch0 < 10.f) break;
    }
    g_cineSide = Rand01() < 0.5f ? -1.f : 1.f;
    g_cineLen = fmaxf(g_cfg.cinematic_shot_time, 2.f) * (0.85f + 0.3f * Rand01());
}

static void StartCinematic() {
    if (g_cine) return;
    g_cineReturn = g_mode;
    EnterMode(EXTERIOR);
    ApplyViewType();
    g_cine = true;
    g_cineBagPos = kShotCount;
    g_cineSinceFlyby = 0;
    StartShot(false);
    ShowToastText("Cinematic camera");
}

static void StopCinematic(bool restoreView) {
    if (!g_cine) return;
    g_cine = false;
    g_fade = 0.f;
    g_mouseDx = g_mouseDy = g_wheel = 0;
    g_hdgSmooth = XPLMGetDataf(dr_psi);   // Smooth() didn't run meanwhile
    if (restoreView) SetMode(g_cineReturn);
}

static void CinematicCamera(XPLMCameraPosition_t* out, float dt) {
    g_mouseDx = g_mouseDy = g_wheel = 0;   // no manual orbit while the director has the camera
    if (!XPLMGetDatai(dr_paused)) g_cineT += dt;
    if (g_cineT >= g_cineLen) StartShot(true);

    float fade = g_cfg.cinematic_fade;
    g_fade = fade > 0.f ? Clamp(1.f - (float)fmin(g_cineT, g_cineLen - g_cineT) / fade, 0.f, 1.f) : 0.f;

    float S, L, R;
    AircraftSize(&S, &L, &R);
    V3 acf = {XPLMGetDatad(dr_lx), XPLMGetDatad(dr_ly), XPLMGetDatad(dr_lz)};
    V3 pos, look;
    float zoom = 1.f, shakeYaw = 0.f, shakePitch = 0.f;
    if (g_cineShot < 0) {
        double x, y, z;
        XPLMWorldToLocal(g_flyLat, g_flyLon, g_flyAlt, &x, &y, &z);
        pos = {x, y, z};
        look = acf;
        V3 d = acf - pos;
        double r = sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        if (r > 10000.0) g_cineT = g_cineLen;   // teleported or turned away: next shot
        zoom = Clamp((float)r / (3.f * R), 1.f, 4.f); // keep it framed while it's far off
    } else {
        const Shot& s = kShots[g_cineShot];
        float k = (float)Clamp((float)(g_cineT / g_cineLen), 0.f, 1.f);
        float b = g_cfg.heading_follow > 0.f ? 1.f - expf(-g_cfg.heading_follow * dt) : 1.f;
        g_cineHdg = Wrap180(g_cineHdg + Wrap180(XPLMGetDataf(dr_psi) - g_cineHdg) * b);
        V3 off = s.eye ? V3{0, XPLMGetDataf(dr_peY), XPLMGetDataf(dr_peZ)}
                       : V3{s.px * S * g_cineSide, s.py * R, s.pz * L};
        V3 pivot = acf + Orient(off, XPLMGetDataf(dr_psi), XPLMGetDataf(dr_the), XPLMGetDataf(dr_phi));
        double yaw = g_cineSide * (s.yaw0 + (s.yaw1 - s.yaw0) * k);
        double pitch = s.pitch0 + (s.pitch1 - s.pitch0) * k;
        if (g_cineGround) pitch = fmin(pitch, -3.0);   // keep low shots out of the grass
        double dist = fmax(6.0,(s.dist0 + (s.dist1 - s.dist0) * k) * R * Clamp(g_cfg.cinematic_distance, 0.3f, 5.f));
        pos = pivot - Orient({0, 0, -1}, g_cineHdg + yaw, pitch, 0) * dist;
        look = pivot;
        shakeYaw = g_fxExtYaw;
        shakePitch = g_fxExtPitch;
    }

    XPLMProbeInfo_t info = {sizeof(info)};
    if (XPLMProbeTerrainXYZ(g_probe, (float)pos.x, (float)pos.y, (float)pos.z, &info) == xplm_ProbeHitTerrain &&
        pos.y < info.locationY + 1.5)
        pos.y = info.locationY + 1.5;

    V3 d = look - pos;
    out->x = (float)pos.x;
    out->y = (float)pos.y;
    out->z = (float)pos.z;
    out->heading = (float)(atan2(d.x, -d.z) * R2D) + shakeYaw;
    out->pitch = (float)(atan2(d.y, sqrt(d.x * d.x + d.z * d.z)) * R2D) + shakePitch;
    out->roll = 0.f;
    out->zoom = zoom;
}

// Black dip between cinematic shots, drawn under X-Plane's own windows.
static int FadeDrawCB(XPLMDrawingPhase, int, void*) {
    if (!g_cine || g_fade <= 0.001f) return 1;
    XPLMSetGraphicsState(0, 0, 0, 0, 1, 0, 0);
    glColor4f(0.f, 0.f, 0.f, g_fade);
    glBegin(GL_QUADS);   // oversized: 2D coordinates may be pixels or UI units
    glVertex2f(-20000.f, -20000.f);
    glVertex2f(20000.f, -20000.f);
    glVertex2f(20000.f, 20000.f);
    glVertex2f(-20000.f, 20000.f);
    glEnd();
    return 1;
}

static int CameraCB(XPLMCameraPosition_t* out, int losingControl, void*) {
    if (losingControl || !out) {
        StopCinematic(false);
        g_inControl = false;
        ClearHeldInput();
        Log("lost camera control");
        return 0;
    }
    if (g_mode != EXTERIOR) { g_inControl = false; return 0; }
    static double last = Now();
    double now = Now();
    float dt = (float)fmin(now - last, 0.1);
    last = now;

    if (g_cine) {
        CinematicCamera(out, dt);
        return 1;
    }

    ProcessInput(dt);
    Smooth(dt);

    V3 acf = {XPLMGetDatad(dr_lx), XPLMGetDatad(dr_ly), XPLMGetDatad(dr_lz)};
    V3 pivot = acf + RotY({g_cur.x, g_cur.y, g_cur.z}, -g_hdgSmooth * D2R);
    double h = g_hdgSmooth + g_cur.yaw, p = g_cur.pitch;
    V3 pos = pivot - Orient({0, 0, -1}, h, p, 0) * g_cur.dist;

    // Keep the camera above ground, still looking at the pivot.
    XPLMProbeInfo_t info = {sizeof(info)};
    if (XPLMProbeTerrainXYZ(g_probe, (float)pos.x, (float)pos.y, (float)pos.z, &info) == xplm_ProbeHitTerrain &&
        pos.y < info.locationY + 1.0) {
        pos.y = info.locationY + 1.0;
        V3 d = pivot - pos;
        p = atan2(d.y, sqrt(d.x * d.x + d.z * d.z)) * R2D;
    }

    out->x = (float)pos.x;
    out->y = (float)pos.y;
    out->z = (float)pos.z;
    out->heading = (float)h + g_fxExtYaw + g_binoYawCur;
    out->pitch = Clamp((float)p + g_fxExtPitch + g_binoPitchCur, -89.f, 89.f);
    out->roll = 0.f;
    out->zoom = g_binoZoomCur;
    g_viewHdg = out->heading;
    return 1;
}

// Cockpit views drive the pilot's head, which is relative to the aircraft.
// A world-space camera can't be used here: at tens of km from X-Plane's local
// origin a float position moves in ~4 mm steps, which makes the cockpit shimmer.
// The head is written outright every frame; anything X-Plane does to it in
// between is overwritten rather than accumulated (that made the view drift).
static void UpdateCockpit(float dt) {
    ProcessInput(dt);
    Smooth(dt);

    XPLMSetDataf(dr_headX, g_cur.x + (float)g_fxHead.x);
    XPLMSetDataf(dr_headY, g_cur.y + (float)g_fxHead.y);
    XPLMSetDataf(dr_headZ, g_cur.z + (float)g_fxHead.z);
    XPLMSetDataf(dr_headPsi, g_cur.yaw + g_fxYaw);
    XPLMSetDataf(dr_headThe, g_cur.pitch + g_fxPitch);
    ApplyCockpitFov();
    g_viewHdg = XPLMGetDataf(dr_psi) + g_cur.yaw + g_fxYaw;
}

// ---------------------------------------------------------------------------
// Mouse: subclass X-Plane's main window so a right-drag never reaches X-Plane
// (it would otherwise start its own pan and warp the cursor).

static HWND g_hwnd = nullptr;
static WNDPROC g_origProc = nullptr;

static bool Active() { return g_running; }
static bool g_mEaten = false;        // swallow the middle-button release of a binocular toggle

static LRESULT CALLBACK MainWndProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_RBUTTONDOWN:
    case WM_RBUTTONDBLCLK:
        if (Active() && g_cfg.mouse_pan) {
            g_rDrag = true;
            g_lastPt = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            SetCapture(h);
            return 0;
        }
        break;
    case WM_RBUTTONUP:
        if (g_rDrag) {
            g_rDrag = false;
            ReleaseCapture();
            return 0;
        }
        break;
    case WM_MOUSEMOVE:
        if (g_rDrag) {
            POINT pt = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            g_mouseDx += pt.x - g_lastPt.x;
            g_mouseDy += pt.y - g_lastPt.y;
            g_lastPt = pt;
            return 0;
        }
        break;
    case WM_MBUTTONDOWN:
    case WM_MBUTTONDBLCLK:
        // Middle click while right-dragging raises / lowers the binoculars.
        if (g_rDrag && Active() && !g_cine) {
            g_binoToggleReq = true;
            g_mEaten = true;
            return 0;
        }
        break;
    case WM_MBUTTONUP:
        if (g_mEaten) { g_mEaten = false; return 0; }
        break;
    case WM_MOUSEWHEEL:
        // Binoculars take the wheel for their own magnification, everywhere.
        if (Active() && g_bino) {
            g_wheel += GET_WHEEL_DELTA_WPARAM(w) / (double)WHEEL_DELTA;
            return 0;
        }
        // Right-drag + wheel in the cockpit is OrbitView's own zoom.
        if (Active() && g_rDrag && g_mode == COCKPIT && !g_cine) {
            g_wheel += GET_WHEEL_DELTA_WPARAM(w) / (double)WHEEL_DELTA;
            return 0;
        }
        // Otherwise in the cockpit X-Plane's own wheel zoom (which leaves
        // knobs alone) applies, so only exterior views use the wheel here.
        if (Active() && g_mode == EXTERIOR) {
            g_wheel += GET_WHEEL_DELTA_WPARAM(w) / (double)WHEEL_DELTA;
            if (g_rDrag) return 0;
            // Otherwise pass it on too, so UI windows keep scrolling.
        }
        break;
    case WM_CAPTURECHANGED:
        if ((HWND)l != h) g_rDrag = false;
        break;
    case WM_KILLFOCUS:
        ClearHeldInput();
        break;
    }
    return CallWindowProcW(g_origProc, h, msg, w, l);
}

static BOOL CALLBACK FindMainWindow(HWND h, LPARAM lp) {
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid != GetCurrentProcessId() || !IsWindowVisible(h) || GetWindow(h, GW_OWNER)) return TRUE;
    RECT rc;
    GetClientRect(h, &rc);
    auto* best = reinterpret_cast<std::pair<HWND, long>*>(lp);
    long area = (rc.right - rc.left) * (rc.bottom - rc.top);
    if (area > best->second) *best = {h, area};
    return TRUE;
}

static void HookMainWindow() {
    if (g_hwnd && IsWindow(g_hwnd)) return;
    std::pair<HWND, long> best = {nullptr, 0};
    EnumWindows(FindMainWindow, reinterpret_cast<LPARAM>(&best));
    if (!best.first) return;
    g_hwnd = best.first;
    g_origProc = (WNDPROC)SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, (LONG_PTR)MainWndProc);
    Log("hooked main window %p", (void*)g_hwnd);
}

static void UnhookMainWindow() {
    if (g_hwnd && IsWindow(g_hwnd) && (WNDPROC)GetWindowLongPtrW(g_hwnd, GWLP_WNDPROC) == MainWndProc)
        SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, (LONG_PTR)g_origProc);
    if (g_rDrag) ReleaseCapture();
    g_hwnd = nullptr;
    g_origProc = nullptr;
    g_rDrag = false;
}

// ---------------------------------------------------------------------------
// Keyboard

static int MoveIndexForKey(unsigned char vk) {
    switch (vk) {
    case XPLM_VK_W: return MV_FWD;
    case XPLM_VK_S: return MV_BACK;
    case XPLM_VK_A: return MV_LEFT;
    case XPLM_VK_D: return MV_RIGHT;
    case XPLM_VK_E: return MV_UP;
    case XPLM_VK_Q: return MV_DOWN;
    }
    return -1;
}

// Return 0 to consume the key. Registered after windows, so keys typed into a
// focused text field never get here.
static int KeySniffer(char, XPLMKeyFlags flags, char vkey, void*) {
    if (!Active() || !g_cfg.keyboard) return 1;
    unsigned char vk = (unsigned char)vkey;
    int mods = flags & (xplm_ShiftFlag | xplm_ControlFlag | xplm_OptionAltFlag);
    bool down = (flags & xplm_DownFlag) != 0, up = (flags & xplm_UpFlag) != 0;

    int mv = MoveIndexForKey(vk);
    if (mv >= 0) {
        if (up && g_moveKey[mv]) { g_moveKey[mv] = false; return 0; }
        if (mods == xplm_ShiftFlag) {
            if (down) g_moveKey[mv] = true;
            return 0;
        }
    }
    if (vk == XPLM_VK_SPACE && (mods == xplm_ShiftFlag || mods == (xplm_ShiftFlag | xplm_ControlFlag))) {
        if (down) {
            if (mods == xplm_ShiftFlag) ResetView();
            else AddView(nullptr);
        }
        return 0;
    }
    if (mods == 0 && vk == XPLM_VK_F12) {
        if (down) {
            if (g_cine) StopCinematic(true);
            else StartCinematic();
        }
        return 0;
    }
    if (mods == 0 && (vk == XPLM_VK_A || vk == XPLM_VK_D || vk == XPLM_VK_END)) {
        if (down) {
            if (g_cine) {   // in cinematic: A / D skip a shot, End leaves
                if (vk == XPLM_VK_END) StopCinematic(true);
                else StartShot(true);
            } else if (vk == XPLM_VK_END) SetMode(g_mode == COCKPIT ? EXTERIOR : COCKPIT);
            else StepView(vk == XPLM_VK_D ? 1 : -1);
        }
        return 0;
    }
    return 1;
}

// ---------------------------------------------------------------------------
// OrbitView window (XPWidgets), two pages: the views in the current category
// with add / save / rename / delete / reset, and Preferences.

enum { PAGE_VIEWS, PAGE_PREFS };
static XPWidgetID g_win = nullptr, g_tabs[2], g_pages[2];
static XPWidgetID g_catLabel, g_switchBtn, g_rows[kMaxViews], g_nameField;
static XPWidgetID g_btnAdd, g_btnSave, g_btnRename, g_btnDelete, g_btnReset;
static XPWidgetID g_chkEffects, g_chkExterior;
static const int kFxCount = 3;
static const char* kFxLabel[kFxCount] = {"G-force sway", "Turbulence", "Ground, touchdown"};
static XPWidgetID g_fxSlider[kFxCount], g_fxValue[kFxCount];
static int g_page = PAGE_VIEWS;

static float& FxStrength(int i) {
    return i == 0 ? g_cfg.fx_gforce : (i == 1 ? g_cfg.fx_turbulence : g_cfg.fx_ground);
}

static void RefreshViewWindow() {
    if (!g_win || !XPIsWidgetVisible(g_win) || g_page != PAGE_VIEWS || !g_viewsReady) return;
    char buf[64];
    snprintf(buf, sizeof(buf), "%s views", kCategory[g_mode]);
    XPSetWidgetDescriptor(g_catLabel, buf);
    XPSetWidgetDescriptor(g_switchBtn, g_mode == COCKPIT ? "Go exterior" : "Go cockpit");
    const std::vector<Preset>& list = g_presets[g_mode];
    for (int i = 0; i < kMaxViews; ++i) {
        if (i < (int)list.size()) {
            snprintf(buf, sizeof(buf), i == g_idx[g_mode] ? "> %s <" : "%s", list[i].name);
            XPSetWidgetDescriptor(g_rows[i], buf);
            XPShowWidget(g_rows[i]);
        } else {
            XPHideWidget(g_rows[i]);
        }
    }
}

static void RefreshFxValue(int i) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", (int)lroundf(FxStrength(i) * 100.f));
    XPSetWidgetDescriptor(g_fxValue[i], buf);
}

static void RefreshPrefsPage() {
    if (!g_win) return;
    XPSetWidgetProperty(g_chkEffects, xpProperty_ButtonState, g_cfg.effects != 0);
    XPSetWidgetProperty(g_chkExterior, xpProperty_ButtonState, g_cfg.effects_exterior != 0);
    for (int i = 0; i < kFxCount; ++i) {
        XPSetWidgetProperty(g_fxSlider[i], xpProperty_ScrollBarSliderPosition, lroundf(FxStrength(i) * 100.f));
        RefreshFxValue(i);
    }
}

static void ShowPage(int p) {
    g_page = p;
    for (int i = 0; i < 2; ++i) {
        if (i == p) XPShowWidget(g_pages[i]); else XPHideWidget(g_pages[i]);
        const char* name = i == PAGE_VIEWS ? "Views" : "Preferences";
        char buf[32];
        snprintf(buf, sizeof(buf), i == p ? "> %s <" : "%s", name);
        XPSetWidgetDescriptor(g_tabs[i], buf);
    }
    if (p == PAGE_PREFS) RefreshPrefsPage();
    RefreshViewWindow();
}

// Preferences are written to config.ini a moment after the last change, so
// dragging a slider doesn't rewrite the file every frame.
static double g_prefsDirtyAt = -1;
static void PrefsChanged() { g_prefsDirtyAt = Now(); }

static void NameFieldText(char* out, int size) {
    XPGetWidgetDescriptor(g_nameField, out, size - 1);
    out[size - 1] = 0;
}

static int ViewWindowCB(XPWidgetMessage msg, XPWidgetID, intptr_t p1, intptr_t p2) {
    if (msg == xpMessage_CloseButtonPushed) {
        XPHideWidget(g_win);
        return 1;
    }
    if (msg == xpMsg_ButtonStateChanged) {
        XPWidgetID box = (XPWidgetID)p1;
        if (box == g_chkEffects) {
            g_cfg.effects = p2 ? 1 : 0;
            ShowToastText(p2 ? "Camera effects on" : "Camera effects off");
        } else if (box == g_chkExterior) {
            g_cfg.effects_exterior = p2 ? 1 : 0;
        } else {
            return 0;
        }
        PrefsChanged();
        XPSetKeyboardFocus(0);
        return 1;
    }
    if (msg == xpMsg_ScrollBarSliderPositionChanged) {
        for (int i = 0; i < kFxCount; ++i)
            if ((XPWidgetID)p1 == g_fxSlider[i]) {
                FxStrength(i) = XPGetWidgetProperty(g_fxSlider[i], xpProperty_ScrollBarSliderPosition, nullptr) / 100.f;
                RefreshFxValue(i);
                PrefsChanged();
                return 1;
            }
        return 0;
    }
    if (msg != xpMsg_PushButtonPressed) return 0;
    XPWidgetID btn = (XPWidgetID)p1;
    if (btn == g_tabs[PAGE_VIEWS] || btn == g_tabs[PAGE_PREFS]) {
        ShowPage(btn == g_tabs[PAGE_VIEWS] ? PAGE_VIEWS : PAGE_PREFS);
        XPSetKeyboardFocus(0);
        return 1;
    }
    if (!g_viewsReady) return 1;
    StopCinematic(false);
    char name[32];
    NameFieldText(name, sizeof(name));
    if (btn == g_switchBtn) SetMode(g_mode == COCKPIT ? EXTERIOR : COCKPIT);
    else if (btn == g_btnAdd) AddView(name);
    else if (btn == g_btnSave) SaveCurrentView();
    else if (btn == g_btnRename) RenameCurrentView(name);
    else if (btn == g_btnDelete) DeleteCurrentView();
    else if (btn == g_btnReset) ResetView();
    else {
        for (int i = 0; i < kMaxViews; ++i)
            if (btn == g_rows[i] && i < (int)g_presets[g_mode].size()) {
                SelectView(i);
                XPSetWidgetDescriptor(g_nameField, Current().name);
            }
    }
    XPSetKeyboardFocus(0);   // give A/D etc. back to the sim
    RefreshViewWindow();
    return 1;
}

static XPWidgetID MakeButton(XPWidgetID parent, int l, int t, int r, const char* label) {
    XPWidgetID w = XPCreateWidget(l, t, r, t - 20, 1, label, 0, parent, xpWidgetClass_Button);
    XPSetWidgetProperty(w, xpProperty_ButtonType, xpPushButton);
    return w;
}

static XPWidgetID MakeCaption(XPWidgetID parent, int l, int t, int r, const char* text) {
    return XPCreateWidget(l, t, r, t - 20, 1, text, 0, parent, xpWidgetClass_Caption);
}

static XPWidgetID MakeCheckBox(XPWidgetID parent, int l, int t, int r, const char* label) {
    XPWidgetID w = XPCreateWidget(l, t, l + 20, t - 20, 1, "", 0, parent, xpWidgetClass_Button);
    XPSetWidgetProperty(w, xpProperty_ButtonType, xpRadioButton);
    XPSetWidgetProperty(w, xpProperty_ButtonBehavior, xpButtonBehaviorCheckBox);
    MakeCaption(parent, l + 24, t, r, label);
    return w;
}

// Pages are invisible containers; hiding one hides everything on it.
static int PageCB(XPWidgetMessage, XPWidgetID, intptr_t, intptr_t) { return 0; }

static void CreateViewWindow() {
    int sl, st, sr, sb;
    XPLMGetScreenBoundsGlobal(&sl, &st, &sr, &sb);
    const int W = 300, H = 76 + kMaxViews * 22 + 90;
    int x = sl + 60, y = st - 100;
    g_win = XPCreateWidget(x, y, x + W, y - H, 0, "OrbitView", 1, nullptr, xpWidgetClass_MainWindow);
    XPSetWidgetProperty(g_win, xpProperty_MainWindowHasCloseBoxes, 1);
    XPAddWidgetCallback(g_win, ViewWindowCB);

    int half = (W - 30) / 2, third = (W - 40) / 3;
    g_tabs[PAGE_VIEWS] = MakeButton(g_win, x + 10, y - 25, x + 10 + half, "");
    g_tabs[PAGE_PREFS] = MakeButton(g_win, x + 20 + half, y - 25, x + W - 10, "");
    int top = y - 51;
    for (int i = 0; i < 2; ++i) g_pages[i] = XPCreateCustomWidget(x, top, x + W, y - H, 0, "", 0, g_win, PageCB);

    // Views page.
    XPWidgetID pg = g_pages[PAGE_VIEWS];
    int cy = top;
    g_catLabel = MakeCaption(pg, x + 10, cy, x + 180, "");
    g_switchBtn = MakeButton(pg, x + 185, cy, x + W - 10, "");
    cy -= 28;
    for (int i = 0; i < kMaxViews; ++i, cy -= 22) g_rows[i] = MakeButton(pg, x + 10, cy, x + W - 10, "");
    cy -= 6;
    MakeCaption(pg, x + 10, cy, x + 55, "Name:");
    g_nameField = XPCreateWidget(x + 55, cy, x + W - 10, cy - 20, 1, "", 0, pg, xpWidgetClass_TextField);
    XPSetWidgetProperty(g_nameField, xpProperty_TextFieldType, xpTextEntryField);
    XPSetWidgetProperty(g_nameField, xpProperty_MaxCharacters, 31);
    cy -= 28;
    g_btnAdd = MakeButton(pg, x + 10, cy, x + 10 + half, "Add as new view");
    g_btnSave = MakeButton(pg, x + 20 + half, cy, x + W - 10, "Save to selected");
    cy -= 26;
    g_btnRename = MakeButton(pg, x + 10, cy, x + 10 + third, "Rename");
    g_btnDelete = MakeButton(pg, x + 20 + third, cy, x + 20 + 2 * third, "Delete");
    g_btnReset = MakeButton(pg, x + 30 + 2 * third, cy, x + W - 10, "Reset");

    // Preferences page.
    pg = g_pages[PAGE_PREFS];
    cy = top;
    g_chkEffects = MakeCheckBox(pg, x + 10, cy, x + W - 10, "Camera effects");
    cy -= 24;
    g_chkExterior = MakeCheckBox(pg, x + 10, cy, x + W - 10, "Also shake exterior views");
    cy -= 34;
    MakeCaption(pg, x + 10, cy, x + W - 10, "Strength (100% = default):");
    cy -= 26;
    for (int i = 0; i < kFxCount; ++i, cy -= 26) {
        MakeCaption(pg, x + 10, cy, x + 125, kFxLabel[i]);
        g_fxSlider[i] = XPCreateWidget(x + 130, cy - 1, x + W - 55, cy - 19, 1, "", 0, pg, xpWidgetClass_ScrollBar);
        XPSetWidgetProperty(g_fxSlider[i], xpProperty_ScrollBarType, xpScrollBarTypeSlider);
        XPSetWidgetProperty(g_fxSlider[i], xpProperty_ScrollBarMin, 0);
        XPSetWidgetProperty(g_fxSlider[i], xpProperty_ScrollBarMax, 200);
        XPSetWidgetProperty(g_fxSlider[i], xpProperty_ScrollBarPageAmount, 10);
        g_fxValue[i] = MakeCaption(pg, x + W - 50, cy, x + W - 10, "");
    }
    cy -= 10;
    MakeCaption(pg, x + 10, cy, x + W - 10, "G-force: head sways with acceleration,");
    MakeCaption(pg, x + 10, cy - 16, x + W - 10, "pull-ups and turns.");
    MakeCaption(pg, x + 10, cy - 36, x + W - 10, "Turbulence: jolts from bumpy air.");
    MakeCaption(pg, x + 10, cy - 56, x + W - 10, "Ground: runway rumble and a bump");
    MakeCaption(pg, x + 10, cy - 72, x + W - 10, "on touchdown that grows with sink rate.");
    MakeCaption(pg, x + 10, cy - 100, x + W - 10, "Saved to config.ini automatically.");
}

static void OpenWindow(int page) {
    if (!g_win) CreateViewWindow();
    XPShowWidget(g_win);
    XPBringRootWidgetToFront(g_win);
    ShowPage(page);
}

static void ToggleViewWindow() {
    if (g_win && XPIsWidgetVisible(g_win)) XPHideWidget(g_win);
    else OpenWindow(g_page);
}

// ---------------------------------------------------------------------------
// Commands

enum {
    CMD_NEXT, CMD_PREV, CMD_TOGGLE_INOUT,
    CMD_MOVE_FWD, CMD_MOVE_BACK, CMD_MOVE_LEFT, CMD_MOVE_RIGHT, CMD_MOVE_UP, CMD_MOVE_DOWN,
    CMD_ZOOM_IN, CMD_ZOOM_OUT, CMD_RESET, CMD_ADD, CMD_SAVE, CMD_DELETE,
    CMD_WINDOW, CMD_TOGGLE_ENABLED, CMD_CINEMATIC, CMD_FLYBY, CMD_BINOCULARS, CMD_COUNT
};
static const char* kCmds[CMD_COUNT][2] = {
    {"orbitview/next_view", "OrbitView: next view"},
    {"orbitview/prev_view", "OrbitView: previous view"},
    {"orbitview/toggle_inside_outside", "OrbitView: toggle cockpit / exterior"},
    {"orbitview/move_forward", "OrbitView: move forward (hold)"},
    {"orbitview/move_back", "OrbitView: move back (hold)"},
    {"orbitview/move_left", "OrbitView: move left (hold)"},
    {"orbitview/move_right", "OrbitView: move right (hold)"},
    {"orbitview/move_up", "OrbitView: move up (hold)"},
    {"orbitview/move_down", "OrbitView: move down (hold)"},
    {"orbitview/zoom_in", "OrbitView: zoom in (hold)"},
    {"orbitview/zoom_out", "OrbitView: zoom out (hold)"},
    {"orbitview/reset_view", "OrbitView: reset current view to its saved position"},
    {"orbitview/add_view", "OrbitView: add current camera as a new view"},
    {"orbitview/save_view", "OrbitView: save current camera into the selected view"},
    {"orbitview/delete_view", "OrbitView: delete the selected view"},
    {"orbitview/toggle_window", "OrbitView: show / hide the views window"},
    {"orbitview/toggle", "OrbitView: enable / disable"},
    {"orbitview/cinematic", "OrbitView: cinematic camera on / off"},
    {"orbitview/cinematic_flyby", "OrbitView: cinematic flyby now"},
    {"orbitview/binoculars", "OrbitView: raise / lower binoculars"},
};
static XPLMCommandRef g_cmdRefs[CMD_COUNT];

static void SetEnabled(bool on);

static int CommandCB(XPLMCommandRef, XPLMCommandPhase phase, void* ref) {
    int id = (int)(intptr_t)ref;
    if (id >= CMD_MOVE_FWD && id <= CMD_MOVE_DOWN) {
        g_moveCmd[id - CMD_MOVE_FWD] = phase != xplm_CommandEnd;
        return 0;
    }
    if (id == CMD_ZOOM_IN || id == CMD_ZOOM_OUT) {
        // In the cockpit, hand over to X-Plane's own zoom.
        static XPLMCommandRef forwarded = nullptr;
        if (phase == xplm_CommandBegin && Active() && g_mode == COCKPIT) {
            forwarded = XPLMFindCommand(id == CMD_ZOOM_IN ? "sim/general/zoom_in" : "sim/general/zoom_out");
            XPLMCommandBegin(forwarded);
        } else if (phase == xplm_CommandEnd && forwarded) {
            XPLMCommandEnd(forwarded);
            forwarded = nullptr;
        }
        g_zoomCmd = (phase == xplm_CommandEnd || forwarded) ? 0.f : (id == CMD_ZOOM_IN ? 1.f : -1.f);
        return 0;
    }
    if (phase != xplm_CommandBegin) return 0;
    if (id == CMD_TOGGLE_ENABLED) { SetEnabled(!g_enabled); return 0; }
    if (id == CMD_WINDOW) { ToggleViewWindow(); return 0; }
    if (!Active()) return 0;
    if (id == CMD_BINOCULARS) {
        if (!g_cine) ToggleBinoculars();
        return 0;
    }
    if (id == CMD_CINEMATIC) {
        if (g_cine) StopCinematic(true); else StartCinematic();
        return 0;
    }
    if (id == CMD_FLYBY) {
        if (!FlybyPossible()) { ShowToastText("Flyby needs the aircraft moving"); return 0; }
        StartCinematic();
        g_cineT = 0;
        StartFlyby();
        return 0;
    }
    if (g_cine) {
        if (id == CMD_NEXT || id == CMD_PREV) { StartShot(true); return 0; }
        StopCinematic(id == CMD_TOGGLE_INOUT);
        if (id == CMD_TOGGLE_INOUT) return 0;
    }
    switch (id) {
    case CMD_NEXT: StepView(1); break;
    case CMD_PREV: StepView(-1); break;
    case CMD_TOGGLE_INOUT: SetMode(g_mode == COCKPIT ? EXTERIOR : COCKPIT); break;
    case CMD_RESET: ResetView(); break;
    case CMD_ADD: AddView(nullptr); break;
    case CMD_SAVE: SaveCurrentView(); break;
    case CMD_DELETE: DeleteCurrentView(); break;
    }
    return 0;
}

// X-Plane's zoom commands don't affect a plugin-controlled camera, so in
// exterior views they drive OrbitView's zoom instead. Refcon = zoom rate.
static const struct { const char* cmd; float rate; } kSimZoom[] = {
    {"sim/general/zoom_in", 1.f},       {"sim/general/zoom_out", -1.f},
    {"sim/general/zoom_in_fast", 2.5f}, {"sim/general/zoom_out_fast", -2.5f},
    {"sim/general/zoom_in_slow", 0.4f}, {"sim/general/zoom_out_slow", -0.4f},
};

static int SimZoomCB(XPLMCommandRef, XPLMCommandPhase phase, void* ref) {
    if (!Active() || g_mode != EXTERIOR) { g_zoomCmd = 0.f; return 1; }
    g_zoomCmd = phase == xplm_CommandEnd ? 0.f : kSimZoom[(intptr_t)ref].rate;
    return 0;
}

// ---------------------------------------------------------------------------
// Menu

enum { MENU_ENABLED, MENU_WINDOW, MENU_FACTORY, MENU_RELOAD, MENU_PREFS };
static XPLMMenuID g_menu = nullptr;
static int g_menuItem = -1;

static void MenuCB(void*, void* item) {
    switch ((int)(intptr_t)item) {
    case MENU_ENABLED: SetEnabled(!g_enabled); break;
    case MENU_WINDOW: OpenWindow(PAGE_VIEWS); break;
    case MENU_PREFS: OpenWindow(PAGE_PREFS); break;
    case MENU_FACTORY: if (g_viewsReady) RestoreFactoryViews(); break;
    case MENU_RELOAD:
        LoadConfig();
        g_binoZoom = 0.f;            // next raise starts at the new binocular_zoom
        g_stickX = g_stickY = -1;
        RefreshPrefsPage();
        Log("config reloaded");
        break;
    }
}

static void SetEnabled(bool on) {
    g_enabled = on;
    if (!on) StopRunning();
    if (g_menu) XPLMCheckMenuItem(g_menu, MENU_ENABLED, on ? xplm_Menu_Checked : xplm_Menu_Unchecked);
}

// ---------------------------------------------------------------------------
// Housekeeping loop

static void ResolveStickAxes() {
    g_stickX = g_cfg.stick_x_axis;
    g_stickY = g_cfg.stick_y_axis;
    if (g_stickX >= 0 && g_stickY >= 0) return;
    // X-Plane's axis enum: 41 = View left/right, 42 = View up/down.
    int assign[500];
    XPLMGetDatavi(dr_axisAssign, assign, 0, 500);
    for (int i = 0; i < 500; ++i) {
        if (g_cfg.stick_x_axis < 0 && assign[i] == 41) g_stickX = i;
        if (g_cfg.stick_y_axis < 0 && assign[i] == 42) g_stickY = i;
    }
}

static float FlightLoop(float, float, int, void*) {
    double now = Now();

    static double nextSlow = 0;
    if (now >= nextSlow) {
        if (nextSlow == 0) RecoverFovAfterRestart();   // first loop: prefs are loaded by now
        nextSlow = now + 1.0;
        HookMainWindow();
        int sx = g_stickX, sy = g_stickY;
        ResolveStickAxes();
        if (sx != g_stickX || sy != g_stickY) {
            g_stickArmed = false;
            Log("stick axes: x=%d y=%d", g_stickX, g_stickY);
        }
    }

    if (g_toastUntil && now > g_toastUntil) {
        XPLMSetWindowIsVisible(g_toast, 0);
        g_toastUntil = 0;
    }

    if (g_initAt >= 0 && now >= g_initAt) {
        g_initAt = -1;
        InitViews();
    }

    if (g_prefsDirtyAt >= 0 && now - g_prefsDirtyAt > 1.0) {
        g_prefsDirtyAt = -1;
        SavePrefs();
    }

    bool vr = XPLMGetDatai(dr_vr) != 0;
    if (!g_enabled || vr || !g_viewsReady) {
        if (g_running) StopRunning();
        ResetEffects();
        g_binoToggleReq = false;
        ShowCompass(false);
        return -1;
    }
    if (g_binoToggleReq) {
        g_binoToggleReq = false;
        if (g_running && !g_cine) ToggleBinoculars();
    }
    ShowCompass(g_running && g_bino);

    if (!g_running) {
        g_running = true;
        HoldGloadCamOff(true);
        EnterMode(XPLMGetDatai(dr_isExternal) ? EXTERIOR : COCKPIT);
        ApplyViewType();
        Log("active");
    }

    // Follow view changes made through X-Plane's own view keys/menus.
    if (now - g_lastViewCmdTime > 1.0) {
        int m = XPLMGetDatai(dr_isExternal) ? EXTERIOR : COCKPIT;
        if (m != g_mode) {
            StopCinematic(false);
            EnterMode(m);
        }
    }

    // Re-take the camera if another plugin let go of it.
    UpdateCameraOwnership();

    static double lastCockpit = now;
    float dt = (float)fmin(now - lastCockpit, 0.1);
    lastCockpit = now;
    UpdateEffects(dt);
    if (g_mode == COCKPIT) UpdateCockpit(dt);

    RefreshViewWindow();
    return -1;
}

// ---------------------------------------------------------------------------
// Plugin entry points

PLUGIN_API int XPluginStart(char* name, char* sig, char* desc) {
    strcpy_s(name, 256, "OrbitView");
    strcpy_s(sig, 256, "orbitview.camera");
    strcpy_s(desc, 256, "Preset-based cockpit/exterior camera with mouse, stick and keyboard control.");
    XPLMEnableFeature("XPLM_USE_NATIVE_PATHS", 1);
    XPLMEnableFeature("XPLM_USE_NATIVE_WIDGET_WINDOWS", 1);   // widget windows follow X-Plane's UI size

    char path[1024];
    XPLMGetPluginInfo(XPLMGetMyID(), nullptr, path, nullptr, nullptr);
    std::string p = path;                       // ...\OrbitView\64\win.xpl
    p = p.substr(0, p.find_last_of("\\/"));     // ...\OrbitView\64
    p = p.substr(0, p.find_last_of("\\/") + 1); // ...\OrbitView plus separator
    g_pluginDir = p;
    LoadConfig();

    dr_lx = XPLMFindDataRef("sim/flightmodel/position/local_x");
    dr_ly = XPLMFindDataRef("sim/flightmodel/position/local_y");
    dr_lz = XPLMFindDataRef("sim/flightmodel/position/local_z");
    dr_psi = XPLMFindDataRef("sim/flightmodel/position/psi");
    dr_isExternal = XPLMFindDataRef("sim/graphics/view/view_is_external");
    dr_vr = XPLMFindDataRef("sim/graphics/VR/enabled");
    dr_headX = XPLMFindDataRef("sim/graphics/view/pilots_head_x");
    dr_headY = XPLMFindDataRef("sim/graphics/view/pilots_head_y");
    dr_headZ = XPLMFindDataRef("sim/graphics/view/pilots_head_z");
    dr_headPsi = XPLMFindDataRef("sim/graphics/view/pilots_head_psi");
    dr_headThe = XPLMFindDataRef("sim/graphics/view/pilots_head_the");
    dr_peX = XPLMFindDataRef("sim/aircraft/view/acf_peX");
    dr_peY = XPLMFindDataRef("sim/aircraft/view/acf_peY");
    dr_peZ = XPLMFindDataRef("sim/aircraft/view/acf_peZ");
    dr_sizeX = XPLMFindDataRef("sim/aircraft/view/acf_size_x");
    dr_sizeZ = XPLMFindDataRef("sim/aircraft/view/acf_size_z");
    dr_axisValues = XPLMFindDataRef("sim/joystick/joystick_axis_values");
    dr_axisAssign = XPLMFindDataRef("sim/joystick/joystick_axis_assignments");    dr_gloadCam = XPLMFindDataRef("sim/graphics/view/gloaded_internal_cam");
    dr_ax = XPLMFindDataRef("sim/flightmodel/position/local_ax");
    dr_ay = XPLMFindDataRef("sim/flightmodel/position/local_ay");
    dr_az = XPLMFindDataRef("sim/flightmodel/position/local_az");
    dr_vy = XPLMFindDataRef("sim/flightmodel/position/local_vy");
    dr_the = XPLMFindDataRef("sim/flightmodel/position/theta");
    dr_phi = XPLMFindDataRef("sim/flightmodel/position/phi");
    dr_gs = XPLMFindDataRef("sim/flightmodel/position/groundspeed");
    dr_onGround = XPLMFindDataRef("sim/flightmodel/failures/onground_any");
    dr_paused = XPLMFindDataRef("sim/time/paused");
    dr_vx = XPLMFindDataRef("sim/flightmodel/position/local_vx");
    dr_vz = XPLMFindDataRef("sim/flightmodel/position/local_vz");
    dr_fov = XPLMFindDataRef("sim/graphics/view/field_of_view_deg");
    dr_magPsi = XPLMFindDataRef("sim/flightmodel/position/mag_psi");

    g_probe = XPLMCreateProbe(xplm_ProbeY);

    for (int i = 0; i < CMD_COUNT; ++i) {
        g_cmdRefs[i] = XPLMCreateCommand(kCmds[i][0], kCmds[i][1]);
        XPLMRegisterCommandHandler(g_cmdRefs[i], CommandCB, 1, (void*)(intptr_t)i);
    }
    for (intptr_t i = 0; i < (intptr_t)(sizeof(kSimZoom) / sizeof(kSimZoom[0])); ++i)
        XPLMRegisterCommandHandler(XPLMFindCommand(kSimZoom[i].cmd), SimZoomCB, 1, (void*)i);

    g_menuItem = XPLMAppendMenuItem(XPLMFindPluginsMenu(), "OrbitView", nullptr, 0);
    g_menu = XPLMCreateMenu("OrbitView", XPLMFindPluginsMenu(), g_menuItem, MenuCB, nullptr);
    XPLMAppendMenuItem(g_menu, "Enabled", (void*)MENU_ENABLED, 0);
    XPLMAppendMenuItem(g_menu, "Views window...", (void*)MENU_WINDOW, 0);
    XPLMAppendMenuItem(g_menu, "Preferences...", (void*)MENU_PREFS, 0);
    XPLMAppendMenuItem(g_menu, "Restore factory views for this aircraft", (void*)MENU_FACTORY, 0);
    XPLMAppendMenuItem(g_menu, "Reload config.ini", (void*)MENU_RELOAD, 0);
    XPLMCheckMenuItem(g_menu, MENU_ENABLED, xplm_Menu_Checked);

    XPLMCreateWindow_t wc = {};
    wc.structSize = sizeof(wc);
    wc.left = 0; wc.top = 30; wc.right = 260; wc.bottom = 0;
    wc.visible = 0;
    wc.drawWindowFunc = DrawToast;
    wc.handleMouseClickFunc = ToastClick;
    wc.handleRightClickFunc = ToastClick;
    wc.handleKeyFunc = ToastKey;
    wc.handleCursorFunc = ToastCursor;
    wc.handleMouseWheelFunc = ToastWheel;
    wc.decorateAsFloatingWindow = xplm_WindowDecorationNone;
    wc.layer = xplm_WindowLayerFloatingWindows;
    g_toast = XPLMCreateWindowEx(&wc);
    wc.drawWindowFunc = DrawCompass;
    wc.layer = xplm_WindowLayerFlightOverlay;
    g_compass = XPLMCreateWindowEx(&wc);

    return 1;
}

PLUGIN_API void XPluginStop(void) {
    for (int i = 0; i < CMD_COUNT; ++i)
        XPLMUnregisterCommandHandler(g_cmdRefs[i], CommandCB, 1, (void*)(intptr_t)i);
    for (intptr_t i = 0; i < (intptr_t)(sizeof(kSimZoom) / sizeof(kSimZoom[0])); ++i)
        XPLMUnregisterCommandHandler(XPLMFindCommand(kSimZoom[i].cmd), SimZoomCB, 1, (void*)i);
    if (g_prefsDirtyAt >= 0) SavePrefs();
    if (g_win) XPDestroyWidget(g_win, 1);
    if (g_toast) XPLMDestroyWindow(g_toast);
    if (g_compass) XPLMDestroyWindow(g_compass);
    if (g_menu) XPLMDestroyMenu(g_menu);
    if (g_probe) XPLMDestroyProbe(g_probe);
}

PLUGIN_API int XPluginEnable(void) {
    XPLMRegisterFlightLoopCallback(FlightLoop, -1, nullptr);
    XPLMRegisterKeySniffer(KeySniffer, 0, nullptr);
    XPLMRegisterDrawCallback(FadeDrawCB, xplm_Phase_Window, 1, nullptr);
    g_initAt = Now() + 1.0;
    return 1;
}

PLUGIN_API void XPluginDisable(void) {
    // Put the field of view back but keep fov_restore.txt: X-Plane may already
    // have saved the changed value, so the next start restores it again.
    if (g_savedFov >= 0.f && dr_fov) XPLMSetDataf(dr_fov, g_savedFov);
    g_savedFov = -1.f;
    XPLMUnregisterFlightLoopCallback(FlightLoop, nullptr);
    XPLMUnregisterKeySniffer(KeySniffer, 0, nullptr);
    XPLMUnregisterDrawCallback(FadeDrawCB, xplm_Phase_Window, 1, nullptr);
    StopRunning();
    g_viewsReady = false;
    UnhookMainWindow();
    ClearHeldInput();
}

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID, int msg, void* param) {
    if (msg == XPLM_MSG_PLANE_LOADED && (intptr_t)param == 0) {
        // Give X-Plane a moment to reset the pilot's head to the new eye point.
        StopRunning();
        g_viewsReady = false;
        g_initAt = Now() + 1.0;
    }
}
