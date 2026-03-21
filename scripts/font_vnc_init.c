#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <math.h>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_ifloor(x)   ((int)(x))
#define STBTT_iceil(x)    ((int)((x)+0.999999))
#include "stb_truetype.h"

static void msg(const char *s){int fd=open("/dev/tty0",O_WRONLY);if(fd>=0){write(fd,s,strlen(s));close(fd);}}

/* Alpha-blend a grayscale glyph pixel onto ARGB framebuffer */
static inline void blend_pixel(uint32_t *fb, int idx, uint32_t color, uint8_t alpha) {
    if (alpha == 0) return;
    uint32_t bg = fb[idx];
    uint8_t br = (bg >> 16) & 0xFF, bg_g = (bg >> 8) & 0xFF, bb = bg & 0xFF;
    uint8_t fr = (color >> 16) & 0xFF, fg = (color >> 8) & 0xFF, fb_b = color & 0xFF;
    uint8_t r = (fr * alpha + br * (255 - alpha)) / 255;
    uint8_t g = (fg * alpha + bg_g * (255 - alpha)) / 255;
    uint8_t b = (fb_b * alpha + bb * (255 - alpha)) / 255;
    fb[idx] = 0xFF000000 | (r << 16) | (g << 8) | b;
}

static stbtt_fontinfo font;
static int font_loaded = 0;

static void fill(uint32_t *fb, int stride, int scrW, int scrH, int x, int y, int w, int h, uint32_t c) {
    if (x < 0) x = 0; if (y < 0) y = 0;
    for (int r = y; r < y + h && r < scrH; r++)
        for (int col = x; col < x + w && col < scrW; col++)
            fb[r * stride + col] = c;
}

/* Render a string with stb_truetype */
static void draw_text(uint32_t *fb, int stride, int scrW, int scrH,
                      int px, int py, const char *text, float size, uint32_t color) {
    if (!font_loaded) {
        /* Fallback: block font */
        for (int i = 0; text[i] && i < 200; i++) {
            if (text[i] != ' ' && text[i] != '\n') {
                int bw = (int)(size * 0.55f);
                int bh = (int)(size * 0.85f);
                fill(fb, stride, scrW, scrH, px + i * bw, py, bw - 1, bh, color);
            }
        }
        return;
    }

    float scale = stbtt_ScaleForPixelHeight(&font, size);
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &lineGap);
    int baseline = py + (int)(ascent * scale);

    float xpos = (float)px;
    for (int i = 0; text[i]; i++) {
        int ch = (unsigned char)text[i];
        if (ch < 32) continue;

        int advance, lsb;
        stbtt_GetCodepointHMetrics(&font, ch, &advance, &lsb);

        int x0, y0, x1, y1;
        stbtt_GetCodepointBitmapBox(&font, ch, scale, scale, &x0, &y0, &x1, &y1);

        int gw = x1 - x0, gh = y1 - y0;
        if (gw > 0 && gh > 0 && gw < 200 && gh < 200) {
            unsigned char bitmap[200 * 200];
            stbtt_MakeCodepointBitmap(&font, bitmap, gw, gh, gw, scale, scale, ch);

            int dx = (int)(xpos + x0 + lsb * scale);
            int dy = baseline + y0;
            for (int row = 0; row < gh; row++) {
                for (int col = 0; col < gw; col++) {
                    int sx = dx + col, sy = dy + row;
                    if (sx >= 0 && sx < scrW && sy >= 0 && sy < scrH) {
                        blend_pixel(fb, sy * stride + sx, color, bitmap[row * gw + col]);
                    }
                }
            }
        }
        xpos += advance * scale;
        if (i + 1 < (int)strlen(text)) {
            int kern = stbtt_GetCodepointKernAdvance(&font, ch, (unsigned char)text[i + 1]);
            xpos += kern * scale;
        }
    }
}

