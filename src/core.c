#include "star.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

void *mem_alloc(size_t n) {
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n ? n : 1);
}
void *mem_resize(void *p, size_t n) {
    return p ? HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, p, n ? n : 1) : mem_alloc(n);
}
void mem_free(void *p) {
    if (p)
        HeapFree(GetProcessHeap(), 0, p);
}
char *str_copy(const char *s) {
    size_t n = s ? strlen(s) : 0;
    char *p = mem_alloc(n + 1);
    if (p && n)
        memcpy(p, s, n);
    return p;
}
void secure_free(char *s) {
    if (s) {
        SecureZeroMemory(s, strlen(s));
        mem_free(s);
    }
}
wchar_t *wide(const char *s) {
    int n;
    wchar_t *p;
    if (!s)
        s = "";
    n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, NULL, 0);
    if (!n)
        return NULL;
    p = mem_alloc((size_t)n * sizeof(wchar_t));
    if (p)
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s, -1, p, n);
    return p;
}
char *utf8(const wchar_t *s) {
    int n;
    char *p;
    if (!s)
        s = L"";
    n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s, -1, NULL, 0, NULL, NULL);
    if (!n)
        return NULL;
    p = mem_alloc(n);
    if (p)
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s, -1, p, n, NULL, NULL);
    return p;
}
void buf_addn(Buffer *b, const char *s, size_t n) {
    size_t cap;
    void *p;
    if (b->failed)
        return;
    if (n > MAX_BODY || b->len > MAX_BODY - n) {
        b->failed = true;
        return;
    }
    if (b->len + n + 1 > b->cap) {
        cap = b->cap ? b->cap : 256;
        while (cap < b->len + n + 1)
            cap *= 2;
        p = mem_resize(b->data, cap);
        if (!p) {
            b->failed = true;
            return;
        }
        b->data = p;
        b->cap = cap;
    }
    if (n)
        memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = 0;
}
void buf_add(Buffer *b, const char *s) {
    if (s)
        buf_addn(b, s, strlen(s));
}
void buf_num(Buffer *b, uint64_t n) {
    char tmp[32];
    int i = 31;
    tmp[i] = 0;
    do {
        tmp[--i] = (char)('0' + n % 10);
        n /= 10;
    } while (n);
    buf_add(b, tmp + i);
}
void buf_json_string(Buffer *b, const char *s) {
    static const char hex[] = "0123456789abcdef";
    const unsigned char *p = (const unsigned char *)(s ? s : "");
    buf_add(b, "\"");
    for (; *p; p++) {
        char x[7] = "\\u0000";
        if (*p == '"' || *p == '\\') {
            char q[2] = {'\\', (char)*p};
            buf_addn(b, q, 2);
        } else if (*p < 32) {
            x[4] = hex[*p >> 4];
            x[5] = hex[*p & 15];
            buf_addn(b, x, 6);
        } else
            buf_addn(b, (const char *)p, 1);
    }
    buf_add(b, "\"");
}
void buf_drop(Buffer *b) {
    mem_free(b->data);
    memset(b, 0, sizeof(*b));
}
void error_set(char *e, size_t cap, const char *s) {
    if (e && cap) {
        size_t n = strlen(s);
        if (n >= cap)
            n = cap - 1;
        memcpy(e, s, n);
        e[n] = 0;
    }
}
void utc_now(char out[32]) {
    SYSTEMTIME t;
    GetSystemTime(&t);
    sprintf_s(out, 32, "%04u-%02u-%02uT%02u:%02u:%02uZ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
              t.wSecond);
}
J *root(Library *l) {
    return yyjson_mut_doc_get_root(l->doc);
}
J *jget(J *o, const char *k) {
    return yyjson_mut_obj_get(o, k);
}
const char *jstr(J *o, const char *k) {
    const char *s = yyjson_mut_get_str(jget(o, k));
    return s ? s : "";
}
uint64_t jnum(J *o, const char *k) {
    return yyjson_mut_get_uint(jget(o, k));
}
bool jbool(J *o, const char *k) {
    return yyjson_mut_get_bool(jget(o, k));
}
void jset(Library *l, J *o, const char *k, J *v) {
    yyjson_mut_obj_put(o, yyjson_mut_strcpy(l->doc, k), v);
}
void jtext(Library *l, J *o, const char *k, const char *v) {
    jset(l, o, k, yyjson_mut_strcpy(l->doc, v ? v : ""));
}
void jnumber(Library *l, J *o, const char *k, uint64_t v) {
    jset(l, o, k, yyjson_mut_uint(l->doc, v));
}
void jboolean(Library *l, J *o, const char *k, bool v) {
    jset(l, o, k, yyjson_mut_bool(l->doc, v));
}
J *jclone(Library *l, J *v) {
    return yyjson_mut_val_mut_copy(l->doc, v);
}
J *find_repo(Library *l, uint64_t id) {
    size_t i, n;
    J *v;
    yyjson_mut_arr_foreach(jget(root(l), "repos"), i, n, v) if (jnum(v, "id") == id) return v;
    return NULL;
}
J *find_candidate(Library *l, uint64_t id) {
    size_t i, n;
    J *v;
    yyjson_mut_arr_foreach(jget(root(l), "discoveries"), i, n, v) if (jnum(v, "id") == id) return v;
    return NULL;
}
bool library_new(Library *l) {
    J *r, *s;
    memset(l, 0, sizeof(*l));
    l->doc = yyjson_mut_doc_new(NULL);
    if (!l->doc)
        return false;
    r = yyjson_mut_obj(l->doc);
    yyjson_mut_doc_set_root(l->doc, r);
    jnumber(l, r, "schema", 1);
    jnumber(l, r, "revision", 0);
    jtext(l, r, "account", "");
    jtext(l, r, "synced_at", "");
    jset(l, r, "repos", yyjson_mut_arr(l->doc));
    jset(l, r, "discoveries", yyjson_mut_arr(l->doc));
    jset(l, r, "feedback", yyjson_mut_arr(l->doc));
    jset(l, r, "profile", yyjson_mut_null(l->doc));
    s = yyjson_mut_obj(l->doc);
    jtext(l, s, "username", "");
    jtext(l, s, "ai_url", "");
    jtext(l, s, "ai_model", "");
    jtext(l, s, "guidance", "");
    jboolean(l, s, "light", false);
    jnumber(l, s, "ai_concurrency", 5);
    jset(l, r, "settings", s);
    return true;
}
static bool valid_text(J *o, const char *k, size_t max) {
    J *v = jget(o, k);
    return !v || (yyjson_mut_is_str(v) && yyjson_mut_get_len(v) <= max &&
                  !memchr(yyjson_mut_get_str(v), 0, yyjson_mut_get_len(v)));
}
static bool valid_string_array(J *a, size_t limit) {
    size_t i, n;
    J *v;
    if (!yyjson_mut_is_arr(a) || yyjson_mut_arr_size(a) > limit)
        return false;
    yyjson_mut_arr_foreach(a, i, n,
                           v) if (!yyjson_mut_is_str(v) || yyjson_mut_get_len(v) > 512 ||
                                  memchr(yyjson_mut_get_str(v), 0, yyjson_mut_get_len(v))) return false;
    return true;
}
static int compare_ids(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y ? 1 : 0;
}
unsigned dimension_mask(J *dimensions) {
    if (!yyjson_mut_is_arr(dimensions) || !yyjson_mut_arr_size(dimensions) ||
        yyjson_mut_arr_size(dimensions) > 3)
        return 0;
    unsigned mask = 0;
    size_t i, n;
    J *v;
    yyjson_mut_arr_foreach(dimensions, i, n, v) {
        uint64_t id = yyjson_mut_get_uint(v);
        if (!yyjson_mut_is_uint(v) || id >= 6 || (mask & (1u << id)))
            return 0;
        mask |= 1u << id;
    }
    return mask;
}
static bool read_version(const char *text, unsigned parts[3]) {
    if (*text == 'v')
        text++;
    for (unsigned k = 0; k < 3; k++) {
        if (*text < '0' || *text > '9')
            return false;
        unsigned n = 0;
        while (*text >= '0' && *text <= '9') {
            if (n > 99999)
                return false;
            n = n * 10 + (unsigned)(*text++ - '0');
        }
        parts[k] = n;
        if (k < 2 && *text++ != '.')
            return false;
    }
    return !*text;
}
bool version_newer(const char *candidate, const char *current) {
    unsigned a[3], b[3];
    if (!read_version(candidate, a) || !read_version(current, b))
        return false;
    for (unsigned k = 0; k < 3; k++)
        if (a[k] != b[k])
            return a[k] > b[k];
    return false;
}
uint64_t discovery_weight(Library *l, J *repo) {
    unsigned penalty = 0;
    J *feedback;
    size_t i, n;
    J *items = jget(root(l), "feedback");
    size_t total = yyjson_mut_arr_size(items);
    for (size_t index = total; index && index + 100 > total; index--) {
        feedback = yyjson_mut_arr_get(items, index - 1);
        if (strcmp(jstr(feedback, "state"), "ignored"))
            continue;
        unsigned shared = 0;
        J *topic, *other;
        size_t x, xn;
        yyjson_mut_arr_foreach(jget(repo, "topics"), i, n, topic) {
            yyjson_mut_arr_foreach(jget(jget(feedback, "repo"), "topics"), x, xn, other) {
                if (!strcmp(yyjson_mut_get_str(topic), yyjson_mut_get_str(other))) {
                    shared++;
                    break;
                }
            }
        }
        if (shared >= 2)
            penalty += shared;
        else if (shared)
            penalty++;
    }
    uint64_t stars = jnum(repo, "stars");
    if (stars > 100000000)
        stars = 100000000;
    return stars * 100 / (100 + penalty * 60);
}
static bool contains_word(const char *text, const char *word) {
    for (; *text; text++)
        if (!_strnicmp(text, word, strlen(word)))
            return true;
    return false;
}
bool discovery_practical(J *repo) {
    const char *name = jstr(repo, "full_name"), *slash = strchr(name, '/');
    if (slash)
        name = slash + 1;
    const char *names[] = {"awesome-",         "project-list", "project-ideas",
                           "coding-exercises", "leetcode",     "interview-questions"};
    for (unsigned k = 0; k < sizeof(names) / sizeof(names[0]); k++)
        if (contains_word(name, names[k]))
            return false;
    const char *bad[] = {"tutorial",           "tutorials",         "awesome-list",
                         "project-ideas",      "coding-challenges", "interview-questions",
                         "final-year-project", "college-project",   "programming-exercises"};
    J *topic;
    size_t i, n;
    yyjson_mut_arr_foreach(jget(repo, "topics"), i, n,
                           topic) for (unsigned k = 0; k < sizeof(bad) / sizeof(bad[0]);
                                       k++) if (!_stricmp(yyjson_mut_get_str(topic), bad[k])) return false;
    const char *phrases[] = {"list of project ideas",
                             "list of projects for",
                             "programming exercises",
                             "coding exercises",
                             "毕业设计",
                             "课程作业",
                             "编程练习",
                             "练习题库",
                             "项目点子清单"};
    for (unsigned k = 0; k < sizeof(phrases) / sizeof(phrases[0]); k++)
        if (contains_word(jstr(repo, "description"), phrases[k]))
            return false;
    return true;
}
static bool discovery_fields_valid(J *r) {
    if (jget(r, "profile_dimensions") &&
        (!dimension_mask(jget(r, "profile_dimensions")) || !valid_text(r, "search_intent", 600) ||
         !*jstr(r, "search_intent") || !valid_text(r, "query", 200) || !*jstr(r, "query")))
        return false;
    bool present = jget(r, "hook") || jget(r, "try_idea");
    if (!present)
        return !*jstr(r, "discovery_lane") || !strcmp(jstr(r, "discovery_lane"), "core") ||
               !strcmp(jstr(r, "discovery_lane"), "adjacent");
    const char *lane = jstr(r, "discovery_lane");
    const char *hook = jstr(r, "hook"), *idea = jstr(r, "try_idea");
    if (strpbrk(hook, "\r\n") || strspn(hook, " \t") == strlen(hook) ||
        strspn(idea, " \t\r\n") == strlen(idea))
        return false;
    return valid_text(r, "hook", 256) && *jstr(r, "hook") && valid_text(r, "try_idea", 2048) &&
           *jstr(r, "try_idea") && valid_text(r, "discovery_lane", 16) &&
           (!strcmp(lane, "core") || !strcmp(lane, "adjacent")) && valid_text(r, "recommendation", 4096) &&
           *jstr(r, "recommendation");
}
bool library_validate(Library *l, char *e) {
    size_t i, n;
    J *r = root(l), *v, *a;
    uint64_t id;
    uint64_t *ids = NULL;
    if (!yyjson_mut_is_obj(r) || jnum(r, "schema") != 1 || !yyjson_mut_is_uint(jget(r, "revision")) ||
        !yyjson_mut_is_obj(jget(r, "settings")) || !yyjson_mut_is_arr(jget(r, "feedback")) ||
        !valid_text(r, "account", 128))
        goto bad;
    const char *arrays[] = {"repos", "discoveries"};
    for (int which = 0; which < 2; which++) {
        a = jget(r, arrays[which]);
        if (!yyjson_mut_is_arr(a) || yyjson_mut_arr_size(a) > MAX_REPOS)
            goto bad;
        ids = mem_alloc((yyjson_mut_arr_size(a) + 1) * sizeof(uint64_t));
        if (!ids)
            goto bad;
        yyjson_mut_arr_foreach(a, i, n, v) {
            id = jnum(v, "id");
            if (!yyjson_mut_is_obj(v) || !id || !valid_text(v, "full_name", 300) || !*jstr(v, "full_name") ||
                !strchr(jstr(v, "full_name"), '/') || !valid_text(v, "description", 32000) ||
                !valid_text(v, "note", 65536) || !valid_text(v, "ai_summary", 65536) ||
                !valid_text(v, "summary_language", 16) || !valid_text(v, "summary_status", 32) ||
                !valid_text(v, "summary_source", 16) || !valid_text(v, "summary_requested", 16) ||
                !valid_text(v, "readme_excerpt", 2400) || !valid_text(v, "readme_checked", 16) ||
                (jget(v, "readme_checked") && (strlen(jstr(v, "readme_checked")) != 16 ||
                                               strspn(jstr(v, "readme_checked"), "0123456789abcdef") != 16 ||
                                               !jget(v, "readme_excerpt"))) ||
                (jget(v, "summary_requested") &&
                 (strlen(jstr(v, "summary_requested")) != 16 ||
                  strspn(jstr(v, "summary_requested"), "0123456789abcdef") != 16)) ||
                !valid_text(v, "avatar_url", 1024) ||
                (jget(v, "owner_id") && !yyjson_mut_is_uint(jget(v, "owner_id"))))
                goto bad;
            if (jget(v, "tags") && !valid_string_array(jget(v, "tags"), 64))
                goto bad;
            ids[i] = id;
            if (jget(v, "active") && !yyjson_mut_is_bool(jget(v, "active")))
                goto bad;
            if (jget(v, "topics") && !valid_string_array(jget(v, "topics"), 100))
                goto bad;
            if (!discovery_fields_valid(v))
                goto bad;
        }
        size_t count = yyjson_mut_arr_size(a);
        qsort(ids, count, sizeof(uint64_t), compare_ids);
        for (size_t k = 1; k < count; k++)
            if (ids[k] == ids[k - 1])
                goto bad;
        mem_free(ids);
        ids = NULL;
    }
    J *settings = jget(r, "settings");
    J *concurrency = jget(settings, "ai_concurrency");
    if (concurrency && (!yyjson_mut_is_uint(concurrency) ||
                        (yyjson_mut_get_uint(concurrency) != 1 && yyjson_mut_get_uint(concurrency) != 5 &&
                         yyjson_mut_get_uint(concurrency) != 10)))
        goto bad;
    const char *fields[] = {"username", "ai_url", "ai_model", "guidance"};
    for (int k = 0; k < 4; k++)
        if (!valid_text(settings, fields[k], k == 3 ? 65536 : 8192))
            goto bad;
    if (yyjson_mut_arr_size(jget(r, "feedback")) > MAX_REPOS)
        goto bad;
    J *profile = jget(r, "profile");
    if (profile && !yyjson_mut_is_null(profile) && !profile_validate_value(l, profile, e))
        goto bad;
    l->revision = jnum(r, "revision");
    return true;
bad:
    mem_free(ids);
    error_set(e, 1024, "数据格式无效：请检查 schema、仓库 ID、重复项目、字段类型或长度。");
    return false;
}
bool library_parse(Library *l, const char *json, size_t n, char *e) {
    yyjson_doc *d;
    Library temp = {0};
    yyjson_read_err err;
    d = yyjson_read_opts((char *)json, n, 0, NULL, &err);
    if (!d) {
        error_set(e, 1024, "JSON 无效或包含不合法的 UTF-8。");
        return false;
    }
    temp.doc = yyjson_doc_mut_copy(d, NULL);
    yyjson_doc_free(d);
    if (!temp.doc) {
        error_set(e, 1024, "内存不足。");
        return false;
    }
    if (!library_validate(&temp, e)) {
        library_free(&temp);
        return false;
    }
    library_free(l);
    *l = temp;
    return true;
}
char *library_json(Library *l, size_t *n) {
    char *s = yyjson_mut_write(l->doc, 0, n);
    return s;
}
bool library_clone(Library *to, Library *from) {
    memset(to, 0, sizeof(*to));
    to->doc = yyjson_mut_doc_mut_copy(from->doc, NULL);
    to->revision = from->revision;
    return to->doc != NULL;
}
void library_free(Library *l) {
    if (l->doc)
        yyjson_mut_doc_free(l->doc);
    memset(l, 0, sizeof(*l));
}
void library_touch(Library *l) {
    l->revision++;
    jnumber(l, root(l), "revision", l->revision);
}
bool repo_annotate(Library *l, uint64_t id, const char *tags, const char *note, char *e) {
    J *r = find_repo(l, id), *a;
    const char *p, *q;
    size_t n;
    char text[513];
    if (!r || strlen(tags) > 4096 || strlen(note) > 65536) {
        error_set(e, 1024, "笔记或标签太长，或项目不存在。");
        return false;
    }
    a = yyjson_mut_arr(l->doc);
    p = tags;
    while (*p) {
        q = strchr(p, ',');
        n = q ? (size_t)(q - p) : strlen(p);
        while (n && (*p == ' ' || *p == '\t')) {
            p++;
            n--;
        }
        while (n && (p[n - 1] == ' ' || p[n - 1] == '\t'))
            n--;
        if (n > 512 || yyjson_mut_arr_size(a) >= 64) {
            error_set(e, 1024, "最多 64 个标签，每个标签最多 512 字节。");
            return false;
        }
        if (n) {
            memcpy(text, p, n);
            text[n] = 0;
            yyjson_mut_arr_add_strcpy(l->doc, a, text);
        }
        if (!q)
            break;
        p = q + 1;
    }
    jset(l, r, "tags", a);
    jtext(l, r, "note", note);
    jnumber(l, r, "version", jnum(r, "version") + 1);
    library_touch(l);
    return true;
}
static bool evidence_valid(Library *l, J *a) {
    size_t i, n;
    J *v;
    if (!yyjson_mut_is_arr(a) || !yyjson_mut_arr_size(a) || yyjson_mut_arr_size(a) > 50)
        return false;
    yyjson_mut_arr_foreach(a, i, n, v) if (!yyjson_mut_is_uint(v) ||
                                           !find_repo(l, yyjson_mut_get_uint(v))) return false;
    return true;
}
static bool word_char(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}
bool text_mentions_repo(const char *text, J *repo) {
    const char *full = jstr(repo, "full_name"), *slash = strchr(full, '/');
    const char *name = slash ? slash + 1 : full;
    size_t n = strlen(name), total = strlen(text);
    if (*full && strstr(text, full))
        return true;
    if (n < 3 || n > total)
        return false;
    for (size_t i = 0; i <= total - n; i++)
        if (!_strnicmp(text + i, name, n) && (!i || !word_char((unsigned char)text[i - 1])) &&
            !word_char((unsigned char)text[i + n]))
            return true;
    return false;
}
char *profile_reflection(J *p) {
    Buffer b = {0};
    size_t i, n;
    J *v;
    yyjson_mut_arr_foreach(jget(p, "narrative"), i, n, v) buf_add(&b, jstr(v, "text"));
    if (b.failed || !b.len) {
        buf_drop(&b);
        return NULL;
    }
    return b.data;
}
bool profile_validate_value(Library *l, J *p, char *e) {
    J *a, *v;
    size_t i, n;
    if (!yyjson_mut_is_obj(p) || !valid_text(p, "summary", 20000) || !*jstr(p, "summary"))
        goto bad;
    const char *keys[] = {"interests", "preferences"};
    for (int k = 0; k < 2; k++) {
        a = jget(p, keys[k]);
        if (!yyjson_mut_is_arr(a) || yyjson_mut_arr_size(a) > 20)
            goto bad;
        yyjson_mut_arr_foreach(a, i, n, v) {
            if (!valid_text(v, "name", 256) || !*jstr(v, "name") || !valid_text(v, "reason", 4096) ||
                !*jstr(v, "reason") || !evidence_valid(l, jget(v, "evidence")))
                goto bad;
            if (k == 0 && strcmp(jstr(v, "confidence"), "low") && strcmp(jstr(v, "confidence"), "medium") &&
                strcmp(jstr(v, "confidence"), "high"))
                goto bad;
        }
    }
    if (jget(p, "classifications")) {
        if (!profile_classifications_valid(l, jget(p, "classifications"), NULL, e) ||
            !valid_text(p, "source_stamp", 16) || strlen(jstr(p, "source_stamp")) != 16 ||
            (jnum(p, "source_version") != 2 && !yyjson_mut_is_bool(jget(p, "includes_notes"))))
            goto bad;
        J *paragraphs = jget(p, "narrative");
        if (!yyjson_mut_is_arr(paragraphs) || yyjson_mut_arr_size(paragraphs) < 2 ||
            yyjson_mut_arr_size(paragraphs) > 3)
            goto bad;
        yyjson_mut_arr_foreach(paragraphs, i, n, v) {
            if (!valid_text(v, "text", 2400) || !*jstr(v, "text") || !evidence_valid(l, jget(v, "evidence")))
                goto bad;
            size_t ei, en;
            J *id;
            yyjson_mut_arr_foreach(jget(v, "evidence"), ei, en, id) {
                bool found = false;
                size_t ci, cn;
                J *record;
                yyjson_mut_arr_foreach(jget(p, "classifications"), ci, cn,
                                       record) if (jnum(record, "id") == yyjson_mut_get_uint(id) &&
                                                   jbool(record, "sufficient")) found = true;
                if (!found)
                    goto bad;
                for (size_t x = 0; x < ei; x++)
                    if (yyjson_mut_get_uint(yyjson_mut_arr_get(jget(v, "evidence"), x)) ==
                        yyjson_mut_get_uint(id))
                        goto bad;
            }
        }
    } else if (jget(p, "narrative"))
        goto bad;
    if (!valid_text(p, "narrative_style", 32))
        goto bad;
    if (!strcmp(jstr(p, "narrative_style"), PROFILE_STYLE)) {
        char *reflection = profile_reflection(p);
        bool valid = reflection && strlen(reflection) <= 2400 && !strpbrk(reflection, "0123456789/%\r\n");
        J *repo;
        yyjson_mut_arr_foreach(jget(root(l), "repos"), i, n,
                               repo) if (reflection && text_mentions_repo(reflection, repo)) valid = false;
        mem_free(reflection);
        if (!valid)
            goto bad;
    }
    return true;
bad:
    error_set(e, 1024, "画像缺少有效字段或真实仓库证据，已保留上一份画像。");
    return false;
}

