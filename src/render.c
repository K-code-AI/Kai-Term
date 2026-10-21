/* EGL + OpenGL 2.x. A frame is whatever layout.c says it is: the tab bar strip, one Cell grid per pane,
   * and the file-manager band's two grids, all drawn the same way. GL blending is never enabled, so draw
   * order alone does the layering — hence two vertex batches with the pictures drawn between them. */
/* GL/glext.h only declares the GL 2.x entry points when GL_GLEXT_PROTOTYPES is
   set; must be defined before any GL header is included. */
#define GL_GLEXT_PROTOTYPES 1

#include "kai-term.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>

/* Per-instance record layout (19 floats): origin(2) size(2) tex0(2) tex1(2) fg(4) bg(4) glyph(1)
   band0(1) band1(1). Per-vertex expanded layout (21 floats): corner(2) + the 19-float record.
   band0/band1 are the selection band's absolute pixel x edges (0 when there is no band). */
#define FLOATS_PER_INST 19
#define FLOATS_PER_VERT 21

static const char *vs_src =
    "#version 120\n"
    "attribute vec2 a_pos;\n"      /* unit quad corner (0 or 1) */
    "attribute vec2 a_origin;\n"   /* per-instance quad bottom-left */
    "attribute vec2 a_size;\n"     /* per-instance quad size */
    "attribute vec2 a_tex0;\n"     /* texcoord at corner (0,0) */
    "attribute vec2 a_tex1;\n"     /* texcoord at corner (1,1) */
    "attribute vec4 a_fg;\n"
    "attribute vec4 a_bg;\n"
    "attribute float a_glyph;\n"
    "attribute float a_band0;\n"   /* selection band left edge (absolute px), 0 if none */
    "attribute float a_band1;\n"   /* selection band right edge (absolute px), 0 if none */
    "uniform mat4 u_mvp;\n"
    "varying vec2 v_tex;\n"
    "varying vec4 v_fg;\n"
    "varying vec4 v_bg;\n"
    "varying float v_glyph;\n"
    "varying float v_px;\n"        /* fragment absolute pixel x */
    "varying float v_band0;\n"
    "varying float v_band1;\n"
    "void main(void) {\n"
    "    vec2 p = a_origin + a_pos * a_size;\n"
    "    gl_Position = u_mvp * vec4(p, 0.0, 1.0);\n"
    "    v_tex = mix(a_tex0, a_tex1, a_pos);\n"
    "    v_fg = a_fg;\n"
    "    v_bg = a_bg;\n"
    "    v_glyph = a_glyph;\n"
    "    v_px = p.x;\n"
    "    v_band0 = a_band0;\n"
    "    v_band1 = a_band1;\n"
    "}\n";

static const char *fs_src =
    "#version 120\n"
    "uniform sampler2D u_atlas;\n"      /* unit 0: alpha8 mono glyph atlas */
    "uniform sampler2D u_catlas;\n"     /* unit 1: RGBA8 color emoji atlas */
    "uniform sampler2D u_img;\n"        /* unit 2: the RGBA8 texture of one application picture */
    "uniform vec4 u_sel;\n"        /* selection highlight colour */
    "varying vec2 v_tex;\n"
    "varying vec4 v_fg;\n"
    "varying vec4 v_bg;\n"
    "varying float v_glyph;\n"
    "varying float v_px;\n"
    "varying float v_band0;\n"
    "varying float v_band1;\n"
    "void main(void) {\n"
    /* No GL blending: the shader composites as premul + bg*(1-a), picking the background per fragment.
       v_bg.a carries the window opacity; ink must NOT be translucent, and the ARGB buffer is
       premultiplied. */
    "    vec4 bg = v_bg;\n"
    "    if (v_glyph > 0.5 && v_px >= v_band0 && v_px < v_band1)\n"
    "        bg = u_sel;\n"
    "    vec3 bgp = bg.rgb * bg.a;\n"      /* the background behind this fragment, premultiplied */
    /* a_glyph: 0 background quad, 1 mono glyph (alpha8, tinted by v_fg), 2 color emoji, 3 an application
       picture. A color texel is already premultiplied, so it is deliberately not tinted; v_fg is white so
       ATTR_DIM still works without a new attribute. */
    "    if (v_glyph > 2.5) {\n"
    "        vec4 c = texture2D(u_img, v_tex);\n"
    "        gl_FragColor = vec4(c.rgb + bgp * (1.0 - c.a), c.a + bg.a * (1.0 - c.a));\n"
    "        return;\n"
    "    }\n"
    "    if (v_glyph > 1.5) {\n"
    "        vec4 c = texture2D(u_catlas, v_tex) * v_fg;\n"
    "        gl_FragColor = vec4(c.rgb + bgp * (1.0 - c.a), c.a + bg.a * (1.0 - c.a));\n"
    "        return;\n"
    "    }\n"
    "    float a = texture2D(u_atlas, v_tex).r;\n"
    "    a *= v_glyph;\n"
    /* Source-over in premultiplied space: the alpha is the ink's coverage PLUS whatever of the
       (translucent) background is still visible under it. NOT max(a, bg.a) — that forced alpha 1 across
       the whole quad rectangle and painted a glyph's bounding box near-black on a transparent window. */
    "    gl_FragColor = vec4(v_fg.rgb * a + bgp * (1.0 - a), a + bg.a * (1.0 - a));\n"
    "}\n";

/* unit quad geometry: two triangles in [0,1]^2 (per-vertex corners for the instanced path) */
static const float quad_verts[12] = {
    0, 0,  1, 0,  1, 1,
    0, 0,  1, 1,  0, 1,
};

typedef GLvoid (*fn_set_divisor)(GLuint, GLuint);
typedef GLvoid (*fn_draw_arrays_instanced)(GLenum, GLint, GLsizei, GLsizei);

static void col2f(uint32_t argb, float *rgba)
{
    rgba[0] = ((argb >> 16) & 0xff) / 255.0f;
    rgba[1] = ((argb >> 8) & 0xff) / 255.0f;
    rgba[2] = (argb & 0xff) / 255.0f;
    rgba[3] = ((argb >> 24) & 0xff) / 255.0f;
}

static unsigned compile_prog(Render *r)
{
    unsigned vs = glCreateShader(GL_VERTEX_SHADER);
    unsigned fs = glCreateShader(GL_FRAGMENT_SHADER);
    const char *v = vs_src, *f = fs_src;
    glShaderSource(vs, 1, &v, NULL);
    glShaderSource(fs, 1, &f, NULL);
    glCompileShader(vs);
    glCompileShader(fs);
    int ok = 0;
    glGetShaderiv(vs, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(vs, sizeof(log), NULL, log);
        klog(L_ERROR, "vertex shader: %s", log);
        return 0;
    }
    glGetShaderiv(fs, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(fs, sizeof(log), NULL, log);
        klog(L_ERROR, "fragment shader: %s", log);
        return 0;
    }
    unsigned p = glCreateProgram();
    glBindAttribLocation(p, 0, "a_pos");
    glBindAttribLocation(p, 1, "a_origin");
    glBindAttribLocation(p, 2, "a_size");
    glBindAttribLocation(p, 3, "a_tex0");
    glBindAttribLocation(p, 4, "a_tex1");
    glBindAttribLocation(p, 5, "a_fg");
    glBindAttribLocation(p, 6, "a_bg");
    glBindAttribLocation(p, 7, "a_glyph");
    glBindAttribLocation(p, 8, "a_band0");
    glBindAttribLocation(p, 9, "a_band1");
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(p, sizeof(log), NULL, log);
        klog(L_ERROR, "program link: %s", log);
        glDeleteProgram(p);
        return 0;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    r->u_mvp = glGetUniformLocation(p, "u_mvp");
    r->u_atlas = glGetUniformLocation(p, "u_atlas");
    r->u_catlas = glGetUniformLocation(p, "u_catlas");
    r->u_img = glGetUniformLocation(p, "u_img");
    r->u_sel = glGetUniformLocation(p, "u_sel");
    return p;
}

static void setup_gl(Render *r)
{
    glGenVertexArrays(1, &r->vao);
    glBindVertexArray(r->vao);

    /* static unit-quad corner buffer (a_pos) for the instanced path */
    glGenBuffers(1, &r->quad_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, r->quad_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad_verts), quad_verts, GL_STATIC_DRAW);

    /* dynamic buffer: per-instance records (instanced) or expanded per-vertex (fallback) */
    glGenBuffers(1, &r->vbo);

    for (unsigned i = 0; i < 10; i++)
        glEnableVertexAttribArray(i);

    /* Load the ARB instancing entry points. They are ARB extensions on a GL 2.1 context
       (merged into core only in GL 3.2), so fetch them via eglGetProcAddress and require
       BOTH extension names in GL_EXTENSIONS. There is no GL_ARB_vertex_instanced. */
    r->set_div = (void *)eglGetProcAddress("glVertexAttribDivisorARB");
    r->draw_inst = (void *)eglGetProcAddress("glDrawArraysInstancedARB");
    const char *exts = (const char *)glGetString(GL_EXTENSIONS);
    r->inst_ok = (r->set_div && r->draw_inst && exts &&
                  strstr(exts, "GL_ARB_instanced_arrays") &&
                  strstr(exts, "GL_ARB_draw_instanced")) ? 1 : 0;
    if (r->inst_ok)
        klog(L_INFO, "instancing enabled (GL_ARB_draw_instanced + GL_ARB_instanced_arrays)");
    else
        klog(L_INFO, "instancing unavailable; using per-vertex expansion");
}

/* Clear colour = the configured background, premultiplied by the window opacity. The padding margin and the
   whole-cell remainder are never covered by a quad, and a resized buffer has no defined contents. */
static void set_clear_color(Render *r)
{
    float a = r->opacity;
    glClearColor(((r->bg >> 16) & 0xff) / 255.0f * a,
                 ((r->bg >> 8) & 0xff) / 255.0f * a,
                 (r->bg & 0xff) / 255.0f * a, a);
}

