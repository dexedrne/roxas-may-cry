#include "look.h"
#include "tweaks_game.h"
#include "settings.h"
#include "campaign.h"
#include <stdatomic.h>
#include <wchar.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <share.h>
#define ci_equal(a,b) (!_stricmp(a,b))
#else
#include <strings.h>
#define ci_equal(a,b) (!strcasecmp(a,b))
#endif

#define N(a) (sizeof(a)/sizeof(*(a)))
static const char *const packs[] = {"roxas","xion","xion-hooded"};
static const struct { const char *stem; int face; } forms[] = {
    {"P_EX100",36},{"P_EX100_BTLF",47},{"P_EX100_MAGF",38},{"P_EX100_KH1F",47},
    {"P_EX100_TRIF",5},{"P_EX100_ULTF",41},{"P_EX100_HTLF",3},{"P_EX101",36},
    {"P_EX101_BTLF",47},{"P_EX110_BTLF",0},{"P_EX121",36},{"P_EX100_WI",36},
    {"P_EX100_WI_MAGF",38},{"P_EX100_WI_KH1F",39},{"P_EX100_WI_ULTF",41},
    {"H_EX500",-1},{"P_EX110",-1},{"P_EX110_NPC_PAJAMAS",-1},{"H_EX510",-1},{"H_EX510_PAJAMAS",-1}
};
enum { HIGH_SORA=15 };
static const char *const shop_faces[]={
    "menu/shopface/p_ex100.bin","menu/shopface/p_ex101.bin","menu/shopface/p_ex100_wi.bin"
};
static const char *const banks[] = {
#include "look_voice.inc"
};
static int ready[3], preference=-1, finishing, last_voice, gauge_pending, high_scene;
static int save_room=-1;
static float save_position[3];
static int save_position_valid;
static _Atomic int bank_id[4], bank_dirty[4];
static _Atomic int bank_retired[4];
struct scd { int size; _Alignas(16) u8 data[]; };
static _Atomic(struct scd *) bank_cache[2][N(banks)];
static Fn4 face_original,shop_original,enqueue_original, bank_original, resource_original, release_original, end_original[2], preload_original, scene_end_original;
static u64 (*spawn_original)(u32,u64,float);

static int readable(u64 addr,size_t size) {
    if (!addr || addr>UINTPTR_MAX-size) return 0;
#ifdef _WIN32
    MEMORY_BASIC_INFORMATION mem;
    if (!VirtualQuery((const void *)addr,&mem,sizeof mem) || mem.State!=MEM_COMMIT ||
        mem.Protect & (PAGE_GUARD|PAGE_NOACCESS)) return 0;
    return addr+size<=(uintptr_t)mem.BaseAddress+mem.RegionSize;
#else
    return 1;
#endif
}

static FILE *open_asset(int target,const char *key) {
    if (!rmc_folder) return NULL;
    wchar_t path[1024];
    int n=swprintf(path,N(path),L"%ls/dll/rmc/looks/%hs/%hs",rmc_folder,packs[target],key);
    if (n<0 || (size_t)n>=N(path)) return NULL;
#ifdef _WIN32
    return _wfsopen(path,L"rb",_SH_DENYWR);
#else
    char host[4096];
    return wcstombs(host,path,sizeof host)<sizeof host ? fopen(host,"rb") : NULL;
#endif
}
static int asset_header(int target,const char *key,const char *signature,size_t length) {
    FILE *f=open_asset(target,key);
    if (!f) return 0;
    u8 header[20];
    int ok=fread(header,1,sizeof header,f)==sizeof header && !memcmp(header,signature,length);
    if (ok && !memcmp(signature,"SEDB",4)) {
        ok=!fseek(f,0,SEEK_END) && ftell(f)==(long)rd32((uintptr_t)header+16);
    }
    fclose(f);
    return ok;
}

