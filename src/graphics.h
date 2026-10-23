#pragma once
#include "star.h"
#include <d2d1.h>

/* The Windows SDK exposes Direct2D's complete interfaces only to C++.
   These narrow C ABI wrappers use the published COM vtable order from d2d1.h
   and dwrite.h (base interfaces only). STDMETHODCALLTYPE is required on x86.
   No C++ translation unit or C++ runtime is used. */
typedef struct ComObject {
    void **v;
} ComObject;
#define COM_CALL(o, slot, type) ((type)((ComObject *)(o))->v[slot])
typedef void *Com;
static void com_release(Com p) {
    if (p)
        COM_CALL(p, 2, ULONG(STDMETHODCALLTYPE *)(void *))(p);
}
typedef struct {
    Com factory, write, target, brush, gradient, formats[8];
    unsigned gradient_color;
    float scale;
    UINT dpi;
} Graphics;
static const GUID d2d_factory_iid = {
    0x06152247, 0x6f50, 0x465a, {0x92, 0x45, 0x11, 0x8b, 0xfd, 0x3b, 0x60, 0x07}};
static const GUID dwrite_factory_iid = {
    0xb859ee5a, 0xd838, 0x4b5b, {0xa2, 0xe8, 0x1a, 0xdc, 0x7d, 0x93, 0xdb, 0x48}};
