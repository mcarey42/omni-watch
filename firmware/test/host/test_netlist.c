#include "unity.h"
#include "omni/netlist.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

static omni_scan_rec_t rec(const char *name, int rssi, uint8_t auth, uint8_t ch)
{
    omni_scan_rec_t r;
    memset(&r, 0, sizeof r);
    r.ssid_len = (uint8_t)strlen(name);
    memcpy(r.ssid, name, r.ssid_len);
    r.rssi = (int8_t)rssi; r.authmode = auth; r.channel = ch;
    return r;
}

void test_netlist_dedup_strongest(void)
{
    omni_scan_rec_t in[3] = { rec("home", -70, OMNI_WA_WPA2_PSK, 1), rec("other", -80, OMNI_WA_OPEN, 3),
                              rec("home", -50, OMNI_WA_WPA3_PSK, 11) };
    omni_net_row_t out[OMNI_NETLIST_MAX];
    TEST_ASSERT_EQUAL_INT(2, omni_netlist_build(in, 3, NULL, 0, out));
    TEST_ASSERT_EQUAL_INT8(-50, out[0].rec.rssi);
    TEST_ASSERT_EQUAL_UINT8(11, out[0].rec.channel);
    TEST_ASSERT_EQUAL_UINT8(OMNI_WA_WPA3_PSK, out[0].rec.authmode);
    TEST_ASSERT_EQUAL_UINT8(OMNI_AUTH_SAE, out[0].auth);
    TEST_ASSERT_EQUAL_UINT8(4, out[0].bars);
    TEST_ASSERT_EQUAL_INT8(-80, out[1].rec.rssi);
    /* the weaker one second: the strongest stays */
    omni_scan_rec_t in2[2] = { rec("home", -50, OMNI_WA_WPA2_PSK, 6), rec("home", -70, OMNI_WA_OPEN, 1) };
    TEST_ASSERT_EQUAL_INT(1, omni_netlist_build(in2, 2, NULL, 0, out));
    TEST_ASSERT_EQUAL_UINT8(6, out[0].rec.channel);
}

void test_netlist_empty_dropped(void)
{
    omni_scan_rec_t in[2] = { rec("", -40, OMNI_WA_OPEN, 1), rec("x", -60, OMNI_WA_OPEN, 2) };
    omni_net_row_t out[OMNI_NETLIST_MAX];
    TEST_ASSERT_EQUAL_INT(1, omni_netlist_build(in, 2, NULL, 0, out));
    TEST_ASSERT_EQUAL_UINT8(1, out[0].rec.ssid_len);
    TEST_ASSERT_EQUAL_INT(0, omni_netlist_build(in, 0, NULL, 0, out));
}

void test_netlist_sort_stable(void)
{
    omni_scan_rec_t in[5] = { rec("a", -60, 0, 1), rec("b", -50, 0, 2), rec("c", -60, 0, 3), rec("d", -60, 0, 4),
                              rec("e", -50, 0, 5) };
    omni_net_row_t out[OMNI_NETLIST_MAX];
    TEST_ASSERT_EQUAL_INT(5, omni_netlist_build(in, 5, NULL, 0, out));
    const char *want = "beacd";
    for (int i = 0; i < 5; i++) TEST_ASSERT_EQUAL_UINT8((uint8_t)want[i], out[i].rec.ssid[0]);
}

void test_netlist_auth_classes(void)
{
    static const uint8_t want[] = {
        [OMNI_WA_OPEN] = OMNI_AUTH_OPEN, [OMNI_WA_WEP] = OMNI_AUTH_UNSUPPORTED, [OMNI_WA_WPA_PSK] = OMNI_AUTH_PSK,
        [OMNI_WA_WPA2_PSK] = OMNI_AUTH_PSK, [OMNI_WA_WPA_WPA2_PSK] = OMNI_AUTH_PSK,
        [OMNI_WA_ENTERPRISE] = OMNI_AUTH_UNSUPPORTED, [OMNI_WA_WPA3_PSK] = OMNI_AUTH_SAE,
        [OMNI_WA_WPA2_WPA3_PSK] = OMNI_AUTH_SAE, [OMNI_WA_WAPI_PSK] = OMNI_AUTH_UNSUPPORTED,
        [OMNI_WA_OWE] = OMNI_AUTH_UNSUPPORTED, [OMNI_WA_WPA3_ENT_192] = OMNI_AUTH_UNSUPPORTED,
        [OMNI_WA_WPA3_EXT_PSK] = OMNI_AUTH_SAE, [OMNI_WA_WPA3_EXT_PSK_MIXED] = OMNI_AUTH_SAE,
        [OMNI_WA_DPP] = OMNI_AUTH_UNSUPPORTED, [OMNI_WA_WPA3_ENTERPRISE] = OMNI_AUTH_UNSUPPORTED,
        [OMNI_WA_WPA2_WPA3_ENTERPRISE] = OMNI_AUTH_UNSUPPORTED, [OMNI_WA_WPA_ENTERPRISE] = OMNI_AUTH_UNSUPPORTED,
        [OMNI_WA_UNKNOWN] = OMNI_AUTH_UNSUPPORTED };
    TEST_ASSERT_EQUAL_INT(18, (int)(sizeof want / sizeof want[0]));
    for (unsigned m = 0; m < sizeof want; m++) TEST_ASSERT_EQUAL_INT_MESSAGE(want[m], omni_netlist_auth((uint8_t)m), "mode");
    TEST_ASSERT_EQUAL_INT(OMNI_AUTH_UNSUPPORTED, omni_netlist_auth(OMNI_WA_UNKNOWN + 1));
    TEST_ASSERT_EQUAL_INT(OMNI_AUTH_UNSUPPORTED, omni_netlist_auth(255));
    /* the raw values mirror wifi_auth_mode_t (ESP-IDF v5.5.6): spot-pin the ends and the later additions */
    TEST_ASSERT_EQUAL_INT(5, OMNI_WA_ENTERPRISE);
    TEST_ASSERT_EQUAL_INT(11, OMNI_WA_WPA3_EXT_PSK);
    TEST_ASSERT_EQUAL_INT(16, OMNI_WA_WPA_ENTERPRISE);
    TEST_ASSERT_EQUAL_INT(17, OMNI_WA_UNKNOWN);
}

