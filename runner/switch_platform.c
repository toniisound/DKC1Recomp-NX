/* Nintendo Switch platform layer for DKC1Recomp (libnx + SDL2).
 *
 * sdl_host.c drives the game through the Dkc1Mac* platform surface, with
 * per-OS implementations (macos_*.m on Apple, windows_platform.c on
 * Windows). This file is the Switch implementation: no-file-picker ROM
 * flow (sdmc:/switch/dkc1/rom.smc), fixed handheld settings, and no-op
 * native menus/launchers. Only compiled on __SWITCH__; desktop builds
 * keep their own platform files.
 *
 * Only this translation unit and sdl_host.c's __SWITCH__ blocks know
 * about Horizon; everything else stays portable.
 */
#ifdef __SWITCH__

#include <SDL.h>
#include <switch.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "desktop_graphics.h"
#include "desktop_input.h"
#include "dkc1_edge_policy.h"
#include "macos_controls.h"
#include "macos_file_picker.h"
#include "macos_metal_presenter.h"
#include "macos_pause_menu.h"
#include "switch_gamepad.h"

/* ROM and content pickers: the Switch build boots straight from the SD
 * app dir (see SwitchImpl_ResolveRom) and never prompts. */
char *Dkc1MacChooseRom(void) { return NULL; }
char *Dkc1MacChooseBabyKongRom(void) { return NULL; }
char *Dkc1MacSavedBabyKongRom(void) { return NULL; }
void Dkc1MacSetBabyKongRom(const char *path) { (void)path; }
int Dkc1MacSavedBabyKongEnabled(void) { return 0; }
void Dkc1MacSetBabyKongEnabled(int enabled) { (void)enabled; }
char *Dkc1MacChooseMsu1(void) { return NULL; }
char *Dkc1MacSavedMsu1(void) { return NULL; }
void Dkc1MacClearMsu1(void) {}

Dkc1MacFullscreenScaling Dkc1MacSavedFullscreenScaling(void) {
  return kDkc1MacFullscreenSharpBilinear;
}
void Dkc1MacSetFullscreenScaling(Dkc1MacFullscreenScaling scaling) {
  (void)scaling;
}
Dkc1EdgePolicy Dkc1MacSavedWidescreenEdge(void) { return kDkc1EdgeGlide; }
void Dkc1MacSetWidescreenEdge(Dkc1EdgePolicy policy) { (void)policy; }

/* ---- Persistent settings ------------------------------------------------
 * The pause menu edits these and they survive reboots in
 * sdmc:/switch/<app>/dkc1nx.ini (cwd is the app dir after SwitchImpl_Init).
 * Plain key=value lines; unknown keys are ignored, missing keys keep the
 * defaults, so older/newer files stay compatible. */
#define DKC1_SWITCH_SETTINGS_FILE "dkc1nx.ini"

static int s_rumble_enabled = 1;

static int ClampInt(int value, int lo, int hi) {
  return value < lo ? lo : value > hi ? hi : value;
}

void Dkc1MacLoadGraphics(Dkc1GraphicsSettings *settings) {
  FILE *file;
  char line[128];
  if (!settings) return;
  Dkc1GraphicsDefault(settings);
  /* Switch defaults: 16:9 picture, smooth (bilinear) scaling. */
  settings->aspect = 2;
  settings->upscaler = kDkc1UpscalerBilinear;
  file = fopen(DKC1_SWITCH_SETTINGS_FILE, "r");
  if (!file) return;
  while (fgets(line, sizeof line, file)) {
    char key[32];
    int value;
    if (sscanf(line, " %31[a-z_] = %d", key, &value) != 2) continue;
    if (strcmp(key, "aspect") == 0)
      settings->aspect = ClampInt(value, 0, 2);
    else if (strcmp(key, "smooth") == 0) /* older builds */
      settings->upscaler =
          value ? kDkc1UpscalerBilinear : kDkc1UpscalerNearest;
    else if (strcmp(key, "scaling") == 0)
      settings->upscaler = value == 0   ? kDkc1UpscalerNearest
                           : value == 2 ? kDkc1UpscalerReconstruct
                                        : kDkc1UpscalerBilinear;
    else if (strcmp(key, "hd_detail") == 0)
      settings->reconstruct_mode = ClampInt(value, 0, 4);
    else if (strcmp(key, "volume") == 0)
      settings->volume = ClampInt(value, 0, 100);
    else if (strcmp(key, "slot") == 0)
      settings->state_slot = ClampInt(value, 0, 4);
    else if (strcmp(key, "rumble") == 0)
      s_rumble_enabled = value != 0;
  }
  fclose(file);
}