static int inspect_pack(int target) {
    char key[128];
    for (size_t i=0;i<N(forms);++i) {
        if (!target && i!=HIGH_SORA) continue;
        snprintf(key,sizeof key,"obj/%s.mdlx",forms[i].stem);
        if (!asset_header(target,key,"BAR\1",4)) return 0;
        if (forms[i].face<0) continue;
        snprintf(key,sizeof key,"obj/%s.a.us",forms[i].stem);
        if (!asset_header(target,key,"BAR\1",4)) return 0;
        snprintf(key,sizeof key,"remastered/obj/%s.a.us/-%d.dds",forms[i].stem,forms[i].face);
        if (!asset_header(target,key,"DDS ",4)) return 0;
    }
    if (!target) {
        for (int i=0;i<3;i++) {
            snprintf(key,sizeof key,"remastered/obj/H_EX500.mdlx/-%d.dds",i);
            if (!asset_header(0,key,"DDS ",4)) return 0;
        }
    } else {
        for (size_t i=0;i<N(banks);++i)
            if (!asset_header(target,banks[i],"SEDBSSCF",8)) return 0;
        for (size_t i=0;i<N(shop_faces);++i) {
            snprintf(key,sizeof key,"remastered/%s/-0.dds",shop_faces[i]);
            if (!asset_header(target,shop_faces[i],"BAR\1",4) || !asset_header(target,key,"DDS ",4)) return 0;
        }
    }
    return 1;
}
int look_available(int target) {return target==0 || (target>0 && target<3 && ready[target]);}
int look_current(void) {return preference < 0 ? (campaign_mode() && ready[1]) : preference;}

void look_shopface(char *path,size_t cap) {
    int target=look_current();
    if (!target || !path) return;
    for (size_t i=0;i<N(shop_faces);i++) if (ci_equal(path,shop_faces[i])) {
        char alias[128];
        int n=snprintf(alias,sizeof alias,"dll/rmc/looks/%s/%s",packs[target],shop_faces[i]);
        if (n>0 && (size_t)n<sizeof alias && (size_t)n<cap) memcpy(path,alias,n+1);
        return;
    }
}
static u64 stock_shopface(u64 id,u64 out,u64 c,u64 d) {
    u64 result=shop_original(id,out,c,d);
    look_shopface((char *)out,260);
    return result;
}

static u64 player(void) {
    if (rd8(data_at(0x717008))==0xff) return 0;
    u64 actor=rd64(data_at(0x2a11100));
    if (!readable(actor,0xb00)) return 0;
    u64 sheet=rd64(actor+0x5c0),first=data_at(0x2a17290);
    return sheet>=first && sheet-first<80*0x278 && (sheet-first)%0x278==0 && (s32)rd32(sheet)>0?actor:0;
}
static u64 nearest_point(void) {
    u64 actor=player();
    if (!actor) return 0;
    u64 obj=rd64(data_at(0x2a171c8));
    for (int n=0;n<512 && readable(obj,0xb00);++n) {
        u32 entry=rd32(obj+8);
        u64 row=entry>>31?call4(0x4ad270,entry,0,0,0):0;
        if (readable(row,40) && !memcmp((void *)(row+8),"SAVE_POINT",11)) {
            float p[3],q[3];
            memcpy(p,(void *)(actor+0x670),sizeof p);
            memcpy(q,(void *)(obj+0x670),sizeof q);
            float x=p[0]-q[0],y=p[1]-q[1],z=p[2]-q[2];
            if (x*x+y*y+z*z<=60.f*60.f) return obj;
        }
        u32 next=rd32(obj+0xa90);
        if (!(next>>31)) break;
        obj=call4(0x4ad270,next,0,0,0);
    }
    return 0;
}
void look_save_open(void) {
    u64 actor=player();
    save_position_valid=actor!=0;
    save_room=rd16(data_at(0x717008));
    if (actor) memcpy(save_position,(void *)(actor+0x670),sizeof save_position);
}
static int at_save_point(u64 actor) {
    if (save_room!=rd16(data_at(0x717008))) return 0;
    if (!save_position_valid) return nearest_point()!=0;
    float pos[3];
    memcpy(pos,(void *)(actor+0x670),sizeof pos);
    int same=1;
    for (int i=0;i<3;i++) {
        float delta=pos[i]-save_position[i];
        same &= delta>-1.f && delta<1.f;
    }
    return same || nearest_point()!=0;
}

/* The native high-level reader appends the resource handle to BAR+12. Keep
 * each registered buffer alive for the life of the plugin. */