void test_netlist_bars(void)
{
    TEST_ASSERT_EQUAL_UINT8(4, omni_netlist_bars(-55));
    TEST_ASSERT_EQUAL_UINT8(4, omni_netlist_bars(0));
    TEST_ASSERT_EQUAL_UINT8(3, omni_netlist_bars(-56));
    TEST_ASSERT_EQUAL_UINT8(3, omni_netlist_bars(-65));
    TEST_ASSERT_EQUAL_UINT8(2, omni_netlist_bars(-66));
    TEST_ASSERT_EQUAL_UINT8(2, omni_netlist_bars(-75));
    TEST_ASSERT_EQUAL_UINT8(1, omni_netlist_bars(-76));
    TEST_ASSERT_EQUAL_UINT8(1, omni_netlist_bars(-85));
    TEST_ASSERT_EQUAL_UINT8(0, omni_netlist_bars(-86));
    TEST_ASSERT_EQUAL_UINT8(0, omni_netlist_bars(-128));
}

void test_netlist_non_ascii_exact(void)
{
    const uint8_t name[6] = { 0xC3, 0xA9, 'c', 'o', 'l', 'e' };
    omni_scan_rec_t r;
    memset(&r, 0xEE, sizeof r);                 /* the bytes past the length are not part of the name */
    memcpy(r.ssid, name, 6); r.ssid_len = 6; r.rssi = -60; r.authmode = OMNI_WA_WPA2_PSK; r.channel = 6;
    omni_net_row_t out[OMNI_NETLIST_MAX];
    TEST_ASSERT_EQUAL_INT(1, omni_netlist_build(&r, 1, name, 6, out));
    TEST_ASSERT_TRUE(out[0].saved);
    TEST_ASSERT_EQUAL_UINT8(6, out[0].rec.ssid_len);
    TEST_ASSERT_EQUAL_MEMORY(name, out[0].rec.ssid, 6);
    uint8_t other[6]; memcpy(other, name, 6); other[1] = 0xA8;
    TEST_ASSERT_EQUAL_INT(1, omni_netlist_build(&r, 1, other, 6, out));
    TEST_ASSERT_FALSE(out[0].saved);
    /* a prefix of the name, and the name plus a byte, are not it either; saved_len 0 and NULL mark nothing */
    TEST_ASSERT_EQUAL_INT(1, omni_netlist_build(&r, 1, name, 5, out));
    TEST_ASSERT_FALSE(out[0].saved);
    uint8_t longer[7]; memcpy(longer, name, 6); longer[6] = 0xEE;
    TEST_ASSERT_EQUAL_INT(1, omni_netlist_build(&r, 1, longer, 7, out));
    TEST_ASSERT_FALSE(out[0].saved);
    TEST_ASSERT_EQUAL_INT(1, omni_netlist_build(&r, 1, name, 0, out));
    TEST_ASSERT_FALSE(out[0].saved);
    TEST_ASSERT_EQUAL_INT(1, omni_netlist_build(&r, 1, NULL, 6, out));
    TEST_ASSERT_FALSE(out[0].saved);
    /* two names that differ only past an embedded NUL are two rows */
    omni_scan_rec_t z[2];
    memset(z, 0, sizeof z);
    z[0].ssid[0] = 'a'; z[0].ssid[2] = 'x'; z[0].ssid_len = 3; z[0].rssi = -50;
    z[1].ssid[0] = 'a'; z[1].ssid[2] = 'y'; z[1].ssid_len = 3; z[1].rssi = -51;
    TEST_ASSERT_EQUAL_INT(2, omni_netlist_build(z, 2, NULL, 0, out));
}