int render_init(Render *r, void *wl_display, void *egl_window, int w, int h, float opacity,
                uint32_t bg, uint32_t fg, uint32_t cursor_color, uint32_t selection_color,
                int cursor_width, int bell_on)
{
    memset(r, 0, sizeof(*r));
    r->vw = w;
    r->vh = h;
    /* Nothing is hovered until main.c says so. The field is a TAB index, so the memset's 0 would otherwise read as
       "the run under the pointer is in tab 0" — and a frame drawn before redraw() filled the field in would underline
       a run nobody is pointing at. */
    r->hover_tab = -1;

    PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)(uintptr_t)eglGetProcAddress("eglGetPlatformDisplayEXT");
    EGLDisplay dpy;
    if (get_platform)
        dpy = get_platform(EGL_PLATFORM_WAYLAND_EXT, wl_display, NULL);
    else
        dpy = eglGetDisplay((EGLNativeDisplayType)wl_display);
    if (dpy == EGL_NO_DISPLAY) {
        klog(L_ERROR, "eglGetDisplay failed");
        return -1;
    }
    EGLint vmaj, vmin;
    if (!eglInitialize(dpy, &vmaj, &vmin)) {
        klog(L_ERROR, "eglInitialize failed: 0x%x", eglGetError());
        return -1;
    }

    /* Wayland transparency needs an ARGB window visual + alpha in the clear
       colour; no EGL extension attributes are required (EGL_TRANSPARENT_*_EXT
       is an X11-era extension and is absent from the installed eglext.h). */
    EGLint cfg_attrs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_NONE
    };
    const int cfg_n = (int)(sizeof(cfg_attrs) / sizeof(cfg_attrs[0]));

    EGLConfig cfg;
    EGLint ncfg = 0;
    if (!eglChooseConfig(dpy, cfg_attrs, &cfg, 1, &ncfg) || ncfg == 0) {
        /* Fallback: drop the EGL_ALPHA_SIZE attribute pair (compositor/driver
           without an ARGB visual). Build it by element count, not sizeof bytes. */
        EGLint fallback[16];
        int fn = 0;
        for (int i = 0; i < cfg_n; i++) {
            if (cfg_attrs[i] == EGL_ALPHA_SIZE) { i++; continue; }  /* skip attr + its value */
            fallback[fn++] = cfg_attrs[i];
        }
        fallback[fn++] = EGL_NONE;
        if (!eglChooseConfig(dpy, fallback, &cfg, 1, &ncfg) || ncfg == 0) {
            klog(L_ERROR, "eglChooseConfig failed: 0x%x", eglGetError());
            return -1;
        }
    }

    EGLint win_id = 0;
    eglGetConfigAttrib(dpy, cfg, EGL_NATIVE_VISUAL_ID, &win_id);

    EGLSurface surf = eglCreateWindowSurface(dpy, cfg, (EGLNativeWindowType)egl_window, NULL);
    if (surf == EGL_NO_SURFACE) {
        klog(L_ERROR, "eglCreateWindowSurface failed: 0x%x", eglGetError());
        return -1;
    }

    if (!eglBindAPI(EGL_OPENGL_API)) {
        klog(L_ERROR, "eglBindAPI(EGL_OPENGL_API) failed");
        return -1;
    }
    EGLint ctx_attrs[] = { EGL_CONTEXT_MAJOR_VERSION, 2, EGL_CONTEXT_MINOR_VERSION, 1, EGL_NONE };
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attrs);
    if (ctx == EGL_NO_CONTEXT) {
        ctx_attrs[3] = 0;
        ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attrs);
    }
    if (ctx == EGL_NO_CONTEXT) {
        klog(L_ERROR, "eglCreateContext failed: 0x%x", eglGetError());
        return -1;
    }
    if (!eglMakeCurrent(dpy, surf, surf, ctx)) {
        klog(L_ERROR, "eglMakeCurrent failed: 0x%x", eglGetError());
        return -1;
    }

    r->edpy = dpy;
    r->esurf = surf;
    r->ectx = ctx;
    r->nwin = egl_window;
    r->opacity = fclamp_(opacity, 0.0f, 1.0f);
    r->bg = bg;
    r->fg = fg;   /* the tab bar's text colour (see Render.fg) */
    r->cursor_color = cursor_color;   /* the `cursor_color =` line (see Render.cursor_color) */
    r->selection_color = selection_color;   /* the `selection_color =` line (see Render.selection_color) */
    r->cursor_width = cursor_width;   /* the `cursor_width =` line (see Render.cursor_width) */
    r->bell_on = bell_on;             /* the `bell =` line (see Render.bell_on) */
    r->ecfg = malloc(sizeof(cfg));
    if (r->ecfg)
        memcpy(r->ecfg, &cfg, sizeof(cfg));
    (void)win_id;

    klog(L_INFO, "GL: %s | %s", glGetString(GL_VERSION), glGetString(GL_RENDERER));

    r->prog = compile_prog(r);
    if (!r->prog)
        return -1;
    setup_gl(r);

    /* The buffer must start as the CONFIGURED background, not DEF_BG: with padding, the margin is painted only
       by the clear, so a hardcoded colour shows up as a differently-coloured frame. */
    set_clear_color(r);
    glClear(GL_COLOR_BUFFER_BIT);
    /* Do NOT swap here: attaching a buffer before the first xdg_surface.configure is
       acked is a Wayland protocol error. The first eglSwapBuffers happens in render_draw. */
    (void)win_id;
    return 0;
}

static float *ensure_vdata(Render *r, int max_quads)
{
    /* The caller counts the quads this frame can emit: at most 3 per grid cell (background + selection
       band + glyph) plus the chrome (one background and one separator per bar item, one glyph per label
       character, four for the focus border). */
    int need = max_quads * FLOATS_PER_INST;
    if (need > r->vcap) {
        free(r->vdata);
        r->vcap = need;
        r->vdata = malloc((size_t)need * sizeof(float));
        if (!r->vdata)
            abort();
    }
    return r->vdata;
}

static float *ensure_edata(Render *r, int max_quads)
{
    /* fallback expansion: 6 vertices per quad, FLOATS_PER_VERT floats per vertex */
    int need = max_quads * 6 * FLOATS_PER_VERT;
    if (need > r->ecap) {
        free(r->edata);
        r->ecap = need;
        r->edata = malloc((size_t)need * sizeof(float));
        if (!r->edata)
            abort();
    }
    return r->edata;
}

/* emit one quad as a single per-instance record (19 floats).
   Texcoords are Y-flipped (atlas rows go top-down, GL y goes bottom-up): tex0 = corner(0,0)
   = (u0,v1), tex1 = corner(1,1) = (u1,v0). The shader derives the other two corners. */
static void emit_quad(float **ip, float x, float y, float w, float h,
                      float u0, float v0, float u1, float v1,
                      const float *fg, const float *bg, float glyph,
                      float band0, float band1)
{
    float *v = *ip;
    *v++ = x;  *v++ = y;                 /* origin */
    *v++ = w;  *v++ = h;                 /* size */
    *v++ = u0; *v++ = v1;                /* tex0 */
    *v++ = u1; *v++ = v0;                /* tex1 */
    *v++ = fg[0]; *v++ = fg[1]; *v++ = fg[2]; *v++ = fg[3];
    *v++ = bg[0]; *v++ = bg[1]; *v++ = bg[2]; *v++ = bg[3];
    *v++ = glyph;
    *v++ = band0;
    *v++ = band1;
    *ip = v;
}

/* expand per-instance records into interleaved per-vertex data (fallback path):
   for each instance, for each of the 6 unit corners, write corner(2) + record(19). */
static void expand_instances(const float *inst, int nquads, float *out)
{
    float *o = out;
    for (int i = 0; i < nquads; i++) {
        const float *rec = inst + i * FLOATS_PER_INST;
        for (int c = 0; c < 6; c++) {
            *o++ = quad_verts[c * 2];
            *o++ = quad_verts[c * 2 + 1];
            memcpy(o, rec, FLOATS_PER_INST * sizeof(float));
            o += FLOATS_PER_INST;
        }
    }
}

static void set_attribs_interleaved(void)
{
    const GLint s = FLOATS_PER_VERT * sizeof(float);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, s, (const void *)(0 * sizeof(float)));
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, s, (const void *)(2 * sizeof(float)));
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, s, (const void *)(4 * sizeof(float)));
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, s, (const void *)(6 * sizeof(float)));
    glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, s, (const void *)(8 * sizeof(float)));
    glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, s, (const void *)(10 * sizeof(float)));
    glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, s, (const void *)(14 * sizeof(float)));
    glVertexAttribPointer(7, 1, GL_FLOAT, GL_FALSE, s, (const void *)(18 * sizeof(float)));
    glVertexAttribPointer(8, 1, GL_FLOAT, GL_FALSE, s, (const void *)(19 * sizeof(float)));
    glVertexAttribPointer(9, 1, GL_FLOAT, GL_FALSE, s, (const void *)(20 * sizeof(float)));
}

static void set_attribs_instanced(Render *r)
{
    fn_set_divisor set_div = (fn_set_divisor)r->set_div;
    const GLint s = FLOATS_PER_INST * sizeof(float);
    /* a_pos from the static unit-quad VBO (per-vertex, divisor 0) */
    glBindBuffer(GL_ARRAY_BUFFER, r->quad_vbo);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (const void *)0);
    set_div(0, 0);
    /* per-instance attributes from the dynamic VBO (divisor 1) */
    glBindBuffer(GL_ARRAY_BUFFER, r->vbo);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, s, (const void *)(0 * sizeof(float)));
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, s, (const void *)(2 * sizeof(float)));
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, s, (const void *)(4 * sizeof(float)));
    glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, s, (const void *)(6 * sizeof(float)));
    glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, s, (const void *)(8 * sizeof(float)));
    glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, s, (const void *)(12 * sizeof(float)));
    glVertexAttribPointer(7, 1, GL_FLOAT, GL_FALSE, s, (const void *)(16 * sizeof(float)));
    glVertexAttribPointer(8, 1, GL_FLOAT, GL_FALSE, s, (const void *)(17 * sizeof(float)));
    glVertexAttribPointer(9, 1, GL_FLOAT, GL_FALSE, s, (const void *)(18 * sizeof(float)));
    for (unsigned i = 1; i < 10; i++)
        set_div(i, 1);
}

/* ---------- application pictures: one GL texture per model image ---------- */

#define IMAGE_TEX_MAX 512      /* textures held at once — more than any screen can show */
#define IMG_VIEW_MAX  64       /* pictures of one pane considered per frame */
#define CATLAS_RETRY_FRAMES 60 /* frames to wait before re-trying a refused color-atlas upload (~1 s) */

/* A refused texture upload is retried every frame while the GPU stays short of memory, so one error
   line per frame would bury everything else the log has to say. 1 on the first failure and then about
   once every two seconds at 60 fps; the caller resets its counter when an upload succeeds. */
static int log_rate(int *streak)
{
    return (++*streak - 1) % 120 == 0;
}

/* Drop every texture that is not a glyph atlas: the application-picture cache and the file-manager
   preview. Called when an atlas upload was refused — a picture can be rebuilt from the pixels the model
   still holds, a glyph cannot. Returns how many textures were dropped. */
static int free_picture_textures(Render *r)
{
    int n = 0;
    for (int i = 0; i < r->nimgtex; i++)
        if (r->imgtex[i].tex) {
            GLuint t = r->imgtex[i].tex;
            glDeleteTextures(1, &t);
            n++;
        }
    r->nimgtex = 0;          /* the array stays allocated; render_destroy() walks this count */
    if (r->fm_tex) {
        GLuint t = r->fm_tex;
        glDeleteTextures(1, &t);
        r->fm_tex = 0;
        r->fm_tex_uid = 0;
        r->fm_tex_w = r->fm_tex_h = 0;
        n++;
    }
    return n;
}

/* The texture holding one image's texels: created on first use, re-uploaded when the model bumped
   `version` (an animation frame replaces the pixels). Keyed by KImage.serial, which the model never
   reuses, so a recycled KImage pointer can never make this cache hand back a stale texture. */