static u64 bars[3][N(forms)][2];
static int valid_bar(u64 bar,int size,int face) {
    if (size<16 || memcmp((void *)bar,"BAR",3) || (rd8(bar+3)&15)!=1) return 0;
    u32 count=rd32(bar+4);
    if (!count || count>((unsigned)size-16)/16) return 0;
    int primary=0,secondary=0;
    for (u32 i=0;i<count;i++) {
        u64 row=bar+16+16*i;
        u32 off=rd32(row+8),len=rd32(row+12);
        if (!len) continue;
        if (off<16+count*16 || off%4 || off>(unsigned)size || len>(unsigned)size-off) return 0;
        primary |= rd16(row)==(face?24:4);
        secondary |= rd16(row)==(face?25:7);
    }
    return primary && secondary;
}
static u64 get_bar(int target,int index,int face,u64 native) {
    u64 *entry=&bars[target][index][face];
    if (*entry) return *entry==UINT64_MAX?0:*entry;
    char key[112],member[64];
    snprintf(member,sizeof member,"obj/%s.%s",forms[index].stem,face?"a.us":"mdlx");
    snprintf(key,sizeof key,"dll/rmc/looks/%s/%s",packs[target],member);
    if (strnlen((const char *)data_at(0x2bd4340),260)+strlen(key)+2>260) return 0;
    FILE *file=open_asset(target,member);
    if (!file) return 0;
    int size=(int)call4(0x3a09b0,key,0,0,0);
    if (size<16 || size>16*1024*1024 || fseek(file,0,SEEK_END) || ftell(file)!=size) {
        fclose(file);
        return 0;
    }
    void *mem=malloc((size_t)size+127);
    if (!mem) { fclose(file); return 0; }
    u64 bar=((uintptr_t)mem+127)&~(uintptr_t)127;
    /* BAR members carry 32-bit game handles. A full segment table cannot
     * represent an arbitrary process-heap allocation. */
    u64 end=bar+(u32)size-1;
    if (call4(0x4ad270,call4(0x4ad240,bar,0,0,0),0,0,0)!=bar ||
        call4(0x4ad270,call4(0x4ad240,end,0,0,0),0,0,0)!=end) {
        fclose(file);free(mem);return 0;
    }
    rewind(file);
    if (fread((void *)bar,1,size,file)!=(size_t)size || !valid_bar(bar,size,face)) {
        fclose(file);free(mem);*entry=UINT64_MAX;return 0;
    }
    /* Keep the installed member locked against writes across the game's
     * size-less read. Its high-level loader installs the HD handle in BAR+12. */
    int result=(int)call4(0x3f9850,key,bar,0,0);
    fclose(file);
    if (result!=size || !valid_bar(bar,size,face)) {
        /* A failed HD registration can retain the address until shutdown. */
        *entry=UINT64_MAX;
        return 0;
    }
    wr8(bar+3,(rd8(bar+3)&63)|(rd8(native+3)&192));
    call4(0x3a64f0,bar,0,0,0);
    call4(0x3a6840,bar,0,0,0);
    *entry=bar;
    return bar;
}

struct resource {u64 address,body,face; struct resource *next;};
static struct resource *resources;
static struct resource **find_resource(u64 resource) {
    struct resource **p=&resources;
    while (*p && (*p)->address!=resource) p=&(*p)->next;
    return p;
}
static int form_index(u64 resource) {
    u64 row=rd64(resource+8);
    if (!readable(row,40)) return -1;
    for (size_t i=0;i<N(forms);++i)
        if (!strncmp((char *)(row+8),forms[i].stem,32)) return (int)i;
    return -1;
}
static int select_resource(u64 resource,int target,int i) {
    struct resource **p=find_resource(resource);
    u64 body=*p?(*p)->body:rd64(resource+16),face=*p?(*p)->face:rd64(resource+24);
    if (!body) return 0;
    if (target || i==HIGH_SORA) {
        if (!ready[target]) return 0;
        u64 next=get_bar(target,i,0,body);
        u64 next_face=face && forms[i].face>=0 ? get_bar(target,i,1,face) : face;
        if (!next || (face && !next_face)) return 0;
        if (!*p) {
            struct resource *entry=malloc(sizeof *entry);
            if (!entry) return 0;
            *entry=(struct resource){resource,body,face,NULL};
            *p=entry;
        }
        body=next;face=next_face;
    }
    wr64(resource+16,body);wr64(resource+24,face);
    return 1;
}
static u64 resource_open(u64 r,u64 b,u64 c,u64 d) {
    u64 result=resource_original(r,b,c,d);
    int index=form_index(r),target=look_current();
    if ((index==HIGH_SORA && high_scene) || (index>=0 && index<HIGH_SORA && target) ||
        (index>HIGH_SORA && campaign_mode() && target)) select_resource(r,target,index);
    return result;
}
static u64 resource_end(u64 r,u64 b,u64 c,u64 d) {
    struct resource **p=find_resource(r),*entry=*p;
    if (entry) {
        wr64(r+16,entry->body);wr64(r+24,entry->face);
        *p=entry->next;free(entry);
    }
    return release_original(r,b,c,d);
}

