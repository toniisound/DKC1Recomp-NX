/* OpenGL presenter for the Switch build. See switch_graphics.h.
 *
 * Adapted from runner/windows_graphics.c (same shaders, same uniform
 * layout). Differences: every GL entry point, including GL 1.x ones, is
 * resolved through SDL_GL_GetProcAddress (EGL on Horizon does not export
 * them for direct linking); only the Flat and Reconstruct passes are
 * compiled; the swap is vsync'd (the display paces the game); and an
 * alpha-blended overlay pass draws the pause menu and toasts. */
#ifdef __SWITCH__

#include "switch_graphics.h"

#include <SDL_opengl.h>
#include <SDL_opengl_glext.h>
#include <stdio.h>
#include <string.h>

#include "switch_shaders.h"

typedef void(APIENTRYP Dkc1PfnGenTextures)(GLsizei, GLuint *);
typedef void(APIENTRYP Dkc1PfnDeleteTextures)(GLsizei, const GLuint *);
typedef void(APIENTRYP Dkc1PfnBindTexture)(GLenum, GLuint);
typedef void(APIENTRYP Dkc1PfnTexParameteri)(GLenum, GLenum, GLint);
typedef void(APIENTRYP Dkc1PfnTexImage2D)(GLenum, GLint, GLint, GLsizei,
                                          GLsizei, GLint, GLenum, GLenum,
                                          const void *);
typedef void(APIENTRYP Dkc1PfnTexSubImage2D)(GLenum, GLint, GLint, GLint,
                                             GLsizei, GLsizei, GLenum, GLenum,
                                             const void *);
