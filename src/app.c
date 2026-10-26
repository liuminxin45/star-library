#include "star.h"
#include "graphics.h"
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <imm.h>
#include <windowsx.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

enum { PAGE_LIBRARY, PAGE_PROFILE, PAGE_DISCOVER, PAGE_SETTINGS };
enum {
    ACT_NAV = 10,
    ACT_SYNC = 20,
    ACT_PROFILE,
    ACT_DISCOVER,
    ACT_SETTINGS,
    ACT_OPEN,
    ACT_STAR,
    ACT_IGNORE,
    ACT_EXPORT,
    ACT_IMPORT,
    ACT_THEME,
    ACT_FILTER,
    ACT_SORT,
    ACT_CANCEL,
    ACT_AI_TEST,
    ACT_GH_HELP,
    ACT_REVEAL,
    ACT_DIR,
    ACT_ABOUT,
    ACT_SCROLL,
    ACT_CONNECT,
    ACT_DISCONNECT,
    ACT_SUMMARY,
    ACT_DIMENSION,
    ACT_CONCURRENCY,
    ACT_RELEASE,
    ACT_STAR_ME,
    ACT_EVIDENCE = 100000
};
enum { EDIT_SEARCH = 301, EDIT_GH_KEY, EDIT_AI_URL, EDIT_AI_MODEL, EDIT_AI_KEY };
typedef struct {
    D2D1_RECT_F r;
    int action;
    uint64_t id;
    HWND child;
    bool mouse_hidden;
} Target;
typedef struct {
    unsigned bg, sidebar, surface, raised, hover, text, muted, faint, border, accent, accent_text;
} Palette;
typedef struct {
    int scroll, filter, sort;
    uint64_t selected;
    wchar_t search[512];
    char language[128];
} ListState;
typedef struct {
    HWND notify;
    HANDLE cancel;
    char *token;
    char latest[32];
    bool ok;
} UpdateCheck;
typedef struct {
    HWND window, search, ghkey, aiurl, aimodel, aikey, tooltip;
    HFONT font;
    HBRUSH input_brush;
    HINSTANCE instance;
    HANDLE mutex, thread;
    HANDLE update_thread;
    UpdateCheck *update;
    ULONGLONG update_checked;
    char latest[32];
    Library library;
    Storage storage;
    Graphics gfx;
    Job *job;
    Target targets[500];
    int target_count, focus, hover, page, filter, sort, scroll, profile_scroll, setting_scroll;
    int pointer_action, focus_action;
    int pressed_action;
    uint64_t pressed_id;
    uint64_t pointer_id, focus_id;
    uint64_t hovered_card;
    UINT dpi, font_dpi;
    float width, height, side;
    uint64_t selected;
    bool light, settings_dirty, loading_edits, close_after_job, test_preview, summary_retry;
    wchar_t notice[1024], search_text[512];
    ULONGLONG notice_until;
    J **visible;
    size_t visible_count;
    char **lang_filters;
    int lang_count;
    char language[128];
    wchar_t capture[32768];
    wchar_t snapshot[32768];
    ListState lists[2];
    wchar_t ui_test_report[32768];
    int capture_page, capture_w, capture_h, capture_dpi;
    bool capture_exit;
    bool capture_expand, capture_command, capture_pressed, capture_inject;
    int ui_failures;
    int scroll_limit;
    bool dragging_scroll;
    unsigned concurrency;
    wchar_t *tip_text;
    int expanded_dimension;
    float thumb_top, thumb_height, drag_offset, track_top, track_height;
} App;
static App a;
static DWORD WINAPI update_thread(void *context) {
    UpdateCheck *check = context;
    check->ok = update_latest(check->token, check->cancel, check->latest);
    PostMessageW(check->notify, WM_UPDATE, 0, (LPARAM)check);
    return 0;
}
static void update_cleanup(void) {
    if (!a.update)
        return;
    SetEvent(a.update->cancel);
    if (a.update_thread) {
        WaitForSingleObject(a.update_thread, INFINITE);
        CloseHandle(a.update_thread);
    }
    CloseHandle(a.update->cancel);
    secure_free(a.update->token);
    mem_free(a.update);
    a.update = NULL;
    a.update_thread = NULL;
}
static void update_start(void) {
    if (a.test_preview || a.storage.demo || a.update || !a.storage.github_key || !*a.storage.github_key ||
        (a.update_checked && GetTickCount64() - a.update_checked < 21600000))
        return;
    a.update = mem_alloc(sizeof(*a.update));
    if (!a.update)
        return;
    a.update->notify = a.window;
    a.update->token = str_copy(a.storage.github_key);
    a.update->cancel = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (a.update->token && a.update->cancel)
        a.update_thread = CreateThread(NULL, 0, update_thread, a.update, 0, NULL);
    if (!a.update_thread)
        update_cleanup();
}
static LRESULT CALLBACK window_proc(HWND, UINT, WPARAM, LPARAM);
static bool save_settings(void);
static void run_ui_tests(void);
static int list_limit(void);
static void reveal_selected(void);
static float list_bottom(void);
static unsigned card_columns(void);
static J *lookup_repo(uint64_t id) {
    if (a.page == PAGE_DISCOVER) {
        J *candidate = find_candidate(&a.library, id);
        if (candidate)
            return candidate;
    }
    J *r = find_repo(&a.library, id);
    if (!r)
        r = find_candidate(&a.library, id);
    return r;
}
static void redraw(void) {
    InvalidateRect(a.window, NULL, FALSE);
}
static Palette colors(void) {
    Palette p = a.light ? (Palette){0xffffff, 0xf5f5f5, 0xffffff, 0xeeeeee, 0xe7e7e7, 0x242424,
                                    0x626262, 0x666666, 0xeeeeee, 0x2e60b4, 0xffffff}
                        : (Palette){0x191919, 0x212121, 0x191919, 0x2b2b2b, 0x353535, 0xececec,
                                    0xb1b1b1, 0x9e9e9e, 0x2d2d2d, 0x8bb2f1, 0x172234};
    return p;
}
static void notice(const char *s, bool persistent) {
    wchar_t *w = wide(s);
    if (w) {
        wcsncpy_s(a.notice, 1024, w, _TRUNCATE);
        mem_free(w);
    }
    a.notice_until = persistent ? 0 : GetTickCount64() + 7000;
    redraw();
}
static bool save_library(void) {
    char e[1024] = {0};
    if (!storage_save(&a.storage, &a.library, e)) {
        notice(e, true);
        return false;
    }
    return true;
}
static char *edit_text(HWND h) {
    int n = GetWindowTextLengthW(h);
    wchar_t *w = mem_alloc(((size_t)n + 1) * sizeof(wchar_t));
    if (!w)
        return str_copy("");
    GetWindowTextW(h, w, n + 1);
    char *s = utf8(w);
    mem_free(w);
    return s ? s : str_copy("");
}
static void edit_set(HWND h, const char *s) {
    wchar_t *w = wide(s);
    a.loading_edits = true;
    SetWindowTextW(h, w ? w : L"");
    a.loading_edits = false;
    mem_free(w);
}
static bool flush_settings(void) {
    return !a.settings_dirty || save_settings();
}
static void clear_filters(void) {
    for (int i = 0; i < a.lang_count; i++)
        mem_free(a.lang_filters[i]);
    mem_free(a.lang_filters);
    a.lang_filters = NULL;
    a.lang_count = 0;
}
static bool matches(J *r) {
    if (a.page == PAGE_LIBRARY) {
        if (a.filter == 0 && !jbool(r, "active"))
            return false;
        if (a.filter == 1 && (!jbool(r, "active") || summary_current(r)))
            return false;
        if (a.filter == 2 && jbool(r, "active"))
            return false;
    }
    if (*a.language && strcmp(a.language, jstr(r, "language")))
        return false;
    if (*a.search_text) {
        const char *fields[] = {"full_name", "description", "language",      "ai_summary",
                                "hook",      "try_idea",    "recommendation"};
        bool found = false;
        for (int i = 0; i < 7 && !found; i++) {
            wchar_t *w = wide(jstr(r, fields[i]));
            if (w) {
                found = FindNLSStringEx(LOCALE_NAME_INVARIANT, FIND_FROMSTART | NORM_IGNORECASE, w, -1,
                                        a.search_text, -1, NULL, NULL, NULL, 0) >= 0;
                mem_free(w);
            }
        }
        J *tags = jget(r, "tags"), *t;
        size_t i, n;
        yyjson_mut_arr_foreach(tags, i, n, t) {
            wchar_t *w = wide(yyjson_mut_get_str(t));
            if (w) {
                if (FindNLSStringEx(LOCALE_NAME_INVARIANT, FIND_FROMSTART | NORM_IGNORECASE, w, -1,
                                    a.search_text, -1, NULL, NULL, NULL, 0) >= 0)
                    found = true;
                mem_free(w);
            }
        }
        if (!found)
            return false;
    }
    return true;
}
static int compare_repos(const void *one, const void *two) {
    J *x = *(J **)one, *y = *(J **)two;
    if (a.sort == 1) {
        uint64_t s = jnum(x, "stars"), t = jnum(y, "stars");
        if (s != t)
            return s > t ? -1 : 1;
    } else if (a.sort == 2) {
        int c = strcmp(jstr(y, "pushed_at"), jstr(x, "pushed_at"));
        if (c)
            return c;
    } else if (a.sort == 0) {
        int c = strcmp(jstr(y, "starred_at"), jstr(x, "starred_at"));
        if (c)
            return c;
    }
    return _stricmp(jstr(x, "full_name"), jstr(y, "full_name"));
}
static void rebuild_visible(void) {
    mem_free(a.visible);
    a.visible = NULL;
    a.visible_count = 0;
    J *arr = jget(root(&a.library), a.page == PAGE_DISCOVER ? "discoveries" : "repos"), *r;
    size_t i, n, total = yyjson_mut_arr_size(arr);
    a.visible = mem_alloc((total + 1) * sizeof(J *));
    if (!a.visible)
        return;
    yyjson_mut_arr_foreach(arr, i, n, r) if (matches(r)) a.visible[a.visible_count++] = r;
    if (a.page != PAGE_DISCOVER)
        qsort(a.visible, a.visible_count, sizeof(J *), compare_repos);
    int max = list_limit();
    if (a.scroll > max)
        a.scroll = max;
    if (a.scroll < 0)
        a.scroll = 0;
    redraw();
}

