#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <string.h>

/* Generates a crisp 512x512 RGBA icon for DiskDude (squircle dark card with a 4-segment
 * sunburst ring and a 3x2 mini treemap below) and writes both a valid PNG and a Windows .ico file. */

static uint32_t crc_table[256];
static int crc_init = 0;

static void make_crc_table(void) {
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) {
            c = (c & 1) ? (0xEDB88320U ^ (c >> 1)) : (c >> 1);
        }
        crc_table[n] = c;
    }
    crc_init = 1;
}

static uint32_t update_crc(uint32_t crc, const uint8_t *buf, size_t len) {
    if (!crc_init) make_crc_table();
    uint32_t c = crc;
    for (size_t n = 0; n < len; n++) {
        c = crc_table[(c ^ buf[n]) & 0xFF] ^ (c >> 8);
    }
    return c;
}

static void write_be32(FILE *fp, uint32_t v) {
    uint8_t b[4] = { (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v };
    fwrite(b, 1, 4, fp);
}

static void write_chunk(FILE *fp, const char type[4], const uint8_t *data, uint32_t len) {
    write_be32(fp, len);
    fwrite(type, 1, 4, fp);
    if (len > 0) fwrite(data, 1, len, fp);
    uint32_t crc = 0xFFFFFFFFU;
    crc = update_crc(crc, (const uint8_t *)type, 4);
    if (len > 0) crc = update_crc(crc, data, len);
    write_be32(fp, crc ^ 0xFFFFFFFFU);
}

static int write_uncompressed_png(const char *path, const uint8_t *rgba, int w, int h) {
    FILE *fp = fopen(path, "wb");
    if (!fp) return -1;
    const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    fwrite(sig, 1, 8, fp);

    uint8_t ihdr[13] = {
        (uint8_t)(w >> 24), (uint8_t)(w >> 16), (uint8_t)(w >> 8), (uint8_t)w,
        (uint8_t)(h >> 24), (uint8_t)(h >> 16), (uint8_t)(h >> 8), (uint8_t)h,
        8, 6, 0, 0, 0
    };
    write_chunk(fp, "IHDR", ihdr, 13);

    size_t row_bytes = (size_t)w * 4 + 1;
    size_t raw_size = row_bytes * (size_t)h;
    size_t blocks = (raw_size + 65534) / 65535;
    size_t z_size = 2 + raw_size + blocks * 5 + 4;
    uint8_t *zbuf = (uint8_t *)malloc(z_size);
    if (!zbuf) { fclose(fp); return -1; }

    size_t pos = 0;
    zbuf[pos++] = 0x78;
    zbuf[pos++] = 0x01;

    uint32_t s1 = 1, s2 = 0;
    size_t rem = raw_size;
    size_t src_idx = 0;
    while (rem > 0) {
        uint16_t blen = (rem > 65535) ? 65535 : (uint16_t)rem;
        uint8_t final_block = (rem <= 65535) ? 1 : 0;
        zbuf[pos++] = final_block;
        zbuf[pos++] = (uint8_t)(blen & 0xFF);
        zbuf[pos++] = (uint8_t)(blen >> 8);
        uint16_t nlen = ~blen;
        zbuf[pos++] = (uint8_t)(nlen & 0xFF);
        zbuf[pos++] = (uint8_t)(nlen >> 8);

        for (uint16_t i = 0; i < blen; i++) {
            uint8_t byte = 0;
            size_t r = src_idx / row_bytes;
            size_t c = src_idx % row_bytes;
            if (c == 0) {
                byte = 0;
            } else {
                byte = rgba[r * (size_t)w * 4 + (c - 1)];
            }
            zbuf[pos++] = byte;
            s1 = (s1 + byte) % 65521;
            s2 = (s2 + s1) % 65521;
            src_idx++;
        }
        rem -= blen;
    }
    uint32_t adler = (s2 << 16) | s1;
    zbuf[pos++] = (uint8_t)(adler >> 24);
    zbuf[pos++] = (uint8_t)(adler >> 16);
    zbuf[pos++] = (uint8_t)(adler >> 8);
    zbuf[pos++] = (uint8_t)adler;

    write_chunk(fp, "IDAT", zbuf, (uint32_t)pos);
    free(zbuf);
    write_chunk(fp, "IEND", NULL, 0);
    fclose(fp);
    return 0;
}

static void render_icon(uint8_t *rgba, int size) {
    double s = (double)size / 512.0;
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            double px = (x + 0.5) / s;
            double py = (y + 0.5) / s;
            uint8_t r = 0, g = 0, b = 0, a = 0;

            /* Rounded squircle bounds [24..488] with corner radius 96 */
            double cx = 256.0, cy = 256.0;
            double dx = fabs(px - cx) - (232.0 - 96.0);
            double dy = fabs(py - cy) - (232.0 - 96.0);
            double dist = sqrt(fmax(dx, 0.0) * fmax(dx, 0.0) + fmax(dy, 0.0) * fmax(dy, 0.0)) - 96.0;
            if (dist <= 1.0) {
                double cov = dist < -1.0 ? 1.0 : (0.5 - dist * 0.5);
                /* Dark slate gradient #0E1015 to #191D26 */
                double t = py / 512.0;
                r = (uint8_t)(14 + t * 14);
                g = (uint8_t)(16 + t * 16);
                b = (uint8_t)(21 + t * 22);
                a = (uint8_t)(cov * 255.0);

                /* Sunburst center at (256, 198), inner R=52, outer R=118 */
                double sdx = px - 256.0;
                double sdy = py - 198.0;
                double sr = sqrt(sdx * sdx + sdy * sdy);
                if (sr >= 52.0 && sr <= 118.0) {
                    double ang = atan2(sdy, sdx);
                    if (ang < 0) ang += 6.283185307179586;
                    /* 4 colored segments with 0.07 rad gap */
                    if (ang > 0.04 && ang < 1.80) { r = 0xFF; g = 0x6B; b = 0x4A; }      /* Coral */
                    else if (ang > 1.87 && ang < 3.40) { r = 0x7D; g = 0xD3; b = 0xFC; } /* Sky */
                    else if (ang > 3.47 && ang < 4.85) { r = 0x86; g = 0xEF; b = 0xAC; } /* Mint */
                    else if (ang > 4.92 && ang < 6.24) { r = 0xFC; g = 0xD3; b = 0x4D; } /* Amber */
                } else if (sr < 38.0) {
                    r = 0x24; g = 0x28; b = 0x34;
                }

                /* 3x2 Mini Treemap blocks at bottom [96..416] x [344..444] */
                if (py >= 344.0 && py <= 390.0) {
                    if (px >= 96.0 && px <= 228.0) { r = 0xFD; g = 0xA4; b = 0xAF; }
                    else if (px >= 238.0 && px <= 336.0) { r = 0x93; g = 0xC5; b = 0xFD; }
                    else if (px >= 346.0 && px <= 416.0) { r = 0x5E; g = 0xEA; b = 0xD4; }
                } else if (py >= 400.0 && py <= 444.0) {
                    if (px >= 96.0 && px <= 184.0) { r = 0xC4; g = 0xB5; b = 0xFD; }
                    else if (px >= 194.0 && px <= 318.0) { r = 0xFD; g = 0xBA; b = 0x74; }
                    else if (px >= 328.0 && px <= 416.0) { r = 0x86; g = 0xEF; b = 0xAC; }
                }
            }
            size_t idx = ((size_t)y * (size_t)size + (size_t)x) * 4;
            rgba[idx + 0] = r;
            rgba[idx + 1] = g;
            rgba[idx + 2] = b;
            rgba[idx + 3] = a;
        }
    }
}

int main(int argc, char **argv) {
    const char *png_out = (argc > 1) ? argv[1] : "Packaging/Icon/diskdude.png";
    uint8_t *rgba = (uint8_t *)calloc(512 * 512 * 4, 1);
    if (!rgba) return 1;
    render_icon(rgba, 512);
    if (write_uncompressed_png(png_out, rgba, 512, 512) != 0) {
        free(rgba);
        return 1;
    }
    free(rgba);
    return 0;
}