static unsigned img_tex(Render *r, KImage *im, int frame)
{
    ImgTex *sl = NULL;
    for (int i = 0; i < r->nimgtex; i++)
        if (r->imgtex[i].serial == im->serial) {
            sl = &r->imgtex[i];
            break;
        }
    if (!sl) {
        if (r->nimgtex >= r->imgtex_cap) {
            int cap = r->imgtex_cap ? r->imgtex_cap * 2 : 16;
            if (cap > IMAGE_TEX_MAX)
                cap = IMAGE_TEX_MAX;
            if (r->nimgtex >= cap)
                return 0;
            ImgTex *nt = realloc(r->imgtex, (size_t)cap * sizeof *nt);
            if (!nt)
                return 0;
            r->imgtex = nt;
            r->imgtex_cap = cap;
        }
        sl = &r->imgtex[r->nimgtex++];
        memset(sl, 0, sizeof *sl);
        sl->serial = im->serial;
        GLuint t = 0;
        glGenTextures(1, &t);
        sl->tex = t;
    }
    sl->seen = frame;
    if (sl->version != im->version || sl->w != im->w || sl->h != im->h) {
        glBindTexture(GL_TEXTURE_2D, sl->tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, im->w, im->h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, im->rgba);
        GLenum err = glGetError();
        if (err != GL_NO_ERROR) {
            /* A draw that samples a texture with no storage faults inside libnvidia-eglcore. version/w/h
               keep their old values so the next frame retries the upload. */
            if (log_rate(&r->img_upload_fails))
                klog(L_WARN, "image glTexImage2D failed: 0x%x (%dx%d) — the picture is skipped",
                     err, im->w, im->h);
            return 0;
        }
        r->img_upload_fails = 0;
        /* LINEAR, not NEAREST: a picture is scaled from its own pixel grid to the cell grid, and
           nearest turns that into a mosaic. No mipmaps, so a non-power-of-two size is legal on a
           GL 2.1 context with CLAMP_TO_EDGE. */
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        sl->version = im->version;
        sl->w = im->w;
        sl->h = im->h;
    }
    return sl->tex;
}

/* Drop the textures of pictures that were not on screen this frame, so the GPU never holds more than
   what the user can see and an image the model evicted loses its texture in the same frame. */
static void img_tex_sweep(Render *r, int frame)
{
    int keep = 0;
    for (int i = 0; i < r->nimgtex; i++) {
        if (r->imgtex[i].seen == frame) {
            if (keep != i)
                r->imgtex[keep] = r->imgtex[i];
            keep++;
        } else if (r->imgtex[i].tex) {
            GLuint t = r->imgtex[i].tex;
            glDeleteTextures(1, &t);
        }
    }
    r->nimgtex = keep;
}

/* Upload one batch of instance records and draw it. Reusing r->vbo per batch is safe: glBufferData
   copies the staging data into driver storage before the next batch overwrites it. */
static void draw_quads(Render *r, const float *inst, int nquads)
{
    if (nquads <= 0)
        return;
    if (r->inst_ok) {
        fn_draw_arrays_instanced draw = (fn_draw_arrays_instanced)r->draw_inst;
        glBindBuffer(GL_ARRAY_BUFFER, r->vbo);
        glBufferData(GL_ARRAY_BUFFER, (size_t)nquads * FLOATS_PER_INST * sizeof(float),
                     inst, GL_STREAM_DRAW);
        set_attribs_instanced(r);
        draw(GL_TRIANGLES, 0, 6, nquads);
    } else {
        float *edata = ensure_edata(r, nquads);
        expand_instances(inst, nquads, edata);
        glBindBuffer(GL_ARRAY_BUFFER, r->vbo);
        glBufferData(GL_ARRAY_BUFFER, (size_t)nquads * 6 * FLOATS_PER_VERT * sizeof(float),
                     edata, GL_STREAM_DRAW);
        set_attribs_interleaved();
        glDrawArrays(GL_TRIANGLES, 0, nquads * 6);
    }
}

/* The GL state every batch shares. Called before each batch: an atlas texture can be created between
   batches (the glyph lookups run while the quads are emitted) and the picture sampler must point at a
   bound texture when nothing is on screen. */
static void setup_draw_state(Render *r, const Font *f, const float *selc)
{
    glViewport(0, 0, r->vw, r->vh);
    glUseProgram(r->prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, f->atlas_tex);
    glUniform1i(r->u_atlas, 0);
    if (f->catlas_tex) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, f->catlas_tex);
        glUniform1i(r->u_catlas, 1);
    } else {
        /* no color glyph has ever been shaped: point the second sampler at the mono atlas so no
           texture unit is left unbound (no quad in this frame uses a_glyph == 2 anyway) */
        glUniform1i(r->u_catlas, 0);
    }
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, f->atlas_tex);
    glUniform1i(r->u_img, 2);
    glActiveTexture(GL_TEXTURE0);
    glUniform4f(r->u_sel, selc[0], selc[1], selc[2], selc[3]);
    float mvp[16] = {
        2.0f / r->vw, 0, 0, 0,
        0, 2.0f / r->vh, 0, 0,
        0, 0, -1, 0,
        -1, -1, 0, 1,
    };
    glUniformMatrix4fv(r->u_mvp, 1, GL_FALSE, mvp);
}

/* ---------- the frame: the tab bar plus one grid per pane ---------- */

/* Blend two ARGB colours, k100 = percent of b (0..100). Only the CHROME uses this: the cells keep their
   own colours untouched, so an application that repaints its palette still cannot repaint the bar. */
static uint32_t mix_c(uint32_t a, uint32_t b, int k100)
{
    uint32_t out = 0;
    for (int sh = 0; sh < 32; sh += 8) {
        int av = (int)((a >> sh) & 0xff), bv = (int)((b >> sh) & 0xff);
        int v = av + (bv - av) * k100 / 100;
        out |= (uint32_t)(v < 0 ? 0 : (v > 255 ? 255 : v)) << sh;
    }
    return out;
}

/* The bell's colour: paper mixed toward ink by BELL_MIX_MAX at full strength, fading back to paper as
   strength falls, so the last frame is indistinguishable from no flash. The clamp guards a public entry
   point; bell_strength() is already bounded. */
uint32_t render_bell(uint32_t bg, uint32_t fg, int strength)
{
    if (strength <= 0)
        return bg;
    if (strength > 1000)
        strength = 1000;
    return mix_c(bg, fg, BELL_MIX_MAX * strength / 1000);
}

/* One place derives every CHROME colour, so a configured colour can be checked where it becomes a vertex
   colour, not only where it is parsed (see Chrome in kai-term.h). */
void render_chrome(const Render *r, Chrome *out)
{
    out->sel_bg      = r->selection_color;
    out->item_off_bg = mix_c(r->bg, r->fg, 10);
    out->item_off_fg = mix_c(r->fg, r->bg, 45);
    out->item_on_bg  = r->selection_color;
    out->sep         = mix_c(r->fg, r->bg, 65);
    out->cursor      = r->cursor_color;
    out->cursor_off  = mix_c(r->cursor_color, r->bg, 55);
    out->border      = r->selection_color;
    /* Config.selection_color owns every "this is the thing in this window" signal — the band, the active
       tab item, the focus border and this question. */
    out->notice_bg   = r->selection_color;
    out->notice_fg   = r->fg;
    /* With no GL blending, "in front of" has to be earned by a colour that differs from what is behind.
       No takes the palette's red, so a theme or an OSC 4 repaint moves it. */
    out->dialog_bg     = mix_c(r->bg, r->fg, 15);
    out->dialog_border = r->selection_color;
    out->dialog_fg     = r->fg;
    out->dialog_yes    = r->selection_color;
    out->dialog_no     = term_palette()[1];
    out->dialog_btn_bg = r->bg;
}

/* ONE rule for a line under a cell's characters, asked by both underlines this renderer draws. A quad's
   y is its BOTTOM edge measured UP from the cell's bottom edge, so `cell_h - h` is the TOP of the cell —
   a strikethrough. */
static int underline_y(int cell_h, int ascent_px, int descent_px, int h)
{
    int baseline = cell_h - ascent_px;      /* px from the cell's bottom edge up to the baseline */
    if (baseline < 0)
        baseline = 0;                       /* a cell shorter than the ascent: the baseline is the floor */
    int lo = baseline - (descent_px > 0 ? descent_px : 0);   /* the bottom of the descender band */
    if (lo > baseline - h)                  /* the band is thinner than the line: keep it under the baseline */
        lo = baseline - h;
    if (lo < 0)
        lo = 0;                             /* never below the cell */
    return lo;
}

/* The default is a bar and there is no `cursor_style` key; an application asks for another shape with
   DECSCUSR. The clamp is the whole geometry: a bar wider than cell/3 is a block wearing a hat, and the
   floor of 1 matters because a cell can be narrower than 3 px. */
void render_cursor(int style, int visible, int band_on, int cell_w, int cell_h, int width_px,
                   int ascent_px, int descent_px, CursorDraw *o)
{
    o->block = o->quad = o->invert = o->tint = 0;
    o->x = o->y = o->w = o->h = 0;
    if (!visible)
        return;                              /* DEC 25l, scrolled back, or the blink phase off */
    if (style == CUR_UNSET)
        style = CUR_BAR;

    if (style == CUR_BLOCK) {
        /* The block IS the cell's own background, so a band that covers this cell paints over it: the
           background step has to skip exactly the cells the band step paints, or a selection ending on
           an empty cursor cell erases the cursor without replacing it. The ink is tinted, not inverted. */
        o->block  = band_on ? 0 : 1;
        o->invert = band_on ? 0 : 1;
        o->tint   = band_on ? 1 : 0;
        return;
    }

    int w = width_px > 0 ? width_px : 1;
    if (style == CUR_BAR) {
        int cap = cell_w / 3;
        if (cap < 1)
            cap = 1;
        if (w > cap)
            w = cap;
        o->x = 0; o->y = 0; o->w = w; o->h = cell_h;
    } else {                                  /* CUR_UNDERLINE */
        int cap = cell_h / 3;
        if (cap < 1)
            cap = 1;
        if (w > cap)
            w = cap;
        o->x = 0; o->y = underline_y(cell_h, ascent_px, descent_px, w);
        o->w = cell_w; o->h = w;
    }
    o->quad = 1;                              /* its own quad, emitted after the bands */
}

/* The hover underline's rect: deliberately the CUR_UNDERLINE shape, asked of the same underline_y() the
   cursor asks, so the two underlines cannot disagree. Full cell width makes a run read as ONE underlined
   address. Thickness is a fraction of the cell, not `cursor_width` — that key belongs to the cursor. */
void render_hover(int cell_w, int cell_h, int ascent_px, int descent_px, HoverDraw *o)
{
    o->x = o->y = o->w = o->h = 0;
    if (cell_w <= 0 || cell_h <= 0)
        return;                              /* no cell to underline */
    int h = cell_h / 12;
    if (h < 1)
        h = 1;
    int cap = cell_h / 3;
    if (cap < 1)
        cap = 1;
    if (h > cap)
        h = cap;
    o->x = 0;
    o->y = underline_y(cell_h, ascent_px, descent_px, h);   /* under the baseline, inside the cell */
    o->w = cell_w;
    o->h = h;
}

/* Decode one UTF-8 sequence of a tab-bar label. A tab's title arrives through OSC 0/2, so a label is
   arbitrary UTF-8; a stray continuation byte becomes U+FFFD, which the font draws as its notdef box. */
