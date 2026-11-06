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

    remove(ini);
    remove("test_config_out.ini");
    printf("config: all checks pass\n");
    return 0;
}
