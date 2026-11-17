/* bgpack: the three brass backgrounds (R112) as LVGL v9 RGB565 images for the assets partition, rendered by omni_core's
 * omni_bg_render (omni/brassbg.h): <out_dir>/bg_bezel.bin, bg_picker.bin and bg_settings.bin. Each is the 12-byte
 * header tools/assets.sh writes for an opaque RGB565 image (magic 0x19, cf 0x12, 466 x 466, stride 932), then
 * 466 x 466 little-endian RGB565 pixels. Prints one line per file: "<file> <bytes> crc32 <pixel CRC-32, zlib's>".
 *
 *   bgpack <out_dir>
 *
 * Built by tools/bgpack/CMakeLists.txt. tools/assets.sh step 3d runs it and compares its output with the committed
 * files (a difference is a warning: the bytes depend on libm and FMA contraction, R115); --bg copies it over them. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "omni/brassbg.h"

static const char *const NAMES[OMNI_BG__COUNT] = {"bg_bezel.bin", "bg_picker.bin", "bg_settings.bin"};

static uint32_t crc32(const uint8_t *p, size_t n) /* zlib's CRC-32 (= esp_rom_crc32_le(0, ...)) */
{
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
    }
    return ~c;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: bgpack <out_dir>\n");
        return 2;
    }
    const size_t npx = (size_t)OMNI_BG_W * OMNI_BG_W;
    uint16_t *px = malloc(npx * 2);
    uint8_t *strip = malloc((size_t)OMNI_BG_W * 4 * OMNI_BG_STRIP);
    if (px == NULL || strip == NULL) return 1;
    /* magic, cf RGB565, flags 0, w, h, stride, reserved: tools/assets.sh's '<BBHHHHH' */
    const uint8_t hdr[12] = {0x19, 0x12, 0, 0, OMNI_BG_W & 0xff, OMNI_BG_W >> 8, OMNI_BG_W & 0xff, OMNI_BG_W >> 8,
                             (OMNI_BG_W * 2) & 0xff, (OMNI_BG_W * 2) >> 8, 0, 0};
    for (int i = 0; i < OMNI_BG__COUNT; i++) {
        omni_bg_render((omni_bg_t)i, px, strip, NULL, NULL);
        uint8_t *bytes = malloc(npx * 2);
        if (bytes == NULL) return 1;
        for (size_t k = 0; k < npx; k++) bytes[2 * k] = (uint8_t)px[k], bytes[2 * k + 1] = (uint8_t)(px[k] >> 8);
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", argv[1], NAMES[i]);
        FILE *f = fopen(path, "wb");
        if (f == NULL || fwrite(hdr, 1, sizeof hdr, f) != sizeof hdr || fwrite(bytes, 1, npx * 2, f) != npx * 2 ||
            fclose(f) != 0) {
            fprintf(stderr, "bgpack: cannot write %s\n", path);
            return 1;
        }
        printf("%s %zu crc32 %08x\n", NAMES[i], sizeof hdr + npx * 2, (unsigned)crc32(bytes, npx * 2));
        free(bytes);
    }
    free(strip);
    free(px);
    return 0;
}