static uint32_t next_cp(const char **pp)
{
    const unsigned char *s = (const unsigned char *)*pp;
    uint32_t cp = *s++;
    int need = 0;
    if (cp >= 0xf0) { cp &= 0x07u; need = 3; }
    else if (cp >= 0xe0) { cp &= 0x0fu; need = 2; }
    else if (cp >= 0xc0) { cp &= 0x1fu; need = 1; }
    else if (cp >= 0x80) { *pp = (const char *)s; return 0xfffd; }
    for (int i = 0; i < need; i++) {
        if (s[0] < 0x80 || s[0] >= 0xc0)
            break;
        cp = (cp << 6) | (uint32_t)(s[0] & 0x3f);
        s++;
    }
    *pp = (const char *)s;
    return cp;
}

/* The glyph quad of one cell: the color emoji the color font has for the whole cluster, or the mono glyph.
   A function because the file-manager panes need exactly the same rules and a second copy would drift.
   fg/bg are already resolved by the caller; ATTR_DIM is applied here so both callers dim identically. */
static int emit_cell_glyph(Font *f, const Cell *c, float px, float py, float cw, float ch,
                           float ascent, const float *fg, const float *bg,
                           float band0, float band1, float **vp)
{
    /* Color emoji first: the cell's whole cluster is shaped by HarfBuzz into ONE color glyph. When the color
       font has nothing for that sequence, fall back to the mono glyph from the fallback face. */
    uint32_t seq[CLUSTER_MAX + 1];
    int nseq = 0;
    ColorGlyph *cg = NULL;
    if (f->color_ok && !f->catlas_broken && (c->nextra || cp_is_emoji(c->ch))) {
        nseq = 1;
        seq[0] = c->ch;
        for (int k = 0; k < (int)c->nextra && nseq <= CLUSTER_MAX; k++)
            seq[nseq++] = c->extra[k];
        cg = font_color_glyph(f, seq, nseq);
    }
    Glyph *gl_ = NULL;
    if (!cg) {
        gl_ = font_glyph(f, c->ch);
        if (!gl_ || gl_->w <= 0 || gl_->h <= 0)
            return 0;
    }

    float cf[4];
    cf[0] = fg[0]; cf[1] = fg[1]; cf[2] = fg[2]; cf[3] = fg[3];

    if (cg) {
        /* The color bitmap carries UNSCALED strike metrics and f->color_px is that strike's own height, so the scale
           is cell-height / strike-height: zooming resizes the quad without re-rasterizing the emoji. */
        float s = ch / (float)f->color_px;
        float gw = (float)cg->w * s, gh = (float)cg->h * s;
        /* The span is the WHOLE cluster's width, not just the base codepoint's: vt.c reserved 2 cells for a wide
           cluster using this same predicate, so the quad always matches the reservation. */
        float span = cluster_is_wide(seq, nseq) ? 2.0f * cw : cw;
        if (gw > span) {
            float k = span / gw;
            gw *= k;
            gh *= k;
        }
        float gx = px + (span - gw) * 0.5f;   /* centered in the cell span */
        float gy = py + (ch - gh) * 0.5f;     /* centered in the line box */
        cf[0] = cf[1] = cf[2] = 1.0f; cf[3] = 1.0f;   /* color, never fg-tinted */
        if (c->attr & ATTR_DIM)
            cf[0] = cf[1] = cf[2] = 0.5f;     /* shader scales the premul texel */
        emit_quad(vp, gx, gy, gw, gh, cg->u0, cg->v0, cg->u1, cg->v1, cf, bg, 2.0f,
                  band0, band1);
        return 1;
    }

    float gw = (float)gl_->w * f->scale;
    float gh = (float)gl_->h * f->scale;
    float gx = px + (float)gl_->bearing_x * f->scale;
    /* baseline: cell_top=py+ch; baseline=cell_top-ascent;
       glyph_top=baseline+bearing_y*scale; quad_bottom=glyph_top-gh */
    float gy = py + ch - ascent + (float)gl_->bearing_y * f->scale - gh;
    if (c->attr & ATTR_DIM) {
        cf[0] *= 0.5f; cf[1] *= 0.5f; cf[2] *= 0.5f;
    }
    emit_quad(vp, gx, gy, gw, gh, gl_->u0, gl_->v0, gl_->u1, gl_->v1, cf, bg, 1.0f,
              band0, band1);
    return 1;
}

/* A rectangular border `t` px thick, drawn just OUTSIDE the rect (in the gap around it), so it never
   covers a glyph. Used for the side-by-side focus border and for the file-manager focus border. */
static void emit_border(float **vp, float x, float y, float w, float h, float t,
                        const float *bg, float du, float dv, float vh)
{
    const float white[4] = { 1, 1, 1, 1 };
    float gl_bot = vh - (y + h), gl_top = vh - y;
    emit_quad(vp, x - t, gl_top, w + 2.0f * t, t, du * 0.5f, dv * 0.5f, du * 1.5f, dv * 1.5f,
              white, bg, 0.0f, 0.0f, 0.0f);
    emit_quad(vp, x - t, gl_bot - t, w + 2.0f * t, t, du * 0.5f, dv * 0.5f, du * 1.5f, dv * 1.5f,
              white, bg, 0.0f, 0.0f, 0.0f);
    emit_quad(vp, x - t, gl_bot, t, h, du * 0.5f, dv * 0.5f, du * 1.5f, dv * 1.5f,
              white, bg, 0.0f, 0.0f, 0.0f);
    emit_quad(vp, x + w, gl_bot, t, h, du * 0.5f, dv * 0.5f, du * 1.5f, dv * 1.5f,
              white, bg, 0.0f, 0.0f, 0.0f);
}

/* Everything one pane needs to draw, computed once per pane per frame. */
typedef struct {
    Term *t;
    Grid *g;
    const PaneGeom *pg;
    int nrows, ncols;    /* the grid, clamped to what the pane can hold */
    float gx;            /* the grid's left edge in buffer px (== the pane's x: no inner margin) */
    float pad_gl;        /* GL y of the bottom of the last drawn row */
    int sel_on;
    SelSpan selspan;
} PaneDraw;

static void pane_prepare(Render *r, Term *t, const PaneGeom *pg, Font *f, PaneDraw *pd)
{
    float cw = (float)f->cell_w * f->scale;
    float ch = (float)f->cell_h * f->scale;
    pd->t = t;
    pd->g = &t->g;
    pd->pg = pg;
    /* Never draw more than the pane holds. main.c resizes every displayed session to its pane, so this
       only bites on a grid that has not caught up with a layout change yet — and then it stops the
       drawing from spilling into the neighbouring pane. */
    int cwi = imax_((int)(cw + 0.5f), 1), chi = imax_((int)(ch + 0.5f), 1);
    pd->nrows = iclamp_(pd->g->rows, 0, pg->h / chi);
    pd->ncols = iclamp_(pd->g->cols, 0, pg->w / cwi);
    pd->gx = (float)pg->x;
    /* Window y grows down while GL y grows up, and the grid's top row starts at the pane's top edge, so the
       pixels that do not fill a row fall BELOW the last row and those that do not fill a column fall to the
       RIGHT — the same remainder rule the pointer mapping depends on. */
    pd->pad_gl = (float)r->vh - (float)pg->y - (float)pd->nrows * ch;
    if (pd->pad_gl < 0.0f)
        pd->pad_gl = 0.0f;
    pd->sel_on = t->sel_active;
    if (pd->sel_on)
        term_selection_span(t, &pd->selspan);
}

/* The session a pane shows. `terms` is indexed by TAB index (see the render_draw comment in kai-term.h). */
static Term *pane_term(Term *const *terms, int tab)
{
    if (!terms || tab < 0 || tab >= TAB_MAX)
        return NULL;
    return terms[tab];
}

/* Draw every picture of a pane that is partly inside it. The model places pictures in CELLS and the protocol
   sizes them in PIXELS, so the rect is built in window px and then clipped to the pane: unclipped, a
   picture whose anchor row scrolled away would be painted over the tab bar. */
static int draw_images(Render *r, Term *t, const PaneGeom *pg, const PaneDraw *pd,
                       float cw, float ch, int frame)
{
    ImgView view[IMG_VIEW_MAX];
    int n = term_visible_images(t, view, IMG_VIEW_MAX);
    if (n <= 0 || pd->nrows <= 0 || pd->ncols <= 0)
        return 0;

    const float white[4] = { 1, 1, 1, 1 };
    int drawn = 0;

    for (int i = 0; i < n; i++) {
        KImage *im = view[i].img;
        if (!im->rgba || im->w <= 0 || im->h <= 0)
            continue;
        unsigned tex = img_tex(r, im, frame);
        if (!tex)
            continue;

        /* An explicit c=/r= scales the picture into exactly that cell area (vt.c derives the missing
           dimension from the picture's own aspect ratio); with neither, the protocol draws it at its
           native pixel size and the ceil()ed span only says how many cells it covers. */
        float w_px, h_px;
        if (im->cols > 0 || im->rows > 0) {
            w_px = (float)view[i].ccols * cw;
            h_px = (float)view[i].crows * ch;
        } else {
            w_px = (float)im->w;
            h_px = (float)im->h;
        }
        if (w_px <= 0.0f || h_px <= 0.0f)
            continue;

        float x0 = pd->gx + (float)view[i].ccol * cw + (float)im->px;
        float y0 = (float)pg->y + (float)view[i].crow * ch + (float)im->py;
        float cl = x0, ct = y0, cr = x0 + w_px, cb = y0 + h_px;
        if (cl < (float)pg->x) cl = (float)pg->x;
        if (ct < (float)pg->y) ct = (float)pg->y;
        if (cr > (float)(pg->x + pg->w)) cr = (float)(pg->x + pg->w);
        if (cb > (float)(pg->y + pg->h)) cb = (float)(pg->y + pg->h);
        if (cr <= cl || cb <= ct)
            continue;

        /* the visible sub-rectangle of the texture: v grows down through the image rows, exactly the
           atlas convention emit_quad expects (it Y-flips for GL's bottom-up y) */
        float u0 = (cl - x0) / w_px, u1 = (cr - x0) / w_px;
        float v0 = (ct - y0) / h_px, v1 = (cb - y0) / h_px;

        /* No GL blending, so a picture composites against its OWN copy of the background — the colour of
           the cell its top-left visible corner sits in, even when it spans cells with different ones. */
        const Cell *anchor = &grid_vprow(pd->g, iclamp_(view[i].crow, 0, pd->nrows - 1))
                                [iclamp_(view[i].ccol, 0, pd->ncols - 1)];
        uint32_t cellbg = (anchor->attr & ATTR_INVERSE) ? anchor->fg : anchor->bg;
        float bg[4];
        col2f(cellbg, bg);
        bg[3] *= r->opacity;

        float rec[FLOATS_PER_INST];
        float *ip = rec;
        emit_quad(&ip, cl, (float)r->vh - cb, cr - cl, cb - ct,
                  u0, v0, u1, v1, white, bg, 3.0f, 0.0f, 0.0f);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, tex);
        draw_quads(r, rec, 1);
        drawn++;
    }
    glActiveTexture(GL_TEXTURE0);
    return drawn;
}