/* Load TTF font from file */
static unsigned char *font_buffer = NULL;
static int load_font(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    struct stat st;
    if (fstat(fd, &st) < 0) { close(fd); return 0; }
    font_buffer = (unsigned char *)malloc(st.st_size);
    if (!font_buffer) { close(fd); return 0; }
    read(fd, font_buffer, st.st_size);
    close(fd);
    if (!stbtt_InitFont(&font, font_buffer, 0)) {
        free(font_buffer); font_buffer = NULL; return 0;
    }
    font_loaded = 1;
    return 1;
}

int main() {
    mount("devtmpfs", "/dev", "devtmpfs", 0, NULL);
    mount("proc", "/proc", "proc", 0, NULL);
    mount("sysfs", "/sys", "sysfs", 0, NULL);
    mkdir("/data", 0755);
    msg("\n*** Font VNC Init ***\n");
    sleep(2);

    /* Load font from ramdisk */
    if (load_font("/font.ttf"))
        msg("Font loaded OK\n");
    else
        msg("Font not found, using block fallback\n");

    /* Mount userdata */
    const char *devs[] = {"/dev/vda", "/dev/vdb", "/dev/vdc", "/dev/vdd", NULL};
    int ok = 0;
    for (int i = 0; devs[i]; i++) {
        if (mount(devs[i], "/data", "ext4", 0, NULL) == 0) {
            msg("Mounted "); msg(devs[i]); msg("\n");
            ok = 1; break;
        }
    }
    if (!ok) { msg("No data mount\n"); while(1) sleep(3600); }

    mkdir("/data/a2oh/dalvik-cache", 0755);

    /* Clear old output */
    unlink("/data/a2oh/viewdump.txt");

    /* Run dalvikvm */
    msg("Starting Dalvik ViewDumper...\n");
    pid_t pid = fork();
    if (pid == 0) {
        int out = open("/data/a2oh/dalvik_stdout.txt", O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (out >= 0) { dup2(out, 1); close(out); }
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) { dup2(devnull, 2); close(devnull); }
        setenv("ANDROID_DATA", "/data/a2oh", 1);
        setenv("ANDROID_ROOT", "/data/a2oh", 1);
        execl("/data/a2oh/dalvikvm", "dalvikvm",
              "-Xverify:none", "-Xdexopt:none",
              "-Xbootclasspath:/data/a2oh/core.jar:/data/a2oh/viewdumper.dex",
              "-classpath", "/data/a2oh/viewdumper.dex",
              "CanvasViewDumper", "com.example.showcase.ShowcaseActivity", NULL);
        _exit(127);
    }

    /* Wait for Dalvik */
    msg("Waiting for Dalvik (max 180s)...\n");
    int elapsed = 0;
    while (elapsed < 180) {
        int st;
        if (waitpid(pid, &st, WNOHANG) > 0) {
            char buf[32]; snprintf(buf, sizeof(buf), "Exit: %d\n", WEXITSTATUS(st));
            msg(buf); break;
        }
        sleep(5); elapsed += 5;
        if (elapsed % 30 == 0) msg(".");
    }
    if (elapsed >= 180) { msg("\nTimeout!\n"); kill(pid, 9); waitpid(pid, NULL, 0); }

    /* Read ViewDumper output */
    msg("\nReading viewdump.txt...\n");
    char output[65536] = {};
    int total = 0;
    int ofd = open("/data/a2oh/viewdump.txt", O_RDONLY);
    if (ofd >= 0) { total = read(ofd, output, sizeof(output) - 1); close(ofd); }
    if (total < 0) total = 0;
    output[total] = 0;

    /* Strip null bytes */
    int clean = 0;
    for (int i = 0; i < total; i++)
        if (output[i] != 0) output[clean++] = output[i];
    output[clean] = 0; total = clean;

    char lenbuf[64];
    snprintf(lenbuf, sizeof(lenbuf), "Output: %d bytes\n", total);
    msg(lenbuf);

    /* Check for canvas pixel output from software renderer */
    int has_canvas = 0;
    {
        int cpfd = open("/data/a2oh/canvas_pixels.raw", O_RDONLY);
        if (cpfd >= 0) {
            int cw = 0, ch = 0;
            read(cpfd, &cw, 4);
            read(cpfd, &ch, 4);
            if (cw > 0 && ch > 0 && cw <= 4096 && ch <= 4096) {
                has_canvas = 1;
                char cbuf[64];
                snprintf(cbuf, sizeof(cbuf), "Canvas output: %dx%d\n", cw, ch);
                msg(cbuf);
            }
            close(cpfd);
        }
    }

    /* Open framebuffer */
    int fb = open("/dev/fb0", O_RDWR);
    if (fb < 0) { msg("No fb0\n"); msg(output); while(1) sleep(3600); }

    struct fb_var_screeninfo vi = {};
    struct fb_fix_screeninfo fi = {};
    ioctl(fb, FBIOGET_VSCREENINFO, &vi);
    ioctl(fb, FBIOGET_FSCREENINFO, &fi);
    int W = vi.xres, H = vi.yres, sz = fi.line_length * H;
    uint32_t *px = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_SHARED, fb, 0);
    if (px == MAP_FAILED) { msg("mmap fail\n"); while(1) sleep(3600); }
    int stride = fi.line_length / 4;

    /* If canvas pixels available, blit directly */
    if (has_canvas) {
        int cpfd = open("/data/a2oh/canvas_pixels.raw", O_RDONLY);
        if (cpfd >= 0) {
            int cw = 0, ch = 0;
            read(cpfd, &cw, 4);
            read(cpfd, &ch, 4);
            if (cw > 0 && ch > 0) {
                uint32_t *cpx = (uint32_t*)malloc(cw * ch * 4);
                if (cpx) {
                    read(cpfd, cpx, cw * ch * 4);
                    /* Blit canvas to framebuffer — 1:1, clip at screen edge */
                    for (int y = 0; y < ch && y < H; y++)
                        for (int x = 0; x < cw && x < W; x++)
                            px[y * stride + x] = cpx[y * cw + x];
                    free(cpx);
                    msg("Canvas pixels blitted to fb0!\n");
                    /* Add footer */
                    fill(px, stride, W, H, 0, H - 30, W, 30, 0xFF0D1117);
                    char footer[128];
                    snprintf(footer, sizeof(footer), "Canvas render %dx%d | Software OH_Drawing | ARM32 QEMU VNC", cw, ch);
                    draw_text(px, stride, W, H, 12, H - 24, footer, 14.0f, 0xFF888888);
                    /* Skip RECT parsing */
                    munmap(px, sz); close(fb); close(cpfd);
                    msg("Done!\n");
                    while(1) sleep(3600);
                }
            }
            close(cpfd);
        }
        msg("Canvas blit failed, falling back to RECT\n");
    }

    /* Clear to dark background */
    for (int i = 0; i < H * stride; i++) px[i] = 0xFF1A1A2E;

    /* Title bar */
    fill(px, stride, W, H, 0, 0, W, 40, 0xFFE53935);
    draw_text(px, stride, W, H, 12, 8, "MockDonalds", 26.0f, 0xFFFFFFFF);
    draw_text(px, stride, W, H, W - 300, 14, "Android on OHOS ARM32 QEMU", 16.0f, 0xFFFFCDD2);

    /* Parse RECT lines and render with real fonts */
    int y_offset = 45; /* below title bar */
    int rect_count = 0;

    /* Collect menu items for nice rendering */
    typedef struct { char name[64]; char price[16]; int bgColor; char type[32]; } MenuItem;
    MenuItem items[32];
    int item_count = 0;
    char cart_text[64] = "";
    char title_text[64] = "";
    int has_button = 0;
    char button_text[64] = "";

    char *line = output;
    while (*line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;

        if (strncmp(line, "RECT ", 5) == 0) {
            int x, y, w, h;
            unsigned int color;
            char type[32] = {};
            char text[128] = {};
            int n = sscanf(line + 5, "%d %d %d %d %x %31s %127[^\n]", &x, &y, &w, &h, &color, type, text);
            if (n >= 5) {
                rect_count++;
                if (strcmp(type, "Button") == 0) {
                    has_button = 1;
                    strncpy(button_text, text, sizeof(button_text) - 1);
                } else if (strcmp(type, "TextView") == 0 && strlen(text) > 0) {
                    /* Check if it's a title, cart, price, or menu item */
                    if (strstr(text, "Menu") || strstr(text, "MockDonalds")) {
                        strncpy(title_text, text, sizeof(title_text) - 1);
                    } else if (strstr(text, "Cart")) {
                        strncpy(cart_text, text, sizeof(cart_text) - 1);
                    } else if (strstr(text, "$")) {
                        /* Price - attach to previous item */
                        char *dollar = strstr(text, "$");
                        if (item_count > 0 && dollar)
                            strncpy(items[item_count - 1].price, dollar, sizeof(items[0].price) - 1);
                    } else if (item_count < 32) {
                        strncpy(items[item_count].name, text, sizeof(items[0].name) - 1);
                        items[item_count].bgColor = color;
                        strncpy(items[item_count].type, type, sizeof(items[0].type) - 1);
                        item_count++;
                    }
                }
            }
        }
        line = nl ? nl + 1 : line + strlen(line);
    }

    /* Now render the nice UI */
    int cy = y_offset;

    /* Subtitle / cart */
    if (strlen(cart_text) > 0) {
        fill(px, stride, W, H, 0, cy, W, 28, 0xFF2D2D44);
        draw_text(px, stride, W, H, 12, cy + 4, cart_text, 18.0f, 0xFFBBBBCC);
        cy += 32;
    }

    /* Menu items */
    for (int i = 0; i < item_count; i++) {
        int row_h = 52;
        uint32_t bg = (i % 2 == 0) ? 0xFF252540 : 0xFF2A2A48;
        fill(px, stride, W, H, 0, cy, W, row_h, bg);

        /* Item number circle */
        int cx_circle = 30, cy_circle = cy + row_h / 2;
        for (int dy = -12; dy <= 12; dy++)
            for (int dx = -12; dx <= 12; dx++)
                if (dx * dx + dy * dy <= 12 * 12) {
                    int sx = cx_circle + dx, sy = cy_circle + dy;
                    if (sx >= 0 && sx < W && sy >= 0 && sy < H)
                        px[sy * stride + sx] = 0xFFE53935;
                }
        char num[4]; snprintf(num, sizeof(num), "%d", i + 1);
        draw_text(px, stride, W, H, cx_circle - 4, cy_circle - 8, num, 16.0f, 0xFFFFFFFF);

        /* Item name */
        draw_text(px, stride, W, H, 56, cy + 8, items[i].name, 22.0f, 0xFFFFFFFF);

        /* Price */
        if (strlen(items[i].price) > 0) {
            draw_text(px, stride, W, H, W - 120, cy + 12, items[i].price, 20.0f, 0xFF4CAF50);
        }

        /* Divider line */
        fill(px, stride, W, H, 56, cy + row_h - 1, W - 70, 1, 0xFF3A3A5C);

        cy += row_h;
    }

    /* Button */
    if (has_button) {
        cy += 12;
        int btn_w = 300, btn_h = 48;
        int btn_x = (W - btn_w) / 2;
        /* Rounded button (just a rect with color) */
        fill(px, stride, W, H, btn_x, cy, btn_w, btn_h, 0xFF4CAF50);
        /* Button text centered */
        int text_x = btn_x + (btn_w - (int)(strlen(button_text) * 12)) / 2;
        draw_text(px, stride, W, H, text_x, cy + 10, button_text, 24.0f, 0xFFFFFFFF);
        cy += btn_h + 12;
    }

    /* Footer */
    fill(px, stride, W, H, 0, H - 30, W, 30, 0xFF0D1117);
    char footer[128];
    snprintf(footer, sizeof(footer), "%d views | %d menu items | %s | ARM32 QEMU VNC",
             rect_count, item_count, font_loaded ? "TTF fonts" : "block fonts");
    draw_text(px, stride, W, H, 12, H - 24, footer, 14.0f, 0xFF888888);

    munmap(px, sz); close(fb);
    msg("Done!\n");
    while(1) sleep(3600);
}