const char *dimension_names[6] = {"理解原理", "动手构建", "效率偏好", "表达创造", "探索新奇", "信息秩序"};
static uint64_t hash_text(uint64_t h, const char *s) {
    for (const unsigned char *p = (const unsigned char *)s; *p; p++)
        h = (h ^ *p) * UINT64_C(1099511628211);
    return (h ^ 255) * UINT64_C(1099511628211);
}
static uint64_t hash_array(uint64_t h, J *a) {
    size_t i, n;
    J *v;
    uint64_t items = 0;
    yyjson_mut_arr_foreach(a, i, n, v) {
        const char *s = yyjson_mut_get_str(v);
        if (s)
            items += hash_text(UINT64_C(14695981039346656037), s);
    }
    return (h ^ items ^ yyjson_mut_arr_size(a)) * UINT64_C(1099511628211);
}
static uint64_t repo_hash(J *r) {
    uint64_t h = UINT64_C(14695981039346656037);
    const char *keys[] = {"full_name", "description", "language"};
    for (int k = 0; k < 3; k++)
        h = hash_text(h, jstr(r, keys[k]));
    return hash_array(h, jget(r, "topics"));
}
void repo_fingerprint(J *r, char out[17]) {
    sprintf_s(out, 17, "%016llx", (unsigned long long)repo_hash(r));
}
bool repo_has_material(J *r) {
    const char *description = jstr(r, "description");
    while (*description && (unsigned char)*description <= ' ')
        description++;
    size_t i, n;
    J *topic;
    if (*description)
        return true;
    yyjson_mut_arr_foreach(jget(r, "topics"), i, n, topic) {
        const char *s = yyjson_mut_get_str(topic);
        if (s && *s)
            return true;
    }
    return false;
}
bool summary_current(J *r) {
    char stamp[17];
    if (!repo_has_material(r) && !*jstr(r, "readme_checked"))
        return false;
    summary_fingerprint(r, stamp);
    return !strcmp(jstr(r, "summary_language"), "zh") && !strcmp(jstr(r, "summary_source"), stamp) &&
           ((!strcmp(jstr(r, "summary_status"), "ready") && *jstr(r, "ai_summary")) ||
            (!strcmp(jstr(r, "summary_status"), "insufficient") &&
             !strcmp(jstr(r, "ai_summary"), "资料不足")));
}
bool summary_needed(J *r, bool all) {
    return all || *jstr(r, "summary_requested") || !summary_current(r) ||
           (!repo_has_material(r) && !readme_current(r));
}
void readme_fingerprint(J *r, char out[17]) {
    sprintf_s(out, 17, "%016llx", (unsigned long long)hash_text(repo_hash(r), jstr(r, "pushed_at")));
}
bool readme_current(J *r) {
    char stamp[17];
    readme_fingerprint(r, stamp);
    return !strcmp(stamp, jstr(r, "readme_checked"));
}
bool summary_has_material(J *r) {
    return repo_has_material(r) || *jstr(r, "readme_excerpt");
}
void summary_fingerprint(J *r, char out[17]) {
    uint64_t hash = repo_hash(r);
    if (!repo_has_material(r) && *jstr(r, "readme_excerpt"))
        hash = hash_text(hash, jstr(r, "readme_excerpt"));
    sprintf_s(out, 17, "%016llx", (unsigned long long)hash);
}
unsigned summary_count(Library *l, bool all) {
    unsigned count = 0;
    J *r;
    size_t i, n;
    yyjson_mut_arr_foreach(jget(root(l), "repos"), i, n, r) if (jbool(r, "active") && summary_needed(r, all))
        count++;
    return count;
}
const char *repo_caption(J *r) {
    return summary_current(r) && !strcmp(jstr(r, "summary_status"), "ready") ? jstr(r, "ai_summary")
                                                                             : jstr(r, "description");
}
unsigned ai_concurrency(Library *l) {
    uint64_t n = jnum(jget(root(l), "settings"), "ai_concurrency");
    return n == 1 || n == 10 ? (unsigned)n : 5;
}
static J *summary_target(Library *l, J *source) {
    return !strcmp(jstr(source, "_summary_target"), "discovery") ? find_candidate(l, jnum(source, "id"))
                                                                 : find_repo(l, jnum(source, "id"));
}
static bool chinese_text(const char *s) {
    wchar_t *w = wide(s);
    bool has = false;
    if (!w)
        return false;
    for (wchar_t *p = w; *p; p++)
        if (*p >= 0x3400 && *p <= 0x9fff)
            has = true;
    mem_free(w);
    return has;
}
static bool summary_valid(Library *l, J *source, J *v) {
    J *target = summary_target(l, source);
    char input_stamp[17], current_stamp[17];
    summary_fingerprint(source, input_stamp);
    summary_fingerprint(target, current_stamp);
    return source && target && yyjson_mut_is_uint(jget(v, "id")) && jnum(source, "id") == jnum(v, "id") &&
           !strcmp(input_stamp, current_stamp) && valid_text(v, "status", 16) &&
           valid_text(v, "summary", 600) && chinese_text(jstr(v, "summary")) &&
           !strpbrk(jstr(v, "summary"), "\r\n") &&
           ((!strcmp(jstr(v, "status"), "ready") && summary_has_material(source)) ||
            (!strcmp(jstr(v, "status"), "insufficient") && !strcmp(jstr(v, "summary"), "资料不足")));
}
bool summary_apply(Library *l, J *source, J *v) {
    if (!summary_valid(l, source, v))
        return false;
    J *r = summary_target(l, source);
    char stamp[17];
    summary_fingerprint(r, stamp);
    jtext(l, r, "ai_summary", jstr(v, "summary"));
    jtext(l, r, "summary_language", "zh");
    jtext(l, r, "summary_status", jstr(v, "status"));
    jtext(l, r, "summary_source", stamp);
    yyjson_mut_obj_remove_str(r, "summary_requested");
    jnumber(l, r, "version", jnum(r, "version") + 1);
    library_touch(l);
    return true;
}
J *ai_response_items(Library *l, const char *json, const char *key) {
    J *items = NULL;
    yyjson_doc *d = json ? yyjson_read(json, strlen(json), 0) : NULL;
    if (d) {
        J *response = yyjson_val_mut_copy(l->doc, yyjson_doc_get_root(d));
        items = jget(response, key);
        yyjson_doc_free(d);
    } else if (json) {
        /* A truncated array may still contain complete, independently checked objects. */
        const char *p = json;
        while (*p && (unsigned char)*p <= ' ')
            p++;
        if (*p++ == '{') {
            while (*p && (unsigned char)*p <= ' ')
                p++;
            size_t key_len = strlen(key);
            if (*p == '"' && !strncmp(p + 1, key, key_len) && p[key_len + 1] == '"') {
                p += key_len + 2;
                while (*p && (unsigned char)*p <= ' ')
                    p++;
                if (*p == ':') {
                    p++;
                    while (*p && (unsigned char)*p <= ' ')
                        p++;
                    if (*p == '[') {
                        p++;
                        items = yyjson_mut_arr(l->doc);
                        for (unsigned k = 0; k < 100; k++) {
                            while (*p && (unsigned char)*p <= ' ')
                                p++;
                            if (*p != '{')
                                break;
                            d = yyjson_read_opts((char *)p, strlen(p), YYJSON_READ_STOP_WHEN_DONE, NULL,
                                                 NULL);
                            if (!d)
                                break;
                            p += yyjson_doc_get_read_size(d);
                            yyjson_mut_arr_append(items, yyjson_val_mut_copy(l->doc, yyjson_doc_get_root(d)));
                            yyjson_doc_free(d);
                            while (*p && (unsigned char)*p <= ' ')
                                p++;
                            if (*p != ',')
                                break;
                            p++;
                        }
                    }
                }
            }
        }
    }
    return items;
}
J *summary_results(Library *l, J *batch, const char *json) {
    J *items = ai_response_items(l, json, "summaries"), *valid = yyjson_mut_arr(l->doc);
    if (!items) {
        items = ai_response_items(l, json, "items");
        size_t k, count;
        J *item;
        yyjson_mut_arr_foreach(items, k, count, item) {
            J *s = jget(item, "s");
            if (yyjson_mut_is_str(s) || yyjson_mut_is_null(s)) {
                jtext(l, item, "summary", yyjson_mut_is_null(s) ? "资料不足" : jstr(item, "s"));
                jtext(l, item, "status", yyjson_mut_is_null(s) ? "insufficient" : "ready");
            }
        }
    }
    size_t i, n;
    J *source;
    yyjson_mut_arr_foreach(batch, i, n, source) {
        J *match = NULL, *v;
        size_t k, count;
        unsigned hits = 0;
        yyjson_mut_arr_foreach(items, k, count, v) {
            if (yyjson_mut_is_uint(jget(v, "id")) && jnum(v, "id") == jnum(source, "id")) {
                match = v;
                hits++;
            }
        }
        if (hits == 1 && summary_valid(l, source, match))
            yyjson_mut_arr_append(valid, jclone(l, match));
    }
    return valid;
}
bool summary_accept(Library *l, J *batch, const char *json, char *e) {
    yyjson_doc *d = yyjson_read(json, strlen(json), 0);
    J *response = d ? yyjson_val_mut_copy(l->doc, yyjson_doc_get_root(d)) : NULL;
    if (d)
        yyjson_doc_free(d);
    J *items = jget(response, "summaries"), *v;
    size_t i, n;
    size_t total = yyjson_mut_arr_size(batch);
    if (!total || total > 20 || !yyjson_mut_is_arr(items) || yyjson_mut_arr_size(items) != total)
        goto bad;
    yyjson_mut_arr_foreach(items, i, n, v) {
        uint64_t id = jnum(v, "id");
        J *r = NULL;
        for (size_t k = 0; k < total; k++) {
            J *candidate = yyjson_mut_arr_get(batch, k);
            if (jnum(candidate, "id") == id)
                r = candidate;
        }
        char input_stamp[17], current_stamp[17];
        summary_fingerprint(r, input_stamp);
        summary_fingerprint(summary_target(l, r), current_stamp);
        if (!yyjson_mut_is_uint(jget(v, "id")) || !r || !summary_target(l, r) ||
            strcmp(input_stamp, current_stamp) || !valid_text(v, "status", 16) ||
            !valid_text(v, "summary", 600) || !chinese_text(jstr(v, "summary")) ||
            strpbrk(jstr(v, "summary"), "\r\n") ||
            (strcmp(jstr(v, "status"), "ready") && strcmp(jstr(v, "status"), "insufficient")))
            goto bad;
        if (!strcmp(jstr(v, "status"), "insufficient")) {
            if (strcmp(jstr(v, "summary"), "资料不足"))
                goto bad;
        } else if (!summary_has_material(r))
            goto bad;
        for (size_t k = 0; k < i; k++)
            if (jnum(yyjson_mut_arr_get(items, k), "id") == id)
                goto bad;
    }
    yyjson_mut_arr_foreach(items, i, n, v) {
        J *source = NULL;
        for (size_t k = 0; k < total; k++)
            if (jnum(yyjson_mut_arr_get(batch, k), "id") == jnum(v, "id"))
                source = yyjson_mut_arr_get(batch, k);
        J *r = summary_target(l, source);
        char stamp[17];
        summary_fingerprint(r, stamp);
        jtext(l, r, "ai_summary", jstr(v, "summary"));
        jtext(l, r, "summary_language", "zh");
        jtext(l, r, "summary_status", jstr(v, "status"));
        jtext(l, r, "summary_source", stamp);
        yyjson_mut_obj_remove_str(r, "summary_requested");
        jnumber(l, r, "version", jnum(r, "version") + 1);
    }
    library_touch(l);
    return true;
bad:
    error_set(e, 1024, "概括结果无效，可重试对应项目。");
    return false;
}
void profile_fingerprint(Library *l, bool notes, char out[17]) {
    uint64_t sum = 0;
    unsigned count = 0;
    size_t i, n;
    J *r;
    yyjson_mut_arr_foreach(jget(root(l), "repos"), i, n, r) {
        if (!jbool(r, "active"))
            continue;
        uint64_t h = repo_hash(r) ^ jnum(r, "id");
        h = hash_array(h, jget(r, "tags"));
        sum += h;
        count++;
    }
    sprintf_s(out, 17, "%016llx", (unsigned long long)(sum ^ ((uint64_t)count << 32)));
    (void)notes;
}
bool profile_current(Library *l) {
    J *p = jget(root(l), "profile");
    char stamp[17];
    if (!*jstr(p, "source_stamp"))
        return true; /* Legacy reports are shown until a manual update. */
    if (jnum(p, "source_version") != 2)
        return true;
    profile_fingerprint(l, jbool(p, "includes_notes"), stamp);
    return !strcmp(stamp, jstr(p, "source_stamp"));
}
static void classification_fingerprint(J *repo, char out[17]) {
    sprintf_s(out, 17, "%016llx", (unsigned long long)hash_array(repo_hash(repo), jget(repo, "tags")));
}
static bool classification_valid(Library *l, J *v, J *source) {
    J *r = find_repo(l, jnum(v, "id")), *axes = jget(v, "axes");
    if (!yyjson_mut_is_uint(jget(v, "id")) || !r || !yyjson_mut_is_bool(jget(v, "sufficient")) ||
        !yyjson_mut_is_arr(axes) || yyjson_mut_arr_size(axes) > 3 ||
        (!jbool(v, "sufficient") && yyjson_mut_arr_size(axes)))
        return false;
    if (source) {
        char before[17], current[17];
        classification_fingerprint(source, before);
        classification_fingerprint(r, current);
        if (jnum(v, "id") != jnum(source, "id") || !jbool(r, "active") || strcmp(before, current) ||
            (jbool(v, "sufficient") && !repo_has_material(r) && !yyjson_mut_arr_size(jget(r, "tags"))))
            return false;
    }
    unsigned mask = 0;
    size_t i, n;
    J *axis;
    yyjson_mut_arr_foreach(axes, i, n, axis) {
        uint64_t dim = jnum(axis, "dimension");
        if (!yyjson_mut_is_uint(jget(axis, "dimension")) || dim >= 6 || (mask & (1u << dim)) ||
            !valid_text(axis, "reason", 1200) || !*jstr(axis, "reason"))
            return false;
        mask |= 1u << dim;
    }
    return true;
}
J *profile_classification_cached(Library *l, J *repo) {
    J *cached = jget(repo, "interest_classification");
    char stamp[17];
    classification_fingerprint(repo, stamp);
    return jnum(cached, "source_version") == 1 && !strcmp(stamp, jstr(cached, "source_stamp")) &&
                   classification_valid(l, cached, repo)
               ? cached
               : NULL;
}
bool profile_classification_apply(Library *l, J *source, J *v) {
    if (!classification_valid(l, v, source))
        return false;
    J *r = find_repo(l, jnum(v, "id")), *cached = jclone(l, v);
    if (!cached)
        return false;
    char stamp[17];
    classification_fingerprint(r, stamp);
    jtext(l, cached, "source_stamp", stamp);
    jnumber(l, cached, "source_version", 1);
    jset(l, r, "interest_classification", cached);
    jnumber(l, r, "version", jnum(r, "version") + 1);
    library_touch(l);
    return true;
}
J *profile_classification_results(Library *l, J *batch, const char *json) {
    J *items = ai_response_items(l, json, "classifications"), *valid = yyjson_mut_arr(l->doc), *source;
    size_t i, n;
    yyjson_mut_arr_foreach(batch, i, n, source) {
        J *match = NULL, *v;
        size_t k, count;
        unsigned hits = 0;
        yyjson_mut_arr_foreach(items, k, count, v) {
            if (yyjson_mut_is_uint(jget(v, "id")) && jnum(v, "id") == jnum(source, "id")) {
                match = v;
                hits++;
            }
        }
        if (hits == 1 && classification_valid(l, match, source))
            yyjson_mut_arr_append(valid, jclone(l, match));
    }
    return valid;
}
bool profile_classifications_valid(Library *l, J *records, J *packet, char *e) {
    size_t i, n;
    J *v;
    if (!yyjson_mut_is_arr(records) || yyjson_mut_arr_size(records) > MAX_REPOS ||
        (packet && yyjson_mut_arr_size(records) != yyjson_mut_arr_size(packet)))
        goto bad;
    yyjson_mut_arr_foreach(records, i, n, v) {
        uint64_t id = jnum(v, "id");
        if (!classification_valid(l, v, NULL))
            goto bad;
        if (packet) {
            J *found = NULL;
            size_t pi, pn;
            J *pr;
            yyjson_mut_arr_foreach(packet, pi, pn, pr) if (jnum(pr, "id") == id) found = pr;
            if (!found || !classification_valid(l, v, found))
                goto bad;
        }
        for (size_t k = 0; k < i; k++)
            if (jnum(yyjson_mut_arr_get(records, k), "id") == id)
                goto bad;
    }
    return true;
bad:
    error_set(e, 1024, "兴趣分类结果无效；已保存的分类进度保留，可重试未完成项目。");
    return false;
}
unsigned profile_counts(J *p, unsigned counts[6]) {
    memset(counts, 0, 6 * sizeof(*counts));
    unsigned total = 0;
    size_t i, n;
    J *r;
    yyjson_mut_arr_foreach(jget(p, "classifications"), i, n, r) {
        if (!jbool(r, "sufficient"))
            continue;
        total++;
        size_t ai, an;
        J *axis;
        yyjson_mut_arr_foreach(jget(r, "axes"), ai, an, axis) {
            uint64_t dim = jnum(axis, "dimension");
            if (dim < 6)
                counts[dim]++;
        }
    }
    return total;
}
bool profile_accept(Library *l, const char *json, char *e) {
    yyjson_doc *d = yyjson_read(json, strlen(json), 0);
    J *p;
    char now[32];
    if (!d) {
        error_set(e, 1024, "AI 返回的画像不是有效 JSON。");
        return false;
    }
    p = yyjson_val_mut_copy(l->doc, yyjson_doc_get_root(d));
    yyjson_doc_free(d);
    if (!profile_validate_value(l, p, e))
        return false;
    utc_now(now);
    jnumber(l, p, "based_on_revision", l->revision + 1);
    jtext(l, p, "updated_at", now);
    jset(l, root(l), "profile", p);
    library_touch(l);
    return true;
}
bool discovery_accept(Library *l, J *candidates, const char *json, char *e) {
    J *answers = ai_response_items(l, json, "recommendations"), *v, *selected[6] = {0};
    unsigned core_count = 0, adjacent_count = 0, valid_count = 0, counts[6];
    profile_counts(jget(root(l), "profile"), counts);
    size_t i, n;
    yyjson_mut_arr_foreach(answers, i, n, v) {
        if (i >= 100 || valid_count == 6)
            break;
        uint64_t id = jnum(v, "id");
        if (!yyjson_mut_is_uint(jget(v, "id")) || !id)
            continue;
        unsigned duplicates = 0;
        J *other;
        size_t k, count;
        yyjson_mut_arr_foreach(answers, k, count, other) if (yyjson_mut_is_uint(jget(other, "id")) &&
                                                             jnum(other, "id") == id) duplicates++;
        if (duplicates != 1)
            continue;
        J *candidate = NULL, *r;
        yyjson_mut_arr_foreach(candidates, k, count, r) if (jnum(r, "id") == id) {
            candidate = r;
            break;
        }
        if (!candidate || !discovery_practical(candidate) || jnum(candidate, "stars") < 100 ||
            !valid_text(v, "summary", 600) || !chinese_text(jstr(v, "summary")) ||
            strpbrk(jstr(v, "summary"), "\r\n"))
            continue;
        J *dimensions = jget(v, "profile_dimensions");
        if (!dimensions)
            dimensions = jget(candidate, "profile_dimensions");
        unsigned source = dimension_mask(jget(candidate, "profile_dimensions")),
                 chosen = dimension_mask(dimensions);
        bool valid = source && chosen && !(chosen & ~source);
        for (unsigned dim = 0; dim < 6; dim++)
            if ((chosen & (1u << dim)) && !counts[dim])
                valid = false;
        bool core = !strcmp(jstr(candidate, "discovery_lane"), "core");
        if (!valid || (core ? core_count == 4 : adjacent_count == 2) ||
            (!core && strcmp(jstr(candidate, "discovery_lane"), "adjacent")))
            continue;
        J *item = jclone(l, candidate);
        if (!item)
            break;
        jset(l, item, "profile_dimensions", jclone(l, dimensions));
        char stamp[17];
        repo_fingerprint(item, stamp);
        jtext(l, item, "ai_summary", jstr(v, "summary"));
        jtext(l, item, "summary_source", stamp);
        jtext(l, item, "summary_language", "zh");
        jtext(l, item, "summary_status", "ready");
        if (!discovery_fields_valid(item))
            continue;
        if (core)
            core_count++;
        else
            adjacent_count++;
        selected[valid_count++] = item;
    }
    if (!valid_count) {
        error_set(e, 1024, "本轮没有有效的实用项目推荐，已保留上一批。可稍后换一批。");
        return false;
    }
    J *result = yyjson_mut_arr(l->doc);
    if (!result)
        return false;
    bool used[6] = {0};
    const char *lanes[] = {"core", "core", "adjacent", "core", "core", "adjacent"};
    for (unsigned slot = 0; slot < 6; slot++)
        for (unsigned k = 0; k < valid_count; k++)
            if (!used[k] && !strcmp(jstr(selected[k], "discovery_lane"), lanes[slot])) {
                yyjson_mut_arr_append(result, selected[k]);
                used[k] = true;
                break;
            }
    jset(l, root(l), "discoveries", result);
    return true;
}
void library_demo(Library *l) {
    static const char *names[] = {"yftx888/Pulse", "ibireme/yyjson", "microsoft/terminal", "git/git",
                                  "microsoft/PowerToys"};
    static const char *desc[] = {"Windows 原生桌面监控组件：硬件、电池与网络状态。",
                                 "轻量 C 语言 JSON 读写库，适合嵌入式与原生应用。",
                                 "现代 Windows 终端，支持多标签、分屏和多种命令行。", "分布式版本控制系统。",
                                 "面向 Windows 的实用工具集合。"};
    static const char *lang[] = {"C++", "C", "C++", "C", "C#"};
    for (int i = 0; i < 5; i++) {
        J *r = yyjson_mut_obj(l->doc);
        jnumber(l, r, "id", 1001 + i);
        jtext(l, r, "full_name", names[i]);
        jtext(l, r, "description", desc[i]);
        jtext(l, r, "language", lang[i]);
        jnumber(l, r, "stars", (i + 1) * 1234);
        jboolean(l, r, "active", true);
        jnumber(l, r, "version", 0);
        jtext(l, r, "starred_at", "2026-10-23T00:00:00Z");
        jtext(l, r, "pushed_at", "2026-10-20T00:00:00Z");
        jset(l, r, "tags", yyjson_mut_arr(l->doc));
        jset(l, r, "topics", yyjson_mut_arr(l->doc));
        yyjson_mut_arr_append(jget(root(l), "repos"), r);
    }
    jtext(l, root(l), "account", "preview");
}