struct renders {u64 owner,spare[3];};
static struct renders idle[80];
static u64 active,active_instance,active_model,active_face;
static u64 get_face(u64 actor,u64 texture,u64 animation,u64 d) {
    if (!active_face || actor!=active || rd64(actor+0x7b0)!=active_instance || rd64(actor+0xa88)!=active_model)
        return face_original(actor,texture,animation,d);
    u64 previous=rd64(actor+0x928);
    wr64(actor+0x928,active_face);
    u64 result=face_original(actor,texture,animation,d);
    wr64(actor+0x928,previous);
    return result;
}
static int active_mode=-1;
static void retire(u64 actor) {
    if (actor==active) { active=active_instance=active_model=active_face=0; active_mode=-1; gauge_pending=0; }
    for (size_t i=0;i<N(idle);++i) if (idle[i].owner==actor) {
        for (int j=0;j<3;j++) if (idle[i].spare[j]) {
            u64 inst=idle[i].spare[j],mdl=rd64(inst+32);
            ((Fn4)rd64(rd64(mdl)+0x48))(mdl,call4(0x152590,0,0,0,0),inst,0);
        }
        idle[i]=(struct renders){0};
        break;
    }
}
static u64 end_actor(u64 a,u64 b,u64 c,u64 d) {retire(a);return end_original[0](a,b,c,d);}
static u64 end_model(u64 a,u64 b,u64 c,u64 d) {retire(a);return end_original[1](a,b,c,d);}

static int bind_actor(u64 actor,int target) {
    u64 r=actor+0x910,body=rd64(r+16),face=rd64(r+24),inst=rd64(actor+0x7b0),old_model=rd64(actor+0xa88);
    int index=form_index(r);
    if (index<0 || index>=HIGH_SORA || !body || !face || !inst || !old_model || rd64(inst+32)!=old_model) return 0;
    if (!select_resource(r,target,index)) return 0;
    u64 model=call4(0x3e62e0,r,0,0,0),tex=call4(0x3e6310,r,0,0,0);
    if (!model || !tex || rd64(model)!=rd64(old_model)) goto rollback;
    if (model!=old_model) {
        size_t slot=(rd64(actor+0x5c0)-data_at(0x2a17290))/0x278;
        if (slot>=N(idle) || (idle[slot].owner && idle[slot].owner!=actor)) goto rollback;
        u64 *spares=idle[slot].spare;
        int j=0;
        while (j<3 && spares[j] && rd64(spares[j]+32)!=model) j++;
        if (j==3) goto rollback;
        u64 next=spares[j];
        if (!next) {
            next=((Fn4)rd64(rd64(model)+0x40))(model,call4(0x152590,0,0,0,0),0,0);
            if (!next) goto rollback;
            call4(0x1a4bb0,next,tex,0,0);
            call4(0x1a4e30,next,0,0,0);
        }
        spares[j]=inst; idle[slot].owner=actor;
        inst=next;
        wr64(actor+0x7b0,inst);wr64(actor+0xa88,model);wr64(actor+0x7e0,tex);
    }
    active=actor;active_instance=inst;active_model=model;active_face=rd64(r+24);
    active_mode=target;gauge_pending=1;
    return 1;
rollback:
    wr64(r+16,body);wr64(r+24,face);
    return 0;
}

static int bank_for_path(const char *path) {
    for (size_t i=0;i<N(banks);++i) if (ci_equal(path,banks[i])) return (int)i+1;
    return 0;
}
static int valid_scd(const void *data,int size) {
    return data && size>=80 && size<=32*1024*1024 && !memcmp(data,"SEDBSSCF",8) &&
           rd32((uintptr_t)data+16)==(u32)size;
}
static u64 enqueue(u64 key,u64 b,u64 cb,u64 slot) {
    if (cb==data_at(0x3cddf0) && slot<4) {
        atomic_store(&bank_id[slot],bank_for_path((const char *)key));
        atomic_store(&bank_dirty[slot],0);
        bank_retired[slot]=0;
    }
    return enqueue_original(key,b,cb,slot);
}
/* Capture the game's original bytes, including package-only SCDs. The game's
 * request keeps ownership of its buffer and runs its normal completion. */