void test_netlist_cap_16(void)
{
    omni_scan_rec_t in[20];
    for (int i = 0; i < 20; i++) {
        char nm[4] = { 'n', (char)('a' + i), 0, 0 };
        in[i] = rec(nm, -40 - 2 * i, OMNI_WA_WPA2_PSK, (uint8_t)(i + 1));    /* i 0 strongest */
    }
    omni_net_row_t out[OMNI_NETLIST_MAX];
    TEST_ASSERT_EQUAL_INT(16, omni_netlist_build(in, 20, NULL, 0, out));
    for (int i = 0; i < 16; i++) TEST_ASSERT_EQUAL_INT8(-40 - 2 * i, out[i].rec.rssi);
    /* the strongest arrive last: still the 16 strongest, sorted */
    omni_scan_rec_t rv[20];
    for (int i = 0; i < 20; i++) rv[i] = in[19 - i];
    TEST_ASSERT_EQUAL_INT(16, omni_netlist_build(rv, 20, NULL, 0, out));
    for (int i = 0; i < 16; i++) TEST_ASSERT_EQUAL_INT8(-40 - 2 * i, out[i].rec.rssi);
    /* an evicted name that returns stronger is kept once */
    omni_scan_rec_t ev[22];
    memcpy(ev, in, sizeof in);
    ev[20] = rec("zz", -90, 0, 1); ev[21] = rec("zz", -41, 0, 2);
    TEST_ASSERT_EQUAL_INT(16, omni_netlist_build(ev, 22, NULL, 0, out));
    TEST_ASSERT_EQUAL_INT8(-40, out[0].rec.rssi);
    TEST_ASSERT_EQUAL_INT8(-41, out[1].rec.rssi);
    TEST_ASSERT_EQUAL_UINT8(2, out[1].rec.channel);
}

/* Task 6's parked cases (Task 11): a record whose ssid_len is past the 32-byte field is dropped, never read past the
 * field, and the rest are kept */
void test_netlist_long_ssid_len_dropped(void)
{
    omni_scan_rec_t in[4] = { rec("ok", -50, OMNI_WA_WPA2_PSK, 1), rec("x", -40, OMNI_WA_OPEN, 2),
                              rec("y", -45, OMNI_WA_OPEN, 3), rec("z", -60, OMNI_WA_OPEN, 4) };
    memset(in[1].ssid, 'A', sizeof in[1].ssid);
    in[1].ssid_len = 33;
    in[2].ssid_len = 255;
    omni_net_row_t out[OMNI_NETLIST_MAX];
    TEST_ASSERT_EQUAL_INT(2, omni_netlist_build(in, 4, NULL, 0, out));
    TEST_ASSERT_EQUAL_UINT8(2, out[0].rec.ssid_len);
    TEST_ASSERT_EQUAL_MEMORY("ok", out[0].rec.ssid, 2);
    TEST_ASSERT_EQUAL_MEMORY("z", out[1].rec.ssid, 1);
    /* exactly 32 is a name */
    memset(in[1].ssid, 'A', sizeof in[1].ssid);
    in[1].ssid_len = 32;
    TEST_ASSERT_EQUAL_INT(2, omni_netlist_build(in, 2, NULL, 0, out));
    TEST_ASSERT_EQUAL_UINT8(32, out[0].rec.ssid_len);
}

/* a saved name longer than 32 bytes marks nothing saved (no row can be that long), even when its first 32 bytes are a
 * row's name; the comparison stops at the length, so only saved_len bytes are ever read from saved */
void test_netlist_long_saved_len_marks_nothing(void)
{
    omni_scan_rec_t in[2];
    in[0] = rec("", -50, OMNI_WA_WPA2_PSK, 1);
    memset(in[0].ssid, 'S', 32);
    in[0].ssid_len = 32;
    in[1] = rec("other", -60, OMNI_WA_WPA2_PSK, 6);
    uint8_t saved[255];
    memset(saved, 'S', sizeof saved);
    omni_net_row_t out[OMNI_NETLIST_MAX];
    TEST_ASSERT_EQUAL_INT(2, omni_netlist_build(in, 2, saved, 33, out));
    TEST_ASSERT_FALSE(out[0].saved);
    TEST_ASSERT_FALSE(out[1].saved);
    TEST_ASSERT_EQUAL_INT(2, omni_netlist_build(in, 2, saved, 255, out));
    TEST_ASSERT_FALSE(out[0].saved);
    TEST_ASSERT_FALSE(out[1].saved);
    /* and the same 32 bytes as the saved name do mark it */
    TEST_ASSERT_EQUAL_INT(2, omni_netlist_build(in, 2, saved, 32, out));
    TEST_ASSERT_TRUE(out[0].saved);
    TEST_ASSERT_FALSE(out[1].saved);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_netlist_dedup_strongest);
    RUN_TEST(test_netlist_empty_dropped);
    RUN_TEST(test_netlist_sort_stable);
    RUN_TEST(test_netlist_auth_classes);
    RUN_TEST(test_netlist_bars);
    RUN_TEST(test_netlist_non_ascii_exact);
    RUN_TEST(test_netlist_cap_16);
    RUN_TEST(test_netlist_long_ssid_len_dropped);
    RUN_TEST(test_netlist_long_saved_len_marks_nothing);
    return UNITY_END();
}
