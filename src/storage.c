#include "star.h"
#include <shlobj.h>
#include <wincrypt.h>
#include <string.h>
#include <stdlib.h>

void storage_path(Storage *st, const wchar_t *name, wchar_t *out) {
    wcscpy_s(out, 32768, st->dir);
    wcscat_s(out, 32768, L"\\");
    wcscat_s(out, 32768, name);
}
bool file_write(const wchar_t *path, const void *data, size_t n) {
    if (n > MAX_BODY)
        return false;
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD wrote;
    bool ok;
    if (f == INVALID_HANDLE_VALUE)
        return false;
    ok = n <= MAX_BODY && WriteFile(f, data, (DWORD)n, &wrote, NULL) && wrote == n && FlushFileBuffers(f);
    CloseHandle(f);
    return ok;
}
char *file_read(const wchar_t *path, size_t *n) {
    HANDLE f =
        CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    LARGE_INTEGER size;
    char *p = NULL;
    DWORD got;
    if (f == INVALID_HANDLE_VALUE)
        return NULL;
    if (GetFileSizeEx(f, &size) && size.QuadPart > 0 && size.QuadPart <= MAX_BODY) {
        p = mem_alloc((size_t)size.QuadPart + 1);
        if (p && ReadFile(f, p, (DWORD)size.QuadPart, &got, NULL) && got == size.QuadPart) {
            if (n)
                *n = got;
        } else {
            mem_free(p);
            p = NULL;
        }
    }
    CloseHandle(f);
    return p;
}
bool storage_init(Storage *st, const wchar_t *override_dir, char *e) {
    memset(st, 0, sizeof(*st));
    if (override_dir && *override_dir) {
        if (!GetFullPathNameW(override_dir, 32768, st->dir, NULL))
            goto bad;
    } else {
        if (FAILED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, SHGFP_TYPE_CURRENT, st->dir)))
            goto bad;
        wcscat_s(st->dir, 32768, L"\\StarLibrary");
    }
    SHCreateDirectoryExW(NULL, st->dir, NULL);
    DWORD attributes = GetFileAttributesW(st->dir);
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY))
        goto bad;
    return true;
bad:
    error_set(e, 1024, "无法创建数据目录。请选择可写位置。");
    return false;
}
bool storage_load(Storage *st, Library *lib, char *e) {
    wchar_t path[32768];
    char *json;
    size_t n;
    storage_path(st, L"library.json", path);
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
        return library_new(lib);
    json = file_read(path, &n);
    if (!json) {
        error_set(e, 1024, "无法读取收藏数据。原文件未被修改，请检查权限或备份。");
        return false;
    }
    bool ok = library_parse(lib, json, n, e);
    mem_free(json);
    return ok;
}
bool storage_save(Storage *st, Library *lib, char *e) {
    wchar_t path[32768], tmp[32768], bak[32768];
    char *json;
    size_t n;
    bool ok = false;
    if (st->demo)
        return true;
    if (!library_validate(lib, e))
        return false;
    storage_path(st, L"library.json", path);
    storage_path(st, L"library.json.tmp", tmp);
    storage_path(st, L"library.json.bak", bak);
    json = library_json(lib, &n);
    if (!json)
        goto bad;
    if (file_write(tmp, json, n)) {
        if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
            ok = MoveFileExW(tmp, path, MOVEFILE_WRITE_THROUGH) != 0;
        else
            ok = ReplaceFileW(path, tmp, bak, 0, NULL, NULL) != 0;
    }
    free(json);
    if (ok)
        return true;
bad:
    error_set(e, 1024, "保存失败，本次结果未写入磁盘。请检查磁盘或目录权限。");
    return false;
}
bool storage_keys_save(Storage *st, char *e) {
    wchar_t path[32768], tmp[32768];
    Buffer b = {0};
    DATA_BLOB input, output = {0};
    bool ok = false;
    if (st->demo)
        return true;
    buf_add(&b, st->github_key ? st->github_key : "");
    buf_addn(&b, "\0", 1);
    buf_add(&b, st->ai_key ? st->ai_key : "");
    buf_addn(&b, "\0", 1);
    input.pbData = (BYTE *)b.data;
    input.cbData = (DWORD)b.len;
    if (!b.failed && CryptProtectData(&input, L"Star Library credentials", NULL, NULL, NULL,
                                      CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        storage_path(st, L"credentials.bin", path);
        storage_path(st, L"credentials.bin.tmp", tmp);
        ok = file_write(tmp, output.pbData, output.cbData) &&
             MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        SecureZeroMemory(output.pbData, output.cbData);
        LocalFree(output.pbData);
    }
    if (b.data)
        SecureZeroMemory(b.data, b.cap);
    buf_drop(&b);
    if (!ok)
        error_set(e, 1024, "无法加密或保存凭据。请检查数据目录权限。");
    return ok;
}
void storage_keys_load(Storage *st) {
    wchar_t path[32768];
    size_t n;
    char *p;
    DATA_BLOB in, out = {0};
    storage_path(st, L"credentials.bin", path);
    p = file_read(path, &n);
    if (!p)
        return;
    in.pbData = (BYTE *)p;
    in.cbData = (DWORD)n;
    if (CryptUnprotectData(&in, NULL, NULL, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        char *a = (char *)out.pbData, *sep = memchr(a, 0, out.cbData);
        if (sep && memchr(sep + 1, 0, out.cbData - (size_t)(sep + 1 - a))) {
            st->github_key = str_copy(a);
            st->ai_key = str_copy(sep + 1);
        }
        SecureZeroMemory(out.pbData, out.cbData);
        LocalFree(out.pbData);
    }
    SecureZeroMemory(p, n);
    mem_free(p);
}
/* Keep both in-memory values and disk data on ordinary commit failures. Keys are never
   copied into library.json. Each individual file replacement remains atomic. */
bool storage_commit(Storage *st, Library *next, Library *previous, const char *github_key, const char *ai_key,
                    char *error) {
    if (!library_validate(next, error))
        return false;
    char *gh = str_copy(github_key), *ai = str_copy(ai_key);
    char *oldgh = st->github_key, *oldai = st->ai_key;
    if (!gh || !ai) {
        secure_free(gh);
        secure_free(ai);
        error_set(error, 1024, "内存不足，设置未保存。");
        return false;
    }
    /* Save credentials first: if they are locked, library data is never touched. */
    st->github_key = gh;
    st->ai_key = ai;
    if (!storage_keys_save(st, error))
        goto restore_memory;
    if (!storage_save(st, next, error)) {
        char rollback_error[1024] = {0};
        st->github_key = oldgh;
        st->ai_key = oldai;
        if (!storage_keys_save(st, rollback_error))
            error_set(error, 1024, "设置保存失败，凭据回滚也失败；请重新连接 GitHub。");
        secure_free(gh);
        secure_free(ai);
        return false;
    }
    (void)previous;
    secure_free(oldgh);
    secure_free(oldai);
    return true;
restore_memory:
    st->github_key = oldgh;
    st->ai_key = oldai;
    secure_free(gh);
    secure_free(ai);
    return false;
}
