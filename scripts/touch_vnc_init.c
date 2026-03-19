#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <math.h>
#include <errno.h>
#include <dirent.h>
#include <time.h>

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

/* ========================================================================
 * Touch/Input support
 * ======================================================================== */

/* Clickable region tracking */
#define MAX_REGIONS 64
typedef struct {
    int x, y, w, h;
    int type;       /* 0=menu item, 1=button, 2=cart, 3=title */
    int index;      /* menu item index (for type==0) */
    char label[64];
} ClickRegion;

static ClickRegion regions[MAX_REGIONS];
static int region_count = 0;

static void add_region(int x, int y, int w, int h, int type, int index, const char *label) {
    if (region_count >= MAX_REGIONS) return;
    ClickRegion *r = &regions[region_count++];
    r->x = x; r->y = y; r->w = w; r->h = h;
    r->type = type; r->index = index;
    strncpy(r->label, label, sizeof(r->label) - 1);
    r->label[sizeof(r->label) - 1] = 0;
}

static int find_region(int px, int py) {
    for (int i = 0; i < region_count; i++) {
        ClickRegion *r = &regions[i];
        if (px >= r->x && px < r->x + r->w && py >= r->y && py < r->y + r->h)
            return i;
    }
    return -1;
}

/* Scan /dev/input/event0..event9 for a device with ABS_X capability */
static int find_touch_device(void) {
    for (int i = 0; i < 10; i++) {
        char path[64];
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;

        /* Check if device has ABS_X via EVIOCGBIT(EV_ABS) */
        unsigned long absbits[(ABS_MAX + 8 * sizeof(unsigned long) - 1) / (8 * sizeof(unsigned long))];
        memset(absbits, 0, sizeof(absbits));
        if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absbits)), absbits) >= 0) {
            /* Check bit for ABS_X */
            if (absbits[ABS_X / (8 * sizeof(unsigned long))] & (1UL << (ABS_X % (8 * sizeof(unsigned long))))) {
                char msgbuf[128];
                snprintf(msgbuf, sizeof(msgbuf), "Touch device: %s\n", path);
                msg(msgbuf);
                /* Re-open as blocking for the event loop */
                close(fd);
                fd = open(path, O_RDONLY);
                return fd;
            }
        }
        close(fd);
    }
    return -1;
}

/* Write touch event to file for external consumers */
static void write_touch_event(int sx, int sy, int region_idx) {
    mkdir("/data/a2oh", 0755);
    int fd = open("/data/a2oh/touch_event.txt", O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) return;
    char buf[256];
    if (region_idx >= 0) {
        ClickRegion *r = &regions[region_idx];
        if (r->type == 1) {
            /* Button press - special command */
            snprintf(buf, sizeof(buf), "BUTTON_PRESS %s\n", r->label);
        } else {
            snprintf(buf, sizeof(buf), "TOUCH %d %d %d %s\n", sx, sy, region_idx, r->label);
        }
    } else {
        snprintf(buf, sizeof(buf), "TOUCH %d %d -1 (none)\n", sx, sy);
    }
    write(fd, buf, strlen(buf));
    close(fd);
}

/* ========================================================================
 * Globals for redraw
 * ======================================================================== */
typedef struct { char name[64]; char price[16]; int bgColor; char type[32]; } MenuItem;

static MenuItem items[32];
static int item_count = 0;
static char cart_text[64] = "";
static char title_text[64] = "";
static int has_button = 0;
static char button_text[64] = "";
static int rect_count = 0;

/* Framebuffer globals */
static uint32_t *g_px = NULL;
static int g_stride = 0, g_W = 0, g_H = 0;

/* Highlight state: which region is highlighted, and when to clear it */
static int highlight_region = -1;
static time_t highlight_until = 0;

