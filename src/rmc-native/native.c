void cheats_plan(void);
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <setjmp.h>
#include <wchar.h>
#include "rmc.h"
#include "config.h"
#include "settings.h"
#include <io.h>

const wchar_t *rmc_folder;
static wchar_t *ini_path, *temp_path;
static HANDLE ini_lock;
void campaign_plan(void), look_plan(void), logo_plan(void);

void tweaks_plan(void);
void heapflight_plan(void);
void gameplay_plan(void);
int heapflight_ready(void);
void heapflight_cleanup(void);
void heapflight_start(void);
void tweaks_frame(void);
void debug_frame(void);

unsigned char *rmc_base;
#include "registry.h"
static RUNTIME_FUNCTION tables[COUNT(sites)];
static size_t ntables;
static int initialized, planning;
static jmp_buf failed;
static char diagnostic[256];

void rmc_fail(const char *msg) {
    snprintf(diagnostic, sizeof diagnostic, "Roxas May Cry: %s", msg);
    OutputDebugStringA(diagnostic);
    if (planning)
        longjmp(failed, 1);
    MessageBoxA(NULL, diagnostic, "Roxas May Cry", MB_ICONERROR);
    abort();
}
static void resolve(void) {
    rmc_base = (unsigned char *)GetModuleHandleW(NULL);
    IMAGE_DOS_HEADER *dos = (void *)rmc_base;
    IMAGE_NT_HEADERS64 *nt = (void *)(rmc_base + dos->e_lfanew);
    IMAGE_SECTION_HEADER *section = IMAGE_FIRST_SECTION(nt);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 || nt->FileHeader.TimeDateStamp != STEAM_STAMP ||
        nt->OptionalHeader.SizeOfImage != STEAM_SIZE || section->VirtualAddress != TEXT_START ||
        section->Misc.VirtualSize != TEXT_SIZE)
        rmc_fail("Unsupported Steam executable");
    resolve_text();
}

static void *allocations[512];
static size_t nalloc;
static void *allocate_near(size_t len, DWORD protect) {
    if (!len || nalloc == COUNT(allocations))
        rmc_fail("Allocation plan capacity");
    uintptr_t first = ((uintptr_t)rmc_base + STEAM_SIZE + 65535) & ~(uintptr_t)65535;
    for (uintptr_t p = first; p - (uintptr_t)rmc_base < 0x70000000;) {
        MEMORY_BASIC_INFORMATION m;
        if (!VirtualQuery((void *)p, &m, sizeof m))
            break;
        if (m.State == MEM_FREE && m.RegionSize >= len) {
            void *a = VirtualAlloc((void *)p, len, MEM_RESERVE | MEM_COMMIT, protect);
            if (a) {
                allocations[nalloc++] = a;
                return a;
            }
        }
        p = ((uintptr_t)m.BaseAddress + m.RegionSize + 65535) & ~(uintptr_t)65535;
    }
    rmc_fail("Unable to allocate memory near game image");
    return NULL;
}
void *rmc_near(size_t len) { return allocate_near(len, PAGE_READWRITE); }
struct edit {
    unsigned char *address, *bytes;
    size_t len;
};
static struct edit edits[1024];
static size_t nedits;
void rmc_write_ptr(unsigned char *p, const void *data, size_t n) {
    uintptr_t offset = (uintptr_t)p - (uintptr_t)rmc_base;
    if (offset >= STEAM_SIZE || !n || n > STEAM_SIZE - offset || nedits == COUNT(edits))
        rmc_fail("Invalid game edit range");
    check_text(p, n);
    for (size_t i = 0; i < nedits; ++i)
        if (p < edits[i].address + edits[i].len && p + n > edits[i].address)
            rmc_fail("Overlapping game edits");
    unsigned char *copy = malloc(n);
    if (!copy)
        rmc_fail("Game edit allocation failed");
    memcpy(copy, data, n);
    edits[nedits++] = (struct edit){p, copy, n};
}
void rmc_write(unsigned id, const void *data, size_t n) { rmc_write_ptr(rmc_site(id), data, n); }
#include "branch.h"
void rmc_hook(unsigned id, void *replacement, void **original) {
    size_t i = index_for(id);
    unsigned char *target = resolved[i], *relay = rmc_near(128), patch[5];
    encode_detour(target, relay, replacement, sites[i].stolen, sites[i].fix, original, patch);
    if (original && sites[i].unwind_len) {
        if (sites[i].unwind_len > 64 || ntables == COUNT(tables))
            rmc_fail("Trampoline unwind capacity");
        memcpy(relay + 64, sites[i].unwind, sites[i].unwind_len);
        RUNTIME_FUNCTION *table = &tables[ntables];
        table->BeginAddress = 16;
        table->EndAddress = 16 + sites[i].stolen + 5;
        table->UnwindData = 64;
        if (!RtlAddFunctionTable(table, 1, (ULONG_PTR)relay))
            rmc_fail("Cannot register trampoline unwind data");
        ++ntables;
    }
    DWORD old;
    if (!VirtualProtect(relay, 128, PAGE_EXECUTE_READ, &old) ||
        !FlushInstructionCache(GetCurrentProcess(), relay, 64))
        rmc_fail("Cannot publish trampoline");
    rmc_write(id, patch, 5);
}
static struct {
    unsigned char *p;
    DWORD old;
} pages[2048];
static size_t npages;
static void restore(void) {
    while (npages) {
        --npages;
        DWORD ignored;
        if (!VirtualProtect(pages[npages].p, 4096, pages[npages].old, &ignored))
            abort();
    }
}
static void commit(void) {
    for (size_t i = 0; i < nedits; ++i) {
        struct edit e = edits[i];
        for (uintptr_t a = (uintptr_t)e.address & ~(uintptr_t)4095; a < (uintptr_t)e.address + e.len;
             a += 4096) {
            size_t j = 0;
            while (j < npages && (uintptr_t)pages[j].p != a)
                ++j;
            if (j != npages)
                continue;
            if (npages == COUNT(pages) ||
                !VirtualProtect((void *)a, 4096, PAGE_EXECUTE_READWRITE, &pages[j].old))
                rmc_fail("Cannot prepare game page for edit");
            pages[npages++].p = (void *)a;
        }
    }
    HMODULE pinned;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                            (LPCWSTR)(uintptr_t)rmc_hook, &pinned))
        rmc_fail("Cannot retain plugin callbacks");
    if (!heapflight_ready())
        rmc_fail("Heap initialization failed");
    for (size_t i = 0; i < nedits; ++i)
        memcpy(edits[i].address, edits[i].bytes, edits[i].len);
    if (!FlushInstructionCache(GetCurrentProcess(), NULL, 0))
        abort();
    restore();
}

