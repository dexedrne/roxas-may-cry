#include <assert.h>
#include "settings.c"

static unsigned char menu[80], widgets[4][0x220], *slots[4], room[10], actor[0x600], status[16], enemy[16],
    level[16];
static u64 active, ref, layout = 42;
static u8 keys, running;
static u32 system_id = 3, title_mode;
static int original_rows, built_rows;
static void add_widget(u64 list, int row, int x, int y, u64 resource, int id, u64 desc, u64 animation) {
    assert(list == active && row == original_rows + built_rows && row < 4);
    assert(!x && !y && resource == layout && id == (0x8000 | EXTRA_ID) && animation == desc + 8);
    assert(message(id, 0, 0, 0) == (u64)extra);
    slots[row] = widgets[row];
    ++built_rows;
    wr16(active + 4, row + 1);
}
static u64 native_build(u64 a, u64 b, u64 c, u64 d) {
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    built_rows = 0;
    wr16(active + 4, original_rows);
    for (int i = 0; i < 4; i++) {
        slots[i] = i < original_rows ? widgets[i] : NULL;
        wr64((u64)widgets[i] + 0x1f8, 100 + i);
    }
    return 7;
}
static u64 native_open(u64 id, u64 mode, u64 a, u64 b) {
    assert(id == 1 && mode == 0 && !a && !b);
    return build(0, 0, 0, 0);
}
static int point, appearance, requested = -1, mode, stores, fail_write;
static unsigned char *stock_text;
static u64 stub(u64 a, u64 b, u64 c, u64 d) {
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    return 0;
}
static u64 mock_message(u64 a, u64 b, u64 c, u64 d) {
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    return (u64)stock_text;
}
static u64 mock_tutorial(u64 a, u64 b, u64 c, u64 d) {
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    wr32((u64)status, 2);
    return 42;
}
static u8 draw_nodes[4][32];
static u64 mock_decode(u64 id, u64 b, u64 c, u64 d) {
    (void)b;
    (void)c;
    (void)d;
    if (clean_camp_frame) return id>=1 && id<=4?(u64)draw_nodes[id-1]:0;
    return id == 1 ? (u64)&ref : (u64)actor;
}
static u64 mock_enemy(u64 a, u64 b, u64 c, u64 d) {
    (void)b;
    (void)c;
    (void)d;
    assert(a == 135);
    return (u64)enemy;
}
static u64 mock_level(u64 a, u64 b, u64 c, u64 d) {
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    return (u64)level;
}
unsigned char *rmc_site(unsigned id) {
    switch (id) {
    case 0x2e4950:
        return (void *)open_menu;
    case 0x15731e:
        return NULL;
    case 0x900724:
        return (void *)&system_id;
    case 0x9006b0:
        return &running;
    case 0xb1d5ec:
        return (void *)&title_mode;
    case 0xb1d5d8:
        return (void *)&active;
    case 0xbef650:
        return (void *)&active;
    case 0xbef680:
        return (void *)&layout;
    case 0x5ba580:
        return menu;
    case 0x2f8e20:
        return (void *)add_widget;
    case 0x2f8db0:
    case 0x166ba0:
    case 0x1672c0:
    case 0x2eb740:
    case 0x2f8c90:
    case 0x2fa1d0:
    case 0x2f7fb0:
        return (void *)stub;
    case 0x717008:
        return room;
    case 0x4ad270:
        return (void *)mock_decode;
    case 0x3ebf30:
        return (void *)mock_enemy;
    case 0x3ee8c0:
        return (void *)mock_level;
    default:
        abort();
    }
}
void rmc_hook(unsigned id, void *fn, void **orig) {
    (void)id;
    (void)fn;
    (void)orig;
}
void rmc_fail(const char *msg) {
    fprintf(stderr, "%s\n", msg);
    abort();
}
static unsigned buttons;
unsigned menu_buttons(void) { return buttons; }
void menu_pad_text(char *text, size_t cap, int row) { snprintf(text, cap, "Input %d", row); }
int menu_key(int t) { return t && keys; }
void look_frame(void) {}
void cheats_frame(void) {}
void campaign_frame(void) {}
void campaign_handback(void) {}
const unsigned char *campaign_message(int id) {
    (void)id;
    return NULL;
}
int campaign_mode(void) { return mode; }
void look_save_open(void) { point=1; }
const char *look_reason(int save) { return save && !point ? "Left save point" : NULL; }
int look_current(void) { return appearance; }
const char *look_request(int target,int save) {
    if (look_reason(save)) return look_reason(save);
    if (!look_available(target)) return "Missing character pack";
    requested = appearance = target;
    return NULL;
}
int look_available(int target) { return target != 2; }
int settings_store(const char *s, const char *k, int v) {
    if (fail_write)
        return 0;
    ++stores;
    rmc_config_publish(s, k, v);
    return 1;
}
static void menu_path(void) {
    active = (u64)menu;
    wr64(active + 0x28, (u64)slots);
    orig_title = stub;
    orig_build = native_build;
    orig_open = native_open;
    buttons=0x100;
    title(0,0,0,0);title(0,0,0,0);
    assert(rmc_int("display","title_style")==2);
    buttons=0; title(0,0,0,0);buttons=0x100; title(0,0,0,0);
    assert(rmc_int("display","title_style")==1);
    buttons=0; stores=0;
    keys = 1;
    original_rows = 2;
    title(0, 0, 0, 0);
    assert(standalone && from_title && page == HOME && built_rows == 2);
    assert(rd16(active+4)==3);
    select_row(0);
    assert(page==CHEATS);
    assert(requested == -1);
    assert(open_menu(1, 0, 0, 0) == 7 && !standalone && !from_title && page == STOCK && stock_rows == 2);
    assert(select_row(3) == -1 && page == HOME);
    point = 0;
    original_rows = 3;
    assert(open_menu(1, 0, 0, 0) == 7 && page == STOCK && built_rows == 1);
    assert(select_row(0) == 0 && select_row(3) == -1 && page == CHARACTER);
    point=0;
    select_row(1);
    assert(requested == -1);
    point = 1;
    mode = 2;
    select_row(1);
    assert(requested == 1);
    requested=-1;
    mode = 0;
    select_row(2);
    assert(requested == -1);
    select_row(1);
    assert(requested == 1);
    select_row(-2);
    assert(page == STOCK && rd64((u64)widgets[2] + 0x1f8) == saved[2]);
    standalone = 1;
    point = 0;
    enter(HOME, 0);
    select_row(0);select_row(0);assert(requested==0 && appearance==0);select_row(-2);
    select_row(1);
    assert(page == CHEATS);
    select_row(2);
    assert(rmc_int("cheats","cheat_infinite_drive"));
    select_row(3);
    assert(page == MULTIPLIERS);
    select_row(1);
    assert(rmc_int("cheats", "cheat_drop_mult") == 2);
    fail_write = 1;
    select_row(1);
    assert(feedback && stores == 2);
    select_row(2);
    assert(page == CHEATS);
    fail_write = 0;
    select_row(-4);
    select_row(3);
    select_row(0);
    assert(page == DISPLAY && rd16(active + 4) == 4);
    select_row(1);
    assert(rmc_int("display", "title_logo") == 1);
    select_row(3);
    select_row(1);
    assert(page == GAMEPLAY);
    select_row(0);
    assert(!rmc_int("gameplay", "tutorial_fight"));
    select_row(-2);
    select_row(2);
    assert(page == ABOUT);
    select_row(3);
    select_row(3);
    select_row(2);
    assert(page == CONTROLLER);
    select_row(3);
    assert(page == HOME && select_row(-2) == -2);
}
static void test_names(void) {
    const unsigned char original[] = {7,    0x3f, 0xa8, 0xb1, 0,    0x3f, 0xa8, 0xb1, 0x9a,
                                      0xac, 1,    0x3f, 0xa8, 0xb1, 0x9a, 0xac, 0};
    orig_message = mock_message;
    stock_text = (unsigned char *)original;
    const unsigned char *p = (void *)message(104, 0, 0, 0);
    assert(p != original && !memcmp(p, original, sizeof original));
    appearance = 1;
    names_refresh();
    assert(!memcmp(p, original, 5) && p[5] == 0x45 && p[10] == 0x45);
    assert((void *)message(104, 0, 0, 0) == p && (void *)message(0xc91a, 0, 0, 0) == original);
    rmc_config_publish("gameplay", "names_in_text", 0);
    names_refresh();
    assert(!memcmp(p, original, sizeof original));
    const unsigned char label[] = {0x3f, 0xa8, 0xb1, 0x9a, 0xac, 0};
    p = names_display(0x851f, label);
    assert(p[0] == 0x45 && !p[4]);
    appearance = 0;
    names_refresh();
    assert(!memcmp(p, label, 6));
}
static void test_tutorial(void) {
    u32 args[] = {1, 0, 130};
    orig_tutorial = mock_tutorial;
    wr16((u64)room, 0x312);
    wr16((u64)room + 6, 0x1301);
    wr32((u64)&ref + 4, 2);
    wr64((u64)actor + 0x5c0, (u64)status);
    wr16((u64)enemy, 135);
    wr16((u64)enemy + 4, 151);
    wr16((u64)level, 11);
    assert(tutorial((u64)args, 0, 0, 0) == 42 && rd32((u64)status) == 2);
    rmc_config_publish("gameplay", "tutorial_fight", 1);
    assert(tutorial((u64)args, 0, 0, 0) == 42 && rd32((u64)status) == 17 && rd32((u64)status + 4) == 17);
    room[1]++;
    tutorial((u64)args, 0, 0, 0);
    assert(rd32((u64)status) == 2);
}
static void test_camp_draw(void) {
    u8 task[32]={0},manager[40]={0};
    const u32 groups[]={1,3,2,0};
    for (int i=0;i<4;i++) {
        wr32((u64)draw_nodes[i]+0x14,groups[i]);
        wr32((u64)draw_nodes[i]+0xc,i==3?0:i+2);
    }
    wr64((u64)task+0x18,(u64)manager);
    wr64((u64)manager+0x18,(u64)draw_nodes[0]);
    orig_draw_pass=stub;
    draw_pass((u64)task,0,0,0);
    assert(rd64((u64)manager+0x18)==(u64)draw_nodes[0] && rd32((u64)draw_nodes[1]+0xc)==3);
    clean_camp_frame=1;
    draw_pass((u64)task,0,0,0);
    clean_camp_frame=0;
    assert(rd64((u64)manager+0x18)==(u64)draw_nodes[1] && rd32((u64)draw_nodes[1]+0xc)==4);
}
int main(void) {
    char err[128];
    assert(rmc_config_load(NULL, err, sizeof err));
    menu_path();
    test_names();
    test_tutorial();
    test_camp_draw();
    puts("Native settings routes, field and save-point pickers, persistence errors, message control codes and tutorial HP "
         "passed");
}