static LRESULT CALLBACK edit_proc(HWND window, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR data) {
    (void)id;
    (void)data;
    if (msg == WM_SETCURSOR) {
        SetCursor(LoadCursorW(NULL, IDC_ARROW));
        return TRUE;
    }
    if (msg == EM_SETCUEBANNER) {
        SetPropW(window, L"StarCue", (HANDLE)lp);
        InvalidateRect(window, NULL, FALSE);
        return TRUE;
    }
    if (msg == WM_SETFOCUS || msg == WM_KILLFOCUS)
        redraw();
    if (msg == WM_PAINT || msg == WM_PRINTCLIENT) {
        LRESULT result = DefSubclassProc(window, msg, wp, lp);
        const wchar_t *cue = (const wchar_t *)GetPropW(window, L"StarCue");
        if (cue && !GetWindowTextLengthW(window) && GetFocus() != window) {
            HDC dc = msg == WM_PRINTCLIENT ? (HDC)wp : GetDC(window);
            if (dc) {
                int saved = SaveDC(dc);
                RECT bounds;
                GetClientRect(window, &bounds);
                bounds.left += LOWORD(SendMessageW(window, EM_GETMARGINS, 0, 0));
                Palette p = colors();
                SetTextColor(dc, RGB((p.muted >> 16) & 255, (p.muted >> 8) & 255, p.muted & 255));
                SetBkMode(dc, TRANSPARENT);
                SelectObject(dc, a.font);
                DrawTextW(dc, cue, -1, &bounds,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                RestoreDC(dc, saved);
                if (msg != WM_PRINTCLIENT)
                    ReleaseDC(window, dc);
            }
        }
        return result;
    }
    return DefSubclassProc(window, msg, wp, lp);
}
static HWND make_edit(int id, DWORD style, const wchar_t *name) {
    HWND h = CreateWindowExW(0, L"EDIT", L"",
                             WS_CHILD | WS_TABSTOP | ((style & ES_MULTILINE) ? 0 : ES_AUTOHSCROLL) | style, 0,
                             0, 1, 1, a.window, (HMENU)(INT_PTR)id, a.instance, NULL);
    SetWindowTextW(h, L"");
    SetPropW(h, L"StarLabel", (HANDLE)name);
    SendMessageW(h, EM_SETLIMITTEXT, 4096, 0);
    SetWindowSubclass(h, edit_proc, 1, 0);
    return h;
}
static void controls_create(void) {
    INITCOMMONCONTROLSEX common = {sizeof(common), ICC_WIN95_CLASSES};
    InitCommonControlsEx(&common);
    a.search = make_edit(EDIT_SEARCH, 0, L"搜索星库");
    SendMessageW(a.search, EM_SETCUEBANNER, 0, (LPARAM)L"搜索星库…");
    a.ghkey = make_edit(EDIT_GH_KEY, ES_PASSWORD, L"GitHub Token");
    SendMessageW(a.ghkey, EM_SETCUEBANNER, 0, (LPARAM)L"输入 Token");
    a.aiurl = make_edit(EDIT_AI_URL, 0, L"模型接口地址");
    SendMessageW(a.aiurl, EM_SETCUEBANNER, 0, (LPARAM)L"https://api.example.com");
    a.aimodel = make_edit(EDIT_AI_MODEL, 0, L"模型名称");
    a.aikey = make_edit(EDIT_AI_KEY, ES_PASSWORD, L"模型 API Key");
    a.tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL, WS_POPUP | TTS_NOPREFIX, 0, 0, 0, 0,
                                a.window, NULL, a.instance, NULL);
    TOOLINFOW ti = {sizeof(ti)};
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd = a.window;
    ti.uId = (UINT_PTR)a.window;
    ti.lpszText = LPSTR_TEXTCALLBACKW;
    SendMessageW(a.tooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
    SendMessageW(a.tooltip, TTM_SETMAXTIPWIDTH, 0, 440);
    SendMessageW(a.tooltip, TTM_SETDELAYTIME, TTDT_AUTOPOP, 15000);
    HWND all[] = {a.search, a.ghkey, a.aiurl, a.aimodel, a.aikey};
    for (int i = 0; i < 5; i++) {
        SendMessageW(all[i], WM_SETFONT, (WPARAM)a.font, FALSE);
        SetWindowTheme(all[i], a.light ? L"Explorer" : L"DarkMode_Explorer", NULL);
    }
}
static void set_settings_edits(void) {
    J *s = jget(root(&a.library), "settings");
    edit_set(a.ghkey, "");
    edit_set(a.aiurl, jstr(s, "ai_url"));
    edit_set(a.aimodel, jstr(s, "ai_model"));
    edit_set(a.aikey, a.storage.ai_key);
    a.concurrency = ai_concurrency(&a.library);
    a.settings_dirty = false;
}
static void position(HWND h, float x, float y, float w, float hgt, bool show) {
    float s = a.dpi / 96.0f;
    SetWindowPos(h, NULL, (int)(x * s), (int)(y * s), (int)(w * s), (int)(hgt * s),
                 SWP_NOZORDER | SWP_NOACTIVATE);
    ShowWindow(h, show ? SW_SHOWNA : SW_HIDE);
}
typedef struct {
    float x, y, w;
} SettingsFrame;
#define SETTINGS_CONTENT 740
static SettingsFrame settings_frame(void) {
    float available = a.width - a.side, w = available - 80;
    if (w > 720)
        w = 720;
    return (SettingsFrame){a.side + (available - w) / 2, 104.0f - a.setting_scroll, w};
}
static void layout(void) {
    RECT r;
    unsigned previous_columns = card_columns();
    size_t anchor = (size_t)(a.scroll / 216) * previous_columns;
    GetClientRect(a.window, &r);
    float scale = a.dpi / 96.0f;
    a.width = r.right / scale;
    a.height = r.bottom / scale;
    a.side = 144.0f;
    int settings_max = 104 + SETTINGS_CONTENT - (int)(a.height - 60);
    if (settings_max < 0)
        settings_max = 0;
    if (a.setting_scroll > settings_max)
        a.setting_scroll = settings_max;
    if (previous_columns != card_columns()) {
        a.scroll = (int)((anchor / card_columns()) * 216);
        a.hover = a.focus = -1;
    }
    bool list = a.page == PAGE_LIBRARY || a.page == PAGE_DISCOVER;
    SendMessageW(a.search, EM_SETCUEBANNER, 0,
                 (LPARAM)(a.page == PAGE_DISCOVER ? L"搜索项目…" : L"搜索星库…"));
    float search_width = a.width - a.side - 410;
    if (search_width > 580)
        search_width = 580;
    position(a.search, a.side + 45, 88, search_width - 28, 24, list);
    bool settings = a.page == PAGE_SETTINGS;
    SettingsFrame f = settings_frame();
    HWND entries[] = {a.ghkey, a.aiurl, a.aimodel, a.aikey};
    float ys[] = {f.y + 72, f.y + 256, f.y + 324, f.y + 392};
    for (int i = 0; i < 4; i++)
        position(entries[i], f.x + 14, ys[i], f.w - 28, 26,
                 settings && ys[i] >= 100 && ys[i] + 26 < a.height - 72);
    if (a.font_dpi != a.dpi) {
        HFONT old = a.font;
        a.font = CreateFontW(-MulDiv(14, a.dpi, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
                             L"Segoe UI");
        HWND all[] = {a.search, a.ghkey, a.aiurl, a.aimodel, a.aikey};
        for (int i = 0; i < 5; i++)
            SendMessageW(all[i], WM_SETFONT, (WPARAM)a.font, TRUE);
        if (old)
            DeleteObject(old);
        a.font_dpi = a.dpi;
    }
    for (int i = 0; i < 4; i++)
        EnableWindow(entries[i], !a.job);
}
static void set_page(int p) {
    if (p == a.page)
        return;
    if (!flush_settings())
        return;
    if (a.page == PAGE_LIBRARY || a.page == PAGE_DISCOVER) {
        ListState *state = &a.lists[a.page == PAGE_DISCOVER];
        state->scroll = a.scroll;
        state->filter = a.filter;
        state->sort = a.sort;
        state->selected = a.selected;
        wcscpy_s(state->search, 512, a.search_text);
        strcpy_s(state->language, 128, a.language);
    }
    a.page = p;
    a.focus = -1;
    a.hover = -1;
    a.scroll = 0;
    a.selected = 0;
    a.language[0] = 0;
    edit_set(a.search, "");
    a.search_text[0] = 0;
    clear_filters();
    if (p == PAGE_LIBRARY || p == PAGE_DISCOVER) {
        ListState *state = &a.lists[p == PAGE_DISCOVER];
        a.scroll = state->scroll;
        a.filter = state->filter;
        a.sort = state->sort;
        a.selected = state->selected;
        wcscpy_s(a.search_text, 512, state->search);
        strcpy_s(a.language, 128, state->language);
        a.loading_edits = true;
        SetWindowTextW(a.search, a.search_text);
        a.loading_edits = false;
    }
    if (p == PAGE_SETTINGS)
        set_settings_edits();
    rebuild_visible();
    layout();
    SetFocus(a.window);
    redraw();
}
static void target_add(float x, float y, float w, float h, int action, uint64_t id, HWND child) {
    if (y + h < 0 || y >= a.height)
        return;
    if ((a.page == PAGE_LIBRARY || a.page == PAGE_DISCOVER) &&
        (action == ACT_OPEN || action == ACT_STAR || action == ACT_IGNORE || (action == 0 && id) ||
         (action == ACT_SUMMARY && id)) &&
        (y < 140 || y + h > list_bottom()))
        return;
    if (a.page == PAGE_PROFILE && x > a.side && action != ACT_PROFILE && (y < 104 || y + h > a.height - 50))
        return;
    if (a.page == PAGE_SETTINGS && x > a.side && action != ACT_SCROLL && action != ACT_CANCEL &&
        (y < 104 || y + h > a.height - 50))
        return;
    if (a.target_count >= 500)
        return;
    Target *t = &a.targets[a.target_count++];
    t->r = rectf(x, y, w, h);
    t->action = action;
    t->id = id;
    t->child = child;
    t->mouse_hidden = false;
}
static bool hot(int action, uint64_t id) {
    return a.pointer_action == action && a.pointer_id == id;
}
static bool focused(int action, uint64_t id) {
    return GetFocus() == a.window && a.focus_action == action && a.focus_id == id;
}
static bool pressed(int action, uint64_t id) {
    return a.pressed_action == action && a.pressed_id == id && hot(action, id);
}
static void button(const char *text, float x, float y, float w, int action, uint64_t id, bool primary,
                   bool disabled) {
    Palette p = colors();
    bool h = hot(action, id) || focused(action, id);
    if (primary || h)
        gfx_fill(&a.gfx, x, y, w, 34, 7, primary ? p.accent : p.hover, disabled ? .45f : 1);
    if (!disabled && pressed(action, id))
        gfx_fill(&a.gfx, x, y, w, 34, 7, p.text, .14f);
    if (!disabled && h && primary)
        gfx_border(&a.gfx, x + 1, y + 1, w - 2, 32, 6, p.accent_text, .55f);
    gfx_text_center(&a.gfx, text, x + 12, y + 1, w - 24, 32, 1,
                    disabled ? p.faint : (primary ? p.accent_text : p.text));
    if (!disabled)
        target_add(x, y, w, 34, action, id, NULL);
}
static void text(const char *s, float x, float y, float w, float h, int font, unsigned color) {
    gfx_text(&a.gfx, s, x, y, w, h, font, color, false);
}
static void wrapped(const char *s, float x, float y, float w, float h, int font, unsigned color) {
    gfx_text(&a.gfx, s, x, y, w, h, font, color, true);
}
static void sidebar(void) {
    Palette p = colors();
    gfx_fill(&a.gfx, 0, 0, a.side, a.height, 0, p.sidebar, 1);
    text("Star Library", 20, 26, a.side - 32, 32, 4, p.text);
    const char *names[] = {"星库", "画像", "发现", "设置"};
    float ys[] = {100, 144, 188, a.height - 66};
    for (int i = 0; i < 4; i++) {
        bool selected = a.page == i, h = hot(ACT_NAV + i, 0) || focused(ACT_NAV + i, 0);
        if (selected || h)
            gfx_fill(&a.gfx, 14, ys[i], a.side - 28, 36, 7, selected ? p.raised : p.hover, 1);
        text(names[i], 28, ys[i], a.side - 56, 36, 2, selected ? p.text : p.muted);
        target_add(14, ys[i], a.side - 28, 36, ACT_NAV + i, 0, NULL);
    }
}
static void page_header(const char *title, const char *subtitle) {
    (void)subtitle;
    text(title, a.side + 32, 24, a.width - a.side - 240, 36, 4, colors().text);
}
static float list_bottom(void) {
    return a.height - ((*a.notice || a.job) ? 54.0f : 20.0f);
}
#define CARD_HEIGHT 200.0f
#define CARD_PITCH 216.0f
static unsigned card_columns(void) {
    float content = a.width - a.side - 64;
    unsigned n = content > 300 ? (unsigned)((content + 16) / 316) : 1;
    return n ? n : 1;
}
static float card_width(void) {
    return (a.width - a.side - 64 - 16 * (card_columns() - 1)) / card_columns();
}
static int list_limit(void) {
    unsigned cols = card_columns();
    size_t rows = (a.visible_count + cols - 1) / cols;
    float content = rows ? rows * CARD_PITCH - 16 : 0;
    int max = (int)(content - (list_bottom() - 140));
    return max > 0 ? max : 0;
}
static void reveal_selected(void) {
    for (size_t i = 0; i < a.visible_count; i++)
        if (jnum(a.visible[i], "id") == a.selected) {
            float top = (i / card_columns()) * CARD_PITCH;
            if (top < a.scroll)
                a.scroll = (int)top;
            else if (top + CARD_HEIGHT > a.scroll + list_bottom() - 140)
                a.scroll = (int)(top + CARD_HEIGHT - (list_bottom() - 140));
            return;
        }
}
static bool card_active(uint64_t id) {
    return a.hovered_card == id ||
           (a.pointer_id == id && (a.pointer_action == ACT_OPEN || a.pointer_action == ACT_STAR ||
                                   a.pointer_action == ACT_SUMMARY || a.pointer_action == ACT_IGNORE)) ||
           (a.focus_id == id && GetFocus() == a.window) ||
           (a.capture_expand && a.test_preview && a.visible_count && jnum(a.visible[0], "id") == id);
}
static void card_action(const char *label, float x, float y, float w, int action, uint64_t id, bool active) {
    if (active) {
        Palette p = colors();
        bool h = hot(action, id) || focused(action, id), down = pressed(action, id);
        unsigned color = a.job ? p.faint : down ? p.text : h ? p.accent : p.text;
        gfx_text_center(&a.gfx, label, x + 8, y + 1, w - 16, 32, 1, color);
        if (h && !a.job) {
            float width = gfx_text_width(&a.gfx, label, 1);
            if (width > w - 16)
                width = w - 16;
            gfx_line(&a.gfx, x + (w - width) / 2, y + 28, x + (w + width) / 2, y + 28, color,
                     down ? 2.0f : 1.0f);
        }
        if (!a.job)
            target_add(x, y, w, 34, action, id, NULL);
    } else {
        int count = a.target_count;
        target_add(x, y, w, 34, a.job ? 0 : action, id, NULL);
        if (a.target_count > count)
            a.targets[a.target_count - 1].mouse_hidden = true;
    }
    if (active && a.job)
        target_add(x, y, w, 34, 0, id, NULL);
}
static void repository_list(void) {
    Palette p = colors();
    float x = a.side + 32, w = card_width(), bottom = list_bottom();
    unsigned cols = card_columns();
    gfx_clip(&a.gfx, x, 140, a.width - x - 24, bottom - 140);
    size_t first = (size_t)(a.scroll / CARD_PITCH) * cols;
    for (size_t i = first; i < a.visible_count; i++) {
        float cy = 140 + (i / cols) * CARD_PITCH - a.scroll, cx = x + (i % cols) * (w + 16);
        if (cy >= bottom)
            break;
        J *r = a.visible[i];
        uint64_t id = jnum(r, "id");
        bool active = card_active(id);
        gfx_fill(&a.gfx, cx, cy, w, CARD_HEIGHT, 10, active ? p.hover : p.surface, 1);
        gfx_border(&a.gfx, cx, cy, w, CARD_HEIGHT, 10, p.border, active ? 1 : .65f);
        const char *full = jstr(r, "full_name"), *slash = strchr(full, '/');
        char avatar_fallback[1024];
        const unsigned char *pixels =
            avatar_pixels(jnum(r, "owner_id") ? jnum(r, "owner_id") : id, repo_avatar_url(r, avatar_fallback),
                          a.storage.github_key && *a.storage.github_key);
        if (pixels)
            gfx_avatar(&a.gfx, pixels, cx + 16, cy + 18, 32);
        else {
            gfx_fill(&a.gfx, cx + 16, cy + 18, 32, 32, 6, p.raised, 1);
            char initial[2] = {(char)((*full >= 'a' && *full <= 'z') ? *full - 32 : *full), 0};
            gfx_text_center(&a.gfx, initial, cx + 16, cy + 18, 32, 32, 3, p.muted);
        }
        text(slash ? slash + 1 : full, cx + 60, cy + 13, w - 76, 28, 3, p.text);
        char owner[320];
        sprintf_s(owner, sizeof(owner), "%.*s", slash ? (int)(slash - full) : 0, full);
        text(owner, cx + 60, cy + 39, w - 76, 22, 0, p.faint);
        wrapped(repo_caption(r), cx + 16, cy + 76, w - 32, 57, 1, p.muted);
        /* The repository body never overlaps its independent action strip. */
        float body_height = active ? 145 : CARD_HEIGHT;
        float top = cy < 140 ? 140 : cy, end = cy + body_height > bottom ? bottom : cy + body_height;
        if (end > top)
            target_add(cx, top, w, end - top, ACT_OPEN, id, NULL);
        float action_w = (w - 24) / (a.page == PAGE_DISCOVER ? 3 : 2);
        if (active) {
            J *owned = find_repo(&a.library, id);
            bool starred = owned && jbool(owned, "active");
            card_action(starred ? "Unstar" : "Star", cx + 12, cy + 154, action_w, ACT_STAR, id, true);
            card_action("重新概括", cx + 12 + action_w, cy + 154, action_w, ACT_SUMMARY, id, true);
            if (a.page == PAGE_DISCOVER)
                card_action("不感兴趣", cx + 12 + 2 * action_w, cy + 154, action_w, ACT_IGNORE, id, true);
        } else {
            text(jstr(r, "language"), cx + 16, cy + 158, w - 125, 26, 0, p.faint);
            char count[48];
            sprintf_s(count, sizeof(count), "%llu stars", (unsigned long long)jnum(r, "stars"));
            gfx_text_right(&a.gfx, count, cx + w - 116, cy + 158, 100, 26, 0, p.faint);
            card_action("", cx + 12, cy + 154, action_w, ACT_STAR, id, false);
            card_action("", cx + 12 + action_w, cy + 154, action_w, ACT_SUMMARY, id, false);
            if (a.page == PAGE_DISCOVER)
                card_action("", cx + 12 + 2 * action_w, cy + 154, action_w, ACT_IGNORE, id, false);
        }
    }
    gfx_unclip(&a.gfx);
    if (!a.visible_count) {
        text("暂无项目", x + 12, 207, a.width - x - 40, 38, 4, p.text);
        wrapped(*a.search_text || a.filter || *a.language ? "试试其他关键词或筛选条件。"
                : a.page == PAGE_DISCOVER                 ? "更新画像后，换一批感兴趣的项目。"
                                                          : "连接 GitHub 后同步 Stars。",
                x + 12, 255, a.width - x - 40, 55, 2, p.muted);
    }
}
static bool ai_running(void) {
    return a.job && (a.job->kind == JOB_SUMMARY || a.job->kind == JOB_PROFILE ||
                     a.job->kind == JOB_DISCOVER || a.job->kind == JOB_AI_TEST);
}
static const char *library_ai_label(void) {
    return ai_running() ? "AI 整理中…" : a.summary_retry ? "重试整理" : "AI 整理";
}
static const char *library_sync_label(void) {
    return a.job && a.job->kind == JOB_SYNC ? "同步中…" : "同步";
}
static void library_page(void) {
    Palette p = colors();
    char title[120];
    sprintf_s(title, sizeof(title), "%s  ·  %zu%s", a.page == PAGE_DISCOVER ? "项目发现" : "星库",
              a.visible_count, a.test_preview && !*a.snapshot ? "  ·  演示" : "");
    page_header(title, "");
    if (a.page == PAGE_LIBRARY)
        button(library_ai_label(), a.width - 254, 26, 110, ACT_SUMMARY, 0, false, a.job != NULL);
    button(a.page == PAGE_DISCOVER ? (a.job && a.job->kind == JOB_DISCOVER ? "发现中…" : "换一批")
                                   : library_sync_label(),
           a.width - 136, 26, 104, a.page == PAGE_DISCOVER ? ACT_DISCOVER : ACT_SYNC, 0, false,
           a.job != NULL);
    float sw = a.width - a.side - 410;
    if (sw > 580)
        sw = 580;
    gfx_fill(&a.gfx, a.side + 32, 81, sw, 38, 7, p.raised, 1);
    target_add(a.side + 32, 81, sw, 38, 0, 0, a.search);
    if (a.page == PAGE_LIBRARY) {
        const char *filters[] = {"已 Star ▾", "待整理 ▾", "已 Unstar ▾"};
        button(filters[a.filter], a.width - 370, 83, 110, ACT_FILTER, 0, false, false);
        char language[164];
        sprintf_s(language, sizeof(language), "%.120s ▾", *a.language ? a.language : "全部语言");
        button(language, a.width - 260, 83, 110, ACT_FILTER, 1, false, false);
        const char *sorts[] = {"最近 Star ▾", "最多 Stars ▾", "最近更新 ▾", "名称排序 ▾"};
        button(sorts[a.sort], a.width - 150, 83, 120, ACT_SORT, 0, false, false);
    }
    repository_list();
}
/* Fictional content for visual QA only; never persisted or fetched. */
static void preview_discoveries(void) {
    static const char *names[] = {"preview/desk-weather", "preview/window-layout",   "preview/pixel-garden",
                                  "preview/file-map",     "preview/json-playground", "preview/ambient-mixer"};
    static const char *descriptions[] = {"演示项目：可将天气与时间显示为桌面信息面板。",
                                         "演示项目：在 Windows 上保存和恢复窗口布局。",
                                         "演示项目：通过像素画布创建植物图案并导出 PNG 图片。",
                                         "演示项目：把本地目录显示为可展开的结构图，并按文件大小着色。",
                                         "演示项目：本地交互查看 JSON 树结构，比较两份数据的字段差异。",
                                         "演示项目：将雨声和环境音轨组合为可调整音量的背景声。"};
    J *arr = yyjson_mut_arr(a.library.doc);
    for (int i = 0; i < 6; i++) {
        J *r = yyjson_mut_obj(a.library.doc);
        jnumber(&a.library, r, "id", 9101 + i);
        jtext(&a.library, r, "full_name", names[i]);
        jtext(&a.library, r, "description", descriptions[i]);
        bool adjacent = i == 2 || i == 5;
        jtext(&a.library, r, "discovery_lane", adjacent ? "adjacent" : "core");
        jtext(&a.library, r, "note", "");
        jtext(&a.library, r, "ai_summary", "");
        jboolean(&a.library, r, "active", true);
        yyjson_mut_arr_append(arr, r);
    }
    jset(&a.library, root(&a.library), "discoveries", arr);
}
static void profile_page(void) {
    Palette p = colors();
    page_header(a.test_preview && !*a.snapshot ? "兴趣画像  ·  演示" : "兴趣画像", "");
    J *profile = jget(root(&a.library), "profile");
    button(a.job && a.job->kind == JOB_PROFILE ? "分析中…" : "更新画像", a.width - 152, 26, 120, ACT_PROFILE,
           1, false, a.job != NULL);
    float x = a.side + 48, w = a.width - x - 48, y = 110.0f - a.profile_scroll;
    if (w > 820)
        w = 820;
    gfx_clip(&a.gfx, x - 12, 100, a.width - x - 20, a.height - 154);
    if (!yyjson_mut_is_obj(profile)) {
        text("你的收藏，藏着怎样的好奇心？", x, y, w, 38, 4, p.text);
        wrapped("从项目用途出发，看看你反复被什么吸引。", x, y + 50, w, 60, 2, p.muted);
        a.scroll_limit = 0;
        gfx_unclip(&a.gfx);
        return;
    }
    if (!profile_current(&a.library)) {
        text("收藏线索有变化，可更新画像。", x, y, w, 24, 0, p.muted);
        y += 38;
    }
    size_t i, n;
    J *v;
    char *reflection = profile_reflection(profile);
    if (reflection) {
        float h = gfx_text_height(&a.gfx, reflection, w, 3);
        wrapped(reflection, x, y, w, h, 3, p.text);
        y += h + 42;
        mem_free(reflection);
    }
    if (jget(profile, "classifications")) {
        unsigned counts[6];
        unsigned total = profile_counts(profile, counts);
        if (!total) {
            text("资料不足，暂不显示比例。", x, y, w, 28, 1, p.muted);
            y += 52;
        } else
            for (unsigned dim = 0; dim < 6; dim++) {
                button(dimension_names[dim], x - 12, y, 130, ACT_DIMENSION, dim, false, false);
                char label[100];
                sprintf_s(label, sizeof(label), "%u%%", (counts[dim] * 100 + total / 2) / total);
                gfx_text_right(&a.gfx, label, x + 140, y + 6, w - 140, 24, 0, p.faint);
                target_add(x + 130, y, w - 130, 35, ACT_DIMENSION, dim, NULL);
                gfx_fill(&a.gfx, x, y + 40, w, 3, 1, p.border, 1);
                if (counts[dim])
                    gfx_fill(&a.gfx, x, y + 40, w * counts[dim] / total, 3, 1, p.muted, 1);
                y += 65;
                if (a.expanded_dimension == (int)dim) {
                    yyjson_mut_arr_foreach(jget(profile, "classifications"), i, n, v) {
                        if (!jbool(v, "sufficient"))
                            continue;
                        size_t ai, an;
                        J *axis;
                        yyjson_mut_arr_foreach(jget(v, "axes"), ai, an, axis) {
                            if (jnum(axis, "dimension") != dim)
                                continue;
                            J *r = find_repo(&a.library, jnum(v, "id"));
                            button(jstr(r, "full_name"), x - 12, y, w, ACT_EVIDENCE, jnum(v, "id"), false,
                                   false);
                            y += 36;
                            float h = gfx_text_height(&a.gfx, jstr(axis, "reason"), w, 1);
                            wrapped(jstr(axis, "reason"), x, y, w, h, 1, p.muted);
                            y += h + 20;
                        }
                    }
                    if (!counts[dim]) {
                        text("尚无这方面的收藏线索。", x, y, w, 28, 1, p.muted);
                        y += 40;
                    }
                    y += 10;
                }
            }
    }
    a.scroll_limit = (int)(y + a.profile_scroll + 20 - (a.height - 55));
    if (a.scroll_limit < 0)
        a.scroll_limit = 0;
    gfx_unclip(&a.gfx);
}
static void settings_page(void) {
    Palette p = colors();
    SettingsFrame f = settings_frame();
    float x = f.x, y = f.y, w = f.w;
    text(a.test_preview && !*a.snapshot ? "设置  ·  演示" : "设置", x, 24, w - 120, 36, 4, p.text);
    gfx_clip(&a.gfx, a.side + 25, 100, a.width - a.side - 50, a.height - 154);
    text("GitHub", x, y, w - 240, 32, 4, p.text);
    char connected[200];
    bool has_connection = a.storage.github_key && *a.storage.github_key && *jstr(root(&a.library), "account");
    sprintf_s(connected, sizeof(connected), has_connection ? "已连接 @%s" : "未连接",
              jstr(root(&a.library), "account"));
    gfx_text_right(&a.gfx, connected, x + w - 240, y, 240, 32, 1, p.muted);
    const char *labels[] = {"Token", "接口地址", "模型", "API Key"};
    float ys[] = {y + 40, y + 224, y + 292, y + 360};
    HWND entries[] = {a.ghkey, a.aiurl, a.aimodel, a.aikey};
    for (int i = 0; i < 4; i++) {
        text(labels[i], x, ys[i], w, 24, 1, p.muted);
        gfx_fill(&a.gfx, x, ys[i] + 26, w, 38, 8, p.raised, 1);
        if (GetFocus() == entries[i])
            gfx_border(&a.gfx, x, ys[i] + 26, w, 38, 8, p.accent, 1);
        if (ys[i] + 26 >= 100 && ys[i] + 64 < a.height - 72)
            target_add(x, ys[i] + 26, w, 38, 0, 0, entries[i]);
    }
    button(has_connection ? "更新连接" : "连接 GitHub", x, y + 116, 116, ACT_CONNECT, 0, true, a.job != NULL);
    button("获取 Token", x + 128, y + 116, 108, ACT_GH_HELP, 0, false, false);
    if (has_connection)
        button("断开", x + w - 72, y + 116, 72, ACT_DISCONNECT, 0, false, a.job != NULL);
    gfx_line(&a.gfx, x, y + 170, x + w, y + 170, p.border, 1);
    text("模型", x, y + 184, w, 32, 4, p.text);
    text("AI 并发", x, y + 440, 84, 34, 1, p.muted);
    const unsigned values[] = {1, 5, 10};
    for (int i = 0; i < 3; i++) {
        char label[8];
        sprintf_s(label, sizeof(label), "%u", values[i]);
        if (a.concurrency == values[i])
            gfx_fill(&a.gfx, x + 84 + i * 48, y + 440, 44, 34, 7, p.raised, 1);
        button(label, x + 84 + i * 48, y + 440, 44, ACT_CONCURRENCY, values[i], false, a.job != NULL);
    }
    button("测试连接", x + w - 112, y + 440, 112, ACT_AI_TEST, 0, false, a.job != NULL);
    gfx_line(&a.gfx, x, y + 498, x + w, y + 498, p.border, 1);
    text("外观", x, y + 516, 100, 34, 1, p.text);
    button(a.light ? "切换深色" : "切换浅色", x + w - 112, y + 516, 112, ACT_THEME, 0, false, a.job != NULL);
    text("数据", x, y + 576, 100, 34, 1, p.text);
    button("导出", x + w - 336, y + 576, 112, ACT_EXPORT, 0, false, a.job != NULL);
    button("导入", x + w - 224, y + 576, 112, ACT_IMPORT, 0, false, a.job != NULL);
    button("数据目录", x + w - 112, y + 576, 112, ACT_DIR, 0, false, false);
    text("版本 " APP_VERSION, x, y + 630, w - 200, 34, 1, p.muted);
    if (version_newer(a.latest, APP_VERSION))
        button("更新", x + w - 112, y + 630, 112, ACT_RELEASE, 0, false, false);
    text("关于与许可", x, y + 684, 150, 34, 1,
         hot(ACT_ABOUT, 0) || focused(ACT_ABOUT, 0) ? p.accent : p.muted);
    target_add(x - 12, y + 684, 174, 34, ACT_ABOUT, 0, NULL);
    button("★ Star me", x + w - 112, y + 684, 112, ACT_STAR_ME, 0, true, a.job != NULL);
    text(a.update ? "检查更新中…" : "", x + 130, y + 630, w - 300, 34, 1, p.faint);
    gfx_unclip(&a.gfx);
}
static int current_scroll(void) {
    return a.page == PAGE_SETTINGS ? a.setting_scroll : a.page == PAGE_PROFILE ? a.profile_scroll : a.scroll;
}
static void paint_scrollbar(void) {
    if (a.page == PAGE_SETTINGS)
        a.scroll_limit = (int)(104 + SETTINGS_CONTENT - (a.height - 60));
    else if (a.page != PAGE_PROFILE)
        a.scroll_limit = list_limit();
    if (a.scroll_limit <= 0)
        return;
    Palette p = colors();
    a.track_top = a.page == PAGE_LIBRARY || a.page == PAGE_DISCOVER ? 140.0f : 104.0f;
    a.track_height =
        (a.page == PAGE_LIBRARY || a.page == PAGE_DISCOVER ? list_bottom() : a.height - 56) - a.track_top;
    float track = a.track_height;
    float content = track + (float)a.scroll_limit;
    a.thumb_height = track * track / content;
    if (a.thumb_height < 30)
        a.thumb_height = 30;
    a.thumb_top = a.track_top + (track - a.thumb_height) * (float)current_scroll() / (float)a.scroll_limit;
    gfx_fill(&a.gfx, a.width - 10, a.track_top, 4, track, 2, p.border, .35f);
    gfx_fill(&a.gfx, a.width - 10, a.thumb_top, 4, a.thumb_height, 2, p.faint, .7f);
    target_add(a.width - 16, a.track_top, 16, track, ACT_SCROLL, 0, NULL);
}
static int settings_target_order(const void *one, const void *two) {
    const Target *l = one, *r = two;
    bool left = l->r.left < a.side, right = r->r.left < a.side;
    if (left != right)
        return left ? -1 : 1;
    if (l->r.top != r->r.top)
        return l->r.top < r->r.top ? -1 : 1;
    return l->r.left < r->r.left ? -1 : l->r.left > r->r.left;
}
static void paint(void) {
    if (!gfx_target(&a.gfx, a.window, a.dpi))
        return;
    Palette p = colors();
    a.pointer_action = a.focus_action = 0;
    a.pointer_id = a.focus_id = 0;
    if (a.hover >= 0 && a.hover < a.target_count) {
        a.pointer_action = a.targets[a.hover].action;
        a.pointer_id = a.targets[a.hover].id;
    }
    if (a.focus >= 0 && a.focus < a.target_count) {
        a.focus_action = a.targets[a.focus].action;
        a.focus_id = a.targets[a.focus].id;
    }
    a.target_count = 0;
    a.scroll_limit = 0;
    gfx_begin(&a.gfx, p.bg);
    sidebar();
    if (a.page == PAGE_PROFILE)
        profile_page();
    else if (a.page == PAGE_SETTINGS)
        settings_page();
    else
        library_page();
    paint_scrollbar();
    if (*a.notice || a.job) {
        gfx_fill(&a.gfx, a.side, a.height - 43, a.width - a.side, 43, 0, p.bg, 1);
        gfx_line(&a.gfx, a.side + 32, a.height - 44, a.width - 32, a.height - 44, p.border, 1);
        gfx_text_w(&a.gfx, a.notice, a.side + 32, a.height - 40, a.width - a.side - (a.job ? 160 : 64), 34, 0,
                   p.muted, false);
        if (a.job)
            button("取消任务", a.width - 136, a.height - 39, 104, ACT_CANCEL, 0, false,
                   a.job->kind == JOB_STAR || a.job->kind == JOB_SUPPORT);
    }
    if (a.page == PAGE_SETTINGS)
        qsort(a.targets, a.target_count, sizeof(Target), settings_target_order);
    gfx_end(&a.gfx);
}
static bool save_settings(void) {
    char *url = edit_text(a.aiurl), *model = edit_text(a.aimodel), *key = edit_text(a.aikey);
    char e[1024] = {0};
    bool ok = false;
    Library next = {0};
    if (!url || !model || !key || strpbrk(key, "\r\n")) {
        notice("请检查模型配置。", true);
        goto end;
    }
    char *normalized = GetFocus() == a.aiurl ? NULL : ai_endpoint(url, e);
    if (normalized) {
        mem_free(url);
        url = normalized;
    }
    /* Keep incomplete edits locally; validation belongs to starting a request. */
    e[0] = 0;
    J *current = jget(root(&a.library), "settings");
    if (!strcmp(url, jstr(current, "ai_url")) && !strcmp(model, jstr(current, "ai_model")) &&
        !strcmp(key, a.storage.ai_key ? a.storage.ai_key : "") &&
        a.concurrency == ai_concurrency(&a.library)) {
        a.settings_dirty = false;
        KillTimer(a.window, 5);
        if (GetFocus() != a.aiurl)
            edit_set(a.aiurl, url);
        ok = true;
        goto end;
    }
    if (!library_clone(&next, &a.library))
        goto end;
    J *s = jget(root(&next), "settings");
    jtext(&next, s, "ai_url", url);
    jtext(&next, s, "ai_model", model);
    jnumber(&next, s, "ai_concurrency", a.concurrency);
    library_touch(&next);
    ok = storage_commit(&a.storage, &next, &a.library, a.storage.github_key, key, e);
    if (ok) {
        library_free(&a.library);
        a.library = next;
        memset(&next, 0, sizeof(next));
        a.settings_dirty = false;
        KillTimer(a.window, 5);
        if (GetFocus() != a.aiurl)
            edit_set(a.aiurl, url);
    } else
        notice(*e ? e : "设置未保存。", true);
end:
    library_free(&next);
    mem_free(url);
    mem_free(model);
    secure_free(key);
    return ok;
}
static bool summaries_pending(void) {
    size_t i, n;
    J *r;

    yyjson_mut_arr_foreach(jget(root(&a.library), "repos"), i, n,
                           r) if (jbool(r, "active") && !summary_current(r)) return true;
    return false;
}
static void start_job(int kind, uint64_t repo_id, bool desired) {
    if (a.job)
        return;
    if (a.storage.demo) {
        notice("预览模式不会访问网络或保存数据。请正常启动程序连接你的账号。", false);
        return;
    }
    if (a.page == PAGE_SETTINGS) {
        if (a.settings_dirty && !save_settings())
            return;
    } else if (!flush_settings())
        return;
    Job *j = mem_alloc(sizeof(*j));
    if (!j || !library_clone(&j->library, &a.library)) {
        mem_free(j);
        notice("无法分配任务内存。", true);
        return;
    }
    j->github_key = kind == JOB_GH_CONNECT ? edit_text(a.ghkey) : str_copy(a.storage.github_key);
    if (kind == JOB_GH_CONNECT && (!j->github_key || !*j->github_key || strpbrk(j->github_key, "\r\n"))) {
        job_destroy(j);
        notice("请填写有效 Token。", true);
        return;
    }
    j->ai_key = str_copy(a.storage.ai_key);
    j->notify = a.window;
    j->cancel = CreateEventW(NULL, TRUE, FALSE, NULL);
    j->kind = kind;
    j->repo_id = repo_id;
    j->discovery_summary = kind == JOB_SUMMARY && a.page == PAGE_DISCOVER;
    j->desired_star = desired;
    j->base_revision = a.library.revision;
    a.job = j;
    a.thread = CreateThread(NULL, 0, job_thread, j, 0, NULL);
    if (!a.thread) {
        a.job = NULL;
        job_destroy(j);
        notice("无法创建后台任务。", true);
        return;
    }
    notice("正在准备请求…", true);
    layout();
    redraw();
}
static void open_repo(uint64_t id) {
    J *r = lookup_repo(id);
    if (!r)
        return;
    const char *name = jstr(r, "full_name");
    if (a.storage.demo && !strncmp(name, "preview/", 8)) {
        notice("这是界面演示项目。联网发现后的真实项目可在 GitHub 打开。", false);
        return;
    }
    for (const char *p = name; *p; p++)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
              strchr("/._-", *p)))
            return;
    char url[600];
    sprintf_s(url, sizeof(url), "https://github.com/%s", name);
    wchar_t *w = wide(url);
    if (w) {
        ShellExecuteW(a.window, L"open", w, NULL, NULL, SW_SHOWNORMAL);
        mem_free(w);
    }
}
static void file_dialog(bool importing) {
    wchar_t file[32768] = L"star-library.backup.json";
    OPENFILENAMEW of = {0};
    of.lStructSize = sizeof(of);
    of.hwndOwner = a.window;
    of.lpstrFilter = L"JSON 数据文件\0*.json\0\0";
    of.lpstrFile = file;
    of.nMaxFile = 32768;
    of.lpstrDefExt = L"json";
    of.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST |
               (importing ? OFN_FILEMUSTEXIST : OFN_OVERWRITEPROMPT);
    if (importing ? !GetOpenFileNameW(&of) : !GetSaveFileNameW(&of))
        return;
    if (!importing) {
        size_t n;
        char *json = library_json(&a.library, &n);
        if (json && file_write(file, json, n))
            notice("JSON 备份已导出，不包含密钥。", false);
        else
            notice("导出失败，请检查目标目录。", true);
        free(json);
        return;
    }
    size_t n;
    char *json = file_read(file, &n), e[1024] = {0};
    Library candidate = {0};
    if (!json || !library_parse(&candidate, json, n, e)) {
        notice(*e ? e : "无法读取导入文件。", true);
        mem_free(json);
        return;
    }
    mem_free(json);
    if (MessageBoxW(
            a.window,
            L"导入会替换当前收藏、笔记、画像与设置。现有数据将保留为 .bak 备份。\n确认导入这份 JSON？",
            APP_NAME, MB_YESNO | MB_ICONQUESTION) != IDYES) {
        library_free(&candidate);
        return;
    }
    Library old = a.library;
    a.library = candidate;
    if (!save_library()) {
        library_free(&a.library);
        a.library = old;
        return;
    }
    library_free(&old);
    a.selected = 0;

    rebuild_visible();
    layout();
    notice("数据已导入。", false);
}
static void feedback(uint64_t id, const char *state) {
    if (strcmp(state, "ignored"))
        return;
    Library next = {0};
    char error[1024] = {0};
    if (!library_clone(&next, &a.library)) {
        notice("内存不足。", true);
        return;
    }
    J *repo = find_candidate(&next, id);
    if (!repo) {
        library_free(&next);
        return;
    }
    J *arr = jget(root(&next), "feedback"), *v = NULL, *item;
    size_t i, n;
    yyjson_mut_arr_foreach(arr, i, n, item) if (jnum(item, "id") == id) {
        v = item;
        break;
    }
    if (!v) {
        v = yyjson_mut_obj(next.doc);
        jnumber(&next, v, "id", id);
        yyjson_mut_arr_append(arr, v);
    }
    jtext(&next, v, "state", "ignored");
    jset(&next, v, "repo", jclone(&next, repo));
    J *ds = jget(root(&next), "discoveries");
    yyjson_mut_arr_foreach(ds, i, n, item) if (jnum(item, "id") == id) {
        yyjson_mut_arr_remove(ds, i);
        break;
    }
    library_touch(&next);
    if (!storage_save(&a.storage, &next, error)) {
        library_free(&next);
        notice(error, true);
        return;
    }
    library_free(&a.library);
    a.library = next;
    a.hover = a.focus = -1;
    a.selected = 0;
    rebuild_visible();
    layout();
    notice("已记录不感兴趣。", false);
}
static int popup_menu(HMENU menu) {
    POINT p;
    int focused_target = a.focus >= 0 ? a.focus : a.hover;
    if (focused_target >= 0 && focused_target < a.target_count) {
        Target *t = &a.targets[focused_target];
        p.x = (LONG)(t->r.left * a.dpi / 96.0f);
        p.y = (LONG)(t->r.bottom * a.dpi / 96.0f);
        ClientToScreen(a.window, &p);
    } else
        GetCursorPos(&p);
    int result = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, p.x, p.y, 0, a.window, NULL);
    DestroyMenu(menu);
    SetFocus(a.window);
    return result;
}
static void filter_menu(bool language) {
    HMENU menu = CreatePopupMenu();
    if (language) {
        clear_filters();
        AppendMenuW(menu, MF_STRING | (!*a.language ? MF_CHECKED : 0), 1, L"全部语言");
        J *r;
        size_t i, n;
        yyjson_mut_arr_foreach(jget(root(&a.library), "repos"), i, n, r) {
            const char *s = jstr(r, "language");
            if (!*s)
                continue;
            bool found = false;
            for (int k = 0; k < a.lang_count; k++)
                if (!strcmp(s, a.lang_filters[k]))
                    found = true;
            if (found || a.lang_count >= 80)
                continue;
            char **p = mem_resize(a.lang_filters, (a.lang_count + 1) * sizeof(char *));
            if (!p)
                break;
            a.lang_filters = p;
            a.lang_filters[a.lang_count++] = str_copy(s);
            wchar_t *w = wide(s);
            AppendMenuW(menu, MF_STRING | (!strcmp(s, a.language) ? MF_CHECKED : 0), a.lang_count + 1, w);
            mem_free(w);
        }
    } else {
        AppendMenuW(menu, MF_STRING, 1, L"已 Star");
        AppendMenuW(menu, MF_STRING, 2, L"待整理");
        AppendMenuW(menu, MF_STRING, 3, L"已 Unstar");
        CheckMenuRadioItem(menu, 1, 3, a.filter + 1, MF_BYCOMMAND);
    }
    int result = popup_menu(menu);
    if (!result)
        return;
    if (language) {
        a.language[0] = 0;
        if (result >= 2 && result - 2 < a.lang_count)
            error_set(a.language, sizeof(a.language), a.lang_filters[result - 2]);
    } else
        a.filter = result - 1;
    a.scroll = 0;
    rebuild_visible();
}
static void perform(int action, uint64_t id) {
    if (action >= ACT_NAV && action < ACT_NAV + 4) {
        set_page(action - ACT_NAV);
        return;
    }
    if (action == ACT_EVIDENCE) {
        if (!flush_settings())
            return;
        set_page(PAGE_LIBRARY);
        a.filter = jbool(find_repo(&a.library, id), "active") ? 0 : 2;
        a.language[0] = 0;
        a.search_text[0] = 0;
        edit_set(a.search, "");
        rebuild_visible();
        a.selected = id;
        reveal_selected();
        layout();
        redraw();
        return;
    }
    if (a.job &&
        (action == ACT_IMPORT || action == ACT_IGNORE || action == ACT_THEME || action == ACT_DISCONNECT))
        return;
    switch (action) {
    case ACT_RELEASE:
        ShellExecuteW(a.window, L"open", PROJECT_URL L"/releases/latest", NULL, NULL, SW_SHOWNORMAL);
        break;
    case ACT_STAR_ME:
        if (a.storage.demo)
            break;
        ShellExecuteW(a.window, L"open", PROJECT_URL, NULL, NULL, SW_SHOWNORMAL);
        if (a.storage.github_key && *a.storage.github_key && !a.job)
            start_job(JOB_SUPPORT, 0, true);
        break;
    case ACT_CONCURRENCY:
        if (!a.job && (id == 1 || id == 5 || id == 10)) {
            a.concurrency = (unsigned)id;
            a.settings_dirty = true;
            save_settings();
            redraw();
        }
        break;
    case ACT_DIMENSION:
        a.expanded_dimension = a.expanded_dimension == (int)id ? -1 : (int)id;
        redraw();
        break;
    case ACT_CONNECT:
        start_job(JOB_GH_CONNECT, 0, false);
        break;
    case ACT_DISCONNECT: {
        update_cleanup();
        char e[1024] = {0};
        if (storage_commit(&a.storage, &a.library, &a.library, "", a.storage.ai_key, e)) {
            edit_set(a.ghkey, "");
            notice("已断开 GitHub。", false);
            redraw();
        } else
            notice(e, true);
        break;
    }
    case ACT_SUMMARY:
        if (a.job)
            break;
        if (!id && !summaries_pending()) {
            notice("中文概括已是最新。", false);
            break;
        }

        start_job(JOB_SUMMARY, id, false);
        break;
    case ACT_SETTINGS:
        set_page(PAGE_SETTINGS);
        break;
    case ACT_SYNC:
        start_job(JOB_SYNC, 0, false);
        break;
    case ACT_PROFILE:
        if (!id)
            set_page(PAGE_PROFILE);
        else
            start_job(JOB_PROFILE, 0, false);
        break;
    case ACT_DISCOVER:
        start_job(JOB_DISCOVER, 0, false);
        break;
    case ACT_AI_TEST:
        start_job(JOB_AI_TEST, 0, false);
        break;
    case ACT_OPEN:
        open_repo(id);
        break;
    case ACT_STAR: {
        J *r = find_repo(&a.library, id);
        bool desired = !r || !jbool(r, "active");
        start_job(JOB_STAR, id, desired);
        break;
    }
    case ACT_IGNORE:
        feedback(id, "ignored");
        break;
    case ACT_EXPORT:
        if (flush_settings())
            file_dialog(false);
        break;
    case ACT_IMPORT:
        if (flush_settings())
            file_dialog(true);
        break;
    case ACT_THEME: {
        a.light = !a.light;
        jboolean(&a.library, jget(root(&a.library), "settings"), "light", a.light);
        save_library();
        Palette p = colors();
        if (a.input_brush)
            DeleteObject(a.input_brush);
        a.input_brush = CreateSolidBrush(RGB((p.raised >> 16) & 255, (p.raised >> 8) & 255, p.raised & 255));
        BOOL dark = !a.light;
        DwmSetWindowAttribute(a.window, 20, &dark, sizeof(dark));
        HWND themed[] = {a.search, a.ghkey, a.aiurl, a.aimodel, a.aikey};
        for (int i = 0; i < 5; i++)
            SetWindowTheme(themed[i], a.light ? L"Explorer" : L"DarkMode_Explorer", NULL);
        RedrawWindow(a.window, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);
        break;
    }
    case ACT_FILTER:
        filter_menu(id == 1);
        break;
    case ACT_SORT: {
        HMENU menu = CreatePopupMenu();
        const wchar_t *names[] = {L"最近收藏", L"最多 Stars", L"最近更新", L"名称排序"};
        for (int i = 0; i < 4; i++)
            AppendMenuW(menu, MF_STRING, i + 1, names[i]);
        CheckMenuRadioItem(menu, 1, 4, a.sort + 1, MF_BYCOMMAND);
        int result = popup_menu(menu);
        if (result) {
            a.sort = result - 1;
            a.scroll = 0;
            rebuild_visible();
        }
        break;
    }
    case ACT_CANCEL:

        if (a.job && a.job->kind != JOB_STAR && a.job->kind != JOB_SUPPORT) {
            SetEvent(a.job->cancel);
            notice("正在取消，等待当前网络步骤返回…", true);
        }
        break;
    case ACT_GH_HELP:
        ShellExecuteW(a.window, L"open", L"https://github.com/settings/personal-access-tokens", NULL, NULL,
                      SW_SHOWNORMAL);
        break;
    case ACT_ABOUT: {
        Buffer about = {0};
        buf_add(&about, "Star Library " APP_VERSION "\nC17 · Win32 · Direct2D · DirectWrite\n\n");
        const char *titles[] = {"yyjson — MIT license\n", "\nStar Library — MIT license\n"};
        for (int i = 0; i < 2; i++) {
            HRSRC resource = FindResourceW(a.instance, MAKEINTRESOURCEW(2 + i), RT_RCDATA);
            if (resource) {
                HGLOBAL data = LoadResource(a.instance, resource);
                buf_add(&about, titles[i]);
                buf_addn(&about, LockResource(data), SizeofResource(a.instance, resource));
            }
        }
        wchar_t *body = wide(about.data ? about.data : "");
        MessageBoxW(a.window, body ? body : L"Star Library", L"关于与开源许可", MB_OK);
        mem_free(body);
        buf_drop(&about);
        break;
    }
    case ACT_DIR:
        ShellExecuteW(a.window, L"open", a.storage.dir, NULL, NULL, SW_SHOWNORMAL);
        break;
    }
}
static int hit(float x, float y) {
    for (int i = a.target_count - 1; i >= 0; i--) {
        if (a.targets[i].mouse_hidden)
            continue;
        D2D1_RECT_F r = a.targets[i].r;
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom)
            return i;
    }
    return -1;
}
static uint64_t card_at(float x, float y) {
    if (a.page != PAGE_LIBRARY && a.page != PAGE_DISCOVER)
        return 0;
    float left = a.side + 32, w = card_width();
    if (x < left || y < 140 || y >= list_bottom())
        return 0;
    unsigned col = (unsigned)((x - left) / (w + 16)), row = (unsigned)((y - 140 + a.scroll) / CARD_PITCH);
    if (col >= card_columns() || x - left - col * (w + 16) >= w ||
        y - 140 + a.scroll - row * CARD_PITCH >= CARD_HEIGHT)
        return 0;
    size_t index = (size_t)row * card_columns() + col;
    return index < a.visible_count ? jnum(a.visible[index], "id") : 0;
}
static void capture_window(const wchar_t *path) {
    RECT r;
    GetClientRect(a.window, &r);
    int w = r.right, h = r.bottom;
    HDC screen = GetDC(a.window), dc = CreateCompatibleDC(screen);
    BITMAPINFO bi = {0};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void *bits;
    HBITMAP bitmap = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    HGDIOBJ old = SelectObject(dc, bitmap);
    /* Render the same native UI into a DIB; the desktop compositor can discard
       occluded HWND contents, making PrintWindow return an all-white image. */
    Graphics live = a.gfx;
    a.gfx.target = a.gfx.brush = a.gfx.gradient = NULL;
    if (gfx_dc_target(&a.gfx, dc, &r, a.dpi)) {
        paint();
        HWND inputs[] = {a.search, a.ghkey, a.aiurl, a.aimodel, a.aikey};
        for (int i = 0; i < 5; i++) {
            if (!IsWindowVisible(inputs[i]))
                continue;
            RECT entry;
            GetWindowRect(inputs[i], &entry);
            MapWindowPoints(NULL, a.window, (POINT *)&entry, 2);
            int saved = SaveDC(dc);
            SetViewportOrgEx(dc, entry.left, entry.top, NULL);
            IntersectClipRect(dc, 0, 0, entry.right - entry.left, entry.bottom - entry.top);
            SendMessageW(inputs[i], WM_PRINTCLIENT, (WPARAM)dc, PRF_CLIENT | PRF_ERASEBKGND);
            RestoreDC(dc, saved);
        }
    } else if (!PrintWindow(a.window, dc, 3))
        BitBlt(dc, 0, 0, w, h, screen, 0, 0, SRCCOPY);
    gfx_target_free(&a.gfx);
    a.gfx = live;
    BITMAPFILEHEADER header = {0};
    header.bfType = 0x4d42;
    header.bfOffBits = sizeof(header) + sizeof(BITMAPINFOHEADER);
    header.bfSize = header.bfOffBits + w * h * 4;
    Buffer b = {0};
    buf_addn(&b, (const char *)&header, sizeof(header));
    buf_addn(&b, (const char *)&bi.bmiHeader, sizeof(BITMAPINFOHEADER));
    buf_addn(&b, bits, (size_t)w * h * 4);
    file_write(path, b.data, b.len);
    buf_drop(&b);
    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteDC(dc);
    ReleaseDC(a.window, screen);
}
static void scroll_by(int delta) {
    if (a.page == PAGE_SETTINGS) {
        a.setting_scroll += delta;
        if (a.setting_scroll < 0)
            a.setting_scroll = 0;
        int max = 104 + SETTINGS_CONTENT - (int)(a.height - 60);
        if (max < 0)
            max = 0;
        if (a.setting_scroll > max)
            a.setting_scroll = max;
        layout();
    } else if (a.page == PAGE_PROFILE) {
        a.profile_scroll += delta;
        if (a.profile_scroll < 0)
            a.profile_scroll = 0;
        if (a.profile_scroll > a.scroll_limit)
            a.profile_scroll = a.scroll_limit > 0 ? a.scroll_limit : 0;
    } else {
        a.focus = a.hover = -1;
        a.hovered_card = 0;
        a.scroll += delta;
        rebuild_visible();
    }
    redraw();
}
static LRESULT CALLBACK window_proc(HWND window, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_NOTIFY: {
        NMHDR *header = (NMHDR *)lp;
        if (header->hwndFrom == a.tooltip && header->code == TTN_GETDISPINFOW) {
            NMTTDISPINFOW *info = (NMTTDISPINFOW *)lp;
            POINT point;
            GetCursorPos(&point);
            ScreenToClient(window, &point);
            int index = hit(point.x * 96.0f / a.dpi, point.y * 96.0f / a.dpi);
            mem_free(a.tip_text);
            a.tip_text = NULL;
            if (index >= 0 && a.targets[index].action == ACT_OPEN)
                a.tip_text = wide(repo_caption(lookup_repo(a.targets[index].id)));
            info->lpszText = a.tip_text ? a.tip_text : L"";
            return 0;
        }
        break;
    }
    case WM_CREATE:
        a.window = window;
        controls_create();
        return 0;
    case WM_SIZE:
        gfx_resize(&a.gfx, LOWORD(lp), HIWORD(lp));
        layout();
        rebuild_visible();
        return 0;
    case WM_DPICHANGED:
        a.dpi = HIWORD(wp);
        gfx_target_free(&a.gfx);
        {
            RECT *r = (RECT *)lp;
            SetWindowPos(window, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        layout();
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO *m = (MINMAXINFO *)lp;
        m->ptMinTrackSize.x = MulDiv(980, a.dpi, 96);
        m->ptMinTrackSize.y = MulDiv(700, a.dpi, 96);
        if (*a.capture || *a.ui_test_report) {
            /* Deterministic QA captures may extend beyond the active monitor. */
            m->ptMaxTrackSize.x = m->ptMaxTrackSize.y = 8192;
            m->ptMaxSize.x = m->ptMaxSize.y = 8192;
        }
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(window, &ps);
        paint();
        EndPaint(window, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (*a.capture && !a.capture_inject)
            return 0;
        if (a.dragging_scroll) {
            float y = GET_Y_LPARAM(lp) * 96.0f / a.dpi;
            float range = a.track_height - a.thumb_height;
            int next = range > 0 ? (int)((y - a.drag_offset - a.track_top) * a.scroll_limit / range) : 0;
            if (next < 0)
                next = 0;
            if (next > a.scroll_limit)
                next = a.scroll_limit;
            scroll_by(next - current_scroll());
            return 0;
        }
        uint64_t card = card_at(GET_X_LPARAM(lp) * 96.0f / a.dpi, GET_Y_LPARAM(lp) * 96.0f / a.dpi);
        if (card != a.hovered_card) {
            a.hovered_card = card;
            redraw();
        }
        int h = hit(GET_X_LPARAM(lp) * 96.0f / a.dpi, GET_Y_LPARAM(lp) * 96.0f / a.dpi);
        if (h != a.hover) {
            a.hover = h;
            redraw();
        }
        TRACKMOUSEEVENT t = {sizeof(t), TME_LEAVE, window, 0};
        TrackMouseEvent(&t);
        return 0;
    }
    case WM_MOUSELEAVE:
        a.hover = -1;
        a.hovered_card = 0;
        redraw();
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            SetCursor(LoadCursorW(NULL, IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_LBUTTONDOWN: {
        if (*a.capture && !a.capture_inject)
            return 0;
        int h = hit(GET_X_LPARAM(lp) * 96.0f / a.dpi, GET_Y_LPARAM(lp) * 96.0f / a.dpi);
        if (h < 0) {
            uint64_t card = card_at(GET_X_LPARAM(lp) * 96.0f / a.dpi, GET_Y_LPARAM(lp) * 96.0f / a.dpi);
            if (card) {
                a.pressed_action = ACT_OPEN;
                a.pressed_id = card;
                SetCapture(window);
            }
        }
        if (h >= 0) {
            Target t = a.targets[h];
            a.focus = -1;
            if (t.action == ACT_SCROLL) {
                float y = GET_Y_LPARAM(lp) * 96.0f / a.dpi;
                a.dragging_scroll = true;
                a.drag_offset = (y >= a.thumb_top && y <= a.thumb_top + a.thumb_height) ? y - a.thumb_top
                                                                                        : a.thumb_height / 2;
                SetCapture(window);
                SendMessageW(window, WM_MOUSEMOVE, wp, lp);
            } else if (t.child)
                SetFocus(t.child);
            else {
                SetFocus(window);
                if (t.action) {
                    a.hover = h;
                    a.pressed_action = t.action;
                    a.pressed_id = t.id;
                    SetCapture(window);
                    redraw();
                    UpdateWindow(window);
                }
            }
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        int action = a.pressed_action;
        uint64_t id = a.pressed_id;
        int h = hit(GET_X_LPARAM(lp) * 96.0f / a.dpi, GET_Y_LPARAM(lp) * 96.0f / a.dpi);
        bool activate = h >= 0 && a.targets[h].action == action && a.targets[h].id == id;
        if (action == ACT_OPEN && h < 0)
            activate = card_at(GET_X_LPARAM(lp) * 96.0f / a.dpi, GET_Y_LPARAM(lp) * 96.0f / a.dpi) == id;
        a.pressed_action = 0;
        a.pressed_id = 0;
        if (a.dragging_scroll) {
            a.dragging_scroll = false;
        }
        if (GetCapture() == window)
            ReleaseCapture();
        if (action && activate)
            perform(action, id);
        redraw();
        return 0;
    }
    case WM_CAPTURECHANGED:
        a.dragging_scroll = false;
        a.pressed_action = 0;
        a.pressed_id = 0;
        redraw();
        return 0;
    case WM_MOUSEWHEEL:
        if (*a.capture)
            return 0;
        scroll_by(-GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * 96);
        return 0;
    case WM_VSCROLL:
        if (*a.capture)
            return 0;
        scroll_by(LOWORD(wp) == SB_PAGEUP     ? -300
                  : LOWORD(wp) == SB_PAGEDOWN ? 300
                  : LOWORD(wp) == SB_LINEUP   ? -40
                                              : 40);
        return 0;
    case WM_COMMAND: {
        int id = LOWORD(wp), event = HIWORD(wp);
        if (event == EN_CHANGE && !a.loading_edits) {
            if (id == EDIT_SEARCH) {
                GetWindowTextW(a.search, a.search_text, 512);
                a.scroll = 0;
                rebuild_visible();
            } else if (id > EDIT_GH_KEY && id <= EDIT_AI_KEY) {
                a.settings_dirty = true;
                SetTimer(window, 5, 350, NULL);
            }
        }
        if (event == EN_KILLFOCUS && !a.loading_edits && id > EDIT_GH_KEY && id <= EDIT_AI_KEY &&
            (a.settings_dirty || id == EDIT_AI_URL) && !a.job)
            save_settings();
        return 0;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {
        Palette p = colors();
        HDC dc = (HDC)wp;
        SetTextColor(dc, RGB((p.text >> 16) & 255, (p.text >> 8) & 255, p.text & 255));
        SetBkColor(dc, RGB((p.raised >> 16) & 255, (p.raised >> 8) & 255, p.raised & 255));
        return (LRESULT)a.input_brush;
    }
    case WM_KEYDOWN:
        if (*a.capture)
            return 0;
        if (wp == VK_RETURN || wp == VK_SPACE) {
            if (a.focus >= 0 && a.focus < a.target_count) {
                Target t = a.targets[a.focus];
                if (t.child)
                    SetFocus(t.child);
                else
                    perform(t.action, t.id);
            }
            return 0;
        }
        if (wp == VK_DOWN) {
            scroll_by(90);
            return 0;
        }
        if (wp == VK_UP) {
            scroll_by(-90);
            return 0;
        }
        if (wp == VK_NEXT) {
            scroll_by(450);
            return 0;
        }
        if (wp == VK_PRIOR) {
            scroll_by(-450);
            return 0;
        }
        break;
    case WM_AVATAR:
        redraw();
        return 0;
    case WM_BATCH: {
        Job *j = (Job *)lp;
        char e[1024] = {0};
        Library next = {0};
        if (a.job != j || a.library.revision != j->base_revision ||
            WaitForSingleObject(j->cancel, 0) == WAIT_OBJECT_0) {
            error_set(j->error, sizeof(j->error), "任务已取消或资料已变化。");
            return FALSE;
        }
        if (!library_clone(&next, &j->library) || !storage_save(&a.storage, &next, e)) {
            library_free(&next);
            error_set(j->error, sizeof(j->error), *e ? e : "保存失败。");
            return FALSE;
        }
        library_free(&a.library);
        a.library = next;
        j->base_revision = a.library.revision;
        a.hover = a.focus = -1;
        rebuild_visible();
        redraw();
        return TRUE;
    }
    case WM_PROGRESS: {
        char *s = (char *)lp;
        if (a.job)
            notice(s, true);
        mem_free(s);
        return 0;
    }
    case WM_JOB: {
        Job *j = (Job *)lp;
        bool discovered = false;
        if (a.thread) {
            CloseHandle(a.thread);
            a.thread = NULL;
        }
        a.job = NULL;

        int kind = j->kind;
        if (kind == JOB_SUMMARY)
            a.summary_retry = j->retry_count > 0 || !j->ok;
        if (j->ok && j->base_revision == a.library.revision) {
            if (j->kind == JOB_SUMMARY) {
                char message[160];
                sprintf_s(message, sizeof(message), "已保存 %u 项%s", j->completed,
                          j->insufficient ? "，资料不足的项目已跳过。" : "。");
                notice(message, false);
            } else if (j->kind == JOB_AI_TEST)
                notice("AI 连接测试通过。", false);
            else {
                char e[1024] = {0};
                bool saved = j->kind == JOB_GH_CONNECT ? storage_commit(&a.storage, &j->library, &a.library,
                                                                        j->github_key, a.storage.ai_key, e)
                                                       : storage_save(&a.storage, &j->library, e);
                if (saved) {
                    library_free(&a.library);
                    a.library = j->library;
                    memset(&j->library, 0, sizeof(j->library));

                    discovered = j->kind == JOB_DISCOVER;
                    if (kind == JOB_GH_CONNECT) {
                        a.update_checked = 0;
                        edit_set(a.ghkey, "");
                        notice("GitHub 已连接。", false);
                    } else if (kind == JOB_PROFILE) {
                        char message[160];
                        if (j->retry_count)
                            sprintf_s(message, sizeof(message), "画像已更新，%u 项分类可稍后重试。",
                                      j->retry_count);
                        else
                            strcpy_s(message, sizeof(message), "兴趣画像已更新。");
                        notice(message, false);
                    } else if (kind == JOB_SUPPORT)
                        notice(j->star_unchanged ? "已经 Star。" : "已 Star，感谢支持。", false);
                    else
                        notice(kind == JOB_SYNC       ? "Stars 同步完成。"
                               : kind == JOB_DISCOVER ? "项目发现完成。"
                                                      : "GitHub Star 已更新。",
                               false);
                } else
                    notice(e, true);
            }
        } else
            notice(j->ok ? "资料已变化，本轮未覆盖当前数据，请重试。" : j->error, true);
        job_destroy(j);
        if (discovered) {
            memset(&a.lists[1], 0, sizeof(a.lists[1]));
            if (a.page == PAGE_DISCOVER) {
                a.scroll = 0;
                a.search_text[0] = 0;
                a.language[0] = 0;
                edit_set(a.search, "");
            }
        }
        if (kind != JOB_SUMMARY)
            a.selected = 0;

        rebuild_visible();
        layout();
        if (a.close_after_job)
            PostMessageW(window, WM_CLOSE, 0, 0);
        return 0;
    }
    case WM_TIMER:
        if (wp == 6 && a.page == PAGE_SETTINGS)
            update_start();
        if (wp == 5) {
            KillTimer(window, 5);
            if (a.settings_dirty && !a.job)
                save_settings();
        }
        if (wp == 4 && *a.ui_test_report) {
            HWND dialog = GetWindow(window, GW_ENABLEDPOPUP);
            if (dialog && dialog != window) {
                HWND yes = GetDlgItem(dialog, IDYES);
                if (yes) {
                    KillTimer(window, 4);
                    SendMessageW(yes, BM_CLICK, 0, 0);
                }
            }
        }
        if (wp == 3) {
            KillTimer(window, 3);
            run_ui_tests();
            PostMessageW(window, WM_CLOSE, 0, 0);
        }
        if (wp == 1 && a.notice_until && GetTickCount64() > a.notice_until) {
            a.notice[0] = 0;
            a.notice_until = 0;
            redraw();
        }
        if (wp == 2) {
            KillTimer(window, 2);
            if (a.test_preview && a.capture_command) {
                a.capture_inject = true;
                for (int i = 0; i < a.target_count; i++) {
                    if (a.targets[i].action != ACT_STAR || a.targets[i].mouse_hidden)
                        continue;
                    Target t = a.targets[i];
                    LPARAM point = MAKELPARAM((int)((t.r.left + (t.r.right - t.r.left) / 2) * a.dpi / 96),
                                              (int)((t.r.top + 16) * a.dpi / 96));
                    SendMessageW(window, WM_MOUSEMOVE, 0, point);
                    if (a.capture_pressed)
                        SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, point);
                    UpdateWindow(window);
                    break;
                }
                a.capture_inject = false;
            }
            capture_window(a.capture);
            if (a.capture_exit)
                PostMessageW(window, WM_CLOSE, 0, 0);
        }
        return 0;
    case WM_CLOSE:
        if (a.job) {
            const wchar_t *question = a.job->kind == JOB_STAR || a.job->kind == JOB_SUPPORT
                                          ? L"GitHub Star 写入尚未完成。等待操作返回后退出？"
                                          : L"后台任务尚未完成。请求取消并在任务返回后退出？";
            if (MessageBoxW(window, question, APP_NAME, MB_YESNO | MB_ICONQUESTION) != IDYES)
                return 0;
            /* MessageBox pumps WM_JOB: the task may finish while it is open. */
            if (a.job) {
                if (a.job->kind != JOB_STAR && a.job->kind != JOB_SUPPORT)
                    SetEvent(a.job->cancel);
                a.close_after_job = true;
                notice("正在结束后台任务…", true);
                return 0;
            }
        }
        if (!flush_settings())
            return 0;
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        update_cleanup();
        PostQuitMessage(0);
        return 0;
    case WM_UPDATE:
        if (a.update && (UpdateCheck *)lp == a.update) {
            if (a.update->ok)
                strcpy_s(a.latest, sizeof(a.latest), a.update->latest);
            a.update_checked = GetTickCount64();
            update_cleanup();
            redraw();
        }
        return 0;
    }
    return DefWindowProcW(window, msg, wp, lp);
}
static void keyboard_tab(bool reverse) {
    HWND focused_window = GetFocus();
    int current = a.focus;
    if (focused_window != a.window) {
        current = -1;
        for (int i = 0; i < a.target_count; i++)
            if (a.targets[i].child == focused_window) {
                current = i;
                break;
            }
    }
    if (!a.target_count)
        return;
    int next = current;
    for (int tries = 0; tries < a.target_count; tries++) {
        next = reverse ? (next <= 0 ? a.target_count - 1 : next - 1) : (next + 1) % a.target_count;
        HWND child = a.targets[next].child;
        if ((!child && a.targets[next].action) || (child && IsWindowEnabled(child) && IsWindowVisible(child)))
            break;
    }
    a.focus = next;
    Target *t = &a.targets[next];
    if (t->child)
        SetFocus(t->child);
    else
        SetFocus(a.window);
    redraw();
}
static void parse_args(const wchar_t *command, wchar_t *data_dir) {
    int n;
    wchar_t **args = CommandLineToArgvW(GetCommandLineW(), &n);
    (void)command;
    for (int i = 1; i < n; i++) {
        if (!wcscmp(args[i], L"--data-dir") && i + 1 < n)
            wcscpy_s(data_dir, 32768, args[++i]);
        else if (!wcscmp(args[i], L"--hover-command") || !wcscmp(args[i], L"--press-command")) {
            a.capture_command = a.capture_expand = true;
            a.capture_pressed = !wcscmp(args[i], L"--press-command");
        } else if ((!wcscmp(args[i], L"--expanded") || !wcscmp(args[i], L"--hover-first")))
            a.capture_expand = true;
        else if (!wcscmp(args[i], L"--preview"))
            a.test_preview = true;
        else if (!wcscmp(args[i], L"--snapshot") && i + 1 < n) {
            wcscpy_s(a.snapshot, 32768, args[++i]);
            a.test_preview = true;
        } else if (!wcscmp(args[i], L"--ui-test") && i + 1 < n) {
            wcscpy_s(a.ui_test_report, 32768, args[++i]);
            a.test_preview = true;
        } else if (!wcscmp(args[i], L"--light"))
            a.light = true;
        else if (!wcscmp(args[i], L"--page") && i + 1 < n) {
            i++;
            if (!wcscmp(args[i], L"profile"))
                a.page = PAGE_PROFILE;
            else if (!wcscmp(args[i], L"discover"))
                a.page = PAGE_DISCOVER;
            else if (!wcscmp(args[i], L"settings"))
                a.page = PAGE_SETTINGS;
        } else if (!wcscmp(args[i], L"--capture") && i + 1 < n) {
            wcscpy_s(a.capture, 32768, args[++i]);
            a.capture_exit = true;
        } else if (!wcscmp(args[i], L"--width") && i + 1 < n)
            a.capture_w = _wtoi(args[++i]);
        else if (!wcscmp(args[i], L"--height") && i + 1 < n)
            a.capture_h = _wtoi(args[++i]);
        else if (!wcscmp(args[i], L"--dpi") && i + 1 < n)
            a.capture_dpi = _wtoi(args[++i]);
        else if (!wcscmp(args[i], L"--scroll") && i + 1 < n)
            a.setting_scroll = a.profile_scroll = a.scroll = _wtoi(args[++i]);
    }
    LocalFree(args);
}
int ui_run(HINSTANCE instance, int show, const wchar_t *command) {
    wchar_t data_dir[32768] = {0};
    char e[1024] = {0};
    a.instance = instance;
    a.focus = a.hover = -1;
    a.expanded_dimension = -1;
    a.capture_w = 1280;
    a.capture_h = 820;
    parse_args(command, data_dir);
    if (a.test_preview && !*data_dir) {
        wchar_t temporary[32768];
        if (!GetTempPathW(32768, temporary))
            return 1;
        swprintf_s(data_dir, 32768, L"%sStarLibrary-preview-%lu", temporary, GetCurrentProcessId());
    }
    bool arg_light = a.light;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    a.dpi = GetDpiForSystem();
    if (!storage_init(&a.storage, data_dir, e))
        goto failed;
    if (a.test_preview) {
        a.storage.demo = true;
        if (*a.snapshot) {
            size_t length;
            char *json = file_read(a.snapshot, &length);
            bool ok = json && library_parse(&a.library, json, length, e);
            mem_free(json);
            if (!ok) {
                if (!*e)
                    error_set(e, sizeof(e), "无法读取预览快照。");
                goto failed;
            }
        } else {
            library_new(&a.library);
            library_demo(&a.library);
        }
    } else {
        /* The mutex name hashes the normalized data path, keeping separate libraries independent. */
        unsigned long hash = 2166136261u;
        for (const wchar_t *p = a.storage.dir; *p; p++) {
            wchar_t c = *p;
            if (c >= L'A' && c <= L'Z')
                c += 32;
            hash = (hash ^ c) * 16777619u;
        }
        wchar_t name[128];
        swprintf_s(name, 128, L"Local\\StarLibrary.%08lx", hash);
        a.mutex = CreateMutexW(NULL, TRUE, name);
        if (!a.mutex || GetLastError() == ERROR_ALREADY_EXISTS) {
            MessageBoxW(NULL, L"这个数据目录已有 Star Library 正在运行，请切换到已有窗口。", APP_NAME, MB_OK);
            goto cleanup;
        }
        if (!storage_load(&a.storage, &a.library, e))
            goto failed;
        storage_keys_load(&a.storage);
    }
    a.light = arg_light || jbool(jget(root(&a.library), "settings"), "light");
    if (!gfx_init(&a.gfx)) {
        error_set(e, sizeof(e), "无法初始化 Direct2D / DirectWrite。");
        goto failed;
    }
    if (a.test_preview && !*a.snapshot) {
        const char *profile = "{\"summary\":\"倾向轻量、本地运行的 Windows "
                              "工具，近期关注原生界面和结构化数据处理。\",\"interests\":[{\"name\":\"原生 "
                              "Windows 工具\",\"reason\":\"Pulse、Terminal 与 PowerToys "
                              "都体现了对桌面效率工具的关注。\",\"confidence\":\"medium\",\"evidence\":[1001,"
                              "1003,1005]},{\"name\":\"结构化数据与基础设施\",\"reason\":\"yyjson 与 Git "
                              "对应数据读写和版本管理。\",\"confidence\":\"low\",\"evidence\":[1002,1004]}],"
                              "\"preferences\":[{\"name\":\"小巧、原生、本地运行\",\"reason\":\"Pulse "
                              "的介绍提供了原生桌面工具的线索。\",\"evidence\":[1001]}]}";
        profile_accept(&a.library, profile, e);
        /* Fixed illustrative classifications, not a real AI analysis of the user's account. */
        J *p = jget(root(&a.library), "profile");
        const char *demo =
            "{\"narrative\":["
            "{\"text\":"
            "\"你似乎在意日常与自己之间的距离：当重复的事情变得顺手，注意力便有机会回到真正想做的事上。这些收"
            "藏让人想到，你寻找的也许不只是方便，还有一种能够亲自安排节奏的自主感。\",\"evidence\":[1001,"
            "1003,1005]},"
            "{\"text\":"
            "\"另一条线索，是愿意走近事物的结构，把零散的东西整理成可以理解和改动的秩序。秩序为尝试提供落脚处"
            "，却也可能让陌生的可能暂时留在门外。当手边的一切已经足够顺畅，你会把腾出的时间交给更深的理解，还"
            "是一个还说不出用途的新念头？\",\"evidence\":[1002,1004]}],"
            "\"classifications\":["
            "{\"id\":1001,\"sufficient\":true,\"axes\":[]},"
            "{\"id\":1002,\"sufficient\":true,\"axes\":[{\"dimension\":1,\"reason\":\"C 语言 JSON "
            "库用于为应用添加数据读写能力。\"},{\"dimension\":5,\"reason\":\"JSON "
            "读写直接涉及结构化数据处理。\"}]},"
            "{\"id\":1003,\"sufficient\":true,\"axes\":[{\"dimension\":2,\"reason\":"
            "\"多标签与分屏帮助组织多个命令行工作。\"}]},"
            "{\"id\":1004,\"sufficient\":true,\"axes\":[{\"dimension\":1,\"reason\":"
            "\"版本控制系统支撑开发构建流程。\"},{\"dimension\":5,\"reason\":\"用版本记录组织项目变更。\"}]},"
            "{\"id\":1005,\"sufficient\":true,\"axes\":[{\"dimension\":2,\"reason\":\"Windows "
            "实用工具集合提供日常操作辅助。\"}]}]}";
        yyjson_doc *fixture = yyjson_read(demo, strlen(demo), 0);
        J *values = yyjson_val_mut_copy(a.library.doc, yyjson_doc_get_root(fixture));
        yyjson_doc_free(fixture);
        jset(&a.library, p, "narrative", jclone(&a.library, jget(values, "narrative")));
        jset(&a.library, p, "classifications", jclone(&a.library, jget(values, "classifications")));
        char stamp[17];
        profile_fingerprint(&a.library, false, stamp);
        jtext(&a.library, p, "source_stamp", stamp);
        jnumber(&a.library, p, "source_version", 2);
        jtext(&a.library, p, "narrative_style", PROFILE_STYLE);
        if (!*a.ui_test_report) {
            size_t ri, rn;
            J *r;
            yyjson_mut_arr_foreach(jget(root(&a.library), "repos"), ri, rn, r) {
                repo_fingerprint(r, stamp);
                jtext(&a.library, r, "ai_summary", jstr(r, "description"));
                jtext(&a.library, r, "summary_source", stamp);
                jtext(&a.library, r, "summary_language", "zh");
                jtext(&a.library, r, "summary_status", "ready");
            }
        }
        if (a.capture_expand && a.page == PAGE_PROFILE)
            a.expanded_dimension = 1;
        if (a.page == PAGE_DISCOVER)
            preview_discoveries();
        if (a.capture_expand && (a.page == PAGE_LIBRARY || a.page == PAGE_DISCOVER))
            a.selected = a.page == PAGE_DISCOVER ? 9101 : 1001;
    }
    Palette p = colors();
    a.input_brush = CreateSolidBrush(RGB((p.raised >> 16) & 255, (p.raised >> 8) & 255, p.raised & 255));
    a.font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                         CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = window_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    wc.hIconSm = wc.hIcon;
    wc.lpszClassName = L"StarLibrary.Native";
    RegisterClassExW(&wc);
    RECT wr = {0, 0, MulDiv(a.capture_w, a.dpi, 96), MulDiv(a.capture_h, a.dpi, 96)};
    AdjustWindowRectExForDpi(&wr, WS_OVERLAPPEDWINDOW, FALSE, 0, a.dpi);
    a.window = CreateWindowExW(0, wc.lpszClassName, a.storage.demo ? L"Star Library — 预览" : APP_NAME,
                               WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
                               wr.right - wr.left, wr.bottom - wr.top, NULL, NULL, instance, NULL);
    if (!a.window) {
        error_set(e, sizeof(e), "无法创建应用窗口。");
        goto failed;
    }
    a.dpi = a.capture_dpi > 0 ? (UINT)a.capture_dpi : GetDpiForWindow(a.window);
    if (a.capture_dpi > 0) {
        SetWindowPos(a.window, NULL, 0, 0, MulDiv(a.capture_w, a.dpi, 96) + 16,
                     MulDiv(a.capture_h, a.dpi, 96) + 40, SWP_NOMOVE | SWP_NOZORDER);
    }
    BOOL dark = !a.light;
    DwmSetWindowAttribute(a.window, 20, &dark, sizeof(dark));
    int round = 2;
    DwmSetWindowAttribute(a.window, 33, &round, sizeof(round));
    set_settings_edits();

    layout();
    rebuild_visible();
    if (a.capture_expand && *a.snapshot && a.visible_count)
        a.selected = jnum(a.visible[0], "id");
    Storage avatar_storage = a.storage;
    if (*a.snapshot)
        avatar_storage.demo = false; /* Real snapshots use cached pixels, never generated demo avatars. */
    avatars_init(&avatar_storage, a.window);
    ShowWindow(a.window, show);
    if (*a.capture || *a.ui_test_report) {
        RECT client, frame;
        GetClientRect(a.window, &client);
        GetWindowRect(a.window, &frame);
        int target_width = MulDiv(a.capture_w, a.dpi, 96), target_height = MulDiv(a.capture_h, a.dpi, 96);
        SetWindowPos(a.window, NULL, 0, 0, frame.right - frame.left + target_width - client.right,
                     frame.bottom - frame.top + target_height - client.bottom,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    UpdateWindow(a.window);
    if (*a.capture)
        paint(); /* Queue cached avatars before the delayed capture, including hidden windows. */
    if (a.storage.recovered)
        notice("已恢复本地星库；旧文件和备份已保留。", true);
    SetTimer(a.window, 1, 1000, NULL);
    if (!a.test_preview)
        SetTimer(a.window, 6, 2000, NULL);
    if (*a.capture)
        SetTimer(a.window, 2, 1200, NULL);
    if (*a.ui_test_report)
        SetTimer(a.window, 3, 350, NULL);
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN) {
            bool ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0;
            HIMC ime = ImmGetContext(GetFocus());
            bool composing = ime && ImmGetCompositionStringW(ime, GCS_COMPSTR, NULL, 0) > 0;
            if (ime)
                ImmReleaseContext(GetFocus(), ime);
            if (composing) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
                continue;
            }
            if (msg.wParam == VK_TAB) {
                keyboard_tab(shift);
                continue;
            }
            if (ctrl && (msg.wParam == 'F' || msg.wParam == 'K')) {
                if (a.page == PAGE_LIBRARY || a.page == PAGE_DISCOVER)
                    SetFocus(a.search);
                continue;
            }
            if (ctrl && msg.wParam == 'S') {
                if (a.page == PAGE_SETTINGS && a.settings_dirty)
                    save_settings();
                continue;
            }
            if (ctrl && msg.wParam >= '1' && msg.wParam <= '4') {
                set_page((int)msg.wParam - '1');
                continue;
            }
            if (msg.wParam == VK_F5) {
                start_job(a.page == PAGE_DISCOVER ? JOB_DISCOVER : JOB_SYNC, 0, false);
                continue;
            }
            if (msg.wParam == VK_ESCAPE)
                SendMessageW(a.tooltip, TTM_POP, 0, 0);
            if (msg.wParam == VK_ESCAPE && GetFocus() == a.window) {
                if (a.selected && flush_settings()) {
                    a.selected = 0;
                    rebuild_visible();
                    layout();
                    redraw();
                }
                continue;
            }
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    goto cleanup;
failed: {
    wchar_t *w = wide(e);
    MessageBoxW(NULL, w ? w : L"启动失败", APP_NAME, MB_OK | MB_ICONERROR);
    mem_free(w);
}
cleanup:
    clear_filters();
    mem_free(a.visible);
    gfx_free(&a.gfx);
    library_free(&a.library);
    secure_free(a.storage.github_key);
    secure_free(a.storage.ai_key);
    if (a.font)
        DeleteObject(a.font);
    if (a.input_brush)
        DeleteObject(a.input_brush);
    if (a.mutex) {
        ReleaseMutex(a.mutex);
        CloseHandle(a.mutex);
    }
    CoUninitialize();
    return *e ? 1 : a.ui_failures ? 2 : 0;
}
static void ui_check(Buffer *report, const char *label, bool ok) {
    buf_add(report, ok ? "PASS " : "FAIL ");
    buf_add(report, label);
    buf_add(report, "\n");
    if (!ok)
        a.ui_failures++;
}
static int target_index(int action, uint64_t id) {
    for (int i = 0; i < a.target_count; i++)
        if (a.targets[i].action == action && a.targets[i].id == id)
            return i;
    return -1;
}
static void run_ui_tests(void) {
    Buffer report = {0};
    set_page(PAGE_LIBRARY);
    paint();
    ui_check(&report, "native window renders", IsWindow(a.window) && a.gfx.target != NULL);
    wchar_t render_path[32768];
    swprintf_s(render_path, 32768, L"%s.bmp", a.ui_test_report);
    capture_window(render_path);
    size_t render_size = 0;
    char *render_bytes = file_read(render_path, &render_size);
    unsigned expected_sidebar = colors().sidebar;
    size_t sample = 54 + ((size_t)8 * (size_t)(a.width * a.dpi / 96) + 8) * 4;
    bool pixels_valid = render_bytes && render_size > sample + 3 &&
                        (unsigned char)render_bytes[sample] == (expected_sidebar & 255) &&
                        (unsigned char)render_bytes[sample + 1] == ((expected_sidebar >> 8) & 255) &&
                        (unsigned char)render_bytes[sample + 2] == ((expected_sidebar >> 16) & 255);
    ui_check(&report, "native capture contains rendered pixels without compositor contents", pixels_valid);
    mem_free(render_bytes);
    ui_check(&report, "command centering never leaks into content text",
             COM_CALL(a.gfx.formats[1], 11, int(STDMETHODCALLTYPE *)(void *))(a.gfx.formats[1]) == 0);
    ui_check(&report, "narrow sidebar", a.side == 144);
    ui_check(&report, "responsive three-column grid", card_columns() == 3 && card_width() >= 300);
    ui_check(&report, "five isolated demo cards", a.visible_count == 5 && a.storage.demo);
    ui_check(&report, "batch AI action", target_index(ACT_SUMMARY, 0) >= 0);
    Job busy_profile = {0};
    busy_profile.kind = JOB_PROFILE;
    a.job = &busy_profile;
    ui_check(&report, "profile activity belongs to AI action instead of Sync",
             !strcmp(library_ai_label(), "AI 整理中…") && !strcmp(library_sync_label(), "同步"));
    busy_profile.kind = JOB_SYNC;
    ui_check(&report, "Sync activity does not mark AI as running",
             !strcmp(library_ai_label(), "AI 整理") && !strcmp(library_sync_label(), "同步中…"));
    a.job = NULL;
    int edits = 0, checks = 0;
    for (HWND h = GetWindow(a.window, GW_CHILD); h; h = GetWindow(h, GW_HWNDNEXT)) {
        wchar_t cls[40];
        GetClassNameW(h, cls, 40);
        if (!wcscmp(cls, L"Edit"))
            edits++;
        if (!wcscmp(cls, L"Button"))
            checks++;
    }
    ui_check(&report, "only five inputs and no authorization checkbox", edits == 5 && checks == 0);
    J *r = find_repo(&a.library, 1001);
    ui_check(&report, "unprocessed card uses original About",
             !strcmp(repo_caption(r), jstr(r, "description")));
    char stamp[17];
    repo_fingerprint(r, stamp);
    jtext(&a.library, r, "ai_summary", "中文项目概括");
    jtext(&a.library, r, "summary_status", "ready");
    jtext(&a.library, r, "summary_language", "zh");
    jtext(&a.library, r, "summary_source", stamp);
    ui_check(&report, "Chinese summary takes precedence", !strcmp(repo_caption(r), "中文项目概括"));
    jtext(&a.library, r, "summary_status", "insufficient");
    ui_check(&report, "insufficient summary falls back to About",
             !strcmp(repo_caption(r), jstr(r, "description")));
    SetWindowTextW(a.search, L"Pulse");
    a.hovered_card = 0;
    a.hover = a.focus = -1;
    paint();
    ui_check(&report, "native search finds card", a.visible_count == 1);
    int body = target_index(ACT_OPEN, 1001), star = target_index(ACT_STAR, 1001),
        summary = target_index(ACT_SUMMARY, 1001);
    ui_check(&report, "card has direct GitHub action and independent commands",
             body >= 0 && star >= 0 && summary >= 0);
    ui_check(&report, "hidden commands cannot be clicked",
             star >= 0 && a.targets[star].mouse_hidden &&
                 hit(a.targets[star].r.left + 20, a.targets[star].r.top + 10) == body);
    a.hover = body;
    paint();
    ui_check(&report, "hover survives target rebuild", card_active(1001));
    body = target_index(ACT_OPEN, 1001);
    star = target_index(ACT_STAR, 1001);
    ui_check(&report, "visible body excludes command hit areas",
             body >= 0 && star >= 0 && a.targets[body].r.bottom <= a.targets[star].r.top);
    Target command = a.targets[star];
    LPARAM command_point =
        MAKELPARAM((int)((command.r.left + 20) * a.dpi / 96), (int)((command.r.top + 12) * a.dpi / 96));
    SendMessageW(a.window, WM_LBUTTONDOWN, MK_LBUTTON, command_point);
    paint(); /* Hidden QA windows do not receive compositor-driven WM_PAINT. */
    ui_check(&report, "card command press stays separate from repository open",
             a.pressed_action == ACT_STAR && a.pressed_id == 1001 && pressed(ACT_STAR, 1001));
    SendMessageW(a.window, WM_LBUTTONUP, 0, MAKELPARAM(2, 2));
    ui_check(&report, "card command canceled outside never starts a task", !a.job && !a.pressed_action);
    a.focus = target_index(ACT_SUMMARY, 1001);
    a.hover = -1;
    SetFocus(a.window);
    paint();
    ui_check(&report, "keyboard focus exposes card actions", card_active(1001));
    SetWindowTextW(a.search, L"不存在的项目");
    ui_check(&report, "empty search", a.visible_count == 0);
    SetWindowTextW(a.search, L"");
    a.focus = a.hover = -1;
    set_page(PAGE_PROFILE);
    paint();
    unsigned counts[6];
    ui_check(&report, "portrait ratios retained",
             profile_counts(jget(root(&a.library), "profile"), counts) == 5 && counts[1] == 2);
    unsigned profile_commands = 0;
    for (int i = 0; i < a.target_count; i++)
        profile_commands += a.targets[i].action == ACT_PROFILE;
    char *reflection = profile_reflection(jget(root(&a.library), "profile"));
    ui_check(&report, "portrait has one update command and one connected paragraph",
             profile_commands == 1 && reflection && !strchr(reflection, '\n') &&
                 !strstr(reflection, "Pulse"));
    mem_free(reflection);
    perform(ACT_DIMENSION, 1);
    paint();
    ui_check(&report, "portrait evidence still expands", a.expanded_dimension == 1 && a.scroll_limit > 0);
    perform(ACT_EVIDENCE, 1005);
    paint();
    ui_check(&report, "portrait evidence reveals a card",
             a.page == PAGE_LIBRARY && a.selected == 1005 && a.visible_count == 5);
    set_page(PAGE_SETTINGS);
    paint();
    ui_check(&report, "default AI concurrency is five", a.concurrency == 5);
    perform(ACT_CONCURRENCY, 10);
    ui_check(&report, "concurrency changes apply without Save",
             a.concurrency == 10 && ai_concurrency(&a.library) == 10 && !a.settings_dirty);
    bool light = a.light;
    perform(ACT_THEME, 0);
    ui_check(&report, "theme retains automatic settings",
             a.light != light && a.concurrency == 10 && !a.settings_dirty);
    SetFocus(a.aiurl);
    SetWindowTextW(a.aiurl, L"https://model.example");
    SendMessageW(a.window, WM_TIMER, 5, 0);
    ui_check(&report, "typing automatically persists without rewriting input",
             !strcmp(jstr(jget(root(&a.library), "settings"), "ai_url"), "https://model.example") &&
                 GetWindowTextLengthW(a.aiurl) == 21 && !a.settings_dirty);
    SetFocus(a.window);
    ui_check(&report, "API address completes on leaving input",
             !strcmp(jstr(jget(root(&a.library), "settings"), "ai_url"),
                     "https://model.example/v1/chat/completions"));
    SendMessageW(a.window, WM_SETCURSOR, (WPARAM)a.window, MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
    ui_check(&report, "arrow cursor over application", GetCursor() == LoadCursorW(NULL, IDC_ARROW));
    SendMessageW(a.aiurl, WM_SETCURSOR, (WPARAM)a.aiurl, MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
    ui_check(&report, "arrow cursor over native inputs", GetCursor() == LoadCursorW(NULL, IDC_ARROW));
    SettingsFrame frame = settings_frame();
    ui_check(&report, "settings column centered in content area",
             frame.x - a.side == a.width - frame.x - frame.w && frame.w <= 720);
    float previous_width = a.width;
    a.width = 1920;
    frame = settings_frame();
    ui_check(&report, "fullscreen settings keep equal side margins",
             frame.w == 720 && frame.x - a.side == a.width - frame.x - frame.w);
    a.width = previous_width;
    int theme_target = target_index(ACT_THEME, 0);
    if (theme_target >= 0) {
        Target t = a.targets[theme_target];
        LPARAM point = MAKELPARAM((int)((t.r.left + 20) * a.dpi / 96), (int)((t.r.top + 12) * a.dpi / 96));
        bool previous = a.light;
        SendMessageW(a.window, WM_LBUTTONDOWN, MK_LBUTTON, point);
        paint();
        ui_check(&report, "button down has feedback before activation",
                 a.light == previous && a.pressed_action == ACT_THEME && pressed(ACT_THEME, 0));
        SendMessageW(a.window, WM_LBUTTONUP, 0, point);
        ui_check(&report, "button activates on release", a.light != previous && !a.pressed_action);
        SendMessageW(a.window, WM_LBUTTONDOWN, MK_LBUTTON, point);
        SendMessageW(a.window, WM_LBUTTONUP, 0, MAKELPARAM(2, 2));
        ui_check(&report, "releasing outside cancels click", a.light != previous && !a.pressed_action);
    }
    paint();
    int token = -1, url = -1, connect = target_index(ACT_CONNECT, 0);
    for (int i = 0; i < a.target_count; i++) {
        if (a.targets[i].child == a.ghkey)
            token = i;
        if (a.targets[i].child == a.aiurl)
            url = i;
    }
    ui_check(&report, "settings keyboard follows visible order",
             token >= 0 && connect > token && url > connect);
    scroll_by(400);
    paint();
    ui_check(&report, "settings scroll remains usable", a.setting_scroll > 0 && a.scroll_limit > 0);
    paint();
    ui_check(&report, "settings hide updates until newer version and expose Star me",
             target_index(ACT_RELEASE, 0) < 0 && target_index(ACT_STAR_ME, 0) >= 0);
    int test_link = target_index(ACT_AI_TEST, 0), theme_link = target_index(ACT_THEME, 0);
    int data_link = target_index(ACT_DIR, 0), star_link = target_index(ACT_STAR_ME, 0);
    ui_check(&report, "settings right actions share width and alignment",
             test_link >= 0 && theme_link >= 0 && data_link >= 0 && star_link >= 0 &&
                 a.targets[test_link].r.right == a.targets[theme_link].r.right &&
                 a.targets[test_link].r.right == a.targets[data_link].r.right &&
                 a.targets[test_link].r.right == a.targets[star_link].r.right &&
                 a.targets[test_link].r.right - a.targets[test_link].r.left == 112);
    update_start();
    ui_check(&report, "isolated previews never check real releases", a.update == NULL);
    a.update = mem_alloc(sizeof(*a.update));
    a.update->cancel = CreateEventW(NULL, TRUE, FALSE, NULL);
    a.update->ok = true;
    strcpy_s(a.update->latest, 32, "v99.0.0");
    SendMessageW(a.window, WM_UPDATE, 0, (LPARAM)a.update);
    ui_check(&report, "new version result reaches settings and releases worker state",
             !a.update && version_newer(a.latest, APP_VERSION));
    paint();
    ui_check(&report, "newer version alone exposes update action", target_index(ACT_RELEASE, 0) >= 0);
    strcpy_s(a.latest, sizeof(a.latest), APP_VERSION);
    paint();
    ui_check(&report, "current version has no update action", target_index(ACT_RELEASE, 0) < 0);
    a.settings_dirty = false;
    set_page(PAGE_DISCOVER);
    preview_discoveries();
    rebuild_visible();
    paint();
    ui_check(&report, "discovery uses six identical card layouts",
             a.visible_count == 6 && card_columns() == 3);
    ui_check(&report, "discovery has no sorting or saved filter",
             target_index(ACT_SORT, 0) < 0 && target_index(ACT_FILTER, 2) < 0);
    uint64_t id = jnum(a.visible[0], "id");
    ui_check(&report, "discovery card commands",
             target_index(ACT_OPEN, id) >= 0 && target_index(ACT_STAR, id) >= 0 &&
                 target_index(ACT_SUMMARY, id) >= 0 && target_index(ACT_IGNORE, id) >= 0);
    size_t before = a.visible_count;
    perform(ACT_IGNORE, id);
    ui_check(&report, "ignore removes only chosen card", a.visible_count + 1 == before);
    Job blocked = {0};
    a.job = &blocked;
    layout();
    paint();
    ui_check(&report, "settings and Star mutations blocked while a task runs",
             target_index(ACT_STAR, jnum(a.visible[0], "id")) < 0);
    a.job = NULL;
    layout();
    a.settings_dirty = false;
    set_page(PAGE_LIBRARY);
    a.scroll = 0;
    rebuild_visible();
    Job batch = {0};
    batch.kind = JOB_SUMMARY;
    batch.cancel = CreateEventW(NULL, TRUE, FALSE, NULL);
    batch.base_revision = a.library.revision;
    library_clone(&batch.library, &a.library);
    library_touch(&batch.library);
    a.job = &batch;
    ui_check(&report, "complete summary batch commits on UI thread",
             SendMessageW(a.window, WM_BATCH, 0, (LPARAM)&batch) &&
                 a.library.revision == batch.library.revision);
    uint64_t revision = a.library.revision;
    SetEvent(batch.cancel);
    ui_check(&report, "canceled late batch never commits",
             !SendMessageW(a.window, WM_BATCH, 0, (LPARAM)&batch) && a.library.revision == revision);
    ResetEvent(batch.cancel);
    Storage previous_storage = a.storage, isolated = {0};
    char save_error[1024] = {0};
    wchar_t test_dir[512], locked_path[32768];
    swprintf_s(test_dir, 512, L"artifacts\\ui-save-%lu", GetCurrentProcessId());
    bool prepared =
        storage_init(&isolated, test_dir, save_error) && storage_save(&isolated, &a.library, save_error);
    storage_path(&isolated, L"library.json", locked_path);
    HANDLE locked =
        CreateFileW(locked_path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    a.storage = isolated;
    library_touch(&batch.library);
    ui_check(&report, "failed batch save preserves previously committed data",
             prepared && locked != INVALID_HANDLE_VALUE &&
                 !SendMessageW(a.window, WM_BATCH, 0, (LPARAM)&batch) && a.library.revision == revision);
    if (locked != INVALID_HANDLE_VALUE)
        CloseHandle(locked);
    Library restarted = {0};
    bool restart_ok = storage_load(&isolated, &restarted, save_error);
    ui_check(&report, "restart restores saved repositories and profile",
             restart_ok &&
                 yyjson_mut_arr_size(jget(root(&restarted), "repos")) ==
                     yyjson_mut_arr_size(jget(root(&a.library), "repos")) &&
                 !strcmp(jstr(jget(root(&restarted), "profile"), "summary"),
                         jstr(jget(root(&a.library), "profile"), "summary")));
    if (restart_ok) {
        library_free(&a.library);
        a.library = restarted;
        memset(&restarted, 0, sizeof(restarted));
        set_settings_edits();
        rebuild_visible();
        ui_check(&report, "restarted native cards match persisted data", a.visible_count > 0);
    }
    library_free(&restarted);
    a.storage = previous_storage;
    a.job = NULL;
    library_free(&batch.library);
    CloseHandle(batch.cancel);
    a.storage.github_key = str_copy("ui-test-token");
    a.storage.ai_key = str_copy("ui-model-key");
    perform(ACT_DISCONNECT, 0);
    ui_check(&report, "disconnect retains model key and data",
             !*a.storage.github_key && !strcmp(a.storage.ai_key, "ui-model-key") &&
                 a.library.revision == revision);
    ui_check(&report, "native tooltip exists", IsWindow(a.tooltip));
    ui_check(&report, "preview avatar pixels available", avatar_pixels(123, "", false) != NULL);
    LOGFONTW font = {0};
    GetObjectW(a.font, sizeof(font), &font);
    ui_check(&report, "native font respects DPI", font.lfHeight == -MulDiv(14, a.dpi, 96));
    a.focus = -1;
    SetFocus(a.window);
    keyboard_tab(false);
    ui_check(&report, "Tab moves target focus", a.focus >= 0);
    Job *completed = mem_alloc(sizeof(*completed));
    completed->kind = JOB_AI_TEST;
    error_set(completed->error, sizeof(completed->error), "UI race fixture");
    a.job = completed;
    PostMessageW(a.window, WM_JOB, 0, (LPARAM)completed);
    SetTimer(a.window, 4, 120, NULL);
    SendMessageW(a.window, WM_CLOSE, 0, 0);
    ui_check(&report, "completion during close confirmation", !IsWindow(a.window) && a.job == NULL);
    buf_add(&report, a.ui_failures ? "UI RESULT FAIL\n" : "UI RESULT PASS\n");
    file_write(a.ui_test_report, report.data, report.len);
    buf_drop(&report);
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, LPWSTR command, int show) {
    (void)previous;
    return ui_run(instance, show, command);
}