/* ---------- the file-manager band ---------- */

/* The rectangle of one of the two band panes. which: 0 = the browser, 1 = the preview.
   Returns 0 when that pane is not part of this frame (band off, preview toggled away). */
static int fm_pane_rect(const FmGeom *fg, int which, int *x, int *y, int *w, int *h)
{
    if (!fg->on)
        return 0;
    if (which == 0) {
        *x = fg->list_x; *y = fg->list_y; *w = fg->list_w; *h = fg->list_h;
        return fg->list_cols > 0 && fg->list_rows > 0;
    }
    if (!fg->prev_on)
        return 0;
    *x = fg->prev_x; *y = fg->prev_y; *w = fg->prev_w; *h = fg->prev_h;
    return fg->prev_cols > 0 && fg->prev_rows > 0;
}

typedef struct {
    const Cell *cell;
    int stride;          /* the grid's own column count, which is what indexes cell[] */
    int nrows, ncols;    /* clamped to what the rect can hold, exactly like pane_prepare() */
    float gx;
    float pad_gl;
} FmPaneDraw;

static void fm_pane_prepare(const Render *r, const FmPane *p, int x, int y, int w, int h,
                            Font *f, FmPaneDraw *fp)
{
    float cw = (float)f->cell_w * f->scale;
    float ch = (float)f->cell_h * f->scale;
    int cwi = imax_((int)(cw + 0.5f), 1), chi = imax_((int)(ch + 0.5f), 1);
    fp->cell = p->cell;
    fp->stride = p->cols;
    fp->nrows = p->cell ? iclamp_(p->rows, 0, h / chi) : 0;
    fp->ncols = p->cell ? iclamp_(p->cols, 0, w / cwi) : 0;
    fp->gx = (float)x;
    /* Same remainder rule as a session pane: the top row starts at the rect's top edge, so the pixels
       that do not fill a whole row fall BELOW it and the GL y of the last row's bottom is
       vh - y - nrows*ch. fm_set_size() sized the grid from term_grid_geometry() on this very rect. */
    fp->pad_gl = (float)r->vh - (float)y - (float)fp->nrows * ch;
    if (fp->pad_gl < 0.0f)
        fp->pad_gl = 0.0f;
}

/* At most one preview exists at a time, so this is one texture rather than a cache, re-uploaded when
   FmPreview.uid changes. uid is never reused, so a recycled malloc address cannot paint a stale picture. */
static unsigned fm_preview_tex(Render *r, const FmPreview *pv)
{
    if (!pv || pv->kind != FMP_IMAGE || !pv->rgba || pv->w <= 0 || pv->h <= 0)
        return 0;
    if (r->fm_tex_uid == pv->uid && r->fm_tex_w == pv->w && r->fm_tex_h == pv->h)
        return r->fm_tex;

    size_t need = (size_t)pv->w * (size_t)pv->h * 4u;
    if ((size_t)r->fm_px_cap < need) {
        free(r->fm_px);
        uint8_t *buf = malloc(need);
        if (!buf) {
            r->fm_px = NULL;
            r->fm_px_cap = 0;
            return 0;
        }
        r->fm_px = buf;
        r->fm_px_cap = (int)need;
    }
    memcpy(r->fm_px, pv->rgba, need);
    img_premultiply(r->fm_px, (long long)pv->w * pv->h);

    if (!r->fm_tex)
        glGenTextures(1, &r->fm_tex);
    glBindTexture(GL_TEXTURE_2D, r->fm_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, pv->w, pv->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, r->fm_px);
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        /* Same rule as every other texture here: never hand back a texture the GPU refused to give
           storage to. draw_fm_preview() skips tex 0, and the uid/size cache is left untouched so the
           next frame retries the upload. */
        if (log_rate(&r->fm_upload_fails))
            klog(L_WARN, "fm preview glTexImage2D failed: 0x%x (%dx%d) — the preview is skipped",
                 err, pv->w, pv->h);
        return 0;
    }
    r->fm_upload_fails = 0;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    r->fm_tex_uid = pv->uid;
    r->fm_tex_w = pv->w;
    r->fm_tex_h = pv->h;
    return r->fm_tex;
}

/* Draw the preview picture, clipped to the preview rect. fm_image_fit() decided the box (aspect kept in
   PIXELS, never upscaled, centred, returned in cells AND in px); the cell box is where it goes and the
   px box is what is drawn inside it, so the ceil()-ing into cells cannot stretch the picture. */
static int draw_fm_preview(Render *r, const FileMgr *fm, const Layout *lay, float cw, float ch)
{
    const FmGeom *fgm = &lay->fm;
    if (!fm || !fgm->on || !fgm->prev_on || !fm->pv || fm->pv->kind != FMP_IMAGE)
        return 0;
    if (!fm->prev.cell || fm->prev.cols <= 0 || fm->prev.rows <= 0)
        return 0;

    int col, row, cols, rows, px_w, px_h;
    fm_image_fit(fm, imax_((int)(cw + 0.5f), 1), imax_((int)(ch + 0.5f), 1),
                 &col, &row, &cols, &rows, &px_w, &px_h);
    if (cols <= 0 || rows <= 0 || px_w <= 0 || px_h <= 0)
        return 0;

    unsigned tex = fm_preview_tex(r, fm->pv);
    if (!tex)
        return 0;

    /* the cell box, then the picture centred inside it */
    float bx = (float)fgm->prev_x + (float)col * cw;
    float by = (float)fgm->prev_y + (float)row * ch;
    float x0 = bx + ((float)cols * cw - (float)px_w) * 0.5f;
    float y0 = by + ((float)rows * ch - (float)px_h) * 0.5f;
    float cl = x0, ct = y0, cr = x0 + (float)px_w, cb = y0 + (float)px_h;
    if (cl < (float)fgm->prev_x) cl = (float)fgm->prev_x;
    if (ct < (float)fgm->prev_y) ct = (float)fgm->prev_y;
    if (cr > (float)(fgm->prev_x + fgm->prev_w)) cr = (float)(fgm->prev_x + fgm->prev_w);
    if (cb > (float)(fgm->prev_y + fgm->prev_h)) cb = (float)(fgm->prev_y + fgm->prev_h);
    if (cr <= cl || cb <= ct)
        return 0;

    float u0 = (cl - x0) / (float)px_w, u1 = (cr - x0) / (float)px_w;
    float v0 = (ct - y0) / (float)px_h, v1 = (cb - y0) / (float)px_h;

    /* No GL blending, so the quad composites against its own copy of the background under it: the
       preview pane's cell there, which the cell-background step already painted. fm.c leaves the cells
       under a picture blank on purpose so the picture sits on the pane background. */
    int crow = iclamp_(row, 0, fm->prev.rows - 1);
    int ccol = iclamp_(col, 0, fm->prev.cols - 1);
    const Cell *anchor = &fm->prev.cell[(size_t)crow * (size_t)fm->prev.cols + (size_t)ccol];
    float bg[4];
    col2f(anchor->bg, bg);
    bg[3] *= r->opacity;

    const float white[4] = { 1, 1, 1, 1 };
    float rec[FLOATS_PER_INST];
    float *ip = rec;
    emit_quad(&ip, cl, (float)r->vh - cb, cr - cl, cb - ct, u0, v0, u1, v1, white, bg, 3.0f,
              0.0f, 0.0f);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, tex);
    draw_quads(r, rec, 1);
    glActiveTexture(GL_TEXTURE0);
    return 1;
}

