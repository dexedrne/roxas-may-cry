#define _POSIX_C_SOURCE 200809L
#include "logo.c"
#include <assert.h>
#include <sys/stat.h>
#include <unistd.h>

static int style, hide=1, credit_hooks;
static u64 layout_ptr, menu_ptr;
static int mode, costume, next_xion, next_cycle, writes;
static char *(*hook)(char *, const char *);
const wchar_t *rmc_folder;

int look_current(void) { return costume; }
int rmc_int(const char *section, const char *key)
{
    assert(!strcmp(section, "display"));
    if (!strcmp(key,"title_credit")) return hide;
    if (!strcmp(key,"title_style")) return style;
    if (!strcmp(key, "title_logo")) return mode;
    if (!strcmp(key, "title_logo_cycle_next")) return next_cycle;
    assert(!strcmp(key, "title_logo_xion_next"));
    return next_xion;
}
int settings_store(const char *section, const char *key, int v)
{
    assert(!strcmp(section, "display"));
    if (!strcmp(key, "title_logo_cycle_next")) next_cycle = v;
    else { assert(!strcmp(key, "title_logo_xion_next")); next_xion = v; }
    ++writes;
    return 1;
}

static char *stock(char *out, const char *name)
{
    sprintf(out, "menu/us/%s", name);
    return out;
}
void rmc_hook(unsigned id, void *fn, void **original)
{
    if (id==0x11a2f0) { ++credit_hooks; return; }
    if (id==0x2b4590 || id==0x1948c0) return;
    assert(id == 0x2e3f20);
    hook = fn;
    *original = stock;
}

