#include "boot_brand_policy.h"
#include "boot_brand_decode.h"
#include "boot_brand_timing.h"
#include "boot_brand_image.h"
#include <stdio.h>
#include <string.h>
#define CHECK(e) do { if (!(e)) { fprintf(stderr, "Failed: %s\n", #e); return 1; } } while (0)
int main(void) {
    unsigned int i, y;
    uint8_t arguments[128] = "ordinary boot arguments";
    uint8_t saved[128];
    uint16_t row[MIPSYNC_BOOT_BRAND_WIDTH];
    ps1_boot_brand_decoder decoder = {0, 0, 0};
    const unsigned int count = sizeof(k_mipsync_boot_brand_runs) / sizeof(k_mipsync_boot_brand_runs[0]);
    unsigned int bright = 0, colored = 0;
    memcpy(saved, arguments, sizeof(arguments));
    CHECK(!ps1_boot_handoff_consume(arguments));
    CHECK(!memcmp(saved, arguments, sizeof(arguments)));
    ps1_boot_handoff_stamp(arguments);
    CHECK(ps1_boot_handoff_consume(arguments));
    CHECK(!ps1_boot_handoff_consume(arguments)); /* one-shot, even when returning to scene zero */
    ps1_boot_handoff_stamp(arguments);
    ps1_boot_handoff_clear(arguments); /* failed LoadExec */
    CHECK(!ps1_boot_handoff_consume(arguments));
    for (y=0;y<MIPSYNC_BOOT_BRAND_HEIGHT;y++) {
        CHECK(ps1_boot_brand_decode_row(&decoder, k_mipsync_boot_brand_runs, count, row, MIPSYNC_BOOT_BRAND_WIDTH));
        for(i=0;i<MIPSYNC_BOOT_BRAND_WIDTH;i++) {
            if (row[i] == 0x7bde) ++bright; /* wordmark retains its brightness */
            if ((row[i]&31) != ((row[i]>>5)&31)) ++colored;
        }
    }
    CHECK(bright > 50 && colored > 200);
    CHECK(decoder.run == count && !decoder.remaining);
    CHECK(!ps1_boot_brand_decode_row(&decoder, k_mipsync_boot_brand_runs, count, row, 1));
    for (i=0;i<2;i++) {
        int n = i ? 75 : 90, field;
        CHECK(ps1_boot_brand_brightness(0,n) == 0);
        CHECK(ps1_boot_brand_brightness(n-1,n) == 0);
        CHECK(ps1_boot_brand_brightness(n/2,n) == 128);
        for(field=0;field<n;field++) {
            int level=ps1_boot_brand_brightness(field,n);
            CHECK(level>=0 && level<=128);
            CHECK(level == ps1_boot_brand_brightness(n-1-field,n));
        }
    }
    puts("Boot path classification, one-shot handoff, logo decode and fade passed.");
    return 0;
}