static u64 bank_loaded(u64 data,u64 size,u64 slot,u64 d) {
    int id=slot<4?atomic_load(&bank_id[slot]):0;
    if (id && valid_scd((void *)data,(int)size)) {
        if (!atomic_load(&bank_cache[0][id-1])) {
            struct scd *copy=malloc(sizeof *copy+(size_t)(int)size);
            if (copy) {
                copy->size=(int)size;
                memcpy(copy->data,(void *)data,(size_t)copy->size);
                struct scd *empty=NULL;
                if (!atomic_compare_exchange_strong(&bank_cache[0][id-1],&empty,copy)) free(copy);
            }
        }
        if (atomic_load(&bank_cache[0][id-1])) atomic_store(&bank_dirty[slot],1);
    }
    return bank_original(data,size,slot,d);
}
static struct scd *voice(int selected,int id) {
    struct scd *copy=atomic_load(&bank_cache[selected][id-1]);
    if (copy || !selected) return copy;
    FILE *f=open_asset(ready[1]?1:2,banks[id-1]);
    if (!f) return NULL;
    long size=fseek(f,0,SEEK_END)?-1:ftell(f);
    rewind(f);
    if (size<80 || size>32*1024*1024) {fclose(f);return NULL;}
    copy=malloc(sizeof *copy+(size_t)size);
    if (!copy) {fclose(f);return NULL;}
    int ok=fread(copy->data,1,size,f)==(size_t)size && valid_scd(copy->data,(int)size);
    fclose(f);
    if (!ok) {free(copy);return NULL;}
    copy->size=size;
    atomic_store(&bank_cache[1][id-1],copy);
    return copy;
}
static int update_banks(int selected) {
    int busy=0;
    for (int slot=0;slot<4;slot++) {
        int dirty=atomic_load(&bank_dirty[slot]),id=atomic_load(&bank_id[slot]);
        if (!id || !dirty) continue;
        if (rd64(data_at(0x2a244d8))) {busy=1;continue;}
        if ((rd32(data_at(0x2a244a8)+slot*12)&1) && (selected || dirty==2)) {
            struct scd *bank=voice(selected,id);
            if (!bank) {busy=1;continue;}
            if (!bank_retired[slot]) {
                call4(0x1de4a0,slot,0,0,0);
                call4(0x1363b0,slot,UINT32_MAX,0,0);
                bank_retired[slot]=1;
            }
            /* f_136060 skips deallocation of +0x58 for ownership mode 2. */
            if ((int)call5(0x13b2e0,slot,bank->data,bank->size,UINT32_MAX,2)<=0) {
                u64 head=rd64(data_at(0x713450));
                if (head && rd64(head+0x58)==(u64)bank->data) call4(0x1363b0,slot,UINT32_MAX,0,0);
                busy=1;continue;
            }
        }
        atomic_store(&bank_dirty[slot],0);bank_retired[slot]=0;
    }
    return busy;
}
static void update_gauge(void) {
    if (!gauge_pending) return;
    u64 g=rd64(data_at(0xabd248));
    if (!active || !g || rd64(g+0x40)!=active) {gauge_pending=0;return;}
    call4(0x17eb30,g+0x50,active,rd32(g+0x20),0);
    gauge_pending=!rd8(g+0x354);
}

