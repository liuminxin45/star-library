#define COBJMACROS
#include "star.h"
#include <objbase.h>
#include <initguid.h>
#include <wincodec.h>
#include <stdio.h>
#include <string.h>
#define AVATAR_BYTES (256u * 1024u)
#define AVATAR_ENTRIES 128
typedef struct {
    uint64_t owner, tick;
    char url[1024];
    unsigned char *pixels;
    int state; /* 0 empty, 1 queued, 2 decoding, 3 complete (including failure) */
    bool allow_network;
} Avatar;
static struct {
    CRITICAL_SECTION lock, disk_lock;
    HANDLE cancel, queue, threads[2];
    HWND notify;
    Storage storage;
    Avatar entries[AVATAR_ENTRIES];
    bool initialized;
} cache;
bool avatar_url_valid(const char *url) {
    const char *prefix = "https://avatars.githubusercontent.com/";
    if (!url || strlen(url) >= 1024 || strncmp(url, prefix, strlen(prefix)))
        return false;
    for (const char *p = url; *p; p++)
        if ((unsigned char)*p < 33 || *p == '\\' || *p == '#')
            return false;
    return true;
}
unsigned char *avatar_decode(const void *data, size_t size) {
    if (!data || !size || size > AVATAR_BYTES)
        return NULL;
    IWICImagingFactory *factory = NULL;
    IWICBitmapDecoder *decoder = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    IWICBitmapScaler *scaler = NULL;
    IWICFormatConverter *converter = NULL;
    IStream *stream = NULL;
    unsigned char *pixels = NULL;
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!memory)
        return NULL;
    void *bytes = GlobalLock(memory);
    if (!bytes) {
        GlobalFree(memory);
        return NULL;
    }
    memcpy(bytes, data, size);
    GlobalUnlock(memory);
    if (FAILED(CreateStreamOnHGlobal(memory, TRUE, &stream))) {
        GlobalFree(memory);
        return NULL;
    }
    HRESULT hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                                  &IID_IWICImagingFactory, (void **)&factory);
    if (FAILED(hr))
        goto done;
    hr = IWICImagingFactory_CreateDecoderFromStream(factory, stream, NULL, WICDecodeMetadataCacheOnDemand,
                                                    &decoder);
    if (FAILED(hr) || FAILED(IWICBitmapDecoder_GetFrame(decoder, 0, &frame)))
        goto done;
    UINT w = 0, h = 0;
    if (FAILED(IWICBitmapFrameDecode_GetSize(frame, &w, &h)) || !w || !h || w > 1024 || h > 1024)
        goto done;
    if (FAILED(IWICImagingFactory_CreateBitmapScaler(factory, &scaler)) ||
        FAILED(IWICBitmapScaler_Initialize(scaler, (IWICBitmapSource *)frame, 64, 64,
                                           WICBitmapInterpolationModeFant)))
        goto done;
    if (FAILED(IWICImagingFactory_CreateFormatConverter(factory, &converter)) ||
        FAILED(IWICFormatConverter_Initialize(converter, (IWICBitmapSource *)scaler,
                                              &GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, NULL,
                                              0, WICBitmapPaletteTypeCustom)))
        goto done;
    pixels = mem_alloc(64 * 64 * 4);
    if (!pixels || FAILED(IWICFormatConverter_CopyPixels(converter, NULL, 256, 64 * 64 * 4, pixels))) {
        mem_free(pixels);
        pixels = NULL;
        goto done;
    }
    /* Round the cached image, keeping the Direct2D path small and free of layers. */
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) {
            int dx = x < 10 ? 10 - x : x > 53 ? x - 53 : 0, dy = y < 10 ? 10 - y : y > 53 ? y - 53 : 0;
            if (dx * dx + dy * dy > 100)
                memset(pixels + (y * 64 + x) * 4, 0, 4);
        }
