// at_parse.c (Plan 10 T2): the modem's replies. make -C stm32/firmware/test/host
#include <stdio.h>
#include <string.h>
#include "at_parse.h"

static int failures;
#define CHECK(cond, ...)                                \
    do {                                                \
        if (!(cond)) {                                  \
            failures++;                                 \
            printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                        \
            printf("\n");                               \
        }                                               \
    } while (0)

static void test_final(void)
{
    CHECK(at_final("OK") == AT_OK, "OK");
    CHECK(at_final("ERROR") == AT_ERROR, "ERROR");
    CHECK(at_final("+CME ERROR: 10") == AT_ERROR && at_final("+CMS ERROR: 500") == AT_ERROR, "CME and CMS errors");
    CHECK(at_final("CONNECT") == AT_CONNECT && at_final("CONNECT 150000000") == AT_CONNECT, "CONNECT, with a rate");
    CHECK(at_final("NO CARRIER") == AT_NO_CARRIER, "NO CARRIER");
    CHECK(at_final("POWERED DOWN") == AT_POWERED_DOWN, "POWERED DOWN");
    CHECK(at_final("+CSQ: 18,99") == AT_NONE && at_final("OKAY") == AT_NONE && at_final("CONNECTED") == AT_NONE,
          "other lines are not final");
}

static void test_identity(void)
{
    char imei[16] = "", iccid[21] = "";
    CHECK(at_imei("861234567890123", imei) && !strcmp(imei, "861234567890123"), "IMEI");
    CHECK(!at_imei("86123456789012", imei) && !at_imei("8612345678901234", imei) && !at_imei("OK", imei), "not 15 digits: no IMEI");
    CHECK(at_iccid("+QCCID: 8984012345678901234F", iccid) && !strcmp(iccid, "8984012345678901234F"), "ICCID with a trailing F (%s)", iccid);
    CHECK(at_iccid("+QCCID: 89840123456789012345", iccid) && strlen(iccid) == 20, "a 20-digit ICCID");
    CHECK(!at_iccid("+CME ERROR: 10", iccid), "no SIM: no ICCID");
    CHECK(at_cpin_ready("+CPIN: READY") && !at_cpin_ready("+CPIN: SIM PIN") && !at_cpin_ready("+CME ERROR: 10"), "CPIN");
}

static void test_signal(void)
{
    int r = -1;
    CHECK(at_csq("+CSQ: 18,99", &r) && r == 18, "CSQ 18");
    CHECK(at_csq("+CSQ: 99,99", &r) && r == 99, "CSQ 99: unknown");
    CHECK(!at_csq("+CSQ:", &r) && !at_csq("OK", &r), "not a CSQ line");
    char t[24];
    CHECK(at_nwinfo("+QNWINFO: \"FDD LTE\",\"45204\",\"LTE BAND 3\",1650", t, sizeof t) && !strcmp(t, "LTE"), "LTE (%s)", t);
    CHECK(at_nwinfo("+QNWINFO: \"GSM\",\"45204\",\"GSM 900\",62", t, sizeof t) && !strcmp(t, "GSM"), "GSM");
    CHECK(at_nwinfo("+QNWINFO: No Service", t, sizeof t) && !strcmp(t, "No Service"), "No Service (%s)", t);
    CHECK(!at_nwinfo("OK", t, sizeof t), "not a QNWINFO line");
}

static void test_lines(void)
{
    at_lines_t l = {0};
    const char *in = "\r\nAT+CSQ\r\r\n+CSQ: 18,99\r\n\r\nOK\r\n";
    const char *got[8];
    int n = 0;
    static char keep[8][128];
    for (const char *p = in; *p; p++) {
        const char *line = at_feed(&l, (uint8_t)*p);
        if (line && n < 8) {
            strcpy(keep[n], line);
            got[n] = keep[n];
            n++;
        }
    }
    CHECK(n == 3 && !strcmp(got[0], "AT+CSQ") && !strcmp(got[1], "+CSQ: 18,99") && !strcmp(got[2], "OK"),
          "lines split by CR and LF, empty ones skipped (%d)", n);
    // a line longer than the buffer: cut, and the next line whole
    at_lines_t m = {0};
    const char *line = NULL;
    for (int i = 0; i < 300; i++)
        at_feed(&m, 'x');
    line = at_feed(&m, '\n');
    CHECK(line && strlen(line) == sizeof m.line - 1, "an over-long line is cut (%zu)", line ? strlen(line) : 0);
    const char *next = NULL;
    for (const char *p = "OK\r\n"; *p; p++) {
        const char *x = at_feed(&m, (uint8_t)*p);
        if (x)
            next = x;
    }
    CHECK(next && !strcmp(next, "OK"), "then the next line whole (%s)", next ? next : "-");
}

int main(void)
{
    test_final();
    test_identity();
    test_signal();
    test_lines();
    printf("test_at_parse: %s\n", failures ? "FAILED" : "ok");
    return failures != 0;
}