int render_draw(Render *r, Font *f, const Layout *lay, Term *const *terms, const FileMgr *fm)
{
    glClear(GL_COLOR_BUFFER_BIT);

    float cw = (float)f->cell_w * f->scale;
    float ch = (float)f->cell_h * f->scale;
    float ascent = (float)f->ascent * f->scale;
    /* The font's own descent, scaled exactly like the cell box. The glyphs need only `ascent` (a glyph's quad is
       placed from the baseline, which is cell_top - ascent); the two UNDERLINES need the descent too, because "under
       the text" means under the baseline by as much as this font's descenders reach — see underline_y(). */
    float descent = (float)f->descent * f->scale;
    const float white[4] = { 1, 1, 1, 1 };
    const float du = 1.0f / (float)f->atlas_w, dv = 1.0f / (float)f->atlas_h;

    /* Window transparency (Config.opacity): the cell/selection/cursor and chrome colours are opaque
       ARGB, so the alpha is scaled here and carried to the shader in a_bg.a. Ink keeps alpha 1 (see
       fs_src), only the background becomes see-through. opacity == 1 leaves every value untouched. */
    float selc[4];
    Chrome chrome;
    render_chrome(r, &chrome);
    col2f(chrome.sel_bg, selc);
    selc[3] *= r->opacity;

    /* The rest of the chrome colours come from render_chrome() (see Chrome in kai-term.h). The cursor of
       a session that is DRAWN but not the visible one is dimmed, so a side-by-side window still shows
       which session the keyboard talks to. */

    /* Staging space: at most 3 quads per grid cell, (BAR_LABEL_MAX + 2) per bar item, NOTICE_MAX + 1 for the
       confirm-quit row, the same argument for the dialog's lines and labels, and 4 for the border. */
    int cells = 0;
    for (int p = 0; p < lay->npane; p++)
        cells += lay->pane[p].cols * lay->pane[p].rows;
    int fmcells = 0;
    if (fm && fm->on) {
        fmcells += lay->fm.list_cols * lay->fm.list_rows;
        if (lay->fm.prev_on)
            fmcells += lay->fm.prev_cols * lay->fm.prev_rows;
    }
    /* +8: the focus border is 4 quads and a pane border and a band border can share a frame. +npane: the bell is
       at most one quad per drawn pane. +URL_MAX: the hover underline is at most one quad per cell of the
       hovered run, and a longer run is refused before it can reach here. */
    int maxq = 3 * cells + 3 * fmcells + lay->nitem * (BAR_LABEL_MAX + 2)
             + (lay->notice_on ? NOTICE_MAX + 1 : 0)
             + (lay->dialog.on ? DIALOG_LINES * DIALOG_LINE_MAX + 2 * DIALOG_BTN_MAX + 7 : 0)
             + lay->npane
             + URL_MAX
             + 8;
    float *vdata = ensure_vdata(r, maxq);
    float *vp = vdata;
    int nquads = 0;
    float bg[4], fg[4];

    /* Back to front over EVERY pane: cell backgrounds, selection bands, cursor and hover quads, glyphs.
       All backgrounds go before any glyph because a wide glyph's quad spans into the next cell, and the
       two draws are split there so the pictures land between them — over the paper, under the text. */

    /* Cell backgrounds (always, so every pane is fully painted). An unselected BLOCK cursor paints its
       block here; a bar or an underline does not — it is its own quad in batch B (see render_cursor).
       A selected cursor cell keeps its own background either way: the selection band marks it. */
    for (int p = 0; p < lay->npane; p++) {
        Term *t = pane_term(terms, lay->pane[p].tab);
        if (!t)
            continue;
        PaneDraw pd;
        pane_prepare(r, t, &lay->pane[p], f, &pd);
        uint32_t cur_col = (lay->pane[p].tab == lay->active_tab) ? chrome.cursor : chrome.cursor_off;
        int phase = (lay->pane[p].tab == lay->active_tab) ? r->cursor_on : 1;
        for (int y = 0; y < pd.nrows; y++) {
            const Cell *row = grid_vprow(pd.g, y);
            for (int x = 0; x < pd.ncols; x++) {
                const Cell *c = &row[x];
                uint32_t cellbg = c->bg;
                if (c->attr & ATTR_INVERSE)
                    cellbg = c->fg;
                int is_cursor = t->show_cursor && pd.g->scroll_off == pd.g->live_off &&
                                x == t->cx && y == t->cy;
                if (is_cursor) {
                    int hl_on = 0;
                    if (pd.sel_on) {
                        float h0, hw;
                        sel_band_pixels(&pd.selspan, pd.g->cols, x, y, cw, &h0, &hw);
                        /* must match the selection bands exactly: the block cursor is hidden
                           only where a band is actually painted, otherwise a selection covering
                           the (empty) cursor cell would erase the cursor without replacing it */
                        hl_on = hw > 0.0f && term_cell_highlightable(t, x, y);
                    }
                    CursorDraw cd;
                    render_cursor(t->cur_style, phase, hl_on, (int)cw, (int)ch,
                                  r->cursor_width, (int)ascent, (int)descent, &cd);
                    if (cd.block)
                        cellbg = cur_col;
                }
                col2f(cellbg, bg);
                bg[3] *= r->opacity;
                float px = pd.gx + x * cw, py = (pd.nrows - 1 - y) * ch + pd.pad_gl;
                emit_quad(&vp, px, py, cw, ch, du * 0.5f, dv * 0.5f, du * 1.5f, dv * 1.5f,
                          white, bg, 0.0f, 0.0f, 0.0f);
                nquads++;
            }
        }
    }

    /* One quad per pane, over the cell backgrounds and under the glyphs, so the text stays readable
       while the field fades. Per PANE, not per window: only the pane whose session rang washes out. */
    for (int p = 0; p < lay->npane; p++) {
        int tab = lay->pane[p].tab;
        if (tab < 0 || tab >= TAB_MAX || !pane_term(terms, tab))
            continue;
        /* `bell = off` asks for a duration of 0, and bell_strength() already answers 0 for that (a checked
           rule), so the switch needs no branch of its own and no second number to drift. */
        int dur = r->bell_on ? BELL_FLASH_MS : 0;
        int strength = bell_strength(r->now_ms, r->bell_ms[tab], dur);
        if (strength <= 0)
            continue;
        col2f(render_bell(r->bg, r->fg, strength), bg);
        bg[3] *= r->opacity;
        emit_quad(&vp, (float)lay->pane[p].x,
                  (float)r->vh - (float)lay->pane[p].y - (float)lay->pane[p].h,
                  (float)lay->pane[p].w, (float)lay->pane[p].h,
                  du * 0.5f, dv * 0.5f, du * 1.5f, dv * 1.5f, white, bg, 0.0f, 0.0f, 0.0f);
        nquads++;
    }

    /* The file-manager band's cell backgrounds. fm.c painted these grids with the CONFIGURED
       fg/bg plus its own accents, so the band sits inside the terminal's palette. No cursor and no
       selection live here, which is all that distinguishes a band pane from a session pane. */
    if (fm && fm->on) {
        for (int q = 0; q < 2; q++) {
            int x, y, w, h;
            if (!fm_pane_rect(&lay->fm, q, &x, &y, &w, &h))
                continue;
            FmPaneDraw fp;
            fm_pane_prepare(r, q ? &fm->prev : &fm->list, x, y, w, h, f, &fp);
            for (int yy = 0; yy < fp.nrows; yy++) {
                const Cell *row = fp.cell + (size_t)yy * (size_t)fp.stride;
                for (int xx = 0; xx < fp.ncols; xx++) {
                    const Cell *c = &row[xx];
                    uint32_t cellbg = (c->attr & ATTR_INVERSE) ? c->fg : c->bg;
                    col2f(cellbg, bg);
                    bg[3] *= r->opacity;
                    float px = fp.gx + xx * cw, py = (fp.nrows - 1 - yy) * ch + fp.pad_gl;
                    emit_quad(&vp, px, py, cw, ch, du * 0.5f, dv * 0.5f, du * 1.5f, dv * 1.5f,
                              white, bg, 0.0f, 0.0f, 0.0f);
                    nquads++;
                }
            }
        }
    }

    /* The bar: one background per item (the strip itself is the clear colour), and a 1 px separator at
       the left edge of every item but the first so two inactive items do not merge into one block. */
    float bar_gl = 0.0f;
    if (lay->bar_on) {
        bar_gl = (float)r->vh - (float)lay->bar_y - (float)lay->bar_h;
        for (int i = 0; i < lay->nitem; i++) {
            const BarItem *it = &lay->item[i];
            col2f(it->active ? chrome.item_on_bg : chrome.item_off_bg, bg);
            bg[3] *= r->opacity;
            emit_quad(&vp, (float)it->x, bar_gl, (float)it->w, (float)lay->bar_h,
                      du * 0.5f, dv * 0.5f, du * 1.5f, dv * 1.5f, white, bg, 0.0f, 0.0f, 0.0f);
            nquads++;
            if (i > 0) {
                col2f(chrome.sep, bg);
                bg[3] *= r->opacity;
                emit_quad(&vp, (float)it->x, bar_gl, 1.0f, (float)lay->bar_h,
                          du * 0.5f, dv * 0.5f, du * 1.5f, dv * 1.5f, white, bg, 0.0f, 0.0f, 0.0f);
                nquads++;
            }
        }
    }

    /* The confirm-quit question: one full-width field in batch A, coloured the selection highlight because one
       configured colour owns every "this is the thing in this window" signal. Derived in render_chrome(). */
    if (lay->notice_on) {
        col2f(chrome.notice_bg, bg);
        bg[3] *= r->opacity;
        emit_quad(&vp, (float)lay->notice_x, (float)r->vh - (float)lay->notice_y - (float)lay->notice_h,
                  (float)lay->notice_w, (float)lay->notice_h, du * 0.5f, dv * 0.5f, du * 1.5f, dv * 1.5f,
                  white, bg, 0.0f, 0.0f, 0.0f);
        nquads++;
    }

    /* Everything emitted so far is batch A: cell backgrounds, the block cursor, the bar item
       backgrounds and separators, and the question row's field. The pictures go on top of exactly these,
       and everything after this point (bands, glyphs, labels, border) goes on top of the pictures. */
    int nq_bg = nquads;

    /* Selection bands, over the background and under the glyphs, drawn for every covered cell that holds
       something — including the cursor cell — so the highlight always matches what term_selection_text()
       copies. hl0 is already an absolute pixel x; do NOT add px again. */
    for (int p = 0; p < lay->npane; p++) {
        Term *t = pane_term(terms, lay->pane[p].tab);
        if (!t || !t->sel_active)
            continue;
        PaneDraw pd;
        pane_prepare(r, t, &lay->pane[p], f, &pd);
        for (int y = 0; y < pd.nrows; y++) {
            for (int x = 0; x < pd.ncols; x++) {
                /* No band over a cell that was never written: a selection past the end of a line stops
                   at the last glyph instead of painting the whole window. See term_cell_highlightable. */
                if (!term_cell_highlightable(t, x, y))
                    continue;
                float hl0 = 0, hlw = 0;
                sel_band_pixels(&pd.selspan, pd.g->cols, x, y, cw, &hl0, &hlw);
                if (hlw <= 0.0f)
                    continue;
                float py = (pd.nrows - 1 - y) * ch + pd.pad_gl;
                emit_quad(&vp, pd.gx + hl0, py, hlw, ch,
                          du * 0.5f, dv * 0.5f, du * 1.5f, dv * 1.5f,
                          white, selc, 0.0f, 0.0f, 0.0f);
                nquads++;
            }
        }
    }

    /* The bar/underline cursor, after the selection bands and before the glyphs: over the bands so a bar
       stays visible where a selection ends on the cursor cell, under the glyphs so a character is never
       hidden behind its own cursor. A BLOCK cursor is a cell background instead. */
    for (int p = 0; p < lay->npane; p++) {
        Term *t = pane_term(terms, lay->pane[p].tab);
        if (!t)
            continue;
        PaneDraw pd;
        pane_prepare(r, t, &lay->pane[p], f, &pd);
        if (!t->show_cursor || pd.g->scroll_off != pd.g->live_off)
            continue;
        int x = t->cx, y = t->cy;
        if (x < 0 || x >= pd.ncols || y < 0 || y >= pd.nrows)
            continue;
        int hl_on = 0;
        if (pd.sel_on) {
            float h0, hw;
            sel_band_pixels(&pd.selspan, pd.g->cols, x, y, cw, &h0, &hw);
            hl_on = hw > 0.0f && term_cell_highlightable(t, x, y);
        }
        int phase = (lay->pane[p].tab == lay->active_tab) ? r->cursor_on : 1;
        CursorDraw cd;
        render_cursor(t->cur_style, phase, hl_on, (int)cw, (int)ch, r->cursor_width,
                      (int)ascent, (int)descent, &cd);
        if (!cd.quad)
            continue;
        uint32_t cur_col = (lay->pane[p].tab == lay->active_tab) ? chrome.cursor : chrome.cursor_off;
        col2f(cur_col, bg);
        bg[3] *= r->opacity;
        float px = pd.gx + x * cw + (float)cd.x, py = (pd.nrows - 1 - y) * ch + pd.pad_gl + (float)cd.y;
        emit_quad(&vp, px, py, (float)cd.w, (float)cd.h,
                  du * 0.5f, dv * 0.5f, du * 1.5f, dv * 1.5f,
                  white, bg, 0.0f, 0.0f, 0.0f);
        nquads++;
    }

    /* One quad per cell of the address the pointer is on, in the ONE pane the pointer is over (-1 draws
       nothing). The colour is the CELL's own ink, so an address printed in blue underlines in blue. */
    for (int p = 0; p < lay->npane; p++) {
        if (r->hover_tab < 0 || lay->pane[p].tab != r->hover_tab)
            continue;
        Term *t = pane_term(terms, lay->pane[p].tab);
        if (!t)
            continue;
        PaneDraw pd;
        pane_prepare(r, t, &lay->pane[p], f, &pd);
        const UrlSpan *sp = &r->hover_span;
        if (sp->r0 > sp->r1 || sp->c0 > sp->c1)
            continue;                        /* an empty span draws nothing */
        HoverDraw hd;
        render_hover((int)cw, (int)ch, (int)ascent, (int)descent, &hd);
        if (hd.w <= 0 || hd.h <= 0)
            continue;                        /* render_hover() found no cell to underline */
        int y0 = sp->r0 < 0 ? 0 : sp->r0;
        int y1 = sp->r1 > pd.nrows - 1 ? pd.nrows - 1 : sp->r1;
        for (int y = y0; y <= y1; y++) {
            const Cell *row = grid_vprow(pd.g, y);
            /* Which cells of the run are on this row — the SAME function term_url_text() spells the address with,
               so the underline can never cover a cell the opener would not read (a run that crossed the window's
               edge is one run: first row from c0, last row to c1, the rows between whole). */
            int x0 = 0, x1 = 0;
            if (!url_span_row(sp, y, pd.g->cols, &x0, &x1))
                continue;
            if (x1 > pd.ncols - 1)
                x1 = pd.ncols - 1;           /* never draw past what THIS pane holds (a grid mid-resize) */
            for (int x = x0; x <= x1; x++) {
                const Cell *c = &row[x];
                if (!c->ch || (c->attr & ATTR_WIDE))
                    continue;                /* nothing drawn there; a wide cell is never part of a URL run */
                uint32_t ink = c->fg;
                if (c->attr & ATTR_INVERSE)
                    ink = c->bg;             /* the swap the glyph quads make, so the ink matches */
                col2f(ink, fg);
                fg[3] *= r->opacity;         /* a decoration quad, exactly like the cursor quad above */
                float px = pd.gx + x * cw + (float)hd.x;
                float py = (pd.nrows - 1 - y) * ch + pd.pad_gl + (float)hd.y;
                emit_quad(&vp, px, py, (float)hd.w, (float)hd.h,
                          du * 0.5f, dv * 0.5f, du * 1.5f, dv * 1.5f,
                          white, fg, 0.0f, 0.0f, 0.0f);
                nquads++;
            }
        }
    }

    /* Glyph quads (topmost), per pane. A wide char's continuation cells are skipped — the glyph is drawn
       from its anchor cell. The quad carries the cell's own background and the band edges, which the
       fragment shader uses to pick the selection colour per-fragment. */
    for (int p = 0; p < lay->npane; p++) {
        Term *t = pane_term(terms, lay->pane[p].tab);
        if (!t)
            continue;
        PaneDraw pd;
        pane_prepare(r, t, &lay->pane[p], f, &pd);
        for (int y = 0; y < pd.nrows; y++) {
            const Cell *row = grid_vprow(pd.g, y);
            for (int x = 0; x < pd.ncols; x++) {
                const Cell *c = &row[x];
                if (!c->ch || (c->attr & ATTR_WIDE))
                    continue;

                uint32_t cellbg = c->bg, cellfg = c->fg;
                if (c->attr & ATTR_INVERSE) {
                    uint32_t tmp = cellbg;
                    cellbg = cellfg;
                    cellfg = tmp;
                }
                float hl0 = 0, hlw = 0;
                int hl_on = 0;
                if (pd.sel_on) {
                    sel_band_pixels(&pd.selspan, pd.g->cols, x, y, cw, &hl0, &hlw);
                    hl_on = hlw > 0.0f;
                }
                /* The shader compares the fragment's ABSOLUTE buffer pixel x with the band edges, so the
                   band must carry the pane's grid origin exactly like the quad origin below does. */
                float band0 = hl_on ? pd.gx + hl0 : 0.0f;
                float band1 = hl_on ? pd.gx + hl0 + hlw : 0.0f;
                int is_cursor = t->show_cursor && pd.g->scroll_off == pd.g->live_off &&
                                x == t->cx && y == t->cy;
                uint32_t cur_col = (lay->pane[p].tab == lay->active_tab) ? chrome.cursor : chrome.cursor_off;
                if (is_cursor) {
                    /* The same decision the cell-background and cursor-quad steps made, so the three
                       cannot disagree. A block inverts the ink; a bar or underline has nothing to
                       invert. */
                    int phase = (lay->pane[p].tab == lay->active_tab) ? r->cursor_on : 1;
                    CursorDraw cd;
                    render_cursor(t->cur_style, phase, hl_on, (int)cw, (int)ch, r->cursor_width,
                                  (int)ascent, (int)descent, &cd);
                    if (cd.tint) {
                        cellfg = cur_col;      /* selected block cursor: tint glyph, band already drawn */
                    } else if (cd.invert) {
                        uint32_t oldbg = cellbg;
                        cellbg = cur_col;      /* unselected block: glyph composites over the block */
                        cellfg = oldbg;        /* invert ink under the block cursor */
                    }
                }
                col2f(cellfg, fg);
                col2f(cellbg, bg);
                /* The glyph quad composites the ink over ITS OWN copy of the cell background, so that
                   background must carry the window opacity exactly like the background quad does — at
                   alpha 1 every glyph's bounding box would be opaque. */
                bg[3] *= r->opacity;

                float px = pd.gx + x * cw, py = (pd.nrows - 1 - y) * ch + pd.pad_gl;

                if (emit_cell_glyph(f, c, px, py, cw, ch, ascent, fg, bg, band0, band1, &vp))
                    nquads++;
            }
        }
    }

    /* Same helper, same font, same wide/emoji rules: fm.c gives a wide glyph the same ATTR_WIDE
       continuation cell vt.c uses. No selection band here, hence band0 == band1 == 0. */
    if (fm && fm->on) {
        for (int q = 0; q < 2; q++) {
            int x, y, w, h;
            if (!fm_pane_rect(&lay->fm, q, &x, &y, &w, &h))
                continue;
            FmPaneDraw fp;
            fm_pane_prepare(r, q ? &fm->prev : &fm->list, x, y, w, h, f, &fp);
            for (int yy = 0; yy < fp.nrows; yy++) {
                const Cell *row = fp.cell + (size_t)yy * (size_t)fp.stride;
                for (int xx = 0; xx < fp.ncols; xx++) {
                    const Cell *c = &row[xx];
                    if (!c->ch || (c->attr & ATTR_WIDE))
                        continue;
                    uint32_t cbg = c->bg, cfg = c->fg;
                    if (c->attr & ATTR_INVERSE) {   /* same swap the cell backgrounds apply, so an inverted cell
                                                       cannot get ink over ink */
                        uint32_t tmp = cbg;
                        cbg = cfg;
                        cfg = tmp;
                    }
                    col2f(cfg, fg);
                    col2f(cbg, bg);
                    bg[3] *= r->opacity;
                    float px = fp.gx + xx * cw, py = (fp.nrows - 1 - yy) * ch + fp.pad_gl;
                    if (emit_cell_glyph(f, c, px, py, cw, ch, ascent, fg, bg, 0.0f, 0.0f, &vp))
                        nquads++;
                }
            }
        }
    }

    /* The bar labels, drawn with the text font over the item backgrounds. A label never crosses its own
       item rect, so a long tab title is cut (layout_compute already cut it to the item's columns; this
       bound is what makes that guarantee hold even if the font is wider than the layout assumed). */
    if (lay->bar_on) {
        for (int i = 0; i < lay->nitem; i++) {
            const BarItem *it = &lay->item[i];
            col2f(it->active ? r->fg : chrome.item_off_fg, fg);
            col2f(it->active ? chrome.item_on_bg : chrome.item_off_bg, bg);
            bg[3] *= r->opacity;
            float px0 = (float)it->x;
            int col = 0;
            for (const char *s = it->label; *s; col++) {
                uint32_t cp = next_cp(&s);
                if (px0 + (float)(col + 1) * cw > (float)(it->x + it->w))
                    break;
                Glyph *gl_ = font_glyph(f, cp);
                if (!gl_ || gl_->w <= 0 || gl_->h <= 0)
                    continue;
                float gw = (float)gl_->w * f->scale;
                float gh = (float)gl_->h * f->scale;
                float gx = px0 + (float)col * cw + (float)gl_->bearing_x * f->scale;
                float gy = bar_gl + ch - ascent + (float)gl_->bearing_y * f->scale - gh;
                emit_quad(&vp, gx, gy, gw, gh, gl_->u0, gl_->v0, gl_->u1, gl_->v1,
                          fg, bg, 1.0f, 0.0f, 0.0f);
                nquads++;
            }
        }
    }

    /* The question's text, with the same text font and the same per-column bound as a bar label. The row's
       text was already cut to the row's columns by layout_compute(); this bound is what makes that
       guarantee hold even if the font is wider than the layout assumed, exactly as it does for a label. */
    if (lay->notice_on && lay->notice[0]) {
        col2f(chrome.notice_fg, fg);
        col2f(chrome.notice_bg, bg);
        bg[3] *= r->opacity;
        float npx0 = (float)lay->notice_x;
        float ngl = (float)r->vh - (float)lay->notice_y - (float)lay->notice_h;
        int col = 0;
        for (const char *s = lay->notice; *s; col++) {
            uint32_t cp = next_cp(&s);
            if (npx0 + (float)(col + 1) * cw > (float)(lay->notice_x + lay->notice_w))
                break;
            Glyph *gl_ = font_glyph(f, cp);
            if (!gl_ || gl_->w <= 0 || gl_->h <= 0)
                continue;
            float gw = (float)gl_->w * f->scale;
            float gh = (float)gl_->h * f->scale;
            float gx = npx0 + (float)col * cw + (float)gl_->bearing_x * f->scale;
            float gy = ngl + ch - ascent + (float)gl_->bearing_y * f->scale - gh;
            emit_quad(&vp, gx, gy, gw, gh, gl_->u0, gl_->v0, gl_->u1, gl_->v1,
                      fg, bg, 1.0f, 0.0f, 0.0f);
            nquads++;
        }
    }

    /* Which region owns the keyboard, drawn last so nothing covers it. With the file manager on, exactly one of
       the three regions gets the frame: a switch the user cannot see is a switch they will not notice. */
    col2f(chrome.border, bg);
    bg[3] *= r->opacity;
    if (fm && fm->on && lay->fm.on) {
        int x, y, w, h;
        if (fm->focus == FM_FOCUS_LIST && fm_pane_rect(&lay->fm, 0, &x, &y, &w, &h)) {
            emit_border(&vp, (float)x, (float)y, (float)w, (float)h, 2.0f, bg, du, dv,
                        (float)r->vh);
            nquads += 4;
        } else if (fm->focus == FM_FOCUS_PREVIEW && fm_pane_rect(&lay->fm, 1, &x, &y, &w, &h)) {
            emit_border(&vp, (float)x, (float)y, (float)w, (float)h, 2.0f, bg, du, dv,
                        (float)r->vh);
            nquads += 4;
        }
    }
    if (lay->npane > 1 || (fm && fm->on && lay->fm.on && fm->focus == FM_FOCUS_TERM)) {
        int ap = layout_pane_of_tab(lay, lay->active_tab);
        if (ap >= 0) {
            const PaneGeom *pg = &lay->pane[ap];
            emit_border(&vp, (float)pg->x, (float)pg->y, (float)pg->w, (float)pg->h, 2.0f, bg,
                        du, dv, (float)r->vh);
            nquads += 4;
        }
    }

    /* The confirm-quit dialog is drawn LAST, over the cells, the pictures and the focus border, because it is the
       one thing in the frame the user has to answer. It costs no grid row, so the work being asked about
       stays visible behind it. Inside the box the order is field, border, button fields, then text. */
    if (lay->dialog.on) {
        float dlg_gl = (float)r->vh - (float)lay->dialog.y - (float)lay->dialog.h;
        col2f(chrome.dialog_bg, bg);
        bg[3] *= r->opacity;
        emit_quad(&vp, (float)lay->dialog.x, dlg_gl, (float)lay->dialog.w, (float)lay->dialog.h,
                  du * 0.5f, dv * 0.5f, du * 1.5f, dv * 1.5f, white, bg, 0.0f, 0.0f, 0.0f);
        nquads++;
        col2f(chrome.dialog_border, bg);
        bg[3] *= r->opacity;
        emit_border(&vp, (float)lay->dialog.x, dlg_gl, (float)lay->dialog.w, (float)lay->dialog.h,
                    2.0f, bg, du, dv, (float)r->vh);
        nquads += 4;
        for (int i = 0; i < lay->dialog.nbtn; i++) {
            const DialogBtn *b = &lay->dialog.btn[i];
            col2f(chrome.dialog_btn_bg, bg);
            bg[3] *= r->opacity;
            emit_quad(&vp, (float)b->x, (float)r->vh - (float)b->y - (float)b->h,
                      (float)b->w, (float)b->h, du * 0.5f, dv * 0.5f, du * 1.5f, dv * 1.5f,
                      white, bg, 0.0f, 0.0f, 0.0f);
            nquads++;
        }
        /* The sentence, then the two labels: the same text font, the same one-cell-per-codepoint advance and
           the same per-column bound a bar label uses, so nothing here can draw past the box the geometry
           decided. Each line was already wrapped to the box's columns by layout_compute(). */
        for (int i = 0; i < lay->dialog.nline; i++) {
            col2f(chrome.dialog_fg, fg);
            col2f(chrome.dialog_bg, bg);
            bg[3] *= r->opacity;
            float lx = (float)lay->dialog.text_x;
            float lgl = (float)r->vh - (float)(lay->dialog.text_y + i * ch) - (float)ch;
            int col = 0;
            for (const char *s = lay->dialog.line[i]; *s; col++) {
                uint32_t cp = next_cp(&s);
                if (lx + (float)(col + 1) * cw > (float)(lay->dialog.x + lay->dialog.w))
                    break;
                Glyph *gl_ = font_glyph(f, cp);
                if (!gl_ || gl_->w <= 0 || gl_->h <= 0)
                    continue;
                float gw = (float)gl_->w * f->scale;
                float gh = (float)gl_->h * f->scale;
                float gx = lx + (float)col * cw + (float)gl_->bearing_x * f->scale;
                float gy = lgl + ch - ascent + (float)gl_->bearing_y * f->scale - gh;
                emit_quad(&vp, gx, gy, gw, gh, gl_->u0, gl_->v0, gl_->u1, gl_->v1,
                          fg, bg, 1.0f, 0.0f, 0.0f);
                nquads++;
            }
        }
        for (int i = 0; i < lay->dialog.nbtn; i++) {
            const DialogBtn *b = &lay->dialog.btn[i];
            col2f(b->yes ? chrome.dialog_yes : chrome.dialog_no, fg);
            col2f(chrome.dialog_btn_bg, bg);
            bg[3] *= r->opacity;
            float lx = (float)b->label_x;
            float lgl = (float)r->vh - (float)b->y - (float)b->h;
            int col = 0;
            for (const char *s = b->label; *s; col++) {
                uint32_t cp = next_cp(&s);
                if (lx + (float)(col + 1) * cw > (float)(b->x + b->w))
                    break;
                Glyph *gl_ = font_glyph(f, cp);
                if (!gl_ || gl_->w <= 0 || gl_->h <= 0)
                    continue;
                float gw = (float)gl_->w * f->scale;
                float gh = (float)gl_->h * f->scale;
                float gx = lx + (float)col * cw + (float)gl_->bearing_x * f->scale;
                float gy = lgl + ch - ascent + (float)gl_->bearing_y * f->scale - gh;
                emit_quad(&vp, gx, gy, gw, gh, gl_->u0, gl_->v0, gl_->u1, gl_->v1,
                          fg, bg, 1.0f, 0.0f, 0.0f);
                nquads++;
            }
        }
    }

    /* Upload BOTH atlases here, after the quads were emitted and before the draw: the glyph lookups run
       while the glyph quads are emitted, so a glyph shaped in THIS frame would otherwise be sampled
       before its texels exist. */
    if (f->atlas_dirty) {
        if (!f->atlas_tex)
            glGenTextures(1, &f->atlas_tex);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, f->atlas_tex);
        /* GL_LUMINANCE, not GL_R8/GL_RED: the context is GL 2.1, where those fail glTexImage2D with
           GL_INVALID_ENUM. It replicates the single channel to RGB, so the shader reads .r. */
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, f->atlas_w, f->atlas_h, 0,
                     GL_LUMINANCE, GL_UNSIGNED_BYTE, f->atlas);
        GLenum terr = glGetError();
        if (terr != GL_NO_ERROR) {
            /* A refused atlas upload means there is nothing to draw, and drawing anyway is NOT graceful degradation:
               sampling a texture the GPU never allocated faults inside libnvidia-eglcore. Application
               pictures are the only thing here big enough to help, so drop them and try once more. */
            int dropped = free_picture_textures(r);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, f->atlas_w, f->atlas_h, 0,
                         GL_LUMINANCE, GL_UNSIGNED_BYTE, f->atlas);
            terr = glGetError();
            if (terr != GL_NO_ERROR) {
                if (log_rate(&r->atlas_upload_fails))
                    klog(L_ERROR, "atlas glTexImage2D failed: 0x%x (%dx%d) — no video memory for the "
                         "glyph atlas%s", terr, f->atlas_w, f->atlas_h,
                         dropped ? " (picture textures were dropped first)" : "");
                return -1;
            }
            r->atlas_upload_fails = 0;
            klog(L_WARN, "atlas glTexImage2D recovered after dropping %d picture texture(s)", dropped);
        } else {
            r->atlas_upload_fails = 0;
        }
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        f->atlas_dirty = 0;
    }

    /* Upload the RGBA8 color emoji atlas when new emoji were shaped into it (texture unit 1). The texels are
       premultiplied, which is what the shader's composite expects. While the GPU refuses this atlas the
       retry is backed off to about 1 Hz — the upload is what fails. */
    if (r->catlas_wait > 0)
        r->catlas_wait--;
    if (f->catlas_dirty && f->catlas && r->catlas_wait <= 0) {
        if (!f->catlas_tex)
            glGenTextures(1, &f->catlas_tex);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, f->catlas_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, f->catlas_w, f->catlas_h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, f->catlas);
        GLenum cerr = glGetError();
        if (cerr != GL_NO_ERROR) {
            /* Never draw with a texture the GPU refused. Color emoji are expendable: emit_cell_glyph() already has the
               monochrome fallback, so skip THIS frame and leave the atlas dirty for a later one. */
            if (log_rate(&r->catlas_upload_fails))
                klog(L_WARN, "color atlas glTexImage2D failed: 0x%x (%dx%d) — falling back to "
                     "monochrome glyphs", cerr, f->catlas_w, f->catlas_h);
            GLuint ct = f->catlas_tex;
            if (ct)
                glDeleteTextures(1, &ct);
            f->catlas_tex = 0;
            f->catlas_broken = 1;
            r->catlas_wait = CATLAS_RETRY_FRAMES;
            return 0;          /* nothing was swapped: main.c keeps the frame dirty and retries */
        }
        r->catlas_upload_fails = 0;
        r->catlas_wait = 0;
        f->catlas_broken = 0;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        f->catlas_dirty = 0;
    }

    glViewport(0, 0, r->vw, r->vh);
    /* The margin, pane gaps and whole-cell remainders are outside every quad, so nothing else writes
       them. Cleared ONCE per frame — clearing between the batches would erase the batch already drawn. */
    set_clear_color(r);
    glClear(GL_COLOR_BUFFER_BIT);

    glBindVertexArray(r->vao);
    setup_draw_state(r, f, selc);

    /* batch A: backgrounds */
    draw_quads(r, vdata, nq_bg);

    /* the pictures, each in its own draw call because each has its own texture */
    int frame = ++r->img_frame;
    int nprev = draw_fm_preview(r, fm, lay, cw, ch);
    int nimg = nprev;
    for (int p = 0; p < lay->npane; p++) {
        Term *t = pane_term(terms, lay->pane[p].tab);
        if (!t)
            continue;
        PaneDraw pd;
        pane_prepare(r, t, &lay->pane[p], f, &pd);
        nimg += draw_images(r, t, &lay->pane[p], &pd, cw, ch, frame);
    }

    /* One line per CHANGE in how many pictures are on screen — the evidence for a "the picture is missing"
       report, and not one line per frame. The file-manager preview is counted separately because it has its
       own texture, not one of the cached ones. */
    static int last_app = -1, last_prev = -1;
    if (last_app != nimg - nprev || last_prev != nprev) {
        last_app = nimg - nprev;
        last_prev = nprev;
        klog(L_DEBUG, "graphics: %d image(s) drawn, %d texture(s) held%s",
             nimg - nprev, r->nimgtex,
             nprev ? " + the file-manager preview" : "");
    }

    /* batch B: selection bands, glyphs, bar labels, focus border — on top of the pictures, so text
       stays readable over a picture the way the protocol expects. */
    setup_draw_state(r, f, selc);
    draw_quads(r, vdata + (size_t)nq_bg * FLOATS_PER_INST, nquads - nq_bg);

    /* textures of pictures that were not on screen this frame go now: after the last draw that could
       have used them, before the buffer is presented */
    img_tex_sweep(r, frame);

    /* 1 only when the buffer really was attached: main.c may arm a wl_surface_frame callback only with a
       commit that attaches a buffer, so a failed swap must not arm one. */
    if (!eglSwapBuffers(r->edpy, r->esurf)) {
        klog(L_ERROR, "eglSwapBuffers failed: 0x%x", eglGetError());
        return 0;
    }
    return 1;
}

