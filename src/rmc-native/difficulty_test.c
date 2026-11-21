#include "difficulty.h"
#include "tweaks_game.h"
#include <assert.h>

extern u32 rmc_damage_body(u64 victim,u64 packet,u64 attack,s32 lane,s32 home);
static u8 save[0x4000], sheet[0x278], actor[0xe00], command[0x30], sstm[0x200], params[16];
static u32 last_award;
static unsigned hp_calls, displayed_rewards;
static void *apply_entry, *bonus_entry, *usable_entry;

#define STUB(name, result) static u64 name(u64 a,u64 b,u64 c,u64 d){ \
    (void)a;(void)b;(void)c;(void)d;return (u64)(result); }
STUB(get_sheet,sheet)
STUB(get_actor,actor)
STUB(get_save,save)
STUB(get_sstm,sstm)
STUB(get_command,command)
STUB(get_params,params)
STUB(enabled,1)
STUB(zero,0)
STUB(original_usable,3)
static u64 add_hp(u64 s,u64 amount,u64 unused,u64 flag){
    assert(s==(u64)sheet && unused==0 && flag<=1);
    hp_calls++;
    wr32(s,rd32(s)+(u32)amount);
    return 0;
}
static u64 append_message(u64 list,u64 value,u64 name,u64 unused){
    (void)unused;
    if ((s32)value<=0) return 0;
    u32 count=rd32(list+16);
    assert(count<2);
    wr64(list+8*count,name); wr32(list+16,count+1); displayed_rewards++;
    return 0;
}
static u64 add_exp(u64 amount,u64 b,u64 c,u64 d){
    assert(!b&&!c&&!d); last_award=(u32)amount; return 0;
}
unsigned char *rmc_site(unsigned site){
    switch(site){
    case 0x9abd48: return save+0x2498;
    case 0x9acfb0: return save+0x3700;
    case 0x3c04c0: return (void *)get_sheet;
    case 0x4ad270: case 0x3b3e80: return (void *)get_actor;
    case 0x152380: return (void *)get_save;
    case 0x3e9300: return (void *)get_sstm;
    case 0x3b68b0: return (void *)get_command;
    case 0x3ee8c0: return (void *)get_params;
    case 0x3ed110: return (void *)enabled;
    case 0x3c0c30: return (void *)add_hp;
    case 0x3ecf30: return (void *)add_exp;
    case 0x3ed3d0: return (void *)append_message;
    case 0x3c07e0: case 0x3b58c0: case 0x3c0cd0: case 0x3c0ad0:
    case 0x3ed350: case 0x3edb20: case 0x3ecf20: return (void *)zero;
    default: fprintf(stderr,"unexpected game helper %x\n",site); abort();
    }
}
void rmc_hook(unsigned site,void *replacement,void **orig){
    switch(site){
    case 0x3c4b80: apply_entry=replacement; break;
    case 0x3c3da0: usable_entry=replacement; *orig=original_usable; break;
    case 0x3edf90: bonus_entry=replacement; break;
    default: break;
    }
}
void rmc_fail(const char *why){fprintf(stderr,"%s\n",why); abort();}

int main(void){
    u64 s=(u64)sheet, a=(u64)actor, diff=(u64)save+0x2498;
    assert(rmc_config_load(NULL,NULL,0));
    difficulty_plan();
    u64 (*apply)(u64,u32,u32,u32)=apply_entry;
    Fn4 usable=usable_entry;
    void (*bonus)(u64,u64)=bonus_entry;
    u8 rec[32]={0}, attack[16]={0}, packet[32]={0};
    wr64(a+0x5c0,s); wr16(s+0x18a,20); wr32(s+4,200);
    attack[5]=1; attack[4]=5; wr16((u64)attack+6,50);
    assert(rmc_damage_body(s,(u64)packet,(u64)attack,0,0)==20);
    attack[4]=6;
    assert(rmc_damage_body(s,(u64)packet,(u64)attack,0,0)==10);
    wr32(diff,3); wr32(s+4,119); rec[5]=11; rec[4]=3;
    assert(usable((u64)rec,2,0,0)==1);
    apply((u64)rec,2,0,100); assert(rd32(s+4)==122);
    assert(usable((u64)rec,2,0,0)==3);
    rec[5]=0; rec[4]=50; rec[0]=1; wr32(s,10); wr32(s+4,100);
    apply((u64)rec,2,1,100); assert(rd32(s)==60);
    command[0x2f]=2; apply((u64)rec,2,1,100); assert(rd32(s)==10);
    memset(rec,0,sizeof rec); rec[1]=2; rec[2]=8; rec[3]=10; rec[12]=1;
    unsigned before=hp_calls; bonus((u64)rec,s); assert(hp_calls==before);
    rec[4]=1; rec[12]=8; bonus((u64)rec,s); assert(rd32(s+4)==104 && rd32(s+0x184)==5);
    assert(displayed_rewards==2);
    u64 args[]={1,100}; wr16((u64)params+10,100);
    wr32(s,100); wr32(s+4,100); rmc_add_exp(args); assert(last_award==75);
    args[1]=0; rmc_add_exp(args); assert(last_award==1);
    puts("difficulty: special damage, item polarity/cap, bonus mask and EXP passed");
    return 0;
}

uint64_t campaign_progress_actor(void) { return 0; }
