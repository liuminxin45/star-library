#include "star.h"
#include <shlobj.h>
#include <wincrypt.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

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
    LARGE_INTEGER size = {0};
    char *p = NULL;
    DWORD got;
    if (f == INVALID_HANDLE_VALUE)
        return NULL;
    DWORD failure = ERROR_SUCCESS;
    bool sized = GetFileSizeEx(f, &size) != 0;
    if (sized && size.QuadPart > 0 && size.QuadPart <= MAX_BODY) {
        p = mem_alloc((size_t)size.QuadPart + 1);
        if (p && ReadFile(f, p, (DWORD)size.QuadPart, &got, NULL) && got == size.QuadPart) {
            if (n)
                *n = got;
        } else {
            failure = p ? GetLastError() : ERROR_NOT_ENOUGH_MEMORY;
            if (!failure)
                failure = ERROR_HANDLE_EOF;
            mem_free(p);
            p = NULL;
        }
    } else
        failure = !sized ? GetLastError() : size.QuadPart == 0 ? ERROR_HANDLE_EOF : ERROR_FILE_TOO_LARGE;
    CloseHandle(f);
    if (!p)
        SetLastError(failure);
    return p;
}
bool storage_init(Storage *st, const wchar_t *override_dir, char *e) {
    memset(st, 0, sizeof(*st));
    if (override_dir && *override_dir) {
        if (!GetFullPathNameW(override_dir, 32768, st->dir, NULL))
            goto bad;
    } else {
        /* AppData is redirected when launched by some packaged desktop hosts.
           A profile-relative directory is shared with ordinary Explorer launches. */
        if (FAILED(SHGetFolderPathW(NULL, CSIDL_PROFILE, NULL, SHGFP_TYPE_CURRENT, st->dir)))
            goto bad;
        wcscat_s(st->dir, 32768, L"\\.star-library");
        st->migrate_legacy = true;
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
typedef struct {
    Library library;
    wchar_t source[32768], dir[32768], pattern[32768], key[32768], temporary[32768], destination[32768];
    bool saw_file, invalid, account_conflict;
} Migration;
static void migration_consider(Migration *migration, const wchar_t *dir) {
    wchar_t path[32768];
    const wchar_t *files[] = {L"library.json", L"library.json.bak"};
    for (int i = 0; i < 2; i++) {
        swprintf_s(path, 32768, L"%s\\%s", dir, files[i]);
        if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
            DWORD status = GetLastError();
            if (status != ERROR_FILE_NOT_FOUND && status != ERROR_PATH_NOT_FOUND)
                migration->invalid = true;
            continue;
        }
        migration->saw_file = true;
        size_t size;
        char error[1024], *json = file_read(path, &size);
        Library candidate = {0};
        bool valid = json && library_parse(&candidate, json, size, error);
        mem_free(json);
        if (!valid) {
            migration->invalid = true;
            continue;
        }
        J *before = root(&migration->library), *next = root(&candidate);
        const char *account = jstr(before, "account"), *incoming = jstr(next, "account");
        if (*account && *incoming && _stricmp(account, incoming))
            migration->account_conflict = true;
        bool empty = !yyjson_mut_arr_size(jget(before, "repos"));
        bool nonempty = yyjson_mut_arr_size(jget(next, "repos")) > 0;
        if (!migration->library.doc || (empty && nonempty) ||
            (empty == !nonempty && candidate.revision > migration->library.revision)) {
            library_free(&migration->library);
            migration->library = candidate;
            memset(&candidate, 0, sizeof(candidate));
            wcscpy_s(migration->source, 32768, dir);
        }
        library_free(&candidate);
        break; /* A valid primary takes precedence over its older backup. */
    }
}
bool storage_migrate_from(Storage *st, const wchar_t *old_local, Library *lib, char *e) {
    Migration *migration = mem_alloc(sizeof(*migration));
    bool ok = false;
    if (!migration) {
        error_set(e, 1024, "内存不足，旧星库未被修改。");
        return false;
    }
    wchar_t *dir = migration->dir, *pattern = migration->pattern;
    wchar_t *key = migration->key, *temporary = migration->temporary, *destination = migration->destination;
    storage_path(st, L"library.json", destination);
    if (GetFileAttributesW(destination) != INVALID_FILE_ATTRIBUTES ||
        (GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND)) {
        error_set(e, 1024, "目标星库已存在或无法访问，未覆盖。");
        goto end;
    }
    swprintf_s(dir, 32768, L"%s\\StarLibrary", old_local);
    migration_consider(migration, dir);
    swprintf_s(pattern, 32768, L"%s\\Packages\\*", old_local);
    WIN32_FIND_DATAW entry;
    HANDLE search = FindFirstFileW(pattern, &entry);
    if (search != INVALID_HANDLE_VALUE) {
        do {
            if (!(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || entry.cFileName[0] == L'.')
                continue;
            swprintf_s(dir, 32768, L"%s\\Packages\\%s\\LocalCache\\Local\\StarLibrary", old_local,
                       entry.cFileName);
            migration_consider(migration, dir);
        } while (FindNextFileW(search, &entry));
        FindClose(search);
    }
    if (migration->account_conflict) {
        error_set(e, 1024, "找到多个 GitHub 账号的旧星库，未自动合并。请用 --data-dir 选择原数据目录。");
        goto end;
    }
    if (!migration->saw_file && !migration->invalid) {
        ok = library_new(lib);
        goto end;
    }
    if (!migration->library.doc || (migration->invalid &&
            !yyjson_mut_arr_size(jget(root(&migration->library), "repos")))) {
        error_set(e, 1024, "旧星库读取失败，未创建空库或覆盖原文件。请检查旧数据及备份。");
        goto end;
    }
    /* Copy encrypted credentials as opaque bytes; no decryption or model calls
       are needed for migration. The source directory is never changed. */
    swprintf_s(key, 32768, L"%s\\credentials.bin", migration->source);
    DWORD attributes = GetFileAttributesW(key), status = GetLastError();
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        storage_path(st, L"credentials.bin.migrate.tmp", temporary);
        storage_path(st, L"credentials.bin", destination);
        if (!CopyFileW(key, temporary, FALSE) ||
            !MoveFileExW(temporary, destination, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            goto failed;
    } else if (status != ERROR_FILE_NOT_FOUND && status != ERROR_PATH_NOT_FOUND)
        goto failed;
    size_t size;
    char *json = library_json(&migration->library, &size);
    storage_path(st, L"library.json.bak", destination);
    bool backed_up = json && file_write(destination, json, size);
    free(json);
    if (!backed_up || !storage_save(st, &migration->library, e))
        goto failed;
    library_free(lib);
    *lib = migration->library;
    memset(&migration->library, 0, sizeof(migration->library));
    st->recovered = true;
    if (e)
        *e = 0;
    ok = true;
    goto end;
failed:
    error_set(e, 1024, "旧星库迁移未完成，原数据和加密凭据已保留。请检查新目录权限。");
end:
    library_free(&migration->library);
    mem_free(migration);
    return ok;
}
static bool storage_load_impl(Storage *st, Library *lib, char *e, wchar_t *paths) {
    wchar_t *path = paths, *backup = paths + 32768, *temporary = paths + 65536;
    wchar_t *original = paths + 98304, *old_local = paths + 131072, name[128];
    char *json;
    size_t n;
    Library recovered = {0};
    st->recovered = false;
    storage_path(st, L"library.json", path);
    DWORD attributes = GetFileAttributesW(path), status = GetLastError();
    bool missing = attributes == INVALID_FILE_ATTRIBUTES;
    if (missing && status != ERROR_FILE_NOT_FOUND && status != ERROR_PATH_NOT_FOUND) {
        error_set(e, 1024, "无法访问星库文件，未创建空库。请检查数据目录权限。");
        return false;
    }
    storage_path(st, L"library.json.bak", backup);
    if (missing) {
        if (GetFileAttributesW(backup) == INVALID_FILE_ATTRIBUTES) {
            status = GetLastError();
            if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND) {
                if (st->migrate_legacy) {
                    if (FAILED(SHGetFolderPathW(NULL, CSIDL_PROFILE, NULL, SHGFP_TYPE_CURRENT, old_local)))
                        return false;
                    wcscat_s(old_local, 32768, L"\\AppData\\Local");
                    return storage_migrate_from(st, old_local, lib, e);
                }
                return library_new(lib);
            }
            error_set(e, 1024, "无法访问星库备份，未创建空库。");
            return false;
        }
    } else {
        json = file_read(path, &n);
        if (!json && GetLastError() != ERROR_HANDLE_EOF && GetLastError() != ERROR_FILE_TOO_LARGE) {
            error_set(e, 1024, "无法读取星库数据。原文件未被修改，请检查权限或备份。");
            return false;
        }
        bool ok = json && library_parse(lib, json, n, e);
        mem_free(json);
        if (ok)
            return true;
    }
    /* Recover only a fully validated backup. Never replace the backup with the
       unreadable original, or turn a read/permission failure into a new library. */
    json = file_read(backup, &n);
    bool valid = json && library_parse(&recovered, json, n, e);
    mem_free(json);
    if (!valid) {
        error_set(e, 1024, "星库或备份无效，原文件已保留。请从导出文件恢复，勿重新同步覆盖。");
        return false;
    }
    storage_path(st, L"library.json.restore.tmp", temporary);
    swprintf_s(name, 128, L"library.json.unreadable-%llu-%lu.bak",
               (unsigned long long)GetTickCount64(), GetCurrentProcessId());
    storage_path(st, name, original);
    bool restored = CopyFileW(backup, temporary, FALSE) &&
                    (missing ? MoveFileExW(temporary, path, MOVEFILE_WRITE_THROUGH)
                             : ReplaceFileW(path, temporary, original, 0, NULL, NULL));
    if (!restored) {
        library_free(&recovered);
        error_set(e, 1024, "备份有效，但恢复写入失败。原文件和备份保留，请检查数据目录权限。");
        return false;
    }
    library_free(lib);
    *lib = recovered;
    st->recovered = true;
    if (e)
        *e = 0;
    return true;
}
bool storage_load(Storage *st, Library *lib, char *e) {
    wchar_t *paths = mem_alloc(5u * 32768u * sizeof(wchar_t));
    if (!paths) {
        error_set(e, 1024, "内存不足，星库未被修改。");
        return false;
    }
    bool ok = storage_load_impl(st, lib, e, paths);
    mem_free(paths);
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