void Dkc1MacSaveGraphics(const Dkc1GraphicsSettings *settings) {
  FILE *file;
  if (!settings) return;
  file = fopen(DKC1_SWITCH_SETTINGS_FILE, "w");
  if (!file) return;
  fprintf(file,
          "# DKC1 NX settings (edited from the pause menu)\n"
          "aspect=%d\n"
          "# scaling: 0 sharp, 1 smooth, 2 HD (Reconstruct)\n"
          "scaling=%d\n"
          "# hd_detail: 0 pixels, 1 dither, 2 diagonals, 3 slopes, 4 slopes+\n"
          "hd_detail=%d\n"
          "volume=%d\n"
          "slot=%d\n"
          "rumble=%d\n",
          settings->aspect,
          settings->upscaler == kDkc1UpscalerNearest       ? 0
          : settings->upscaler == kDkc1UpscalerReconstruct ? 2
                                                           : 1,
          settings->reconstruct_mode,
          settings->volume, settings->state_slot, s_rumble_enabled);
  fclose(file);
}

int Dkc1SwitchRumbleEnabled(void) { return s_rumble_enabled; }
void Dkc1SwitchSetRumbleEnabled(int enabled) {
  s_rumble_enabled = enabled != 0;
  if (!s_rumble_enabled) Dkc1SwitchRumbleStop();
}

/* ---- HD Rumble ------------------------------------------------------------
 * Driven straight through hid: SDL2's Switch joystick backend is not used
 * for vibration. Handles exist for handheld mode and for player 1 as a Pro
 * Controller (FullKey) or a pair of Joy-Con (JoyDual); a pulse goes to
 * whichever of those is currently attached and stops on its own after the
 * requested number of frames (Dkc1SwitchRumbleTick, once per frame). */
static HidVibrationDeviceHandle s_vib_handheld[2];
static HidVibrationDeviceHandle s_vib_fullkey[2];
static HidVibrationDeviceHandle s_vib_joydual[2];
static bool s_vib_ok_handheld, s_vib_ok_fullkey, s_vib_ok_joydual;
static bool s_vib_init;
static int s_vib_frames_left;

static void RumbleInit(void) {
  if (s_vib_init) return;
  s_vib_init = true;
  s_vib_ok_handheld = R_SUCCEEDED(hidInitializeVibrationDevices(
      s_vib_handheld, 2, HidNpadIdType_Handheld,
      HidNpadStyleTag_NpadHandheld));
  s_vib_ok_fullkey = R_SUCCEEDED(hidInitializeVibrationDevices(
      s_vib_fullkey, 2, HidNpadIdType_No1, HidNpadStyleTag_NpadFullKey));
  s_vib_ok_joydual = R_SUCCEEDED(hidInitializeVibrationDevices(
      s_vib_joydual, 2, HidNpadIdType_No1, HidNpadStyleTag_NpadJoyDual));
}

static void RumbleSend(const HidVibrationValue *value) {
  HidVibrationValue values[2];
  u32 handheld_style, player_style;
  RumbleInit();
  values[0] = *value;
  values[1] = *value;
  handheld_style = hidGetNpadStyleSet(HidNpadIdType_Handheld);
  player_style = hidGetNpadStyleSet(HidNpadIdType_No1);
  if (s_vib_ok_handheld && (handheld_style & HidNpadStyleTag_NpadHandheld))
    (void)hidSendVibrationValues(s_vib_handheld, values, 2);
  if (s_vib_ok_fullkey && (player_style & HidNpadStyleTag_NpadFullKey))
    (void)hidSendVibrationValues(s_vib_fullkey, values, 2);
  if (s_vib_ok_joydual && (player_style & HidNpadStyleTag_NpadJoyDual))
    (void)hidSendVibrationValues(s_vib_joydual, values, 2);
}

void Dkc1SwitchRumble(float low, float high, int frames) {
  HidVibrationValue value;
  if (!s_rumble_enabled || frames <= 0) return;
  value.amp_low = low;
  value.freq_low = 160.0f;
  value.amp_high = high;
  value.freq_high = 320.0f;
  RumbleSend(&value);
  if (frames > s_vib_frames_left) s_vib_frames_left = frames;
}

void Dkc1SwitchRumbleStop(void) {
  HidVibrationValue value = {0.0f, 160.0f, 0.0f, 320.0f};
  if (!s_vib_init) return;
  s_vib_frames_left = 0;
  RumbleSend(&value);
}

