#include "platform/config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); return 1; } } while (0)

int main(void) {
    const char *ini = "test_config.ini";
    FILE *f = fopen(ini, "w");
    fputs("# comment\ndisc1 = \"C:\\games\\a b\\disc1.cue\"\ndisc2=D:\\disc2.cue\nsave_dir = S:\\sv\nbogus = 1\n", f);
    fclose(f);

    _putenv("MB_DISC1=");
    _putenv("MB_DISC2=");
    _putenv("MB_SAVE_DIR=");
    _putenv("MB_CONFIG=");
    mb_config c;
    CHECK(config_load(&c, ini) == 0);
    CHECK(!strcmp(c.disc[0], "C:\\games\\a b\\disc1.cue"));   /* quotes stripped, spaces kept */
    CHECK(!strcmp(c.disc[1], "D:\\disc2.cue"));
    CHECK(!strcmp(c.save_dir, "S:\\sv"));
    CHECK(strstr(c.data_dir, "data") != NULL);                  /* default kept */

    _putenv("MB_DISC2=E:\\override.cue");                       /* env beats ini */
    CHECK(config_load(&c, ini) == 0);
    CHECK(!strcmp(c.disc[1], "E:\\override.cue"));
    _putenv("MB_DISC2=");

    CHECK(config_load(&c, "does_not_exist.ini") == -1);         /* defaults when file missing */
    CHECK(c.disc[0][0] == 0);

    c.disc[0][0] = 0;
    snprintf(c.disc[0], sizeof c.disc[0], "X:\\one.cue");
    CHECK(config_save_ini(&c, "test_config_out.ini") == 0);
    mb_config d;
    CHECK(config_load(&d, "test_config_out.ini") == 0);
    CHECK(!strcmp(d.disc[0], "X:\\one.cue"));

    /* Host menu options: defaults, parsing, clamping, round trip. */
    CHECK(d.music_on == 1 && d.sfx_on == 1 && d.volume == MB_CONFIG_VOLUME_MAX); /* via config_save_ini */
    CHECK(config_load(&c, "does_not_exist.ini") == -1);
    CHECK(c.music_on == 1 && c.sfx_on == 1 && c.volume == MB_CONFIG_VOLUME_MAX);
    c.music_on = 0; c.sfx_on = 0; c.volume = 99;
    CHECK(config_save_ini(&c, "test_config_out.ini") == 0);
    CHECK(config_load(&d, "test_config_out.ini") == 0);
    CHECK(d.music_on == 0 && d.sfx_on == 0 && d.volume == MB_CONFIG_VOLUME_MAX);
    {
        static const struct { const char *text; int music, sfx; unsigned vol; } cases[] = {
            {"music = off\nsfx = OFF\nvolume = 7\n", 0, 0, 7u},
            {"music=0\nsfx=false\nvolume=0\n", 0, 0, 0u},
            {"music = no\nsfx = On\nvolume = 15\n", 0, 1, 15u},
            {"music = maybe\nsfx = 2\nvolume = loud\n", 1, 1, 15u}, /* junk keeps defaults */
            {"volume = 16\n", 1, 1, 15u},                           /* clamps */
            {"volume = 99999999999999999999\n", 1, 1, 15u},         /* no overflow */
            {"volume = 3\nvolume = -1\nvolume = 3x\nvolume =\n", 1, 1, 3u},
            {"volume = 4\nvolume = 9\n", 1, 1, 9u},                 /* last one wins */
        };
        unsigned i;
        for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
            f = fopen(ini, "w");
            fputs(cases[i].text, f);
            fclose(f);
            CHECK(config_load(&c, ini) == 0);
            CHECK(c.music_on == cases[i].music && c.sfx_on == cases[i].sfx &&
                  c.volume == cases[i].vol);
        }
    }

    /* config_save_options rewrites only its three keys. */
    {
        const char *opt = "test_config_opt.ini";
        char buf[512];
        size_t n;
        remove(opt);
        CHECK(config_save_options(opt, 0, 1, 5) == 0); /* creates the file */
        CHECK(config_load(&c, opt) == 0);
        CHECK(c.music_on == 0 && c.sfx_on == 1 && c.volume == 5u);
        CHECK(c.disc[0][0] == 0);

        f = fopen(opt, "wb");
        fputs("# keep me\r\ndisc1 = Q:\\a b.cue\r\nvolume = 2\r\nbogus = 1\r\nvolume = 3\r\nsave_dir = S:\\sv", f);
        fclose(f);
        _putenv("MB_DISC1=E:\\from_env.cue"); /* an environment path must not be frozen into the file */
        CHECK(config_load(&c, opt) == 0);
        CHECK(!strcmp(c.disc[0], "E:\\from_env.cue"));
        CHECK(config_save_options(opt, 1, 0, 200) == 0);
        _putenv("MB_DISC1=");
        f = fopen(opt, "rb");
        n = fread(buf, 1, sizeof buf - 1, f);
        buf[n] = 0;
        fclose(f);
        CHECK(!strcmp(buf, "# keep me\r\ndisc1 = Q:\\a b.cue\r\nvolume = 15\r\nbogus = 1\r\n"
                           "save_dir = S:\\sv\nmusic = on\nsfx = off\n"));
        CHECK(config_load(&c, opt) == 0);
        CHECK(!strcmp(c.disc[0], "Q:\\a b.cue") && !strcmp(c.save_dir, "S:\\sv"));
        CHECK(c.music_on == 1 && c.sfx_on == 0 && c.volume == 15u);
        /* "volumex" and "music_dir" are not option keys */
        f = fopen(opt, "wb");
        fputs("volumex = 1\nmusic_dir = M\n", f);
        fclose(f);
        CHECK(config_save_options(opt, 0, 0, 1) == 0);
        f = fopen(opt, "rb");
        n = fread(buf, 1, sizeof buf - 1, f);
        buf[n] = 0;
        fclose(f);
        CHECK(!strcmp(buf, "volumex = 1\nmusic_dir = M\nmusic = off\nsfx = off\nvolume = 1\n"));
        CHECK(config_save_options("no_such_dir_xyz\\o.ini", 1, 1, 1) == -1);
        CHECK(config_save_options("", 1, 1, 1) == -1 && config_save_options(NULL, 1, 1, 1) == -1);
        remove(opt);
    }

    remove(ini);
    remove("test_config_out.ini");
    printf("config: all checks pass\n");
    return 0;
}
