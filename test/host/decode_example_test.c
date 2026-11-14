/* host test for examples/decode_frame: output of decode_frames() */
#define DECODE_FRAME_HOST_TEST 1
#include "../../examples/decode_frame/main/decode_frame.c"

static int checks, failed;

#define CHECK(cond) do { \
    checks++; \
    if (!(cond)) { failed++; printf("FAIL line %d: %s\n", __LINE__, #cond); } \
} while (0)

static char *
run_list(const char *list, int *rc)
{
    char *buf = NULL;
    size_t sz = 0;
    FILE *f = open_memstream(&buf, &sz);

    *rc = decode_frames(f, list);
    fclose(f);
    return buf;
}

int
main(void)
{
    int rc;
    char *s;

    /* header frame, id 24083345, one volume record */
    s = run_list("68 15 15 68 08 00 72 45 33 08 24 C5 14 00 04 01 00 00 00 "
                 "04 13 78 56 34 12 27 16", &rc);
    CHECK(rc == 1);
    CHECK(strstr(s, "id=24083345 mfr=EFE medium=heat (outlet)") != NULL);
    CHECK(strstr(s, "volume") != NULL && strstr(s, "305419.896 m^3") != NULL);
    CHECK(strstr(s, "1 of 1 telegram(s) decoded, 1 record(s)") != NULL);
    free(s);

    /* two telegrams, ';' separated, spaces and lower case */
    s = run_list("68 09 09 68 08 01 78 04 13 78 56 34 12 ac 16;"
                 "68 0d 0d 68 08 01 7a 05 00 00 00 04 13 78 56 34 12 b3 16", &rc);
    CHECK(rc == 2);
    CHECK(strstr(s, "2 of 2 telegram(s) decoded, 2 record(s)") != NULL);
    free(s);

    /* bad checksum, garbage, empty */
    s = run_list("68 15 15 68 08 00 72 45 33 08 24 C5 14 00 04 01 00 00 00 "
                 "04 13 78 56 34 12 28 16", &rc);
    CHECK(rc == -1);
    CHECK(strstr(s, "frame 1: not a valid M-Bus telegram") != NULL);
    free(s);

    s = run_list("zz", &rc);
    CHECK(rc == -1);
    free(s);

    s = run_list("", &rc);
    CHECK(rc == -1);
    CHECK(strstr(s, "0 of 0 telegram(s) decoded") != NULL);
    free(s);

    /* one good, one bad: decoded count shows it */
    s = run_list("68 09 09 68 08 01 78 04 13 78 56 34 12 ac 16;00", &rc);
    CHECK(rc == 1);
    CHECK(strstr(s, "1 of 2 telegram(s) decoded") != NULL);
    free(s);

    printf("%d checks passed, %d failed\n", checks - failed, failed);
    return failed ? 1 : 0;
}