void Dkc1SwitchRumbleTick(void) {
  if (s_vib_frames_left <= 0) return;
  if (--s_vib_frames_left == 0) Dkc1SwitchRumbleStop();
}
void Dkc1MacInstallMenu(void) {}
void Dkc1MacUpdateGraphicsMenuState(int display, int upscaler, int screen) {
  (void)display;
  (void)upscaler;
  (void)screen;
}
void Dkc1MacUpdateMenuState(int paused, int fullscreen,
                            Dkc1MacFullscreenScaling fullscreen_scaling,
                            Dkc1VideoAspect aspect, Dkc1EdgePolicy edge,
                            unsigned char layer_mask, int provenance,
                            int replacement_music, int baby_kong_enabled,
                            int baby_kong_ready) {
  (void)paused;
  (void)fullscreen;
  (void)fullscreen_scaling;
  (void)aspect;
  (void)edge;
  (void)layer_mask;
  (void)provenance;
  (void)replacement_music;
  (void)baby_kong_enabled;
  (void)baby_kong_ready;
}

/* Fixed handheld controls: both players on gamepads with the straight
 * Switch layout (labels already sit at the SNES positions, unlike the
 * crossed Xbox-physical desktop defaults). ABXY at face value, ZL/ZR as
 * L/R triggers, Plus as Start, L as Select (Minus opens the pause menu). Assist shortcuts are
 * cleared so triggers never double as rewind/fast-forward. */
void Dkc1MacLoadControls(Dkc1Controls *controls) {
  static const int kSwitchPads[12] = {
      DKC1_PAD_BUTTON(SDL_CONTROLLER_BUTTON_DPAD_UP),
      DKC1_PAD_BUTTON(SDL_CONTROLLER_BUTTON_DPAD_DOWN),
      DKC1_PAD_BUTTON(SDL_CONTROLLER_BUTTON_DPAD_LEFT),
      DKC1_PAD_BUTTON(SDL_CONTROLLER_BUTTON_DPAD_RIGHT),
      DKC1_PAD_BUTTON(SDL_CONTROLLER_BUTTON_A),
      DKC1_PAD_BUTTON(SDL_CONTROLLER_BUTTON_B),
      DKC1_PAD_BUTTON(SDL_CONTROLLER_BUTTON_X),
      DKC1_PAD_BUTTON(SDL_CONTROLLER_BUTTON_Y),
      DKC1_PAD_AXIS(SDL_CONTROLLER_AXIS_TRIGGERLEFT, 1),
      DKC1_PAD_AXIS(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 1),
      DKC1_PAD_BUTTON(SDL_CONTROLLER_BUTTON_START),
      /* SNES Select on the L button: Minus opens the pause menu. */
      DKC1_PAD_BUTTON(SDL_CONTROLLER_BUTTON_LEFTSHOULDER),
  };
  int player;

  if (!controls) return;
  memset(controls, 0, sizeof *controls);
  controls->source[0] = 2;
  controls->source[1] = 2;
  for (player = 0; player < 2; player++) {
    controls->deadzone[player] = 25;
    memcpy(controls->pads[player], kSwitchPads, sizeof kSwitchPads);
  }
}
void Dkc1MacSaveControls(const Dkc1Controls *controls) {
  (void)controls;
}
int Dkc1MacEditControls(Dkc1Controls *controls) {
  (void)controls;
  return 0;
}

/* Pause menu: stubbed (no ImGui/natural UI on Switch). The host's pause
 * chord degrades to a no-op overlay check. */
int Dkc1MacShowPauseMenu(void *window, Dkc1GraphicsSettings *settings,
                         Dkc1Controls *controls, int graphics_page) {
  (void)window;
  (void)settings;
  (void)controls;
  (void)graphics_page;
  return 0;
}
int Dkc1MacPauseMenuIsOpen(void) { return 0; }

/* Display link + Metal presenter: unavailable (host-clock pacing and the
 * SDL GLES2 renderer own the frame instead). */
