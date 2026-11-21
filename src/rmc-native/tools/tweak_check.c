#define _GNU_SOURCE
/* Compare full mapped images for the shipped preset, individual groups, and optional writes. */
#include "../rmc.h"
#include "../config.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mod_static_table.h"

unsigned char *rmc_base;
static unsigned char *expected, *written, *vanilla;
static size_t image_size=KH_MOD_NMC_RVA+KH_MOD_NMC_SIZE;
unsigned char *rmc_site(unsigned id){assert(id<image_size); return rmc_base+id;}
void rmc_write(unsigned id,const void *p,size_t n){
    assert(id+n<=image_size);
    for(size_t i=0;i<n;++i) assert(!written[id+i]);
    memset(written+id,1,n); memcpy(rmc_base+id,p,n);
}
void *rmc_near(size_t n){assert(n==KH_MOD_NMC_SIZE); return rmc_base+KH_MOD_NMC_RVA;}
void rmc_fail(const char *why){fprintf(stderr,"%s\n",why); abort();}
void tweaks_actor_plan(void){}
void tweaks_menu_plan(void){}
void tweaks_motion_plan(void){}
void tweaks_optional_plan(void){}
void tweaks_plan(void);

static void reset_plan(void){
    memcpy(rmc_base,vanilla,image_size);
    memcpy(expected,vanilla,image_size);
    memset(written,0,image_size);
}
static void reference(const char *disabled){
    for(unsigned i=0;i<KH_MOD_STATIC_NEDITS;++i){
        const struct kh_mod_static_edit *e=kh_mod_static_edits+i;
        assert(!memcmp(expected+e->rva,e->old,e->len));
        if(!disabled || strcmp(e->group,disabled)) memcpy(expected+e->rva,e->new_,e->len);
    }
    memcpy(expected+KH_MOD_NMC_RVA,kh_mod_nmc_init,KH_MOD_NMC_INIT_LEN);
}
static void compare(FILE *ini,const char *label){
    char err[256];
    if(!ini || !rmc_config_load(ini,err,sizeof err)){fprintf(stderr,"%s configuration: %s\n",label,ini?err:"missing"); abort();}
    fclose(ini);
    tweaks_plan();
    if(!memcmp(rmc_base,expected,image_size)) return;
    for(size_t i=0;i<image_size;i++) if(rmc_base[i]!=expected[i]){
        fprintf(stderr,"%s differs at %zx\n",label,i); abort();
    }
}
static FILE *input(const char *text){ return fmemopen((void*)text,strlen(text),"r"); }
static void custom_plan(void){
    const char ini[]=
        "[antiform]\nantiform_tweaks=on\ndisable_ability_anti=on\ndisable_random_anti=on\n"
        "[worlds]\nhalve_gummiship_plans=on\nhalve_tinyship_plans=on\nworldpoint_amount=23\n"
        "symbol_es=302,9,8,7,6\n"
        "[levelcap]\nset_level_cap=20\n"
        "[combat]\nblow_gravity=2.5\nglobal_gravity=4.25\n"
        "CursorRemain=31,1,2,3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0\n"
        "[forms]\nvalor_growth=42,301,1,17,10\nfinal_growth=5,34,0,521,7\n"
        "[camera]\ncamera_patches_outdoors=2,3,4,5,6,7\n"
        "camera_patches_indoors=1.75,8,9,10,11,12\n";
    reset_plan(); reference(NULL);
    const unsigned anti[]={0x3a5719,0x3c0b97,0x3dbf6f,0x3f2ed2,0x404d06,0x4050fb,0x40870a};
    for(unsigned i=0;i<sizeof anti/sizeof anti[0];i++) expected[anti[i]]=99;
    expected[0x421e04]=0;
    const unsigned char disable_random[]={0x45,0x6b,0xc0,0};
    memcpy(expected+0x421e16,disable_random,4);
    expected[0x27a84c]=expected[0x27a8dc]=9;
    expected[0x3f205e]=23; expected[0x3a14bc]=expected[0x3ed0bc]=20;
    const unsigned char symbol[]={0x2e,1,0,0,9,8,7,6};
    memcpy(expected+KH_MOD_NMC_RVA+0x78,symbol,8);
    const float blow=2.5f,global=4.25f,camera[]={2,3,4,5,6,7,1.75f,8,9,10,11,12};
    memcpy(expected+KH_MOD_NMC_RVA,&blow,4); memcpy(expected+0x5ce184,&global,4);
    memcpy(expected+0x718c00,camera,sizeof camera); memcpy(expected+0x718bdc,&camera[6],4);
    const uint16_t valor[]={42,301,1,17,10},final[]={5,34,0,521,7};
    memcpy(expected+0x5c9030,valor,10); memcpy(expected+0x5c9030+40,final,10);
    const uint32_t mask=0x8000000e;
    memcpy(expected+0x3b1788,&mask,4);
    compare(input(ini),"custom options");
}

int main(int argc,char **argv){
    assert(argc==2);
    FILE *exe=fopen(argv[1],"rb"); assert(exe);
    unsigned char h[4096];
    assert(fread(h,1,sizeof h,exe)==sizeof h);
    uint32_t pe; memcpy(&pe,h+60,4);
    uint16_t count,opt;
    memcpy(&count,h+pe+6,2); memcpy(&opt,h+pe+20,2);
    assert(pe+24+opt+count*40<sizeof h);
    rmc_base=calloc(1,image_size); expected=malloc(image_size); written=calloc(1,image_size);
    assert(rmc_base && expected && written);
    for(unsigned i=0;i<count;++i){
        uint32_t rva,len,raw;
        unsigned char *s=h+pe+24+opt+40*i;
        memcpy(&rva,s+12,4); memcpy(&len,s+16,4); memcpy(&raw,s+20,4);
        assert(rva+len<=image_size && !fseek(exe,raw,SEEK_SET));
        assert(fread(rmc_base+rva,1,len,exe)==len);
    }
    fclose(exe);
    vanilla=malloc(image_size); assert(vanilla); memcpy(vanilla,rmc_base,image_size);
    reset_plan(); reference(NULL); compare(fopen("../../dll/rmc.ini","rb"),"shipped preset");
    const struct {const char *section,*key;} options[]={
        {"subjective","disable_subjective"},{"shop","shop_restrict_fix"},{"forms","apply_pridelands_patches"},
        {"combat","enable_negative_stats"},{"worlds","apply_worldsymbol_patch"},{"combat","aerial_dodge_patch"},
        {"combat","remove_modifier_attack_flag"},{"forms","disable_drive_restrictions"},{"limits","patch_limits"},
        {"combat","disable_genie_tink_special"},{"forms","uncap_form_level"}
    };
    for(unsigned i=0;i<sizeof options/sizeof options[0];i++){
        char line[100];
        snprintf(line,sizeof line,"[%s]\n%s=off\n",options[i].section,options[i].key);
        reset_plan(); reference(options[i].key); compare(input(line),options[i].key);
    }
    custom_plan();
    const uint32_t fallback=0x7c420;
    reset_plan(); reference(NULL); memcpy(expected+0x3b1788,&fallback,4);
    compare(input("[combat]\nCursorRemain=0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0\n"),"empty cursor mask");
    printf("%u default edits and %u-byte page match; 11 independent disabled groups and custom options pass\n",KH_MOD_STATIC_NEDITS,KH_MOD_NMC_SIZE);
    free(vanilla); free(written); free(expected); free(rmc_base);
}
