#include "campaign.h"
#include "settings.h"
#include "look.h"
#include "tweaks_game.h"

unsigned menu_buttons(void);
void menu_pad_text(char *text, size_t cap, int row);
int menu_key(int title);

void menu_encode(unsigned char *dst, const char *src) {
    while (*src) {
        char c = *src++;
        if (c >= 'A' && c <= 'Z')
            *dst++ = 0x2e + c - 'A';
        else if (c >= 'a' && c <= 'z')
            *dst++ = 0x9a + c - 'a';
        else if (c >= '0' && c <= '9')
            *dst++ = 0x21 + c - '0';
        else
            switch (c) {
            case ' ':
                *dst++ = 1;
                break;
            case ':':
                *dst++ = 0x52;
                break;
            case '(':
                *dst++ = 0x5a;
                break;
            case ')':
                *dst++ = 0x5b;
                break;
            case '/':
                *dst++ = 0x4b;
                break;
            case '-':
                *dst++ = 0x2c;
                break;
            case '.':
                *dst++ = 0x4f;
                break;
            default:
                rmc_fail("Unsupported menu glyph");
            }
    }
    *dst = 0;
}
enum page { STOCK, HOME, MORE, CHARACTER, CHEATS, DISPLAY, GAMEPLAY, CONTROLLER, ABOUT, MULTIPLIERS };
enum { EXTRA_ID = 30100, HELP_ID };
static enum page page;
static int standalone, opening, from_title, stock_rows;
static u64 active_list, saved[4];
static unsigned char rows[4][70], help[128], extra[40];
static const char *feedback;
static const char *const characters[] = {"Roxas", "Xion", "Xion (hooded)"};
static const char *const logos[] = {"Auto", "Cycle", "Mod", "Roxas", "Xion", "XKB"};
struct option {
    const char *label, *section, *key;
};
static const struct option cheats[] = {{"Infinite HP", "cheats", "cheat_infinite_hp"},
                                       {"Infinite MP", "cheats", "cheat_infinite_mp"},
                                       {"Infinite Drive", "cheats", "cheat_infinite_drive"}};
static const struct option multipliers[] = {{"EXP", "cheats", "cheat_exp_mult"},
                                       {"Drop rate", "cheats", "cheat_drop_mult"}};
static const struct option gameplay[] = {{"Tutorial HP", "gameplay", "tutorial_fight"},
                                         {"Names", "gameplay", "names_in_text"},
                                         {"Save labels", "gameplay", "save_difficulty"},
                                         {"Xion modes", "gameplay", "xion_modes"}};
static Fn4 orig_message, orig_build, orig_input, orig_help, orig_open, orig_title, orig_tick, orig_tutorial;

