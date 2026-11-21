#include "tweaks_game.h"
#include "config.h"
#include "settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

extern int look_current(void);
static char *(*menu_path)(char *, const char *);
static int selected[2];

static int choice(int mode, int look)
{
    if (mode >= 2) return mode - 2;
    if (mode == 0 && !look) return 1;
    if (!selected[mode]) {
        const char *key = mode ? "title_logo_cycle_next" : "title_logo_xion_next";
        int count = mode ? 3 : 2;
        int next = rmc_int("display", key);
        selected[mode] = (mode ? 1 : 2) + next;
        if (!settings_store("display", key, (next + 1) % count))
            fprintf(stderr, "RMC: could not persist title logo rotation in rmc.ini\n");
    }
    return selected[mode];
}

static int packed_file(const char *relative, const char *magic)
{
    if (!rmc_folder) return 0;
    size_t length = wcslen(rmc_folder) + strlen(relative) + 2;
    wchar_t *path = malloc(length * sizeof *path);
    if (!path) return 0;
    wcscpy(path, rmc_folder);
    size_t i = wcslen(path);
    path[i++] = L'/';
    while (*relative) path[i++] = (unsigned char)*relative++;
    path[i] = 0;
#ifdef _WIN32
    FILE *f = _wfopen(path, L"rb");
#else
    char *local = malloc(length * MB_CUR_MAX);
    FILE *f = local && wcstombs(local, path, length * MB_CUR_MAX) != (size_t)-1 ? fopen(local, "rb") : NULL;
    free(local);
#endif
    free(path);
    if (!f) return 0;
    char prefix[4];
    int ok = fread(prefix, 1, sizeof prefix, f) == sizeof prefix && !memcmp(prefix, magic, 4);
    fclose(f);
    return ok;
}

static char *title_path(char *out, const char *name)
{
    char *result = menu_path(out, name);
    if (strcmp(name, "title.2ld") || strcmp(result, "menu/us/title.2ld")) return result;
    if (packed_file("menu/us/rmcst.2ld","BAR\1")) {
        for (int i=0;i<19;i++) {
            char hd[64];
            snprintf(hd,sizeof hd,"remastered/menu/us/rmcst.2ld/-%d.png",i);
            if (!packed_file(hd,"\211PNG")) {
                rmc_fail("Incomplete combined title artwork");
                return result;
            }
        }
        memcpy(result,"menu/us/rmcst.2ld",sizeof "menu/us/rmcst.2ld");
        return result;
    }
    int look = look_current();
    int logo = choice(rmc_int("display", "title_logo"), look);
    if (!look && !logo) return result;
    char path[] = "menu/us/rmc00.2ld";
    path[11] += look;
    path[12] += logo;
    if (!packed_file(path, "BAR\1")) return result;
    for (int slot = 0; slot < 12; ++slot) {
        char hd[64];
        snprintf(hd, sizeof hd, "remastered/%s/-%d.png", path, slot);
        if (!packed_file(hd, "\211PNG")) return result;
    }
    memcpy(result, path, sizeof path);
    return result;
}

static Fn4 title_original, layout_original, submit_original;
static unsigned title_depth;
static uintptr_t batch_end[2];
static u64 title_task(u64 a,u64 b,u64 c,u64 d) {
    ++title_depth;
    u64 result=title_original(a,b,c,d);
    --title_depth;
    return result;
}

static u64 draw_layout(u64 layout,u64 b,u64 c,u64 d) {
    u64 obj=layout-0x50;
    if (title_depth && (obj==rd64(data_at(0xb1d5d0)) || obj==rd64(data_at(0xb1d5d8))) &&
        rd32(obj+0x40)==19 && rd32(layout+0x38)==112) {
        u64 lay=rd64(layout+0x10), geometry=lay+79260;
        if (rd32(geometry)!=0x47393854) rmc_fail("Invalid title style geometry");
        int style=rmc_int("display","title_style");
        int picture=style ? style==2 : look_current()!=0;
        int mode=rmc_int("display","title_logo");
        int logo=style==1 ? 1 : style==2 && mode>=2 ? (mode==5 ? 3 : 2) :
                 choice(style==2 ? 0 : mode,picture);
        u64 seq=lay+rd32(rd64(layout+0x30)+24), source=geometry+4+136*logo;
        memcpy((void *)(seq+rd32(seq+12)+92),(void *)source,16);
        memcpy((void *)(seq+rd32(seq+20)),(void *)(source+16),120);
        unsigned group=rd32(layout+0x78);
        u64 active=rd64(layout+0x58),textures=rd64(obj+0x48);
        if (group<20 && active && textures) {
            u64 record=rd64(layout+0x28)+20*group;
            unsigned first=rd16(record), count=rd32(layout+0x60);
            if (count>rd16(record+2) || first+count>112) rmc_fail("Invalid title style property range");
            u64 props=rd64(layout+0x20)+24*first;
            for (unsigned i=0;i<count;i++) {
                unsigned slot=rd32(props+24*i);
                if (slot==4) slot=picture ? 12 : 4;
                else if (slot==7) slot=logo ? 12+logo : 7;
                else continue;
                wr64(active+i*0x1a0+0x60,textures+256*slot);
            }
        }
    }
    return layout_original(layout,b,c,d);
}

static unsigned credit_count(uintptr_t caller,u64 seq,unsigned count) {
    if (!title_depth || (caller!=batch_end[0] && caller!=batch_end[1]) ||
        !seq || (count!=4 && count!=8)) return count;
    u64 parts=rd64(seq+8)+39*44, part=rd64(seq+0x28);
    if (count==8) return part==parts+8*20 && rd32(part+16)==28 && rd32(part+36)==27 ? 0 : count;
    return ((part==parts+10*20 && rd32(part+16)==20) ||
            (part==parts+11*20 && rd32(part+16)==21)) ? 0 : count;
}
__attribute__((used,noinline)) static u64 submit_credit(u64 count,u64 b,u64 c,u64 seq) {
    return submit_original(credit_count((uintptr_t)__builtin_return_address(0),seq,count),b,c,0);
}
#ifdef _WIN32
/* f_1969a0 keeps the active sequence in nonvolatile RDI at both submit calls. */
__attribute__((naked)) static void submit_entry(void) { __asm__("mov %rdi,%r9\n jmp submit_credit"); }
#else
#define submit_entry submit_credit
#endif
void logo_plan(void)
{
    rmc_hook(0x2e3f20, title_path, (void **)&menu_path);
    rmc_hook(0x2b4590, title_task, (void **)&title_original);
    rmc_hook(0x1948c0, draw_layout, (void **)&layout_original);
    if (rmc_int("display","title_credit")) {
        batch_end[0]=data_at(0x19701a);
        batch_end[1]=data_at(0x197033);
        rmc_hook(0x11a2f0, submit_entry, (void **)&submit_original);
    }
}
