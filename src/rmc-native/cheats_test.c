#include <assert.h>
#include <stdio.h>
#include "cheats.c"

static uintptr_t delta(uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d){return b;}
static uintptr_t amount(uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d){return a;}
static float seen_duration;
static void record_duration(uintptr_t sheet,float duration){seen_duration=duration;}
static uintptr_t parked;
uint64_t campaign_progress_actor(void){return parked;}
static unsigned hooks;
void rmc_hook(unsigned id,void *replacement,void **original){++hooks;}
unsigned char *rmc_site(unsigned id){static unsigned char placeholder[16];return placeholder;}

/* Return from the assembly leaf at the point where the game compares XMM1 to XMM0. */
static float adjusted(float chance,float roll){
    __asm__ volatile("movss %0,%%xmm1\nmovss %2,%%xmm0\nmovss %3,%%xmm8\n"
        "lea 1f(%%rip),%%rax\nmov %%rax,after_roll(%%rip)\njmp drop_compare\n"
        "1: movss %%xmm1,%0\nmovss %%xmm0,%1"
        : "+m"(chance),"=m"(roll) : "m"(roll),"m"((float){2})
        : "rax","xmm0","xmm1","xmm8","cc","memory");
    assert(roll==20);
    return chance;
}
int main(void){
    char error[256];
    assert(rmc_config_load(NULL,error,sizeof error));cheats_plan();assert(hooks==6 && adjusted(120,10)==120);
    FILE *ini=tmpfile();assert(ini);
    fputs("[cheats]\ncheat_infinite_hp=on\ncheat_infinite_mp=on\ncheat_infinite_drive=on\ncheat_exp_mult=3\ncheat_drop_mult=5\n",ini);
    rewind(ini);assert(rmc_config_load(ini,error,sizeof error));fclose(ini);
    cheats_refresh();assert(hooks==6);
    uintptr_t actor[0x5c0/sizeof(uintptr_t)+1]={0},selected=(uintptr_t)actor;
    player=&selected;actor[0x5c0/sizeof(uintptr_t)]=123;
    hp_original=mp_original=display_original=delta;award_original=amount;
    recharge_original=record_duration;
    assert(!hp(123,-7,0,0) && hp(123,-7,1,0)==(uintptr_t)-7 && hp(124,-7,0,0)==(uintptr_t)-7);
    assert(hp(123,5,0,0)==5 && !mp(selected,-6,0,0) && mp(1,-6,0,0)==(uintptr_t)-6);
    recharge(123,2.5f);assert(!seen_duration);recharge(124,2.5f);assert(seen_duration==2.5f);
    assert(award(12,0,0,0)==36 && display(0,12,0,0)==36 && award(9999998,0,0,0)==9999999);
    assert(adjusted(2,10)==10 && adjusted(40,10)==100);
    unsigned char gauges[0x1bc]={0}, other[0x1bc]={0};
    uintptr_t second[0x5c0/sizeof(uintptr_t)+1]={0};
    parked=(uintptr_t)second;
    second[0x5c0/sizeof(uintptr_t)]=(uintptr_t)other;
    actor[0x5c0/sizeof(uintptr_t)]=(uintptr_t)gauges;
    gauges[0x1b2]=7; gauges[0x1af]=2; gauges[0x1b8]=55;
    other[0x1b2]=3; other[0x1af]=3; other[0x1b8]=88;
    cheats_frame();
    assert(gauges[0x1b0]==100 && gauges[0x1b1]==7 && gauges[0x1b4]==55);
    assert(other[0x1b1]==3 && !other[0x1b4]);
    assert(rmc_config_load(NULL,error,sizeof error)); cheats_refresh();
    gauges[0x1b1]=2; gauges[0x1b4]=1; cheats_frame();
    assert(gauges[0x1b1]==2 && gauges[0x1b4]==1);
    puts("PC cheat hooks: defaults, player isolation, recharge duration, awards and drop thresholds passed");
}