int Dkc1MacDisplayLinkStart(void *native_window, double preferred_fps) {
  (void)native_window;
  (void)preferred_fps;
  return 0;
}
int Dkc1MacDisplayLinkWait(unsigned long long after_callback_number,
                           double timeout_seconds, double *timestamp,
                           double *target_timestamp, double *duration,
                           unsigned long long *callback_number) {
  (void)after_callback_number;
  (void)timeout_seconds;
  (void)timestamp;
  (void)target_timestamp;
  (void)duration;
  (void)callback_number;
  return 0;
}
void Dkc1MacDisplayLinkStop(void) {}
int Dkc1MacMetalPresenterStart(void *native_window, double preferred_hz,
                               Dkc1MacFullscreenScaling scaling,
                               int fullscreen) {
  (void)native_window;
  (void)preferred_hz;
  (void)scaling;
  (void)fullscreen;
  return 0;
}
void Dkc1MacMetalPresenterQueueFrame(
    const uint32_t *pixels, int width, int height, int presentation_width,
    const Dkc1MacPresentationFrameInfo *info) {
  (void)pixels;
  (void)width;
  (void)height;
  (void)presentation_width;
  (void)info;
}
void Dkc1MacMetalPresenterSetGeometry(int presentation_width, int fullscreen) {
  (void)presentation_width;
  (void)fullscreen;
}
void Dkc1MacMetalPresenterSetScaling(Dkc1MacFullscreenScaling scaling) {
  (void)scaling;
}
void Dkc1MacMetalPresenterSetActive(int active) { (void)active; }
void Dkc1MacMetalPresenterFlush(void) {}
void Dkc1MacMetalPresenterStop(void) {}
void Dkc1MacMetalPresenterSetGraphics(const Dkc1GraphicsSettings *settings) {
  (void)settings;
}

/* ---- On-screen errors ---------------------------------------------------
 * Switch SDL2 has no message-box backend and hbmenu shows no stderr, so a
 * missing ROM used to look like a silent crash. The system error applet
 * draws a native dialog and blocks until the player dismisses it. */
void Dkc1SwitchShowError(const char *title, const char *message) {
  char dialog[512];
  ErrorApplicationConfig config;
  snprintf(dialog, sizeof dialog, "%s\n\n%s",
           title ? title : "DKC1 NX", message ? message : "");
  if (R_SUCCEEDED(errorApplicationCreate(&config, dialog, NULL)))
    (void)errorApplicationShow(&config);
}

/* ---- Performance profile (DKC1 NX) ------------------------------------
 * The stock 1020 MHz CPU clock is not enough to hold the recompiled game
 * plus SPC700/DSP audio at a locked 60 fps in busy stages. Raise the CPU
 * bus to 1785 MHz (the clock the console itself uses for boost/loading
 * screens; safe in handheld and docked) and put it back on exit.
 * Horizon may reset clocks on dock/undock or after sleep, so the main
 * loop re-asserts it periodically via Dkc1SwitchPerfTick(). Emulators that
 * stub clkrst/pcv (Ryujinx) simply ignore it. */
#define DKC1_SWITCH_CPU_HZ 1785000000u

static int s_perf_mode;        /* 0 = off, 1 = clkrst, 2 = pcv */
static ClkrstSession s_clk_cpu;
static u32 s_cpu_hz_saved;

static void PerfApply(void) {
  if (s_perf_mode == 1)
    (void)clkrstSetClockRate(&s_clk_cpu, DKC1_SWITCH_CPU_HZ);
  else if (s_perf_mode == 2)
    (void)pcvSetClockRate(PcvModule_CpuBus, DKC1_SWITCH_CPU_HZ);
}

void Dkc1SwitchPerfBegin(void) {
  if (s_perf_mode) return;
  if (hosversionAtLeast(8, 0, 0)) {
    if (R_SUCCEEDED(clkrstInitialize())) {
      if (R_SUCCEEDED(clkrstOpenSession(&s_clk_cpu, PcvModuleId_CpuBus, 3))) {
        if (R_FAILED(clkrstGetClockRate(&s_clk_cpu, &s_cpu_hz_saved)))
          s_cpu_hz_saved = 0;
        s_perf_mode = 1;
      } else {
        clkrstExit();
      }
    }
  } else if (R_SUCCEEDED(pcvInitialize())) {
    if (R_FAILED(pcvGetClockRate(PcvModule_CpuBus, &s_cpu_hz_saved)))
      s_cpu_hz_saved = 0;
    s_perf_mode = 2;
  }
  PerfApply();
}

void Dkc1SwitchPerfTick(void) { PerfApply(); }

void Dkc1SwitchPerfEnd(void) {
  if (s_perf_mode == 1) {
    if (s_cpu_hz_saved) (void)clkrstSetClockRate(&s_clk_cpu, s_cpu_hz_saved);
    clkrstCloseSession(&s_clk_cpu);
    clkrstExit();
  } else if (s_perf_mode == 2) {
    if (s_cpu_hz_saved)
      (void)pcvSetClockRate(PcvModule_CpuBus, s_cpu_hz_saved);
    pcvExit();
  }
  s_perf_mode = 0;
}

#endif /* __SWITCH__ */