typedef GLenum(APIENTRYP Dkc1PfnGetError)(void);
typedef void(APIENTRYP Dkc1PfnViewport)(GLint, GLint, GLsizei, GLsizei);
typedef void(APIENTRYP Dkc1PfnClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
typedef void(APIENTRYP Dkc1PfnClear)(GLbitfield);
typedef void(APIENTRYP Dkc1PfnDrawArrays)(GLenum, GLint, GLsizei);
typedef void(APIENTRYP Dkc1PfnEnable)(GLenum);
typedef void(APIENTRYP Dkc1PfnDisable)(GLenum);
typedef void(APIENTRYP Dkc1PfnBlendFunc)(GLenum, GLenum);
typedef void(APIENTRYP Dkc1PfnPixelStorei)(GLenum, GLint);
typedef const GLubyte *(APIENTRYP Dkc1PfnGetString)(GLenum);

#define GL_API(X)                                                            \
  X(Dkc1PfnGenTextures, GenTextures)                                         \
  X(Dkc1PfnDeleteTextures, DeleteTextures)                                   \
  X(Dkc1PfnBindTexture, BindTexture)                                         \
  X(Dkc1PfnTexParameteri, TexParameteri)                                     \
  X(Dkc1PfnTexImage2D, TexImage2D)                                           \
  X(Dkc1PfnTexSubImage2D, TexSubImage2D)                                     \
  X(Dkc1PfnGetError, GetError)                                               \
  X(Dkc1PfnViewport, Viewport)                                               \
  X(Dkc1PfnClearColor, ClearColor)                                           \
  X(Dkc1PfnClear, Clear)                                                     \
  X(Dkc1PfnDrawArrays, DrawArrays)                                           \
  X(Dkc1PfnEnable, Enable)                                                   \
  X(Dkc1PfnDisable, Disable)                                                 \
  X(Dkc1PfnBlendFunc, BlendFunc)                                             \
  X(Dkc1PfnPixelStorei, PixelStorei)                                         \
  X(Dkc1PfnGetString, GetString)                                             \
  X(PFNGLCREATESHADERPROC, CreateShader)                                     \
  X(PFNGLSHADERSOURCEPROC, ShaderSource)                                     \
  X(PFNGLCOMPILESHADERPROC, CompileShader)                                   \
  X(PFNGLGETSHADERIVPROC, GetShaderiv)                                       \
  X(PFNGLGETSHADERINFOLOGPROC, GetShaderInfoLog)                             \
  X(PFNGLDELETESHADERPROC, DeleteShader)                                     \
  X(PFNGLCREATEPROGRAMPROC, CreateProgram)                                   \
  X(PFNGLATTACHSHADERPROC, AttachShader)                                     \
  X(PFNGLLINKPROGRAMPROC, LinkProgram)                                       \
  X(PFNGLGETPROGRAMIVPROC, GetProgramiv)                                     \
  X(PFNGLGETPROGRAMINFOLOGPROC, GetProgramInfoLog)                           \
  X(PFNGLDELETEPROGRAMPROC, DeleteProgram)                                   \
  X(PFNGLUSEPROGRAMPROC, UseProgram)                                         \
  X(PFNGLGETUNIFORMLOCATIONPROC, GetUniformLocation)                         \
  X(PFNGLUNIFORM1IVPROC, Uniform1iv)                                         \
  X(PFNGLUNIFORM1FVPROC, Uniform1fv)                                         \
  X(PFNGLACTIVETEXTUREPROC, ActiveTexture)                                   \
  X(PFNGLGENVERTEXARRAYSPROC, GenVertexArrays)                               \
  X(PFNGLBINDVERTEXARRAYPROC, BindVertexArray)                               \
  X(PFNGLDELETEVERTEXARRAYSPROC, DeleteVertexArrays)                         \
  X(PFNGLBINDFRAMEBUFFERPROC, BindFramebuffer)

#define DECLARE(type, name) static type p##name;
GL_API(DECLARE)
#undef DECLARE

/* Indices into kSwitchShaders. */
enum { kPassFlat = 0, kPassReconstruct = 1 };
enum { kProgramFlat, kProgramReconstruct, kProgramOverlay, kProgramCount };

typedef struct Target {
  GLuint texture;
  int w, h;
} Target;

static SDL_Window *s_window;
static SDL_GLContext s_context;
static GLuint s_programs[kProgramCount];
static GLuint s_vao;
static Target s_input, s_overlay;
static bool s_vsync;

static const char *const kVertexSource =
    "#version 330 core\nout vec2 v_uv;\n"
    "uniform int final_pass;\n"
    "void main(){vec2 p[4]=vec2[](vec2(-1,-1),vec2(-1,1),vec2(1,-1),vec2(1,1));"
    "vec2 t[4]=vec2[](vec2(0,0),vec2(0,1),vec2(1,0),vec2(1,1));"
    "v_uv=t[gl_VertexID]; if(final_pass!=0) v_uv.y=1-v_uv.y;"
    "gl_Position=vec4(p[gl_VertexID],0,1);}";

static const char *const kOverlaySource =
    "#version 330 core\nin vec2 v_uv;\nout vec4 result;\n"
    "uniform sampler2D source;\n"
    "void main(){ result=texture(source,v_uv); }\n";

void Dkc1SwitchGraphicsPrepareAttributes(void) {
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
}

void Dkc1SwitchGraphicsResetAttributes(void) { SDL_GL_ResetAttributes(); }

static GLuint Shader(GLenum type, const char *source) {
  GLuint shader = pCreateShader(type);
  GLint ok = 0;
  pShaderSource(shader, 1, &source, NULL);
  pCompileShader(shader);
  pGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char error[2048];
    pGetShaderInfoLog(shader, sizeof error, NULL, error);
    fprintf(stderr, "[switch-graphics] shader compile failure: %s\n", error);
    pDeleteShader(shader);
    return 0;
  }
  return shader;
}