void rmc_fail(const char *message) { fprintf(stderr,"%s\n",message); abort(); }
unsigned char *rmc_site(unsigned id) {
    if (id==0xb1d5d0) return (void *)&layout_ptr;
    if (id==0xb1d5d8) return (void *)&menu_ptr;
    assert(id==0x19701a || id==0x197033);
    return (void *)(uintptr_t)id;
}
static u64 passthrough(u64 a,u64 b,u64 c,u64 d) { (void)b;(void)c;(void)d; return a; }
static void test_layout(void) {
    static u8 obj[256],lay[80000],offsets[32],group[20],props[48],active[2*0x1a0];
    u64 l=(u64)obj+0x50;
    layout_original=passthrough; layout_ptr=(u64)obj;
    wr32(layout_ptr+0x40,19);wr64(layout_ptr+0x48,1000);
    wr32(l+0x38,112);wr64(l+0x10,(u64)lay);wr64(l+0x30,(u64)offsets);
    wr64(l+0x28,(u64)group);wr16((u64)group+2,2);
    wr64(l+0x20,(u64)props);wr32((u64)props,4);wr32((u64)props+24,7);
    wr64(l+0x58,(u64)active);wr32(l+0x60,2);wr32((u64)lay+79260,0x47393854);
    wr32((u64)lay+12,200);wr32((u64)lay+20,400);
    title_depth=1;
    for (int i=1;i<=2;i++) {
        style=i;mode=3;
        lay[79264+136*i]=i;
        assert(draw_layout(l,0,0,0)==l && lay[292]==i);
        assert(rd64((u64)active+0x60)==1000+256u*(i==1?4:12));
        assert(rd64((u64)active+0x200)==1000+256u*(12+i));
    }
    title_depth=style=0;
}
static void test_credit(void) {
    u8 seq[64]={0},table[39*44+16*20]={0};
    u64 parts=(u64)table+39*44;
    wr64((u64)seq+8,(u64)table);
    const unsigned ids[]={28,27,20,21};
    for (unsigned i=0;i<4;i++) wr32(parts+(8+i)*20+16,ids[i]);
    for (int site=0;site<2;site++) for (int i=0;i<16;i++) {
        wr64((u64)seq+0x28,parts+i*20);
        unsigned n=i==8 ? 8 : 4;
        title_depth=0;
        assert(credit_count(batch_end[site],(u64)seq,n)==n);
        title_depth=1;
        assert(credit_count(batch_end[site],(u64)seq,n)==(i==8 || i==10 || i==11 ? 0 : n));
        assert(credit_count(0,(u64)seq,n)==n);
    }
    title_depth=0;
}
static void write_magic(const char *path, const char *magic)
{
    FILE *f = fopen(path, "wb");
    assert(f && fwrite(magic, 1, 4, f) == 4 && !fclose(f));
}
int main(void)
{
    char root[1024], path[64], out[128];
    char cwd[1024];
    assert(getcwd(cwd,sizeof cwd));
    assert(snprintf(root,sizeof root,"%s/rmc-logo-XXXXXX",getenv("TMPDIR") ? getenv("TMPDIR") : cwd)<(int)sizeof root);
    wchar_t wide[1024];
    assert(mkdtemp(root) && mbstowcs(wide, root, 1024) != (size_t)-1 && !chdir(root));
    rmc_folder = wide;
    hide=0;logo_plan();assert(!credit_hooks);
    hide=1;logo_plan();assert(credit_hooks==1);
    test_credit();test_layout();
    for (int run = 0; run < 6; ++run) {
        memset(selected, 0, sizeof selected);
        assert(choice(0, 0) == 1);
        assert(choice(0, 1) == 2 + run % 2 && choice(0, 2) == 2 + run % 2);
        assert(choice(1, 0) == 1 + run % 3 && choice(1, 2) == 1 + run % 3);
        for (int m = 2; m <= 5; ++m) assert(choice(m, 1) == m - 2);
    }
    assert(writes == 12);
    mode = 4; costume = 2;
    assert(hook(out, "title.2ld") == out && !strcmp(out, "menu/us/title.2ld"));
    const char *dirs[] = {"menu", "menu/us", "remastered", "remastered/menu", "remastered/menu/us",
                          "remastered/menu/us/rmc22.2ld"};
    for (size_t i = 0; i < sizeof dirs / sizeof *dirs; ++i) assert(!mkdir(dirs[i], 0700));
    write_magic("menu/us/rmc22.2ld", "BAR\1");
    for (int i = 0; i < 12; ++i) {
        snprintf(path, sizeof path, "remastered/menu/us/rmc22.2ld/-%d.png", i);
        assert(!strcmp(hook(out, "title.2ld"), "menu/us/title.2ld"));
        write_magic(path, "\211PNG");
    }
    assert(!strcmp(hook(out, "title.2ld"), "menu/us/rmc22.2ld"));
    assert(!strcmp(hook(out, "titlejf.2ld"), "menu/us/titlejf.2ld"));
    assert(!strcmp(hook(out, "other.2ld"), "menu/us/other.2ld"));
    assert(!mkdir("remastered/menu/us/rmcst.2ld",0700));
    write_magic("menu/us/rmcst.2ld","BAR\1");
    for (int i=0;i<19;i++) {
        snprintf(path,sizeof path,"remastered/menu/us/rmcst.2ld/-%d.png",i);
        write_magic(path,"\211PNG");
    }
    assert(!strcmp(hook(out,"title.2ld"),"menu/us/rmcst.2ld"));
    for (int i=0;i<19;i++) {
        snprintf(path,sizeof path,"remastered/menu/us/rmcst.2ld/-%d.png",i);
        assert(!unlink(path));
    }
    assert(!unlink("menu/us/rmcst.2ld") && !rmdir("remastered/menu/us/rmcst.2ld"));
    for (int i = 0; i < 12; ++i) {
        snprintf(path, sizeof path, "remastered/menu/us/rmc22.2ld/-%d.png", i);
        assert(!unlink(path));
    }
    assert(!unlink("menu/us/rmc22.2ld"));
    for (size_t i = sizeof dirs / sizeof *dirs; i > 0; --i) assert(!rmdir(dirs[i - 1]));
    assert(!chdir("/") && !rmdir(root));
    puts("Title logo rotation, pinned modes, complete alias lookup, and optional-pack fallback passed.");
}