static u64 message(u64 id, u64 b, u64 c, u64 d) {
    unsigned key = (unsigned)id & 0x7fff;
    if (key == EXTRA_ID)
        return (u64)extra;
    if (key == HELP_ID)
        return (u64)help;
    const unsigned char *text = campaign_message((int)id);
    if (text)
        return (u64)text;
    return (u64)names_display((int)id, (void *)orig_message(id, b, c, d));
}
static u64 widget(u64 menu, int row) { return rd64(rd64(menu + 0x28) + 8 * row); }
static void label(int row, const char *s) {
    rows[row][0] = 0x0b;
    rows[row][1] = 76;
    menu_encode(rows[row] + 2, s);
    wr64(widget(active_list, row) + 0x1f8, (u64)rows[row]);
}
static void draw_page(void) {
    static const char *const fixed[][4] = {
        [HOME] = {"Character", "Cheats", "Controller", "More"},
        [MORE] = {"Display", "Gameplay", "About", "Back"},
        [CHARACTER] = {"Roxas", "Xion", "Xion (hooded)", "Settings"},
        [DISPLAY] = {"Auto", "Cycle", "Pinned", "Back"},
        [CONTROLLER] = {"", "", "", "Back"},
        [CHEATS] = {"", "", "", "Multipliers"},
        [MULTIPLIERS] = {"", "", "Back", ""},
        [ABOUT] = {"Roxas May Cry", "Steam KH2FM", "Native build 88", "Back"}};
    char text[64];
    for (int i = 0; i < 4; i++) {
        const char *s = "";
        if ((page == CHEATS && i < 3) || (page == MULTIPLIERS && i < 2) || page == GAMEPLAY) {
            const struct option *o = (page == CHEATS ? cheats : page == MULTIPLIERS ? multipliers : gameplay) + i;
            int value = rmc_int(o->section, o->key);
            if (page == MULTIPLIERS)
                snprintf(text, sizeof text, "%s x%d", o->label, value);
            else
                snprintf(text, sizeof text, "%s: %s", o->label,
                         page == GAMEPLAY && i == 1 ? (value ? "All" : "Menus") : (value ? "On" : "Off"));
            s = text;
        } else {
            s = page == HOME && from_title ? (i < 3 ? fixed[HOME][i+1] : "") : fixed[page][i];
            if (page == CHARACTER && i < 3) {
                snprintf(text, sizeof text, "%s%s", characters[i], look_current() == i ? " (current)" : "");
                s = text;
            } else if (page == DISPLAY && i < 3) {
                int mode = rmc_int("display", "title_logo");
                snprintf(text, sizeof text, "%s%s", i == 2 && mode >= 2 ? logos[mode] : s,
                         (i < 2 ? mode == i : mode >= 2) ? " (current)" : "");
                s = text;
            } else if (page == CONTROLLER && i < 3) {
                menu_pad_text(text, sizeof text, i);
                s = text;
            }
        }
        label(i, s);
    }
}
static const char *hint(void) {
    if (feedback)
        return feedback;
    if (page == CHARACTER) {
        const char *why = look_reason(!standalone);
        return why ? why : "Confirm to change character";
    }
    if (page == DISPLAY)
        return "Change logo for the next title load";
    if (page == CHEATS || page == MULTIPLIERS)
        return "Confirm to change - applies immediately";
    if (page == GAMEPLAY) {
        static const char *const hints[] = {"Next tutorial fight", "Applies to player names",
                                            "Next save list", "Next New Game"};
        unsigned row = (unsigned)call4(0x2f7fb0, active_list, 0, 0, 0);
        return row < 4 ? hints[row] : "Gameplay";
    }
    if (page == CONTROLLER)
        return "Use Steam Input for controller mapping";
    return "Roxas May Cry";
}
static void show_help(void) {
    menu_encode(help, hint());
    call4(0x2fa1d0, 0x8000 | HELP_ID, 1, 0, 0);
}
static void enter(enum page next, int cursor) {
    page = next;
    wr16(active_list + 4, (next == MULTIPLIERS || (next == HOME && from_title)) ? 3 : 4);
    if (next == STOCK)
        for (int i = 0; i < 4; i++)
            wr64(widget(active_list, i) + 0x1f8, saved[i]);
    else
        draw_page();
    call5(0x2f8c90, active_list, cursor, 1, 0, 2);
}
static u64 build(u64 a, u64 b, u64 c, u64 d) {
    u64 result = orig_build(a, b, c, d);
    if (rd32(data_at(0x900724)) != 3)
        return result;
    active_list = rd64(data_at(0xbef650));
    stock_rows = rd16(active_list + 4);
    if (stock_rows != 2 && stock_rows != 3)
        rmc_fail("Unexpected save menu row count");
    typedef void (*Row)(u64, int, int, int, u64, int, u64, u64);
    Row add = (Row)rmc_site(0x2f8e20);
    for (int i = stock_rows; i < 4; i++) {
        menu_encode(extra, "Change Character");
        add(active_list, i, 0, 0, rd64(data_at(0xbef680)), 0x8000 | EXTRA_ID, data_at(0x5ba580),
            data_at(0x5ba580) + 8);
        call4(0x2f8db0, active_list, i, 0, UINT32_MAX);
        u64 w = widget(active_list, i);
        call4(0x166ba0, w, 0, 0, 0);
        call4(0x1672c0, w, 0, 0, 0);
    }
    static unsigned char root_labels[2][32];
    menu_encode(root_labels[0], "Change Character");
    menu_encode(root_labels[1], "Roxas May Cry");
    for (int i = stock_rows; i < 4; i++)
        wr64(widget(active_list, i) + 0x1f8, (u64)root_labels[i - stock_rows]);
    for (int i = 0; i < 4; i++)
        saved[i] = rd64(widget(active_list, i) + 0x1f8);
    feedback = NULL;
    if (!standalone) look_save_open();
    enter(standalone ? HOME : STOCK, 0);
    return result;
}
static u64 help_update(u64 a, u64 b, u64 c, u64 d) {
    if (active_list && active_list == rd64(data_at(0xbef650)) && rd32(data_at(0x900724)) == 3 &&
        !call4(0x2f5f30, 0, 0, 0, 0) &&
        (page != STOCK || (int)call4(0x2f7fb0, active_list, 0, 0, 0) >= stock_rows)) {
        show_help();
        return 0;
    }
    return orig_help(a, b, c, d);
}
static int change_value(const struct option *o) {
    int value = rmc_int(o->section, o->key);
    if (page == MULTIPLIERS) {
        static const int steps[] = {1, 2, 3, 5, 10, 1};
        for (int i = 0; i < 5; i++)
            if (value == steps[i]) {
                value = steps[i + 1];
                break;
            }
    } else
        value = !value;
    return settings_store(o->section, o->key, value);
}
static int select_row(int row) {
    if (row == -1 || (row < 0 && row != -2 && row != -4))
        return row;
    if (row >= 4)
        return -1;
    feedback = NULL;
    if (page == STOCK) {
        if (row < stock_rows)
            return row;
        enter(row == stock_rows ? CHARACTER : HOME, 0);
    } else if (row < 0) {
        if (page == HOME && standalone)
            return row;
        enum page parent = page == MULTIPLIERS ? CHEATS : page == HOME || (page == CHARACTER && !standalone)     ? STOCK
                           : page == DISPLAY || page == GAMEPLAY || page == ABOUT ? MORE
                                                                                  : HOME;
        enter(parent, 0);
    } else if (page == HOME || page == MORE) {
        static const enum page links[][4] = {{CHARACTER, CHEATS, CONTROLLER, MORE},
                                             {DISPLAY, GAMEPLAY, ABOUT, HOME}};
        if (page == HOME && from_title && ++row > 3) return -1;
        enter(links[page == MORE][row], 0);
    } else if (page == CHARACTER) {
        if (row == 3)
            enter(HOME, 0);
        else
            feedback = look_request(row, !standalone);
    } else if (page == CHEATS && row == 3) {
        enter(MULTIPLIERS, 0);
    } else if (page == MULTIPLIERS && row >= 2) {
        enter(CHEATS, 3);
    } else if (page == CHEATS || page == GAMEPLAY || page == MULTIPLIERS) {
        if (!change_value((page == CHEATS ? cheats : page == MULTIPLIERS ? multipliers : gameplay) + row))
            feedback = "Could not save rmc.ini";
    } else if (page == DISPLAY && row < 3) {
        int mode = rmc_int("display", "title_logo");
        int next = row < 2 ? row : (mode < 2 || mode == 5 ? 2 : mode + 1);
        if (!settings_store("display", "title_logo", next))
            feedback = "Could not save rmc.ini";
    } else if (row == 3)
        enter(page == CONTROLLER ? HOME : MORE, 0);
    call4(0x2eb740, row < 0 ? 3 : 2, 1, 0, 0);
    if (page != STOCK) {
        draw_page();
        show_help();
    }
    return -1;
}
static u64 input(u64 list, u64 b, u64 c, u64 d) {
    uintptr_t caller = (uintptr_t)__builtin_return_address(0);
    int row = (int)orig_input(list, b, c, d);
    if (caller != data_at(0x2e9ebf) || list != active_list || rd32(data_at(0x900724)) != 3)
        return (u32)row;
    if (page == CONTROLLER && row == -1)
        draw_page();
    return (u32)select_row(row);
}
static Fn4 orig_start_task,orig_draw_pass;
static u64 start_task;
static int clean_camp_frame;