static GLuint Program(GLuint vertex, const char *fragment_source) {
  GLuint fragment = Shader(GL_FRAGMENT_SHADER, fragment_source);
  GLuint program;
  GLint ok = 0;
  if (!fragment) return 0;
  program = pCreateProgram();
  pAttachShader(program, vertex);
  pAttachShader(program, fragment);
  pLinkProgram(program);
  pDeleteShader(fragment);
  pGetProgramiv(program, GL_LINK_STATUS, &ok);
  if (!ok) {
    char error[2048];
    pGetProgramInfoLog(program, sizeof error, NULL, error);
    fprintf(stderr, "[switch-graphics] shader link failure: %s\n", error);
    pDeleteProgram(program);
    return 0;
  }
  pUseProgram(program);
  {
    /* Same sampler-unit contract as the Windows presenter. */
    static const char *const names[] = {"source", "lines", "image", "glow",
                                        "halo"};
    static const int units[] = {0, 0, 0, 1, 2};
    for (int i = 0; i < 5; i++) {
      GLint location = pGetUniformLocation(program, names[i]);
      if (location >= 0) pUniform1iv(location, 1, &units[i]);
    }
  }
  return program;
}

static bool Ensure(Target *target, int w, int h) {
  if (target->texture && target->w == w && target->h == h) return true;
  if (!target->texture) pGenTextures(1, &target->texture);
  target->w = w;
  target->h = h;
  pBindTexture(GL_TEXTURE_2D, target->texture);
  pTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  pTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  pTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  pTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  pTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_BGRA, GL_UNSIGNED_BYTE,
              NULL);
  return pGetError() == GL_NO_ERROR;
}

static void Upload(Target *target, const uint32_t *pixels) {
  pActiveTexture(GL_TEXTURE0);
  pBindTexture(GL_TEXTURE_2D, target->texture);
  pPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  pTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, target->w, target->h, GL_BGRA,
                 GL_UNSIGNED_BYTE, pixels);
}

static void Filter(Target *target, bool linear) {
  pBindTexture(GL_TEXTURE_2D, target->texture);
  pTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                 linear ? GL_LINEAR : GL_NEAREST);
  pTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                 linear ? GL_LINEAR : GL_NEAREST);
}

static void SetFinalPass(GLuint program) {
  const int final_pass = 1;
  GLint location = pGetUniformLocation(program, "final_pass");
  if (location >= 0) pUniform1iv(location, 1, &final_pass);
}

