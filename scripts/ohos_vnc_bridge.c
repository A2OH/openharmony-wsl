/*
 * OHOS VNC Bridge — runs alongside Dalvik on full OHOS.
 * Polls for canvas_pixels.raw from AppRunner, blits to /dev/fb0.
 * Also forwards /dev/input touch events to touch_input.txt for Dalvik.
 *
 * Usage (on OHOS shell):
 *   /data/a2oh/vnc_bridge &
 *   sh /data/a2oh/run_dalvik.sh &
 */
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <poll.h>

#define CANVAS_FILE   "/data/a2oh/canvas_pixels.raw"
#define FRAME_READY   "/data/a2oh/frame_ready"
#define TOUCH_INPUT   "/data/a2oh/touch_input.txt"
#define VIEWDUMP_FILE "/data/a2oh/viewdump.txt"

static uint32_t *fb_pixels = NULL;
static int fb_fd = -1;
static int fb_W = 0, fb_H = 0, fb_stride = 0;

static int open_framebuffer(void) {
    fb_fd = open("/dev/fb0", O_RDWR);
    if (fb_fd < 0) return -1;

    struct fb_var_screeninfo vi = {};
    struct fb_fix_screeninfo fi = {};
    ioctl(fb_fd, FBIOGET_VSCREENINFO, &vi);
    ioctl(fb_fd, FBIOGET_FSCREENINFO, &fi);
    fb_W = vi.xres; fb_H = vi.yres;
    fb_stride = fi.line_length / 4;

    int sz = fi.line_length * fb_H;
    fb_pixels = (uint32_t *)mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
    if (fb_pixels == MAP_FAILED) { fb_pixels = NULL; return -1; }

    /* Hide fbcon cursor */
    int tty = open("/dev/tty0", O_WRONLY);
    if (tty >= 0) { write(tty, "\033[?25l", 6); close(tty); }
    int cfd = open("/sys/class/graphics/fbcon/cursor_blink", O_WRONLY);
    if (cfd >= 0) { write(cfd, "0", 1); close(cfd); }

    return 0;
}

static int blit_canvas(void) {
    int fd = open(CANVAS_FILE, O_RDONLY);
    if (fd < 0) return -1;

    int cw = 0, ch = 0;
    if (read(fd, &cw, 4) != 4 || read(fd, &ch, 4) != 4 || cw <= 0 || ch <= 0) {
        close(fd); return -1;
    }

    int npix = cw * ch;
    uint32_t *pixels = (uint32_t *)malloc(npix * 4);
    if (!pixels) { close(fd); return -1; }

    int rd = read(fd, pixels, npix * 4);
    close(fd);
    if (rd < npix * 4) { free(pixels); return -1; }

    /* Blit to framebuffer */
    for (int y = 0; y < ch && y < fb_H; y++)
        for (int x = 0; x < cw && x < fb_W; x++)
            fb_pixels[y * fb_stride + x] = pixels[y * cw + x];

    free(pixels);
    return 0;
}

static int find_touch_device(void) {
    for (int i = 0; i < 10; i++) {
        char path[64];
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        unsigned long absbits[2] = {};
        if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absbits)), absbits) >= 0) {
            if (absbits[0] & (1UL << ABS_X)) {
                close(fd);
                return open(path, O_RDONLY); /* reopen blocking */
            }
        }
        close(fd);
    }
    return -1;
}

static void write_touch(const char *action, int sx, int sy) {
    int fd = open(TOUCH_INPUT, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) return;
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "%s %d %d\n", action, sx, sy);
    write(fd, buf, n);
    close(fd);
}

int main(void) {
    printf("VNC Bridge starting...\n");

    if (open_framebuffer() < 0) {
        printf("No /dev/fb0 — running without display\n");
    } else {
        printf("Framebuffer: %dx%d\n", fb_W, fb_H);
    }

    int touch_fd = find_touch_device();
    if (touch_fd >= 0) {
        struct input_absinfo ai = {};
        int abs_max_x = 32767, abs_max_y = 32767;
        if (ioctl(touch_fd, EVIOCGABS(ABS_X), &ai) == 0) abs_max_x = ai.maximum;
        if (ioctl(touch_fd, EVIOCGABS(ABS_Y), &ai) == 0) abs_max_y = ai.maximum;
        printf("Touch device found (ABS range: %d x %d)\n", abs_max_x, abs_max_y);
    } else {
        printf("No touch device\n");
    }

    /* Main loop */
    long last_frame_mod = 0;
    int abs_x = 0, abs_y = 0, btn_down = 0;
    int abs_max_x = 32767, abs_max_y = 32767;
    int screen_w = fb_W > 0 ? fb_W : 1280;
    int screen_h = fb_H > 0 ? fb_H : 800;

    while (1) {
        /* Check for new frame from Dalvik */
        if (fb_pixels) {
            struct stat st;
            if (stat(FRAME_READY, &st) == 0 && st.st_mtime > last_frame_mod) {
                last_frame_mod = st.st_mtime;
                if (blit_canvas() == 0) {
                    printf("Frame blitted\n");
                } else {
                    printf("Blit failed, trying RECT fallback\n");
                }
            }
        }

        /* Read touch events (non-blocking if also polling frames) */
        if (touch_fd >= 0) {
            struct pollfd pfd = { .fd = touch_fd, .events = POLLIN };
            if (poll(&pfd, 1, 50) > 0) { /* 50ms timeout = 20Hz */
                struct input_event ev;
                while (read(touch_fd, &ev, sizeof(ev)) == sizeof(ev)) {
                    if (ev.type == EV_ABS) {
                        if (ev.code == ABS_X) abs_x = ev.value;
                        else if (ev.code == ABS_Y) abs_y = ev.value;
                    } else if (ev.type == EV_KEY &&
                              (ev.code == BTN_TOUCH || ev.code == BTN_LEFT)) {
                        if (ev.value == 1) {
                            btn_down = 1;
                            int sx = abs_x * screen_w / abs_max_x;
                            int sy = abs_y * screen_h / abs_max_y;
                            write_touch("DOWN", sx, sy);
                        } else if (ev.value == 0 && btn_down) {
                            btn_down = 0;
                            int sx = abs_x * screen_w / abs_max_x;
                            int sy = abs_y * screen_h / abs_max_y;
                            write_touch("UP", sx, sy);
                        }
                    }
                }
            }
        } else {
            usleep(50000); /* 50ms */
        }
    }
}