static void scene_actor(u32 *id) {
    if (!ready[look_current()]) return;
    if (*id==571) *id=84;
    else if (*id==572 || *id==264) high_scene=1;
}
static u64 scene_preload(u64 self,u64 id,u64 slot,u64 d) {
    u32 actor=(u32)id;
    scene_actor(&actor);
    return preload_original(self,actor,slot,d);
}
static u64 scene_spawn(u32 id,u64 pos,float angle) {
    uintptr_t site=(uintptr_t)__builtin_return_address(0)-(uintptr_t)rmc_base;
    if (site==0x2d21db || site==0x2d2453 || site==0x2d2552) {
        u32 actor=id^0x40000000;
        scene_actor(&actor);
        id=actor^0x40000000;
    }
    return spawn_original(id,pos,angle);
}
static u64 scene_end(u64 a,u64 b,u64 c,u64 d) {
    u64 result=scene_end_original(a,b,c,d);
    high_scene=0;
    return result;
}
void look_reset(void) {
    preference=-1;
    finishing=0;
    save_room=-1;
    save_position_valid=0;
}
static const char *field_blocked(int picker) {
    if (!picker && rd8(data_at(0x9006b0))) return "Game menu open";
    if (rd64(data_at(0x2a11268))) return "Special player active";
    if (high_scene || rd64(data_at(0x9ba928)) || rd8(data_at(0x9ba8d1)) ||
        rd64(data_at(0x2a11478))) return "Cutscene active";
    if (rd64(data_at(0x2a244d8)) || rd8(data_at(0x2a11428)) ||
        rd64(data_at(0x2ae9fa8)) || rd32(data_at(0x2a10504))) return "Field transition";
    if (!picker && rd64(data_at(0x2a16310))) return "Waiting for field";
    return NULL;
}
const char *look_reason(int save) {
    if (finishing || gauge_pending) return "Character still loading";
    for (int i=0;i<4;i++)
        if (atomic_load(&bank_dirty[i])) return "Character still loading";
    const char *reason=field_blocked(1);
    if (reason) return reason;
    if (rd16(data_at(0x9acdd4))) return "Drive Form active";
    if (rd32(data_at(0x2a11404))) return "Battle active";
    u64 actor=player();
    if (!actor) return "Player unavailable";
    int i=form_index(actor+0x910);
    if (i<0 || i>=HIGH_SORA) return "Special player active";
    if (forms[i].stem[strlen(forms[i].stem)-1]=='F') return "Drive Form active";
    if (save && !at_save_point(actor)) return "Left save point";
    return NULL;
}
const char *look_request(int target,int save) {
    const char *reason=look_reason(save);
    if (reason) return reason;
    if (!look_available(target)) return "Missing character pack";
    if (!bind_actor(player(),target)) return "Character model unavailable";
    preference=target;
    finishing=1;
    return NULL;
}
void look_frame(void) {
    int current=look_current(),selected=!!current;
    if (selected!=last_voice) {
        for (int i=0;i<4;i++) {
            int id=atomic_load(&bank_id[i]);
            if (id && atomic_load(&bank_cache[0][id-1])) atomic_store(&bank_dirty[i],2);
        }
        last_voice=selected;
    }
    int busy=update_banks(selected);
    update_gauge();
    finishing=busy || gauge_pending;
    if (finishing || (current==0 && active_mode<=0) || field_blocked(0)) return;
    u64 actor=player();
    if (actor && !(active==actor && active_instance==rd64(actor+0x7b0) && active_mode==current))
        bind_actor(actor,current);
}
static Fn4 pool_original;
static _Alignas(128) u8 camp_preview_pool[2*1024*1024];
static u64 menu_pool(u64 base,u64 size,u64 c,u64 d) {
    if (base==data_at(0x938730) && size==0x68000 && look_current()) {
        base=(u64)camp_preview_pool;
        size=sizeof camp_preview_pool;
    }
    return pool_original(base,size,c,d);
}
void look_plan(void) {
    for (int i=0;i<3;i++) ready[i]=inspect_pack(i);
    look_reset();
#define HOOK(id,fn,orig) rmc_hook(id,(void *)(fn),(void **)&(orig))
    if (!rmc_int("shop","shopface_fix")) HOOK(0x29efa0,stock_shopface,shop_original);
    HOOK(0x19c3a0,menu_pool,pool_original);
    HOOK(0x3b57b0,get_face,face_original);
    HOOK(0x3a0b30,enqueue,enqueue_original);
    HOOK(0x3cddf0,bank_loaded,bank_original);
    HOOK(0x3e6120,resource_open,resource_original);
    HOOK(0x3e61e0,resource_end,release_original);
    HOOK(0x3b43c0,end_actor,end_original[0]);
    HOOK(0x3b4ec0,end_model,end_original[1]);
    HOOK(0x2d0350,scene_preload,preload_original);
    HOOK(0x3df930,scene_spawn,spawn_original);
    HOOK(0x2cbd80,scene_end,scene_end_original);
#undef HOOK
}