__declspec(dllimport) HRESULT WINAPI DWriteCreateFactory(int type, REFIID iid, IUnknown **factory);
static D2D1_COLOR_F rgba(unsigned color, float alpha) {
    D2D1_COLOR_F c = {(float)((color >> 16) & 255) / 255, (float)((color >> 8) & 255) / 255,
                      (float)(color & 255) / 255, alpha};
    return c;
}
static D2D1_RECT_F rectf(float x, float y, float w, float h) {
    D2D1_RECT_F r = {x, y, x + w, y + h};
    return r;
}
static void gfx_target_free(Graphics *g) {
    com_release(g->gradient);
    com_release(g->brush);
    com_release(g->target);
    g->gradient = g->brush = g->target = NULL;
}
static void gfx_free(Graphics *g) {
    gfx_target_free(g);
    for (int i = 0; i < 8; i++)
        com_release(g->formats[i]);
    com_release(g->write);
    com_release(g->factory);
    memset(g, 0, sizeof(*g));
}
static bool gfx_init(Graphics *g) {
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2d_factory_iid, NULL, &g->factory);
    if (FAILED(hr))
        return false;
    hr = DWriteCreateFactory(0, &dwrite_factory_iid, (IUnknown **)&g->write);
    if (FAILED(hr))
        return false;
    const float sizes[] = {12, 13, 14, 16, 20, 26, 34, 11};
    for (int i = 0; i < 8; i++) {
        hr = COM_CALL(g->write, 15,
                      HRESULT(STDMETHODCALLTYPE *)(void *, const wchar_t *, void *, int, int, int, float,
                                                   const wchar_t *, void **))(
            g->write, L"Segoe UI", NULL, (i >= 4 && i <= 6) ? 600 : 400, 0, 5, sizes[i], L"zh-CN",
            &g->formats[i]);
        if (FAILED(hr))
            return false;
        COM_CALL(g->formats[i], 4, HRESULT(STDMETHODCALLTYPE *)(void *, int))(
            g->formats[i], 2); /* DWRITE_PARAGRAPH_ALIGNMENT_CENTER */
        COM_CALL(g->formats[i], 5, HRESULT(STDMETHODCALLTYPE *)(void *, int))(
            g->formats[i], 1); /* no wrap; call sites opt in */
        struct {
            int granularity;
            UINT32 delimiter, count;
        } trimming = {1, 0, 0};
        Com ellipsis = NULL;
        hr = COM_CALL(g->write, 20, HRESULT(STDMETHODCALLTYPE *)(void *, void *, void **))(
            g->write, g->formats[i], &ellipsis);
        if (SUCCEEDED(hr)) {
            COM_CALL(g->formats[i], 9, HRESULT(STDMETHODCALLTYPE *)(void *, const void *, void *))(
                g->formats[i], &trimming, ellipsis);
            com_release(ellipsis);
        }
    }
    return true;
}
static bool gfx_target(Graphics *g, HWND hwnd, UINT dpi) {
    RECT r;
    GetClientRect(hwnd, &r);
    g->dpi = dpi;
    g->scale = dpi / 96.0f;
    if (g->target)
        return true;
    D2D1_RENDER_TARGET_PROPERTIES props = {0};
    props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_UNKNOWN;
    props.dpiX = props.dpiY = (float)dpi;
    D2D1_HWND_RENDER_TARGET_PROPERTIES window = {0};
    window.hwnd = hwnd;
    window.pixelSize.width = r.right;
    window.pixelSize.height = r.bottom;
    HRESULT hr = COM_CALL(g->factory, 14,
                          HRESULT(STDMETHODCALLTYPE *)(void *, const D2D1_RENDER_TARGET_PROPERTIES *,
                                                       const D2D1_HWND_RENDER_TARGET_PROPERTIES *,
                                                       void **))(g->factory, &props, &window, &g->target);
    if (FAILED(hr))
        return false;
    D2D1_COLOR_F c = rgba(0xffffff, 1);
    hr = COM_CALL(g->target, 8,
                  HRESULT(STDMETHODCALLTYPE *)(void *, const D2D1_COLOR_F *, const D2D1_BRUSH_PROPERTIES *,
                                               void **))(g->target, &c, NULL, &g->brush);
    if (FAILED(hr)) {
        gfx_target_free(g);
        return false;
    }
    return true;
}
static void gfx_resize(Graphics *g, UINT width, UINT height) {
    if (g->target) {
        D2D1_SIZE_U s = {width, height};
        HRESULT hr =
            COM_CALL(g->target, 58, HRESULT(STDMETHODCALLTYPE *)(void *, const D2D1_SIZE_U *))(g->target, &s);
        if (FAILED(hr))
            gfx_target_free(g);
    }
}
static bool gfx_dc_target(Graphics *g, HDC dc, const RECT *bounds, UINT dpi) {
    D2D1_RENDER_TARGET_PROPERTIES props = {0};
    props.type = D2D1_RENDER_TARGET_TYPE_SOFTWARE;
    props.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_IGNORE;
    props.dpiX = props.dpiY = (float)dpi;
    HRESULT hr = COM_CALL(g->factory, 16,
                          HRESULT(STDMETHODCALLTYPE *)(void *, const D2D1_RENDER_TARGET_PROPERTIES *,
                                                       void **))(g->factory, &props, &g->target);
    if (SUCCEEDED(hr))
        hr = COM_CALL(g->target, 57, HRESULT(STDMETHODCALLTYPE *)(void *, HDC, const RECT *))(g->target, dc,
                                                                                              bounds);
    D2D1_COLOR_F color = rgba(0xffffff, 1);
    if (SUCCEEDED(hr))
        hr =
            COM_CALL(g->target, 8,
                     HRESULT(STDMETHODCALLTYPE *)(void *, const D2D1_COLOR_F *, const D2D1_BRUSH_PROPERTIES *,
                                                  void **))(g->target, &color, NULL, &g->brush);
    if (FAILED(hr)) {
        gfx_target_free(g);
        return false;
    }
    return true;
}
static void gfx_color(Graphics *g, unsigned c, float a) {
    D2D1_COLOR_F v = rgba(c, a);
    COM_CALL(g->brush, 8, void(STDMETHODCALLTYPE *)(void *, const D2D1_COLOR_F *))(g->brush, &v);
}
static void gfx_begin(Graphics *g, unsigned color) {
    D2D1_COLOR_F c = rgba(color, 1);
    COM_CALL(g->target, 48, void(STDMETHODCALLTYPE *)(void *))(g->target);
    COM_CALL(g->target, 47, void(STDMETHODCALLTYPE *)(void *, const D2D1_COLOR_F *))(g->target, &c);
}
static void gfx_end(Graphics *g) {
    HRESULT hr = COM_CALL(g->target, 49,
                          HRESULT(STDMETHODCALLTYPE *)(void *, UINT64 *, UINT64 *))(g->target, NULL, NULL);
    if (FAILED(hr))
        gfx_target_free(g);
}
static void gfx_fill(Graphics *g, float x, float y, float w, float h, float radius, unsigned color,
                     float alpha) {
    if (w <= 0 || h <= 0)
        return;
    D2D1_ROUNDED_RECT r = {rectf(x, y, w, h), radius, radius};
    gfx_color(g, color, alpha);
    COM_CALL(g->target, 19,
             void(STDMETHODCALLTYPE *)(void *, const D2D1_ROUNDED_RECT *, void *))(g->target, &r, g->brush);
}
static void gfx_background(Graphics *g, float x, float y, float w, float h, bool light) {
    unsigned from = light ? 0xf1f4fa : 0x172131, to = light ? 0xf7f8fa : 0x10151f;
    if (g->gradient && g->gradient_color != from) {
        com_release(g->gradient);
        g->gradient = NULL;
    }
    if (!g->gradient) {
        D2D1_GRADIENT_STOP stops[2] = {{0, rgba(from, 1)}, {1, rgba(to, 1)}};
        Com collection = NULL;
        HRESULT hr =
            COM_CALL(g->target, 9,
                     HRESULT(STDMETHODCALLTYPE *)(void *, const D2D1_GRADIENT_STOP *, UINT32, int, int,
                                                  void **))(g->target, stops, 2, 0, 0, &collection);
        if (SUCCEEDED(hr)) {
            D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES props = {{0, 0}, {850, 900}};
            COM_CALL(g->target, 10,
                     HRESULT(STDMETHODCALLTYPE *)(void *, const D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES *,
                                                  const D2D1_BRUSH_PROPERTIES *, void *, void **))(
                g->target, &props, NULL, collection, &g->gradient);
            com_release(collection);
            g->gradient_color = from;
        }
    }
    if (g->gradient) {
        D2D1_RECT_F r = rectf(x, y, w, h);
        COM_CALL(g->target, 17, void(STDMETHODCALLTYPE *)(void *, const D2D1_RECT_F *, void *))(g->target, &r,
                                                                                                g->gradient);
    }
}
static void gfx_border(Graphics *g, float x, float y, float w, float h, float radius, unsigned color,
                       float alpha) {
    D2D1_ROUNDED_RECT r = {rectf(x, y, w, h), radius, radius};
    gfx_color(g, color, alpha);
    COM_CALL(g->target, 18,
             void(STDMETHODCALLTYPE *)(void *, const D2D1_ROUNDED_RECT *, void *, float, void *))(
        g->target, &r, g->brush, 1.0f, NULL);
}
static void gfx_line(Graphics *g, float x1, float y1, float x2, float y2, unsigned color, float width) {
    D2D1_POINT_2F a = {x1, y1}, b = {x2, y2};
    gfx_color(g, color, 1);
    COM_CALL(g->target, 15,
             void(STDMETHODCALLTYPE *)(void *, D2D1_POINT_2F, D2D1_POINT_2F, void *, float, void *))(
        g->target, a, b, g->brush, width, NULL);
}
static void gfx_text_w(Graphics *g, const wchar_t *s, float x, float y, float w, float h, int font,
                       unsigned color, bool wrap) {
    if (!s || w <= 0 || h <= 0)
        return;
    D2D1_RECT_F r = rectf(x, y, w, h);
    Com f = g->formats[font];
    COM_CALL(f, 5, HRESULT(STDMETHODCALLTYPE *)(void *, int))(f, wrap ? 0 : 1);
    gfx_color(g, color, 1);
    COM_CALL(g->target, 27,
             void(STDMETHODCALLTYPE *)(void *, const wchar_t *, UINT32, void *, const D2D1_RECT_F *, void *,
                                       int, int))(g->target, s, (UINT32)wcslen(s), f, &r, g->brush, 2, 0);
}
static void gfx_text(Graphics *g, const char *s, float x, float y, float w, float h, int font, unsigned color,
                     bool wrap) {
    wchar_t *t = wide(s);
    gfx_text_w(g, t, x, y, w, h, font, color, wrap);
    mem_free(t);
}
static void gfx_text_right(Graphics *g, const char *s, float x, float y, float w, float h, int font,
                           unsigned color) {
    Com f = g->formats[font];
    COM_CALL(f, 3, HRESULT(STDMETHODCALLTYPE *)(void *, int))(f, 1);
    gfx_text(g, s, x, y, w, h, font, color, false);
    COM_CALL(f, 3, HRESULT(STDMETHODCALLTYPE *)(void *, int))(f, 0);
}
static void gfx_text_center(Graphics *g, const char *s, float x, float y, float w, float h, int font,
                            unsigned color) {
    Com f = g->formats[font];
    COM_CALL(f, 3, HRESULT(STDMETHODCALLTYPE *)(void *, int))(f, 2);
    gfx_text(g, s, x, y, w, h, font, color, false);
    COM_CALL(f, 3, HRESULT(STDMETHODCALLTYPE *)(void *, int))(f, 0);
}
static void gfx_avatar(Graphics *g, const unsigned char *pixels, float x, float y, float size) {
    if (!pixels)
        return;
    D2D1_SIZE_U dimensions = {64, 64};
    D2D1_BITMAP_PROPERTIES props = {{DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED}, 96, 96};
    Com bitmap = NULL;
    HRESULT hr = COM_CALL(g->target, 4,
                          HRESULT(STDMETHODCALLTYPE *)(void *, D2D1_SIZE_U, const void *, UINT32,
                                                       const D2D1_BITMAP_PROPERTIES *, void **))(
        g->target, dimensions, pixels, 256, &props, &bitmap);
    if (SUCCEEDED(hr)) {
        D2D1_RECT_F dest = rectf(x, y, size, size);
        COM_CALL(g->target, 26,
                 void(STDMETHODCALLTYPE *)(void *, void *, const D2D1_RECT_F *, float, int,
                                           const D2D1_RECT_F *))(g->target, bitmap, &dest, 1.0f, 1, NULL);
        com_release(bitmap);
    }
}
static float gfx_text_width(Graphics *g, const char *s, int font) {
    wchar_t *t = wide(s);
    if (!t)
        return 0;
    Com layout = NULL, f = g->formats[font];
    HRESULT hr = COM_CALL(g->write, 18,
                          HRESULT(STDMETHODCALLTYPE *)(void *, const wchar_t *, UINT32, void *, float, float,
                                                       void **))(g->write, t, (UINT32)wcslen(t), f, 10000.0f,
                                                                 1000.0f, &layout);
    mem_free(t);
    if (FAILED(hr))
        return 0;
    struct {
        float left, top, width, trailing_width, height, layout_width, layout_height;
        UINT32 bidi_depth, lines;
    } metrics = {0};
    hr = COM_CALL(layout, 60, HRESULT(STDMETHODCALLTYPE *)(void *, void *))(layout, &metrics);
    com_release(layout);
    return SUCCEEDED(hr) ? metrics.width : 0;
}
static float gfx_text_height(Graphics *g, const char *s, float width, int font) {
    if (!s || !*s || width <= 0)
        return 0;
    wchar_t *t = wide(s);
    if (!t)
        return 24;
    Com layout = NULL, f = g->formats[font];
    COM_CALL(f, 5, HRESULT(STDMETHODCALLTYPE *)(void *, int))(f, 0);
    HRESULT hr = COM_CALL(g->write, 18,
                          HRESULT(STDMETHODCALLTYPE *)(void *, const wchar_t *, UINT32, void *, float, float,
                                                       void **))(g->write, t, (UINT32)wcslen(t), f, width,
                                                                 1000000.0f, &layout);
    mem_free(t);
    if (FAILED(hr))
        return 24;
    struct {
        float left, top, width, trailing_width, height, layout_width, layout_height;
        UINT32 bidi_depth, lines;
    } metrics = {0};
    hr = COM_CALL(layout, 60, HRESULT(STDMETHODCALLTYPE *)(void *, void *))(layout, &metrics);
    com_release(layout);
    return SUCCEEDED(hr) ? metrics.height + 6 : 24;
}
static void gfx_clip(Graphics *g, float x, float y, float w, float h) {
    D2D1_RECT_F r = rectf(x, y, w, h);
    COM_CALL(g->target, 45, void(STDMETHODCALLTYPE *)(void *, const D2D1_RECT_F *, int))(g->target, &r, 0);
}
static void gfx_unclip(Graphics *g) {
    COM_CALL(g->target, 46, void(STDMETHODCALLTYPE *)(void *))(g->target);
}
