/*
 * tests/test_mod_table.c -- a mod's own data tables (sdk/mod_table.h).
 *
 * Writes table files into the working directory and reads them back: the
 * file format (comments, hex, negative numbers, the default line, a later
 * line winning), the per-mod namespaces and the refusals.
 */

#include <stdio.h>
#include <stdlib.h>

#include "mod_table.h"
#include "port_test.h"

static void write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");

    if (f != NULL) {
        fputs(text, f);
        fclose(f);
    }
}

int main(void)
{
    s32 v = 0;

    write_file("tbl_caps.txt",
               "# level caps per badge count\n"
               "0 = 12\n"
               "1 = 20   # after the first gym\n"
               "0x2 = 25\n"
               "-3 = -7\n"
               "1 = 21\n"
               "default = 100\n");
    write_file("tbl_bad.txt", "1 = twelve\n");
    write_file("tbl_other.txt", "1 = 5\n");

    printf("loading\n");
    BW_CHECK_EQ("a good table loads",
                (long)bw_mod_table_load("modA", "caps", "tbl_caps.txt", "A"), 0);
    BW_CHECK_EQ("a bad value is refused",
                (long)bw_mod_table_load("modA", "bad", "tbl_bad.txt", "A"), -1);
    BW_CHECK_EQ("a missing file is refused",
                (long)bw_mod_table_load("modA", "gone", "tbl_none.txt", "A"), -1);
    BW_CHECK_EQ("another mod's table of the same name loads",
                (long)bw_mod_table_load("modB", "caps", "tbl_other.txt", "B"), 0);

    printf("reading\n");
    BW_CHECK_EQ("a key is found", (long)bw_mod_table_get("modA", "caps", 0, &v), 0);
    BW_CHECK_EQ("its value", (long)v, 12);
    bw_mod_table_get("modA", "caps", 1, &v);
    BW_CHECK_EQ("the later line wins", (long)v, 21);
    bw_mod_table_get("modA", "caps", 2, &v);
    BW_CHECK_EQ("a hex key", (long)v, 25);
    bw_mod_table_get("modA", "caps", -3, &v);
    BW_CHECK_EQ("a negative key and value", (long)v, -7);
    BW_CHECK_EQ("a missing key reads the default",
                (long)bw_mod_table_get("modA", "caps", 9, &v), 1);
    BW_CHECK_EQ("the default", (long)v, 100);
    bw_mod_table_get("modB", "caps", 1, &v);
    BW_CHECK_EQ("each mod reads its own", (long)v, 5);
    bw_mod_table_get("modB", "caps", 9, &v);
    BW_CHECK_EQ("no default line reads 0", (long)v, 0);
    BW_CHECK_EQ("a table the mod does not have",
                (long)bw_mod_table_get("modA", "nope", 1, &v), -1);
    BW_CHECK_EQ("a refused table is not there",
                (long)bw_mod_table_exists("modA", "bad"), 0);

    printf("reset\n");
    bw_mod_table_reset();
    BW_CHECK_EQ("the reset drops every table",
                (long)bw_mod_table_exists("modA", "caps"), 0);

    remove("tbl_caps.txt");
    remove("tbl_bad.txt");
    remove("tbl_other.txt");
    BW_REPORT("test_mod_table");
}
