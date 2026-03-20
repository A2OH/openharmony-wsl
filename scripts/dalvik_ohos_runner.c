/*
 * Dalvik OHOS Runner — launches dalvikvm as a child process, provides
 * real-time touch input via shared memory, and blits Canvas output to
 * /dev/fb0 for VNC display.
 *
 * Replaces file-based IPC with:
 * - Shared memory region for canvas pixels (mmap'd by both processes)
 * - Pipe for touch events (parent writes, child reads via JNI)
 * - Semaphore file for frame synchronization
 *
 * Usage: /data/a2oh/dalvik_runner
 */
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <stdio.h>
#include <poll.h>
#include <signal.h>
#include <errno.h>

/* Shared memory layout for canvas pixels:
 * [0..3]   = width  (int32)
 * [4..7]   = height (int32)
 * [8..11]  = frame_counter (int32, incremented by Dalvik after each draw)
 * [12..15] = touch_action (int32, written by runner: 0=none, 1=DOWN, 2=UP, 3=MOVE)
 * [16..19] = touch_x (int32)
 * [20..23] = touch_y (int32)
 * [24..27] = reserved
 * [28..31] = reserved
 * [32..]   = ARGB8888 pixels (width * height * 4 bytes)
 */
#define SHM_PATH "/data/a2oh/shared_canvas"
#define SHM_HEADER_SIZE 32
#define SHM_WIDTH_OFF   0
#define SHM_HEIGHT_OFF  4
#define SHM_FRAME_OFF   8
#define SHM_TOUCH_OFF   12
#define SHM_TOUCHX_OFF  16
#define SHM_TOUCHY_OFF  20
#define SHM_PIXELS_OFF  32

#define SCREEN_W 1280
#define SCREEN_H 800

static volatile int g_running = 1;
static void sighandler(int s) { g_running = 0; }