void render_resize(Render *r, int w, int h)
{
    /* The EGL window surface tracks the wl_egl_window's buffer size, so only the size is recorded here:
       re-creating the surface can return EGL_BAD_MATCH and leave nothing to paint with. */
    if (r->edpy == EGL_NO_DISPLAY)
        return;
    r->vw = w;
    r->vh = h;
}

void render_destroy(Render *r)
{
    if (r->edpy != EGL_NO_DISPLAY) {
        /* while the context is still current — glDeleteTextures is a no-op without one */
        for (int i = 0; i < r->nimgtex; i++)
            if (r->imgtex[i].tex) {
                GLuint t = r->imgtex[i].tex;
                glDeleteTextures(1, &t);
            }
        if (r->fm_tex) {
            GLuint t = r->fm_tex;
            glDeleteTextures(1, &t);
        }
    }
    free(r->imgtex);
    free(r->fm_px);
    r->fm_tex = 0;
    r->fm_tex_uid = 0;
    r->fm_px = NULL;
    r->fm_px_cap = 0;
    if (r->edpy != EGL_NO_DISPLAY) {
        eglMakeCurrent((EGLDisplay)r->edpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (r->ectx != EGL_NO_CONTEXT)
            eglDestroyContext((EGLDisplay)r->edpy, (EGLContext)r->ectx);
        if (r->esurf != EGL_NO_SURFACE)
            eglDestroySurface((EGLDisplay)r->edpy, (EGLSurface)r->esurf);
        eglTerminate((EGLDisplay)r->edpy);
    }
    free(r->ecfg);
    free(r->vdata);
    free(r->edata);
    memset(r, 0, sizeof(*r));
}
