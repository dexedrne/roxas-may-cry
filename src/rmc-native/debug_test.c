/* Native fixture. Windows header declarations establish the ABI of our stub calls. */
#include "debug.c"
#include <assert.h>

static ULONGLONG ticks;
static int key, reset_enabled, active_debug;
static int damage_modes[2], last_group=-1;
static u8 reset_byte, player[0x600];
static u64 player_global;
static float clock_scale=1;
static const char *pending;
static unsigned warped;
static u8 destination[10];

ULONGLONG WINAPI GetTickCount64(void){return ticks;}
SHORT WINAPI GetKeyState(INT k){return k==key?(SHORT)-32768:0;}
BOOL WINAPI AllocConsole(void){return TRUE;}
HANDLE WINAPI CreateFileA(LPCSTR name,DWORD access,DWORD share,LPSECURITY_ATTRIBUTES sa,
                          DWORD creation,DWORD flags,HANDLE base){
    (void)name;(void)access;(void)share;(void)sa;(void)creation;(void)flags;(void)base;
    return (HANDLE)1;
}
BOOL WINAPI CloseHandle(HANDLE h){(void)h;return TRUE;}
void WINAPI OutputDebugStringA(LPCSTR text){(void)text;}
BOOL WINAPI WriteConsoleA(HANDLE h,const void *buffer,DWORD n,DWORD *written,void *r){
    (void)h;(void)buffer;(void)r;*written=n;return TRUE;
}
BOOL WINAPI GetNumberOfConsoleInputEvents(HANDLE h,DWORD *n){
    (void)h;*n=pending?strlen(pending):0;return TRUE;
}
BOOL WINAPI ReadConsoleInputW(HANDLE h,INPUT_RECORD *ev,DWORD cap,DWORD *count){
    (void)h;assert(cap==1 && pending && *pending);
    memset(ev,0,sizeof *ev);ev->EventType=KEY_EVENT;ev->Event.KeyEvent.bKeyDown=TRUE;
    ev->Event.KeyEvent.uChar.UnicodeChar=*pending++;*count=1;return TRUE;
}
int rmc_int(const char *s,const char *k){assert(!strcmp(s,"debug"));return !strcmp(k,"debug_on")?active_debug:reset_enabled;}
double rmc_value(const char *s,const char *k,unsigned i){assert(!strcmp(s,"debug")&&!strcmp(k,"turbo_speed")&&!i);return 3.5;}
void rmc_debug_damage(int enemy,int active){damage_modes[enemy]=active;}
static u64 warp(u64 loc,u64 a,u64 b,u64 c,u64 d){
    assert(!a&&!b&&!c&&!d);memcpy(destination,(void *)loc,10);warped++;return 0;
}
static u64 hide(u64 group,u64 a,u64 b,u64 c){assert(!a&&!b&&!c);last_group=group;return 0;}
static u64 show(u64 group,u64 a,u64 b,u64 c){assert(!a&&!b&&!c);last_group=64+group;return 0;}
unsigned char *rmc_site(unsigned id){
    switch(id){
    case 0x717424: return (u8 *)&clock_scale;
    case 0x2a105d0: return (u8 *)&player_global;
    case 0xabac5a: return &reset_byte;
    case 0x152990: return (void *)warp;
    case 0x1629b0: return (void *)hide;
    case 0x162a00: return (void *)show;
    default: abort();
    }
}
static void press(int k){
    ticks+=250;key=k;debug_frame();key=0;
}
static void type(const char *entry){
    pending=entry;debug_frame();assert(!*pending);pending=NULL;
}
int main(int argc,char **argv){
    active_debug=argc>=2 && strcmp(argv[1],"off");
    reset_enabled=active_debug && !strcmp(argv[1],"reset");
    player_global=(u64)player;wr64(player_global+0x5c0,player_global);
    press(VK_TAB);
    if(!active_debug){assert(clock_scale==1);press(VK_F6);assert(!reset_byte);return 0;}
    assert(clock_scale==3.5);key=VK_TAB;debug_frame();assert(clock_scale==3.5);
    press(VK_TAB);assert(clock_scale==1);
    press(VK_F1);assert(damage_modes[0]);press(VK_F2);assert(damage_modes[1]);
    press(VK_F2);assert(!damage_modes[1]);press(VK_F6);assert(reset_byte==reset_enabled);
    press(VK_F4);type("67\r7\r");assert(last_group==7);
    press(VK_F5);type("-1\r");assert(last_group==7);
    press(VK_F5);type("13\r");assert(last_group==77);
    press(VK_F3);type("0\r2\r12\r255\r4\r256\r65535\r");
    const u8 expected[]={2,12,255,0,4,0,0,1,255,255};
    assert(warped==1 && !memcmp(destination,expected,10));
    puts("debug: key bindings, delay, reset gate, warp record and map-group input passed");
    return 0;
}