/* Draw the full UI, optionally highlighting a region */
static void draw_ui(void) {
    int W = g_W, H = g_H, stride = g_stride;
    uint32_t *px = g_px;

    /* Clear to dark background */
    for (int i = 0; i < H * stride; i++) px[i] = 0xFF1A1A2E;

    region_count = 0; /* Reset clickable regions */

    /* Title bar */
    fill(px, stride, W, H, 0, 0, W, 40, 0xFFE53935);
    draw_text(px, stride, W, H, 12, 8, "Android on OHOS", 26.0f, 0xFFFFFFFF);
    draw_text(px, stride, W, H, W - 300, 14, "Android on OHOS ARM32 QEMU", 16.0f, 0xFFFFCDD2);
    add_region(0, 0, W, 40, 3, 0, "TitleBar");

    int cy = 45; /* below title bar */

    /* Subtitle / cart */
    if (strlen(cart_text) > 0) {
        fill(px, stride, W, H, 0, cy, W, 28, 0xFF2D2D44);
        draw_text(px, stride, W, H, 12, cy + 4, cart_text, 18.0f, 0xFFBBBBCC);
        add_region(0, cy, W, 28, 2, 0, cart_text);
        cy += 32;
    }

    /* Menu items */
    for (int i = 0; i < item_count; i++) {
        int row_h = 52;
        uint32_t bg = (i % 2 == 0) ? 0xFF252540 : 0xFF2A2A48;

        /* Highlight: brighten the tapped row */
        if (highlight_region >= 0 && regions[highlight_region].type == 0
            && regions[highlight_region].index == i
            && time(NULL) < highlight_until) {
            /* Note: region_count has been reset but we're building them in same order.
               Use a simpler check: compare index */
            bg = 0xFF4A4A70;
        }
        /* Also check if current region_count matches highlight */
        int this_region = region_count;
        add_region(0, cy, W, row_h, 0, i, items[i].name);
        if (highlight_region == this_region && time(NULL) < highlight_until) {
            bg = 0xFF4A4A70;
        }

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
        uint32_t btn_color = 0xFF4CAF50;

        /* Check if button is highlighted */
        int this_region = region_count;
        add_region(btn_x, cy, btn_w, btn_h, 1, 0, button_text);
        if (highlight_region == this_region && time(NULL) < highlight_until) {
            btn_color = 0xFF66BB6A; /* Lighter green */
        }

        fill(px, stride, W, H, btn_x, cy, btn_w, btn_h, btn_color);
        /* Button text centered */
        int text_x = btn_x + (btn_w - (int)(strlen(button_text) * 12)) / 2;
        draw_text(px, stride, W, H, text_x, cy + 10, button_text, 24.0f, 0xFFFFFFFF);
        cy += btn_h + 12;
    }

    /* Touch status line */
    {
        fill(px, stride, W, H, 0, H - 52, W, 22, 0xFF0D1117);
        /* Show last touch info if recent */
        if (highlight_region >= 0 && time(NULL) < highlight_until + 5) {
            char tbuf[128];
            snprintf(tbuf, sizeof(tbuf), "Tapped: %s", regions[highlight_region].label);
            draw_text(px, stride, W, H, 12, H - 50, tbuf, 14.0f, 0xFFFFCC00);
        } else {
            draw_text(px, stride, W, H, 12, H - 50, "Touch/click anywhere to interact", 14.0f, 0xFF666666);
        }
    }

    /* Footer */
    fill(px, stride, W, H, 0, H - 30, W, 30, 0xFF0D1117);
    char footer[128];
    snprintf(footer, sizeof(footer), "%d views | %d items | %s | Touch enabled",
             rect_count, item_count, font_loaded ? "TTF fonts" : "block fonts");
    draw_text(px, stride, W, H, 12, H - 24, footer, 14.0f, 0xFF888888);
}