bool Dkc1SwitchGraphicsInit(SDL_Window *window) {
  GLuint vertex;
  s_window = window;
  s_context = SDL_GL_CreateContext(window);
  if (!s_context) {
    fprintf(stderr, "[switch-graphics] no GL 3.3 core context: %s\n",
            SDL_GetError());
    return false;
  }
#define LOAD(type, name)                                                     \
  p##name = (type)SDL_GL_GetProcAddress("gl" #name);                         \
  if (!p##name) {                                                            \
    fprintf(stderr, "[switch-graphics] missing gl" #name "\n");              \
    Dkc1SwitchGraphicsClose();                                               \
    return false;                                                            \
  }
  GL_API(LOAD)
#undef LOAD
  vertex = Shader(GL_VERTEX_SHADER, kVertexSource);
  if (!vertex) {
    Dkc1SwitchGraphicsClose();
    return false;
  }
  s_programs[kProgramFlat] = Program(vertex, kSwitchShaders[kPassFlat]);
  s_programs[kProgramReconstruct] =
      Program(vertex, kSwitchShaders[kPassReconstruct]);
  s_programs[kProgramOverlay] = Program(vertex, kOverlaySource);
  pDeleteShader(vertex);
  if (!s_programs[kProgramFlat] || !s_programs[kProgramReconstruct] ||
      !s_programs[kProgramOverlay]) {
    Dkc1SwitchGraphicsClose();
    return false;
  }
  pGenVertexArrays(1, &s_vao);
  /* The panel/TV refresh paces the game, exactly like the SDL renderer
   * path with PRESENTVSYNC. */
  s_vsync = SDL_GL_SetSwapInterval(1) == 0;
  fprintf(stderr, "[switch-graphics] OpenGL %s, vsync=%d\n",
          (const char *)pGetString(GL_VERSION), s_vsync);
  return true;
}

bool Dkc1SwitchGraphicsActive(void) { return s_context != NULL; }
bool Dkc1SwitchGraphicsVsync(void) { return s_context && s_vsync; }

void Dkc1SwitchGraphicsDraw(const uint32_t *pixels, int w, int h,
                            int display_width,
                            const Dkc1GraphicsSettings *settings) {
  int ow = 0, oh = 0, vw, vh, x, y;
  float u[27] = {0};
  GLuint program;
  bool reconstruct, linear;
  if (!s_context || !pixels || w < 1 || h < 1 || display_width < 1) return;
  SDL_GL_GetDrawableSize(s_window, &ow, &oh);
  if (ow < 1 || oh < 1) return;
  /* Fit preserving the presentation aspect; a frame wider than the panel
   * (16:9 mode, 399:224 vs 16:9) fills it. */
  vw = ow;
  vh = (int)((long long)ow * h / display_width);
  if (vh > oh) {
    vh = oh;
    vw = (int)((long long)oh * display_width / h);
  }
  if (oh - vh <= 2) vh = oh; /* no 1-pixel bars in 16:9 */
  x = (ow - vw) / 2;
  y = (oh - vh) / 2;

  pBindFramebuffer(GL_FRAMEBUFFER, 0);
  pViewport(0, 0, ow, oh);
  pClearColor(0, 0, 0, 1);
  pClear(GL_COLOR_BUFFER_BIT);
  if (!Ensure(&s_input, w, h)) return;
  Upload(&s_input, pixels);

  reconstruct = settings->upscaler == kDkc1UpscalerReconstruct;
  u[0] = (float)w;
  u[1] = (float)h;
  u[2] = (float)vw;
  u[3] = (float)vh;
  u[4] = reconstruct ? (float)settings->reconstruct_mode
                     : (float)settings->upscaler;
  u[5] = settings->strength / 100.f;
  u[6] = settings->softness / 100.f;
  u[7] = settings->shading / 100.f;
  /* Same sampling rule as the Windows presenter: Flat samples linearly for
   * Bilinear/Sharp Bilinear; Reconstruct reads exact texels. */
  linear = !reconstruct && settings->upscaler != kDkc1UpscalerNearest;
  program = s_programs[reconstruct ? kProgramReconstruct : kProgramFlat];

  pViewport(x, y, vw, vh);
  pUseProgram(program);
  pUniform1fv(pGetUniformLocation(program, "u"), 27, u);
  SetFinalPass(program);
  pActiveTexture(GL_TEXTURE0);
  Filter(&s_input, linear);
  pBindVertexArray(s_vao);
  pDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void Dkc1SwitchGraphicsOverlay(const uint32_t *argb, int w, int h) {
  int ow = 0, oh = 0;
  if (!s_context || !argb) return;
  SDL_GL_GetDrawableSize(s_window, &ow, &oh);
  if (ow < 1 || oh < 1 || !Ensure(&s_overlay, w, h)) return;
  Upload(&s_overlay, argb);
  pViewport(0, 0, ow, oh);
  pUseProgram(s_programs[kProgramOverlay]);
  SetFinalPass(s_programs[kProgramOverlay]);
  Filter(&s_overlay, false); /* crisp glyphs */
  pEnable(GL_BLEND);
  pBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  pBindVertexArray(s_vao);
  pDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  pDisable(GL_BLEND);
}

void Dkc1SwitchGraphicsSwap(void) {
  if (s_context) SDL_GL_SwapWindow(s_window);
}

void Dkc1SwitchGraphicsClose(void) {
  if (!s_context) return;
  if (pDeleteTextures) {
    if (s_input.texture) pDeleteTextures(1, &s_input.texture);
    if (s_overlay.texture) pDeleteTextures(1, &s_overlay.texture);
  }
  memset(&s_input, 0, sizeof s_input);
  memset(&s_overlay, 0, sizeof s_overlay);
  for (int i = 0; i < kProgramCount; i++) {
    if (s_programs[i] && pDeleteProgram) pDeleteProgram(s_programs[i]);
    s_programs[i] = 0;
  }
  if (s_vao && pDeleteVertexArrays) pDeleteVertexArrays(1, &s_vao);
  s_vao = 0;
  SDL_GL_DeleteContext(s_context);
  s_context = NULL;
  s_vsync = false;
}

#endif /* __SWITCH__ */