int settings_store(const char *section, const char *key, int value) {
    DWORD wait = WaitForSingleObject(ini_lock, INFINITE);
    if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) return 0;
    FILE *source = _wfopen(ini_path,L"rb");
    FILE *out = source ? _wfopen(temp_path,L"wb") : NULL;
    int ok = out && rmc_config_edit(source,out,section,key,value) &&
             !fflush(out) && !_commit(_fileno(out));
    if (out && fclose(out)) ok = 0;
    if (source && fclose(source)) ok = 0;
    if (ok) ok = MoveFileExW(temp_path,ini_path,MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (ok) {
        rmc_config_publish(section,key,value);
        if (!strcmp(section,"cheats")) cheats_refresh();
    } else if (out) DeleteFileW(temp_path);
    ReleaseMutex(ini_lock);
    return ok;
}

__declspec(dllexport) void OnInit(const wchar_t *folder) {
    if (initialized)
        return;
    initialized = -1;
    planning = 1;
    if (!setjmp(failed)) {
        const wchar_t suffix[] = L"\\dll\\rmc.ini";
        size_t len = wcslen(folder);
        wchar_t *path = malloc((len + COUNT(suffix)) * sizeof *path);
        if (!path)
            rmc_fail("INI path allocation failed");
        memcpy(path, folder, len * sizeof *path);
        memcpy(path + len, suffix, sizeof suffix);
        FILE *f = _wfopen(path, L"rb");
        ini_path = path;
        ini_lock = CreateMutexW(NULL,FALSE,L"Local\\RoxasMayCry.Native.Ini");
        if (!ini_lock) rmc_fail("Cannot initialize settings mutex");
        wchar_t *root = malloc((len+1)*sizeof *root);
        temp_path = malloc((wcslen(path)+32)*sizeof *path);
        if (!root || !temp_path) rmc_fail("Settings path allocation failed");
        memcpy(root,folder,(len+1)*sizeof *root);
        rmc_folder = root;
        swprintf(temp_path,wcslen(path)+32,L"%ls.%lu.tmp",path,(unsigned long)GetCurrentProcessId());
        if (!f)
            rmc_fail("Cannot open dll/rmc.ini");
        char error[192] = {0};
        int ok = rmc_config_load(f, error, sizeof error);
        fclose(f);
        if (!ok)
            rmc_fail(error);
        resolve();
        heapflight_plan();
        tweaks_plan();
        gameplay_plan();
        cheats_plan();
        settings_plan();
        look_plan();
        campaign_plan();
        logo_plan();
        commit();
        initialized = 1;
    } else {
        restore();
        heapflight_cleanup();
        while (ntables)
            if (!RtlDeleteFunctionTable(&tables[--ntables]))
                abort();
        while (nalloc)
            VirtualFree(allocations[--nalloc], 0, MEM_RELEASE);
        MessageBoxA(NULL, diagnostic, "Roxas May Cry initialization failed", MB_ICONERROR);
    }
    planning = 0;
    while (nedits)
        free(edits[--nedits].bytes);
    if (initialized == 1)
        heapflight_start();
}
__declspec(dllexport) void OnFrame(void) {
    if (initialized == 1) {
        tweaks_frame();
        debug_frame();
    }
}

/* Panacea calls OnInit after LoadLibrary returns. All state has static C initializers. */
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void *reserved) {
    (void)instance;
    (void)reason;
    (void)reserved;
    return TRUE;
}
