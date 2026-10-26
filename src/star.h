#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define UNICODE
#define _UNICODE
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "yyjson.h"

#define APP_NAME L"Star Library"
#define APP_VERSION "0.0.5"
#define PROFILE_STYLE "reflective-v1"
#define MAX_BODY (32u * 1024u * 1024u)
#define MAX_REPOS 50000u
#define WM_JOB (WM_APP + 1)
#define WM_PROGRESS (WM_APP + 2)
#define WM_BATCH (WM_APP + 3)
#define WM_AVATAR (WM_APP + 4)
#define WM_UPDATE (WM_APP + 5)
#define PROJECT_URL L"https://github.com/liuminxin45/star-library"

typedef yyjson_mut_val J;
typedef struct {
    char *data;
    size_t len, cap;
    bool failed;
} Buffer;
typedef struct {
    yyjson_mut_doc *doc;
    uint64_t revision;
} Library;
typedef struct {
    wchar_t dir[32768];
    char *github_key, *ai_key;
    bool demo, recovered, migrate_legacy;
} Storage;
typedef struct {
    const wchar_t *method;
    const char *url, *token, *body;
    bool github;
    size_t max_bytes;
} Request;
typedef struct {
    DWORD status;
    char *body;
    char error[768];
    size_t size;
} Response;
typedef bool (*Transport)(const Request *, Response *, HANDLE);
typedef struct {
    Library library;
    char *github_key, *ai_key;
    HWND notify;
    HANDLE cancel;
    int kind;
    uint64_t repo_id, base_revision;
    bool desired_star, ok, discovery_summary;
    unsigned completed, batches, retry_count, insufficient;
    unsigned output_limit, output_parameter;
    uint64_t input_tokens, output_tokens;
    DWORD ai_status;
    bool usage_known, context_limited, unsupported_output;
    char error[1024];
} Job;
enum {
    JOB_SYNC = 1,
    JOB_PROFILE,
    JOB_DISCOVER,
    JOB_STAR,
    JOB_AI_TEST,
    JOB_GH_CONNECT,
    JOB_SUMMARY,
    JOB_SUPPORT
};

void *mem_alloc(size_t n);
void *mem_resize(void *p, size_t n);
void mem_free(void *p);
char *str_copy(const char *s);
wchar_t *wide(const char *s);
char *utf8(const wchar_t *s);
void secure_free(char *s);
void buf_addn(Buffer *b, const char *s, size_t n);
void buf_add(Buffer *b, const char *s);
void buf_num(Buffer *b, uint64_t n);
void buf_json_string(Buffer *b, const char *s);
void buf_drop(Buffer *b);
void error_set(char *error, size_t cap, const char *text);
void utc_now(char out[32]);
J *root(Library *lib);
J *jget(J *obj, const char *key);
const char *jstr(J *obj, const char *key);
uint64_t jnum(J *obj, const char *key);
bool jbool(J *obj, const char *key);
void jset(Library *lib, J *obj, const char *key, J *value);
void jtext(Library *lib, J *obj, const char *key, const char *value);
void jnumber(Library *lib, J *obj, const char *key, uint64_t value);
void jboolean(Library *lib, J *obj, const char *key, bool value);
J *jclone(Library *lib, J *value);
J *find_repo(Library *lib, uint64_t id);
J *find_candidate(Library *lib, uint64_t id);
J *find_saved(Library *lib, uint64_t id);
bool library_new(Library *lib);
bool library_parse(Library *lib, const char *json, size_t n, char *error);
bool library_validate(Library *lib, char *error);
bool library_clone(Library *to, Library *from);
void library_free(Library *lib);
void library_touch(Library *lib);
char *library_json(Library *lib, size_t *n);
bool repo_annotate(Library *lib, uint64_t id, const char *tags, const char *note, char *error);
bool profile_accept(Library *lib, const char *json, char *error);
bool profile_validate_value(Library *lib, J *profile, char *error);
char *profile_reflection(J *profile);
bool text_mentions_repo(const char *text, J *repo);
unsigned dimension_mask(J *dimensions);
uint64_t discovery_weight(Library *lib, J *repo);
bool version_newer(const char *candidate, const char *current);
bool update_latest(const char *token, HANDLE cancel, char latest[32]);
bool discovery_accept(Library *lib, J *candidates, const char *json, char *error);
void library_demo(Library *lib);
extern const char *dimension_names[6];
void repo_fingerprint(J *repo, char out[17]);
bool repo_has_material(J *repo);
bool summary_current(J *repo);
const char *repo_caption(J *repo);
unsigned ai_concurrency(Library *lib);
bool summary_accept(Library *lib, J *batch, const char *json, char *error);
J *summary_results(Library *lib, J *batch, const char *json);
bool summary_apply(Library *lib, J *source, J *result);
void profile_fingerprint(Library *lib, bool include_notes, char out[17]);
bool profile_current(Library *lib);
bool profile_classifications_valid(Library *lib, J *records, J *packet, char *error);
J *profile_classification_results(Library *lib, J *batch, const char *json);
J *profile_classification_cached(Library *lib, J *repo);
bool profile_classification_apply(Library *lib, J *source, J *result);
unsigned profile_counts(J *profile, unsigned counts[6]);
bool storage_commit(Storage *st, Library *next, Library *previous, const char *github_key, const char *ai_key,
                    char *error);

bool storage_init(Storage *st, const wchar_t *override_dir, char *error);
bool storage_load(Storage *st, Library *lib, char *error);
bool storage_migrate_from(Storage *st, const wchar_t *old_local, Library *lib, char *error);
bool storage_save(Storage *st, Library *lib, char *error);
bool storage_keys_save(Storage *st, char *error);
void storage_keys_load(Storage *st);
bool file_write(const wchar_t *path, const void *data, size_t n);
char *file_read(const wchar_t *path, size_t *n);
void storage_path(Storage *st, const wchar_t *name, wchar_t *out);

bool http_request(const Request *req, Response *res, HANDLE cancel);
char *ai_endpoint(const char *input, char *error);
void response_free(Response *r);
extern Transport network_transport;
bool run_job(Job *job);
DWORD WINAPI job_thread(void *p);
void job_destroy(Job *job);
int ui_run(HINSTANCE instance, int show, const wchar_t *command);
bool avatar_url_valid(const char *url);
const char *repo_avatar_url(J *repo, char fallback[1024]);
unsigned char *avatar_decode(const void *data, size_t size);
void avatars_init(Storage *storage, HWND notify);
const unsigned char *avatar_pixels(uint64_t owner, const char *url, bool connected);
void avatars_free(void);
