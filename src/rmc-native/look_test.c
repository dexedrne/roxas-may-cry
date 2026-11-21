#include <assert.h>
#include "look.c"

unsigned char *rmc_base;
const wchar_t *rmc_folder;
static u8 actor[0xc00], point[0xc00], row[64], save_row[64], vtable[96], models[3][32], instances[3][64];
static u8 bodies[3][64], faces[3][64], texture;
static int mode, destroyed, stops, registrations, attempts, fail_render;
static u8 *last_scd;
int campaign_mode(void){return mode>=2 ? mode : 0;}
int rmc_int(const char *s,const char *k){
    assert(!strcmp(s,"shop") && !strcmp(k,"shopface_fix"));
    return 0;
}
void rmc_hook(unsigned id,void *fn,void **original){(void)id;(void)fn;(void)original;}
static u64 ignore(u64 a,u64 b,u64 c,u64 d){(void)a;(void)b;(void)c;(void)d;return 0;}
static u64 pool_base,pool_size;
static u64 record_pool(u64 base,u64 size,u64 c,u64 d){
    assert(c==42 && d==43);pool_base=base;pool_size=size;return 17;
}
static void test_pool(void){
    int previous=preference;
    pool_original=record_pool;
    u64 base=data_at(0x938730);
    for(preference=0;preference<3;preference++){
        assert(menu_pool(base,0x68000,42,43)==17);
        assert(pool_base==(preference?(u64)camp_preview_pool:base));
        assert(pool_size==(preference?sizeof camp_preview_pool:0x68000));
        menu_pool(base+16,0x68000,42,43);assert(pool_base==base+16 && pool_size==0x68000);
    }
    preference=previous;
}
static u64 decode(u64 id,u64 b,u64 c,u64 d){(void)b;(void)c;(void)d;return id==0x80000001?(u64)save_row:0;}
static u64 model_of(u64 r,u64 b,u64 c,u64 d){
    (void)b;(void)c;(void)d;
    for (int i=0;i<3;i++) if (rd64(r+16)==(u64)bodies[i]) return (u64)models[i];
    return 0;
}
static u64 texture_of(u64 a,u64 b,u64 c,u64 d){(void)a;(void)b;(void)c;(void)d;return (u64)&texture;}
static u64 instance_of(u64 mdl,u64 b,u64 c,u64 d){
    (void)b;(void)c;(void)d;
    if (fail_render) return 0;
    for(int i=0;i<3;i++) if (mdl==(u64)models[i]) return (u64)instances[i];
    return 0;
}
static u64 destroy(u64 mdl,u64 alloc,u64 inst,u64 d){
    (void)alloc;(void)d;assert(rd64(inst+32)==mdl);destroyed++;return 0;
}
static u64 stop_voice(u64 slot,u64 b,u64 c,u64 d){assert(slot==0);(void)b;(void)c;(void)d;++stops;return 0;}
static u64 register_voice(u64 slot,u64 bytes,u64 size,u64 flag,u64 ownership){
    assert(slot==0 && size==80 && (u32)flag==UINT32_MAX && ownership==2 && bytes%16==0);
    if (attempts-->0) return 0;
    ++registrations;last_scd=(u8 *)bytes;return 15;
}
unsigned char *rmc_site(unsigned id) {
    switch(id) {
    case 0x4ad270:return (void *)decode;
    case 0x3e62e0:return (void *)model_of;
    case 0x3e6310:return (void *)texture_of;
    case 0x17eb30:case 0x152590:case 0x1a4bb0:case 0x1a4e30:case 0x1363b0:return (void *)ignore;
    case 0x1de4a0:return (void *)stop_voice;
    case 0x13b2e0:return (void *)register_voice;
    default:return rmc_base+id;
    }
}
static void set_resource(u64 r) {
    wr64(r+8,(u64)row);wr64(r+16,(u64)bodies[0]);wr64(r+24,(u64)faces[0]);
}
static u64 released(u64 r,u64 b,u64 c,u64 d){
    assert(rd64(r+16)==(u64)bodies[0] && rd64(r+24)==(u64)faces[0]);return ignore(r,b,c,d);
}
static void setup(void) {
    rmc_base=calloc(1,0x3000000);assert(rmc_base);
    u64 a=(u64)actor;
    memcpy(row+8,"P_EX100",8);memcpy(save_row+8,"SAVE_POINT",11);
    wr32((u64)point+8,0x80000001);
    wr64(data_at(0x2a171c8),(u64)point);wr64(data_at(0x2a11100),a);
    wr64(a+0x5c0,data_at(0x2a17290));wr32(data_at(0x2a17290),100);
    set_resource(a+0x910);
    wr64(a+0x7b0,(u64)instances[0]);wr64(a+0xa88,(u64)models[0]);
    wr64((u64)vtable+0x40,(u64)instance_of);
    wr64((u64)vtable+0x48,(u64)destroy);
    for (int i=0;i<3;i++) {
        ready[i]=1;
        wr64((u64)models[i],(u64)vtable);
        wr64((u64)instances[i]+32,(u64)models[i]);
        bars[i][0][0]=(u64)bodies[i];bars[i][0][1]=(u64)faces[i];
    }
    resource_original=end_original[0]=enqueue_original=bank_original=ignore;
    release_original=released;
}
static void switch_to(int target) {
    assert(!look_request(target,0));
    assert(look_current()==target && look_request((target+1)%3,0));
    look_frame();
    assert(rd64((u64)actor+0x7b0)==(u64)instances[target]);
    assert(rd64((u64)actor+0x920)==(u64)bodies[target]);
    assert(rd64((u64)actor+0x928)==(u64)faces[target]);
}
static u64 face_probe(u64 actor,u64 texture,u64 animation,u64 d) {
    (void)texture;(void)animation;(void)d;
    return rd64(actor+0x928);
}
static void test_switches(void) {
    setup();
    assert(look_request(3,0));
    /* A loaded actor can become available after the native save list was built. */
    wr64(data_at(0x2a11100),0);
    look_save_open();
    wr64(data_at(0x2a11100),(u64)actor);
    assert(!look_reason(1));
    look_reset();
    assert(!strcmp(look_reason(1),"Left save point"));
    float x=72;
    memcpy(point+0x670,&x,sizeof x);
    look_save_open();
    wr8(data_at(0x9006b0),1);
    assert(!look_request(1,1));look_frame();
    wr16(data_at(0x717008),2);
    assert(!strcmp(look_reason(1),"Left save point"));
    x=200;memcpy(point+0x670,&x,sizeof x);
    switch_to(0);
    fail_render=1;
    assert(!strcmp(look_request(2,0),"Character model unavailable"));
    assert(look_current()==0 && rd64((u64)actor+0x920)==(u64)bodies[0]);
    fail_render=0;
    for (mode=0;mode<4;mode++) {
        for (int repeat=0;repeat<2;repeat++) {
            look_reset();assert(look_current()==(mode>=2));
            for (int n=0;n<21;n++) switch_to(n%3);
        }
    }
    mode=0;
    const struct {unsigned site;const char *reason;} blocks[]={
        {0x9acdd4,"Drive Form active"},{0x2a11404,"Battle active"},
        {0x9ba8d1,"Cutscene active"},{0x2a11428,"Field transition"},
        {0x2a11268,"Special player active"}};
    for (size_t i=0;i<N(blocks);i++) {
        wr8(data_at(blocks[i].site),1);
        assert(!strcmp(look_request(0,0),blocks[i].reason) && look_current()==2);
        wr8(data_at(blocks[i].site),0);
    }
    strcpy((char *)row+8,"P_EX100_BTLF");
    assert(!strcmp(look_reason(0),"Drive Form active"));
    strcpy((char *)row+8,"P_EX100");
    u8 gauge[0x360]={0};
    wr64(data_at(0xabd248),(u64)gauge);
    wr64((u64)gauge+0x40,(u64)actor);
    assert(!look_request(1,0));look_frame();
    assert(!strcmp(look_request(0,0),"Character still loading"));
    wr8((u64)gauge+0x354,1);look_frame();
    assert(look_current()==1);
    bank_dirty[0]=1;
    assert(!strcmp(look_request(2,0),"Character still loading"));
    bank_dirty[0]=0;
    switch_to(2);
    wr64(data_at(0xabd248),0);
    u64 expected=rd64((u64)actor+0x928);
    face_original=face_probe;
    wr64((u64)actor+0x928,(u64)faces[0]);
    assert(get_face((u64)actor,0,0,0)==expected && rd64((u64)actor+0x928)==(u64)faces[0]);
    char shop[260]="menu/shopface/p_ex101.bin";
    look_shopface(shop,sizeof shop);
    assert(!strcmp(shop,"dll/rmc/looks/xion-hooded/menu/shopface/p_ex101.bin"));
    u8 secondary[64]={0};
    set_resource((u64)secondary);
    resource_open((u64)secondary,0,0,0);
    assert(rd64((u64)secondary+16)==(u64)bodies[2]);
    resource_end((u64)secondary,0,0,0);

    end_actor((u64)actor,0,0,0);assert(destroyed==2 && !active);
    resource_end((u64)actor+0x910,0,0,0);assert(!resources);
}
static void test_voice(void) {
    u8 stock[80]={0};memcpy(stock,"SEDBSSCF",8);wr32((u64)stock+16,sizeof stock);
    int id=bank_for_path("VOICE/US/BATTLE/TT0_SORA.WIN32.SCD");
    assert(id && !bank_for_path("voice/us/battle/tt0_donald.win32.scd"));
    enqueue((u64)banks[id-1],0,data_at(0x3cddf0),0);
    bank_loaded((u64)stock,sizeof stock,0,0);
    assert(bank_cache[0][id-1] && bank_cache[0][id-1]->data!=stock);
    struct scd *xion=malloc(sizeof *xion+sizeof stock);
    assert(xion);xion->size=sizeof stock;memcpy(xion->data,stock,sizeof stock);xion->data[79]=42;
    bank_cache[1][id-1]=xion;
    wr32(data_at(0x2a244a8),1);attempts=4;
    for(int i=0;i<4;i++) assert(update_banks(1));
    assert(!update_banks(1) && stops==1 && registrations==1 && last_scd[79]==42);
    bank_dirty[0]=2;assert(!update_banks(0) && stops==2 && registrations==2 && !last_scd[79]);
    bank_dirty[0]=2;enqueue((u64)"other",0,data_at(0x3cddf0),0);
    assert(!update_banks(1) && stops==2);
}
static void inspect_installed_bars(void) {
    for (int target=0;target<3;target++) for (size_t i=0;i<N(forms);i++) {
        if (!target && i!=HIGH_SORA) continue;
        for (int face=0;face<=(forms[i].face>=0);face++) {
            char key[128];
            snprintf(key,sizeof key,"obj/%s.%s",forms[i].stem,face?"a.us":"mdlx");
            FILE *f=open_asset(target,key);
            assert(f && !fseek(f,0,SEEK_END));
            long size=ftell(f);
            assert(size>=16 && size<=16*1024*1024);
            rewind(f);
            void *bar=malloc(size);
            assert(bar && fread(bar,1,size,f)==(size_t)size && valid_bar((u64)bar,(int)size,face));
            free(bar);fclose(f);
        }
    }
}
int main(int argc,char **argv) {
    wchar_t folder[1024];
    if (argc==2) {
        assert(mbstowcs(folder,argv[1],N(folder))<N(folder));
        rmc_folder=folder;
        for(int i=0;i<3;i++) assert(inspect_pack(i));
        inspect_installed_bars();
        puts("all installed model, face, DDS and SCD pack headers passed");
    }
    u8 bar[64]={0};
    memcpy(bar,"BAR\1",4);wr32((u64)bar+4,2);
    wr16((u64)bar+16,4);wr32((u64)bar+24,48);wr32((u64)bar+28,8);
    wr16((u64)bar+32,7);wr32((u64)bar+40,56);wr32((u64)bar+44,8);
    assert(valid_bar((u64)bar,sizeof bar,0) && !valid_bar((u64)bar,sizeof bar,1));
    wr32((u64)bar+24,UINT32_MAX);
    assert(!valid_bar((u64)bar,sizeof bar,0));
    look_plan();
    test_switches();test_pool();test_voice();
    u32 id=571;scene_actor(&id);assert(id==84 && !high_scene);
    id=572;scene_actor(&id);assert(id==572 && high_scene);
    ready[2]=0;id=571;scene_actor(&id);assert(id==571);
    puts("field model, face, save gating, actor teardown, mode defaults, voice retries and scene selection passed");
}
