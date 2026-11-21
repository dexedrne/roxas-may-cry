#define _GNU_SOURCE
#include "config.h"
#include <assert.h>
#include <string.h>

static int from_memory(const char *text, size_t length, char *error)
{
    FILE *file = fmemopen((void *)text, length, "r");
    assert(file);
    int result = rmc_config_load(file, error, 256);
    assert(fclose(file) == 0);
    return result;
}

static void edit_round_trip(void)
{
    const char original[]="\xef\xbb\xbf; old profile\r\n[Cheats]\r\n cheat_EXP_mult = 3\r\ncheat_exp_mult=5\r\n[debug]\r\ndebug_on=1";
    FILE *a=fmemopen((void *)original,sizeof original-1,"r"), *b=tmpfile(), *c=tmpfile();
    char result[256]={0},error[256];
    assert(a && b && c && rmc_config_edit(a,b,"cheats","cheat_exp_mult",10));
    rewind(b);
    assert(rmc_config_edit(b,c,"display","title_logo",5));
    rewind(c);
    assert(fread(result,1,sizeof result-1,c)>0);
    assert(strstr(result,"; old profile\r\n[Cheats]\r\ncheat_exp_mult=10\r\ncheat_exp_mult=5"));
    rewind(c);
    assert(rmc_config_load(c,error,sizeof error) && rmc_int("cheats","cheat_exp_mult")==10);
    assert(rmc_int("debug","debug_on")==1 && rmc_int("display","title_logo")==5);
    rewind(a);
    assert(!rmc_config_edit(a,b,"cheats","cheat_exp_mult",4));
    assert(!rmc_config_edit(a,b,"bad","key",4));
    fclose(a);fclose(b);fclose(c);
}

int main(void)
{
    edit_round_trip();
    char error[256];
    assert(rmc_config_load(NULL, error, sizeof error));
    assert(rmc_int("display","title_credit")==1);
    const char show[]="[display]\ntitle_credit=show";
    assert(from_memory(show,sizeof show-1,error) && !rmc_int("display","title_credit"));
    assert(rmc_int("kh2.cache", "size") == 524288);
    assert(rmc_int("worlds", "worldpoint_amount") == 54);
    assert(!rmc_int("antiform", "disable_random_anti"));
    assert(rmc_int("combat", "proud_party_dmg") == 125);
    assert(rmc_value("combat", "CursorRemain", 18) == 18);
    assert(rmc_value("camera", "camera_patches_indoors", 5) == 0.004999999888);

    FILE *preset = fopen("../../dll/rmc.ini", "rb");
    assert(preset && rmc_config_load(preset, error, sizeof error));
    assert(rmc_int("display","title_credit"));
    assert(fclose(preset) == 0);
    const char valid[] = "\xef\xbb\xbf[Combat]\r\n beginner_HP_mult = 3.5\n"
        "begginer_hp_mult=4\n beginner_hp_mult=2\n[worlds]\nsymbol_di=1000, 1,2,3,255\n"
        "[retry]\nretry=TrUe";
    assert(from_memory(valid, sizeof valid - 1, error));
    assert(rmc_value("combat", "beginner_hp_mult", 0) == 3.5);
    assert(rmc_value("worlds", "symbol_di", 4) == 255);
    assert(rmc_int("retry", "retry") == 1);

    const char *bad[] = {
        "[cheats]\ncheat_drop_mult=4", "[cheats]\ncheat_exp_mult=1.5",
        "[forms]\nvalor_growth=0,1,2,3", "[worlds]\nsymbol_di=12,1,2,3,256",
        "[forms]\nvalor_growth=0,1.5,2,3,4", "[forms]\nvalor_growth=0,1,2,3,4,",
        "[display]\ntitle_credit=maybe", "[debug]\nturbo_speed=nan", "[combat]\nblow_gravity=1junk", "[oops]",
        "[debug]\nunknown_key=0", "[retry]\nretry=maybe", "[kh2.cache]\nplayer=-1"
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        assert(!from_memory(bad[i], strlen(bad[i]), error));
        assert(strstr(error, "rmc.ini line"));
        assert(rmc_value("combat", "beginner_hp_mult", 0) == 3.5);
    }
    const char nul[] = "[combat]\nblow_gravity=1\0junk";
    assert(!from_memory(nul, sizeof nul - 1, error));
    assert(strstr(error, "NUL"));
    puts("config: shipped defaults, tuples, legacy keys, predicates and invalid input passed");
    return 0;
}