int main() {
    mount("devtmpfs", "/dev", "devtmpfs", 0, NULL);
    mount("proc", "/proc", "proc", 0, NULL);
    mount("sysfs", "/sys", "sysfs", 0, NULL);
    mkdir("/data", 0755);
    msg("\n*** Touch VNC Init ***\n");
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

    mkdir("/data/a2oh", 0755);
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
              "CanvasViewDumper", NULL);
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

    /* Open framebuffer */
    int fb = open("/dev/fb0", O_RDWR);
    if (fb < 0) { msg("No fb0\n"); msg(output); while(1) sleep(3600); }

    struct fb_var_screeninfo vi = {};
    struct fb_fix_screeninfo fi = {};
    ioctl(fb, FBIOGET_VSCREENINFO, &vi);
    ioctl(fb, FBIOGET_FSCREENINFO, &fi);
    g_W = vi.xres; g_H = vi.yres;
    int sz = fi.line_length * g_H;
    g_px = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_SHARED, fb, 0);
    if (g_px == MAP_FAILED) { msg("mmap fail\n"); while(1) sleep(3600); }
    g_stride = fi.line_length / 4;

    /* Parse RECT lines from ViewDumper output */
    {
        /* Work on a copy since we modify it with strtok-style splitting */
        char parse_buf[65536];
        memcpy(parse_buf, output, total + 1);
        char *line = parse_buf;
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
                        if (strstr(text, "Menu") || strstr(text, "MockDonalds") || strstr(text, "Hello") || strstr(text, "Real APK")) {
                            strncpy(title_text, text, sizeof(title_text) - 1);
                        } else if (strstr(text, "Cart")) {
                            strncpy(cart_text, text, sizeof(cart_text) - 1);
                        } else if (strstr(text, "$")) {
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
    }

    /* Initial draw */
    draw_ui();

    char mbuf[128];
    snprintf(mbuf, sizeof(mbuf), "Rendered %d items, %d regions\n", item_count, region_count);
    msg(mbuf);

    /* ====================================================================
     * Touch event loop
     * ==================================================================== */
    int touch_fd = find_touch_device();
    if (touch_fd < 0) {
        msg("No touch device found, trying /dev/input/event0...\n");
        /* Create /dev/input if needed */
        mkdir("/dev/input", 0755);
        /* Wait a moment for device nodes to appear */
        sleep(1);
        touch_fd = find_touch_device();
        if (touch_fd < 0) {
            /* Last resort: try event0 directly */
            touch_fd = open("/dev/input/event0", O_RDONLY);
            if (touch_fd >= 0)
                msg("Opened /dev/input/event0 (unchecked)\n");
        }
    }

    if (touch_fd < 0) {
        msg("No input device, running without touch\n");
        while (1) sleep(3600);
    }

    msg("Touch event loop starting...\n");

    /* ABS coordinate ranges: virtio-tablet uses 0-32767 */
    int abs_x = 0, abs_y = 0;
    int btn_down = 0;
    int got_x = 0, got_y = 0;

    /* Get actual ABS range from device */
    struct input_absinfo abs_info_x, abs_info_y;
    int abs_max_x = 32767, abs_max_y = 32767;
    if (ioctl(touch_fd, EVIOCGABS(ABS_X), &abs_info_x) == 0) {
        abs_max_x = abs_info_x.maximum;
        snprintf(mbuf, sizeof(mbuf), "ABS_X range: 0-%d\n", abs_max_x);
        msg(mbuf);
    }
    if (ioctl(touch_fd, EVIOCGABS(ABS_Y), &abs_info_y) == 0) {
        abs_max_y = abs_info_y.maximum;
        snprintf(mbuf, sizeof(mbuf), "ABS_Y range: 0-%d\n", abs_max_y);
        msg(mbuf);
    }

    struct input_event ev;

    while (1) {
        int rd = read(touch_fd, &ev, sizeof(ev));
        if (rd < (int)sizeof(ev)) {
            if (errno == EINTR) continue;
            /* Device disconnected or error */
            msg("Input read error\n");
            sleep(1);
            continue;
        }

        if (ev.type == EV_ABS) {
            if (ev.code == ABS_X) { abs_x = ev.value; got_x = 1; }
            else if (ev.code == ABS_Y) { abs_y = ev.value; got_y = 1; }
        }
        else if (ev.type == EV_KEY) {
            if (ev.code == BTN_TOUCH || ev.code == BTN_LEFT || ev.code == BTN_MOUSE) {
                if (ev.value == 1) btn_down = 1;
                else if (ev.value == 0 && btn_down) {
                    /* Button released = tap/click event */
                    btn_down = 0;

                    /* Translate absolute coords to screen coords */
                    int sx = (abs_max_x > 0) ? (abs_x * g_W / abs_max_x) : abs_x;
                    int sy = (abs_max_y > 0) ? (abs_y * g_H / abs_max_y) : abs_y;

                    /* Clamp */
                    if (sx < 0) sx = 0; if (sx >= g_W) sx = g_W - 1;
                    if (sy < 0) sy = 0; if (sy >= g_H) sy = g_H - 1;

                    snprintf(mbuf, sizeof(mbuf), "Tap at (%d,%d) abs(%d,%d)\n", sx, sy, abs_x, abs_y);
                    msg(mbuf);

                    /* Find which region was tapped */
                    int ri = find_region(sx, sy);
                    if (ri >= 0) {
                        snprintf(mbuf, sizeof(mbuf), "  -> region %d: %s\n", ri, regions[ri].label);
                        msg(mbuf);
                    }

                    /* Write event file */
                    write_touch_event(sx, sy, ri);

                    /* Highlight the tapped region */
                    highlight_region = ri;
                    highlight_until = time(NULL) + 1; /* 1 second highlight */

                    /* Redraw with highlight */
                    draw_ui();
                }
            }
        }
        else if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
            /* Check if highlight has expired, redraw if so */
            if (highlight_region >= 0 && time(NULL) >= highlight_until + 1) {
                highlight_region = -1;
                draw_ui();
            }
        }
    }

    /* Never reached */
    munmap(g_px, sz);
    close(fb);
    close(touch_fd);
    return 0;
}