done:
    if (converter)
        IWICFormatConverter_Release(converter);
    if (scaler)
        IWICBitmapScaler_Release(scaler);
    if (frame)
        IWICBitmapFrameDecode_Release(frame);
    if (decoder)
        IWICBitmapDecoder_Release(decoder);
    if (factory)
        IWICImagingFactory_Release(factory);
    IStream_Release(stream);
    return pixels;
}
static void avatar_path(const char *url, wchar_t path[32768]) {
    uint64_t hash = UINT64_C(14695981039346656037);
    for (const unsigned char *p = (const unsigned char *)url; *p; p++)
        hash = (hash ^ *p) * UINT64_C(1099511628211);
    swprintf_s(path, 32768, L"%s\\avatars\\%016llx.img", cache.storage.dir, (unsigned long long)hash);
}
static void trim_disk(void) {
    wchar_t pattern[32768], oldest[32768];
    swprintf_s(pattern, 32768, L"%s\\avatars\\*.img", cache.storage.dir);
    for (;;) {
        WIN32_FIND_DATAW data;
        HANDLE find = FindFirstFileW(pattern, &data);
        if (find == INVALID_HANDLE_VALUE)
            return;
        uint64_t total = 0, old_time = UINT64_MAX;
        oldest[0] = 0;
        do {
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;
            total += ((uint64_t)data.nFileSizeHigh << 32) | data.nFileSizeLow;
            uint64_t time =
                ((uint64_t)data.ftLastWriteTime.dwHighDateTime << 32) | data.ftLastWriteTime.dwLowDateTime;
            if (time < old_time) {
                old_time = time;
                swprintf_s(oldest, 32768, L"%s\\avatars\\%s", cache.storage.dir, data.cFileName);
            }
        } while (FindNextFileW(find, &data));
        FindClose(find);
        if (total <= 20u * 1024u * 1024u || !*oldest || !DeleteFileW(oldest))
            return;
    }
}
static DWORD WINAPI avatar_thread(void *unused) {
    (void)unused;
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    HANDLE waits[] = {cache.cancel, cache.queue};
    while (WaitForMultipleObjects(2, waits, FALSE, INFINITE) == WAIT_OBJECT_0 + 1) {
        Avatar *entry = NULL;
        char url[1024];
        EnterCriticalSection(&cache.lock);
        for (int i = 0; i < AVATAR_ENTRIES; i++)
            if (cache.entries[i].state == 1) {
                entry = &cache.entries[i];
                entry->state = 2;
                strcpy_s(url, sizeof(url), entry->url);
                break;
            }
        LeaveCriticalSection(&cache.lock);
        if (!entry)
            continue;
        wchar_t path[32768];
        avatar_path(url, path);
        size_t size = 0;
        char *data = file_read(path, &size);
        unsigned char *pixels = avatar_decode(data, size);
        mem_free(data);
        if (pixels) {
            HANDLE f = CreateFileW(path, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (f != INVALID_HANDLE_VALUE) {
                FILETIME now;
                GetSystemTimeAsFileTime(&now);
                SetFileTime(f, NULL, NULL, &now);
                CloseHandle(f);
            }
        }
        if (!pixels && entry->allow_network && WaitForSingleObject(cache.cancel, 0) != WAIT_OBJECT_0) {
            Request req = {L"GET", url, NULL, NULL, false, AVATAR_BYTES};
            Response response = {0};
            if (network_transport(&req, &response, cache.cancel)) {
                pixels = avatar_decode(response.body, response.size);
                if (pixels) {
                    EnterCriticalSection(&cache.disk_lock);
                    file_write(path, response.body, response.size);
                    trim_disk();
                    LeaveCriticalSection(&cache.disk_lock);
                }
            }
            response_free(&response);
        }
        EnterCriticalSection(&cache.lock);
        entry->pixels = pixels;
        entry->state = 3;
        LeaveCriticalSection(&cache.lock);
        if (WaitForSingleObject(cache.cancel, 0) != WAIT_OBJECT_0)
            PostMessageW(cache.notify, WM_AVATAR, 0, 0);
    }
    CoUninitialize();
    return 0;
}
void avatars_init(Storage *storage, HWND notify) {
    memset(&cache, 0, sizeof(cache));
    InitializeCriticalSection(&cache.lock);
    InitializeCriticalSection(&cache.disk_lock);
    cache.initialized = true;
    cache.storage = *storage;
    cache.notify = notify;
    if (storage->demo)
        return;
    wchar_t path[32768];
    swprintf_s(path, 32768, L"%s\\avatars", storage->dir);
    CreateDirectoryW(path, NULL);
    cache.cancel = CreateEventW(NULL, TRUE, FALSE, NULL);
    cache.queue = CreateSemaphoreW(NULL, 0, AVATAR_ENTRIES, NULL);
    for (int i = 0; i < 2; i++)
        cache.threads[i] = CreateThread(NULL, 0, avatar_thread, NULL, 0, NULL);
}
const unsigned char *avatar_pixels(uint64_t owner, const char *url, bool connected) {
    if (!cache.initialized || (!cache.storage.demo && !avatar_url_valid(url)))
        return NULL;
    EnterCriticalSection(&cache.lock);
    Avatar *slot = NULL, *old = NULL;
    for (int i = 0; i < AVATAR_ENTRIES; i++) {
        Avatar *e = &cache.entries[i];
        if (e->state && e->owner == owner && !strcmp(e->url, url)) {
            e->tick = GetTickCount64();
            if (e->state == 3 && !e->pixels && connected && !e->allow_network) {
                e->allow_network = true;
                e->state = 1;
                ReleaseSemaphore(cache.queue, 1, NULL);
            }
            const unsigned char *p = e->pixels;
            LeaveCriticalSection(&cache.lock);
            return p;
        }
        if (!e->state)
            slot = e;
        else if (e->state == 3 && (!old || e->tick < old->tick))
            old = e;
    }
    if (!slot)
        slot = old;
    if (!slot) {
        LeaveCriticalSection(&cache.lock);
        return NULL;
    }
    /* Disk reads and optional downloads both belong to the background workers. */
    mem_free(slot->pixels);
    memset(slot, 0, sizeof(*slot));
    slot->owner = owner;
    slot->allow_network = connected;
    slot->tick = GetTickCount64();
    strcpy_s(slot->url, sizeof(slot->url), url);
    if (cache.storage.demo) {
        slot->pixels = mem_alloc(64 * 64 * 4);
        slot->state = 3;
        if (slot->pixels)
            for (int y = 0; y < 64; y++)
                for (int x = 0; x < 64; x++) {
                    unsigned char *p = slot->pixels + (y * 64 + x) * 4;
                    bool mark = ((x / 8 + y / 8 + (int)(owner % 5)) % 3) == 0;
                    p[0] = (unsigned char)(mark ? 90 + owner % 65 : 220);
                    p[1] = (unsigned char)(mark ? 100 + owner % 90 : 226);
                    p[2] = (unsigned char)(mark ? 75 + owner % 100 : 232);
                    p[3] = 255;
                }
    } else {
        slot->state = 1;
        ReleaseSemaphore(cache.queue, 1, NULL);
    }
    const unsigned char *pixels = slot->pixels;
    LeaveCriticalSection(&cache.lock);
    return pixels;
}
void avatars_free(void) {
    if (!cache.initialized)
        return;
    if (cache.cancel)
        SetEvent(cache.cancel);
    for (int i = 0; i < 2; i++)
        if (cache.threads[i]) {
            WaitForSingleObject(cache.threads[i], INFINITE);
            CloseHandle(cache.threads[i]);
        }
    for (int i = 0; i < AVATAR_ENTRIES; i++)
        mem_free(cache.entries[i].pixels);
    if (cache.cancel)
        CloseHandle(cache.cancel);
    if (cache.queue)
        CloseHandle(cache.queue);
    DeleteCriticalSection(&cache.lock);
    DeleteCriticalSection(&cache.disk_lock);
    memset(&cache, 0, sizeof(cache));
}
