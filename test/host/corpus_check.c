/* host check: parse + decode every .hex frame in a directory */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include "mbus-protocol.h"

static size_t hex2bin(unsigned char *dst, size_t max, const char *s)
{
    size_t n = 0;
    unsigned v;
    int pos;

    while (n < max && sscanf(s, " %2x%n", &v, &pos) == 1) {
        dst[n++] = (unsigned char)v;
        s += pos;
    }
    return n;
}

static int check_file(const char *path, int *nrec)
{
    static char txt[2048];
    unsigned char bin[300];
    mbus_frame fr;
    mbus_frame_data d;
    mbus_data_record *r;
    FILE *f = fopen(path, "r");
    size_t n;

    if (!f) return -1;
    n = fread(txt, 1, sizeof(txt) - 1, f);
    fclose(f);
    txt[n] = 0;

    n = hex2bin(bin, sizeof(bin), txt);
    memset(&fr, 0, sizeof(fr));
    memset(&d, 0, sizeof(d));

    if (mbus_parse(&fr, bin, n) != 0) return -2;
    if (mbus_frame_data_parse(&fr, &d) != 0) return -3;

    *nrec = 0;
    if (d.type == MBUS_DATA_TYPE_VARIABLE) {
        for (r = d.data_var.record; r; r = r->next) {
            /* exercise the decoders; output not compared here */
            (void)mbus_data_record_value(r);
            (void)mbus_data_record_unit(r);
            (*nrec)++;
        }
        mbus_data_record_free(d.data_var.record);
    }
    return 0;
}

int main(int argc, char **argv)
{
    DIR *dir;
    struct dirent *e;
    char path[512];
    int ok = 0, bad = 0, nrec;

    if (argc < 2) { printf("usage: %s <dir>\n", argv[0]); return 2; }
    if (!(dir = opendir(argv[1]))) { printf("cannot open %s\n", argv[1]); return 2; }

    while ((e = readdir(dir))) {
        size_t l = strlen(e->d_name);
        int r;
        if (l < 5 || strcmp(e->d_name + l - 4, ".hex")) continue;
        snprintf(path, sizeof(path), "%s/%s", argv[1], e->d_name);
        r = check_file(path, &nrec);
        if (r == 0) { ok++; }
        else { bad++; printf("FAIL %d  %s\n", r, e->d_name); }
    }
    closedir(dir);
    printf("%d ok, %d failed\n", ok, bad);
    return bad ? 1 : 0;
}