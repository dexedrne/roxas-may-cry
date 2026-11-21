enum CastleTable { ATTACK, APPROACH, COMMAND, OBJENTRY };
struct TableEdit {
    enum CastleTable table;
    unsigned offset, width, original, value;
};
#define ATK(row, off, width, pc, ps2) {ATTACK, 8+48*(row)+(off), width, pc, ps2}
#define HEIGHT(row, pc) {APPROACH, 0x124+68*(row)+40, 4, pc, 0xc3480000u}
static const struct TableEdit edits[] = {
    {OBJENTRY,0x21b4,4,1003,3000}, {OBJENTRY,0x7a34,4,1003,3000},
    HEIGHT(0,0xc37a0000u), HEIGHT(1,0xc3af0000u), HEIGHT(32,0xc37a0000u),
    {COMMAND,8+48*59+2,1,72,0},
    ATK(2499,10,1,11,8), ATK(2499,12,2,45,100),
    ATK(2500,10,1,3,8), ATK(2500,12,2,100,100),
    ATK(2501,10,1,11,8), ATK(2501,12,2,30,100),
    ATK(2127,10,1,11,10), ATK(2127,12,2,15,0xff9c),
    ATK(2128,10,1,11,10), ATK(2128,12,2,15,0xff9c),
    ATK(621,10,1,9,10), ATK(621,12,2,30,0xfe0c),
    ATK(644,10,1,9,10), ATK(644,12,2,0xff9c,100), ATK(644,14,2,0xfff1,0),
    ATK(1587,10,1,11,10), ATK(1584,10,1,11,10),
    ATK(1943,10,1,11,10), ATK(1943,12,2,25,350),
    ATK(1944,10,1,11,10), ATK(1944,12,2,0xff9c,350),
    ATK(1945,10,1,11,10), ATK(1945,12,2,18,350),
    ATK(1946,10,1,11,10), ATK(1946,12,2,18,350),
    ATK(1947,10,1,11,10), ATK(1947,12,2,0xff9c,350),
    ATK(1948,10,1,11,10), ATK(1948,12,2,0xff9c,350)
};

static void combat_tables( int here, int map, int battle, int event){
    static int previous;
    static u64 address[sizeof edits/sizeof edits[0]];
    static u32 saved[sizeof edits/sizeof edits[0]];
    int mask=0;
    if(here==0x1412 && map==74 && event==74) mask|=1<<ATTACK;
    if(here==0x0a12 && (battle==57 || battle==100) && battle==event) mask|=1<<APPROACH;
    if(here==0x1712 && map==73) mask|=1<<COMMAND;
    if(here==0x0212 && battle==10) mask|=1<<OBJENTRY;
    if(!(mask|previous)) return;
    const u64 slot[]={0x2ae57b0,0x2ae57b8,0x2a161e8,0x2a25030};
    const u32 count[]={2713,70,0,0};
    for(size_t i=0;i<sizeof edits/sizeof edits[0];i++){
        const struct TableEdit *e=edits+i;
        int bit=1<<e->table;
        if(!((mask|previous)&bit)) continue;
        u64 table=rd64(VA(slot[e->table]));
        if(!table) continue;
        if(count[e->table] && rd32(table+4)!=count[e->table])
            rmc_fail("ps2-castle-table-layout");
        if(e->table==OBJENTRY && (rd32(table+4)<=325 ||
           rd32(table+8+96*89)!=120 || rd32(table+8+96*325)!=442))
            rmc_fail("ps2-castle-neostatus-row");
        if(e->table==COMMAND && (rd32(table+4)<=59 || rd16(table+8+48*59)!=72))
            rmc_fail("ps2-castle-summon-row");
        u64 p=table+e->offset;
        u32 current=e->width==1?rd8(p):e->width==2?rd16(p):rd32(p);
        u32 wanted;
        if(mask&bit){
            /* The PC controller itself changes ATKP during play. Keep the last native value. */
            if(address[i]!=p || current!=e->value) saved[i]=current;
            address[i]=p;
            wanted=e->value;
        }else{
            if(address[i]!=p || current!=e->value){ address[i]=0; continue; }
            address[i]=0;
            wanted=saved[i];
        }
        if(current==wanted) continue;
        if(e->width==1) wr8(p,wanted);
        else if(e->width==2) wr16(p,wanted);
        else wr32(p,wanted);
    }
    previous=mask;
}

static void encounter_stats(int here,int battle){
    static u64 sheet, owner;
    static u16 original[3], last[3];
    int approach=here==0x0212 && battle>=10 && battle<=13;
    int finale=here==0x0312 && battle==65;
    if(!approach && !finale && !sheet) return;
    u64 player=rd64(FC+0xae0);
    u64 live=player && player==rd64(ACTIVE) ? rd64(player+0x5c0) : 0;
    u64 pool=VA(0x2a17290);
    if(live<pool || live>=pool+80*0x278 || (live-pool)%0x278) live=0;
    if(!live || player!=owner || live!=sheet){
        sheet=owner=0;
        if(!(approach||finale) || !live) return;
        sheet=live; owner=player;
        for(int i=0;i<3;i++) original[i]=last[i]=rd16(sheet+0x188+2*i);
    }
    for(int i=0;i<3;i++){
        u64 p=sheet+0x188+2*i;
        u16 cur=rd16(p);
        if(cur!=last[i]) original[i]=cur;
        u16 next=finale?(i==2?255:100):(approach&&i==2)?40:original[i];
        if(cur!=next) wr16(p,next);
        last[i]=next;
    }
    if(!approach&&!finale) sheet=owner=0;
}


static void castle_frame(void){
    static int previously_enabled;
    int enabled=rmc_int("gameplay","ps2_castle");
    int one_hp=rmc_int("gameplay","ps2_final_one_hp");
    if(!enabled && !one_hp && !previously_enabled) return;
    previously_enabled=enabled;
    u64 place=VA(0x717008);
    int here=enabled?rd16(place):0, map=rd16(place+4), battle=rd16(place+6), event=rd16(place+8);
    if(here==0x0e12 && !map && !battle && !event){
        wr16(place+4,58);
        wr16(place+6,58);
        wr16(place+8,58);
    }
    combat_tables(here,map,battle,event);
    encounter_stats(here,battle);
    if((!enabled && !one_hp) || rd16(place)!=0x1412 || (map!=74 && map!=98)) return;
    unsigned expected=map==74?2079:2506;
    u64 pool=VA(0x2a17290);
    for(u64 obj=rd64(VA(0x2a171c8)); obj; obj=call(0x4ad270,rd32(obj+0xa90),0)){
        u64 sheet=rd64(obj+0x5c0);
        if(sheet!=pool+77*0x278 && sheet!=pool+76*0x278) continue;
        if(rd32(sheet+0x268)!=rd32(obj+4)) continue;
        u32 id=rd32(obj+8);
        u64 entry=(id>>31)?call(0x4ad270,id,0):0;
        if(!entry || rd32(entry)!=expected) continue;
        if(sheet==pool+77*0x278){
            if(enabled) wr32(sheet+0x1a8,0x00323264u);
            if(one_hp && rd32(sheet)==1) wr32(sheet,0);
        }else if(enabled) wr32(sheet+0x1a8,0x00323232u);
    }
}