static u64 start_loop(u64 task,u64 b,u64 c,u64 d) {
    start_task=task;
    u64 result=orig_start_task(task,b,c,d);
    start_task=0;
    return result;
}

/* Each frame rebuilds this draw queue after updating the permanent DK objects. */
static u64 draw_pass(u64 task,u64 b,u64 c,u64 d) {
    if (clean_camp_frame) {
        u64 manager=rd64(task+0x18),prev=0;
        for (u64 obj=rd64(manager+0x18);obj;) {
            u32 next=rd32(obj+0xc),group=rd32(obj+0x14);
            u64 following=call4(0x4ad270,next,0,0,0);
            if (group==1 || group==2) {
                if (prev) wr32(prev+0xc,next);
                else wr64(manager+0x18,following);
            } else prev=obj;
            obj=following;
        }
    }
    return orig_draw_pass(task,b,c,d);
}
static u64 open_menu(u64 id, u64 mode, u64 c, u64 d) {
    uintptr_t caller = (uintptr_t)__builtin_return_address(0);
    if (caller == data_at(0x15731e) && !id && ((menu_buttons() & 0x40) || menu_key(0))) {
        opening = 1;
        id = 1;
        mode = 0;
    }
    if (caller==data_at(0x15731e) && !id && start_task) {
        clean_camp_frame=1;
        call4(0x150060,start_task,0,0,0);
        clean_camp_frame=0;
    }
    campaign_handback();
    standalone = opening && id == 1;
    from_title = standalone && opening == 2;
    opening = 0;
    active_list = 0;
    page = STOCK;
    return orig_open(id, mode, c, d);
}
static u64 title(u64 a, u64 b, u64 c, u64 d) {
    static int held, art_held;
    unsigned buttons=menu_buttons();
    int art=buttons==0x100;
    if (art && !art_held && !rd8(data_at(0x9006b0)) && !rd32(data_at(0xb1d5ec)) &&
        rd64(data_at(0xb1d5d8))) {
        int style=rmc_int("display","title_style");
        int xion=style ? style==2 : look_current()!=0;
        if (!settings_store("display","title_style",xion ? 1 : 2))
            rmc_fail("Could not save title artwork selection");
    }
    art_held=art;
    int key = (buttons & 0x200) || menu_key(1);
    int press = key && !held;
    held = key;
    if (press && !rd8(data_at(0x9006b0)) && !rd32(data_at(0xb1d5ec)) && rd64(data_at(0xb1d5d8))) {
        opening = 2;
        call4(0x2e4950, 1, 0, 0, 0);
        return 0;
    }
    return orig_title(a, b, c, d);
}
static u64 tick(u64 a, u64 b, u64 c, u64 d) {
    campaign_frame();
    cheats_frame();
    look_frame();
    names_refresh();
    return orig_tick(a, b, c, d);
}
static u64 tutorial(u64 args, u64 b, u64 c, u64 d) {
    u64 actor = 0;
    u64 place = data_at(0x717008);
    if (rd16(place) == 0x0312 && rd16(place + 6) == 0x1301 && rd32(args + 8) == 130 &&
        rmc_int("gameplay", "tutorial_fight")) {
        u64 ref = call4(0x4ad270, rd32(args), 0, 0, 0);
        if (ref)
            actor = call4(0x4ad270, rd32(ref + 4), 0, 0, 0);
    }
    u64 result = orig_tutorial(args, b, c, d);
    if (actor) {
        u64 sheet = rd64(actor + 0x5c0), enemy = call4(0x3ebf30, 135, 0, 0, 0),
            level = call4(0x3ee8c0, 0, 0, 0, 0);
        if (sheet && enemy && rd16(enemy) == 135 && level) {
            u32 hp = ((u32)rd16(enemy + 4) * rd16(level) + 99) / 100;
            if (hp) {
                wr32(sheet, hp);
                wr32(sheet + 4, hp);
            }
        }
    }
    return result;
}
void settings_plan(void) {
    if (memcmp(rmc_site(0x321fba), "\xba\x04\x00\x00\x00", 5))
        rmc_fail("Steam save list capacity mismatch");
    rmc_hook(0x2eb490, build, (void **)&orig_build);
    rmc_hook(0x2eb620, help_update, (void **)&orig_help);
    rmc_hook(0x2f9440, input, (void **)&orig_input);
    rmc_hook(0x1572b0,start_loop,(void **)&orig_start_task);
    rmc_hook(0x166640,draw_pass,(void **)&orig_draw_pass);
    rmc_hook(0x2e4950, open_menu, (void **)&orig_open);
    rmc_hook(0x2b35a0, title, (void **)&orig_title);
    rmc_hook(0x3ad9a0, message, (void **)&orig_message);
    rmc_hook(0x105e10, tick, (void **)&orig_tick);
    rmc_hook(0x431a70, tutorial, (void **)&orig_tutorial);
}