int main(void) {
    signal(SIGINT, sighandler);
    signal(SIGTERM, sighandler);

    printf("=== Dalvik OHOS Runner ===\n");

    /* Create shared memory file */
    int shm_size = SHM_HEADER_SIZE + SCREEN_W * SCREEN_H * 4;
    int shm_fd = open(SHM_PATH, O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (shm_fd < 0) { perror("open shm"); return 1; }
    if (ftruncate(shm_fd, shm_size) < 0) { perror("ftruncate"); return 1; }

    uint8_t *shm = (uint8_t *)mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm == MAP_FAILED) { perror("mmap shm"); return 1; }

    /* Initialize header */
    *(int32_t *)(shm + SHM_WIDTH_OFF) = SCREEN_W;
    *(int32_t *)(shm + SHM_HEIGHT_OFF) = SCREEN_H;
    *(int32_t *)(shm + SHM_FRAME_OFF) = 0;
    *(int32_t *)(shm + SHM_TOUCH_OFF) = 0;
    printf("Shared memory: %s (%d bytes)\n", SHM_PATH, shm_size);

    /* Open framebuffer */
    uint32_t *fb = NULL;
    int fb_fd = -1, fb_stride = 0, fb_W = 0, fb_H = 0;
    fb_fd = open("/dev/fb0", O_RDWR);
    if (fb_fd >= 0) {
        struct fb_var_screeninfo vi = {};
        struct fb_fix_screeninfo fi = {};
        ioctl(fb_fd, FBIOGET_VSCREENINFO, &vi);
        ioctl(fb_fd, FBIOGET_FSCREENINFO, &fi);
        fb_W = vi.xres; fb_H = vi.yres; fb_stride = fi.line_length / 4;
        fb = (uint32_t *)mmap(NULL, fi.line_length * fb_H, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
        if (fb == MAP_FAILED) fb = NULL;

        /* Hide cursor */
        int tty = open("/dev/tty0", O_WRONLY);
        if (tty >= 0) { write(tty, "\033[?25l", 6); close(tty); }
        int cfd = open("/sys/class/graphics/fbcon/cursor_blink", O_WRONLY);
        if (cfd >= 0) { write(cfd, "0", 1); close(cfd); }

        printf("Framebuffer: %dx%d\n", fb_W, fb_H);
    } else {
        printf("No /dev/fb0\n");
    }

    /* Find touch device */
    int touch_fd = -1;
    for (int i = 0; i < 10; i++) {
        char p[64]; snprintf(p, sizeof(p), "/dev/input/event%d", i);
        int fd = open(p, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        unsigned long absbits[2] = {};
        if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absbits)), absbits) >= 0) {
            if (absbits[0] & (1UL << ABS_X)) {
                close(fd);
                touch_fd = open(p, O_RDONLY | O_NONBLOCK);
                printf("Touch: %s\n", p);
                break;
            }
        }
        close(fd);
    }

    int abs_max_x = 32767, abs_max_y = 32767;
    if (touch_fd >= 0) {
        struct input_absinfo ai;
        if (ioctl(touch_fd, EVIOCGABS(ABS_X), &ai) == 0) abs_max_x = ai.maximum;
        if (ioctl(touch_fd, EVIOCGABS(ABS_Y), &ai) == 0) abs_max_y = ai.maximum;
    }

    /* Fork dalvikvm */
    printf("Starting Dalvik...\n");
    pid_t dalvik_pid = fork();
    if (dalvik_pid == 0) {
        /* Child: run dalvikvm */
        setenv("ANDROID_DATA", "/data/a2oh", 1);
        setenv("ANDROID_ROOT", "/data/a2oh", 1);
        setenv("LD_LIBRARY_PATH",
               "/data/a2oh:/system/lib/platformsdk:/system/lib/chipset-pub-sdk:/system/lib/chipset-sdk:/system/lib", 1);
        /* Redirect stderr to /dev/null (suppress verbose dalvikvm logs) */
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) { dup2(devnull, 2); close(devnull); }
        execl("/data/a2oh/dalvikvm", "dalvikvm",
              "-Xverify:none", "-Xdexopt:none",
              "-Djava.library.path=/data/a2oh:/system/lib/platformsdk",
              "-Xbootclasspath:/data/a2oh/core.jar:/data/a2oh/app.dex",
              "-classpath", "/data/a2oh/app.dex",
              "AppRunner", NULL);
        _exit(127);
    }
    printf("Dalvik PID: %d\n", dalvik_pid);

    /* Main loop: poll touch + blit frames */
    int last_frame = 0;
    int abs_x = 0, abs_y = 0, btn_down = 0;

    while (g_running) {
        /* Check for new frame from Dalvik */
        int frame = *(volatile int32_t *)(shm + SHM_FRAME_OFF);
        if (frame > last_frame && fb) {
            last_frame = frame;
            uint32_t *pixels = (uint32_t *)(shm + SHM_PIXELS_OFF);
            int cw = *(int32_t *)(shm + SHM_WIDTH_OFF);
            int ch = *(int32_t *)(shm + SHM_HEIGHT_OFF);
            for (int y = 0; y < ch && y < fb_H; y++)
                for (int x = 0; x < cw && x < fb_W; x++)
                    fb[y * fb_stride + x] = pixels[y * cw + x];
            printf("Frame %d blitted\n", frame);
        }

        /* Also check file-based frame_ready (fallback for current AppRunner) */
        {
            struct stat st;
            static long last_fmod = 0;
            if (stat("/data/a2oh/frame_ready", &st) == 0 && st.st_mtime > last_fmod) {
                last_fmod = st.st_mtime;
                /* Blit from canvas_pixels.raw */
                int cpfd = open("/data/a2oh/canvas_pixels.raw", O_RDONLY);
                if (cpfd >= 0 && fb) {
                    int cw = 0, ch = 0;
                    read(cpfd, &cw, 4); read(cpfd, &ch, 4);
                    if (cw > 0 && ch > 0 && cw <= 4096 && ch <= 4096) {
                        uint32_t *px = (uint32_t *)malloc(cw * ch * 4);
                        if (px && read(cpfd, px, cw * ch * 4) == cw * ch * 4) {
                            for (int y = 0; y < ch && y < fb_H; y++)
                                for (int x = 0; x < cw && x < fb_W; x++)
                                    fb[y * fb_stride + x] = px[y * cw + x];
                            printf("File frame blitted (%dx%d)\n", cw, ch);
                        }
                        free(px);
                    }
                    close(cpfd);
                }
            }
        }

        /* Read touch events */
        if (touch_fd >= 0) {
            struct input_event ev;
            while (read(touch_fd, &ev, sizeof(ev)) == sizeof(ev)) {
                if (ev.type == EV_ABS) {
                    if (ev.code == ABS_X) abs_x = ev.value;
                    else if (ev.code == ABS_Y) abs_y = ev.value;
                } else if (ev.type == EV_KEY &&
                          (ev.code == BTN_TOUCH || ev.code == BTN_LEFT)) {
                    int sx = abs_max_x > 0 ? abs_x * SCREEN_W / abs_max_x : abs_x;
                    int sy = abs_max_y > 0 ? abs_y * SCREEN_H / abs_max_y : abs_y;
                    if (ev.value == 1) {
                        /* Touch down — write to shared memory */
                        *(volatile int32_t *)(shm + SHM_TOUCH_OFF) = 1; /* DOWN */
                        *(volatile int32_t *)(shm + SHM_TOUCHX_OFF) = sx;
                        *(volatile int32_t *)(shm + SHM_TOUCHY_OFF) = sy;
                        btn_down = 1;
                        /* Also write file (fallback for current AppRunner) */
                        int fd = open("/data/a2oh/touch_input.txt", O_WRONLY | O_CREAT | O_TRUNC, 0666);
                        if (fd >= 0) { dprintf(fd, "DOWN %d %d\n", sx, sy); close(fd); }
                    } else if (ev.value == 0 && btn_down) {
                        *(volatile int32_t *)(shm + SHM_TOUCH_OFF) = 2; /* UP */
                        *(volatile int32_t *)(shm + SHM_TOUCHX_OFF) = sx;
                        *(volatile int32_t *)(shm + SHM_TOUCHY_OFF) = sy;
                        btn_down = 0;
                        int fd = open("/data/a2oh/touch_input.txt", O_WRONLY | O_CREAT | O_TRUNC, 0666);
                        if (fd >= 0) { dprintf(fd, "UP %d %d\n", sx, sy); close(fd); }
                    }
                }
            }
        }

        /* Check if Dalvik died */
        int status;
        if (waitpid(dalvik_pid, &status, WNOHANG) > 0) {
            printf("Dalvik exited: %d\n", WEXITSTATUS(status));
            break;
        }

        usleep(16000); /* ~60Hz */
    }

    munmap(shm, shm_size);
    close(shm_fd);
    if (fb) munmap(fb, fb_H * fb_stride * 4);
    if (fb_fd >= 0) close(fb_fd);
    if (touch_fd >= 0) close(touch_fd);
    return 0;
}
