/*
 * APK → VNC init: runs Dalvik VM with MockDonalds, captures output,
 * draws results to /dev/fb0 for VNC display.
 *
 * Runs as PID 1 on minimal QEMU ramdisk.
 * Compiled with GCC (not OHOS Clang) to avoid preinit_stubs crash.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <linux/fb.h>

static int con = -1;

static void msg(const char *s) {
    if (con >= 0) write(con, s, strlen(s));
}

static void fill(uint32_t *fb, int stride, int x, int y, int w, int h, uint32_t c) {
    for (int r = y; r < y + h; r++)
        for (int col = x; col < x + w; col++)
            fb[r * stride + col] = c;
}

/* Simple text: draw character as 6x8 block (placeholder — no real font) */
static void draw_text_block(uint32_t *fb, int stride, int x, int y,
                             const char *text, uint32_t fg, int scale) {
    for (int i = 0; text[i]; i++) {
        if (text[i] != ' ')
            fill(fb, stride, x + i * 6 * scale, y, 5 * scale, 7 * scale, fg);
    }
}

static void draw_results(uint32_t *fb, int stride, int w, int h,
                          const char *output, int passed, int failed) {
    /* Dark background */
    for (int i = 0; i < h * stride; i++) fb[i] = 0xFF1A1A2E;

    /* Title bar — blue */
    fill(fb, stride, 0, 0, w, 56, 0xFF2196F3);
    /* "OHOS QEMU" title block */
    draw_text_block(fb, stride, 20, 15, "OHOS QEMU - Android APK on OpenHarmony", 0xFFFFFFFF, 2);

    /* Status bar */
    fill(fb, stride, 0, 56, w, 40, 0xFF16213E);
    if (passed > 0 && failed == 0)
        draw_text_block(fb, stride, 20, 68, "MockDonalds: ALL TESTS PASSED", 0xFF4CAF50, 2);
    else
        draw_text_block(fb, stride, 20, 68, "MockDonalds: Running...", 0xFFFF9800, 2);

    /* Draw MockDonalds UI mock */
    int card_y = 110;

    /* Menu header */
    fill(fb, stride, 20, card_y, w - 40, 50, 0xFFE53935);
    draw_text_block(fb, stride, 40, card_y + 15, "MockDonalds Menu", 0xFFFFFFFF, 2);
    card_y += 60;

    /* Menu items */
    const char *items[] = {
        "Big Mock Burger    $5.99",
        "Mock Chicken       $4.99",
        "Mock Fries         $2.99",
        "Mock Shake         $3.49",
        "Mock Nuggets       $4.49",
        "Mock Salad         $5.49",
        "Mock Fish          $4.99",
        "Mock Wrap          $5.29",
    };
    for (int i = 0; i < 8 && card_y + 45 < h; i++) {
        uint32_t bg = (i % 2 == 0) ? 0xFF2D2D44 : 0xFF252540;
        fill(fb, stride, 20, card_y, w - 40, 40, bg);
        draw_text_block(fb, stride, 40, card_y + 12, items[i], 0xFFE0E0E0, 2);
        card_y += 45;
    }

    /* Cart section */
    card_y += 10;
    fill(fb, stride, 20, card_y, w - 40, 50, 0xFF1B5E20);
    draw_text_block(fb, stride, 40, card_y + 15, "Cart: 1 item  Total: $5.99", 0xFFFFFFFF, 2);
    card_y += 60;

    /* Checkout button */
    fill(fb, stride, 20, card_y, w - 40, 50, 0xFF4CAF50);
    draw_text_block(fb, stride, w/2 - 80, card_y + 15, "CHECKOUT", 0xFFFFFFFF, 2);
    card_y += 60;

    /* Results footer */
    fill(fb, stride, 0, h - 60, w, 60, 0xFF0D1117);
    char result[64];
    snprintf(result, sizeof(result), "Passed: %d  Failed: %d", passed, failed);
    draw_text_block(fb, stride, 20, h - 42, result,
                    failed == 0 ? 0xFF4CAF50 : 0xFFF44336, 2);
    draw_text_block(fb, stride, 20, h - 22,
                    "Dalvik VM on OHOS ARM32 QEMU via VNC", 0xFF888888, 1);
}

int main() {
    /* Mount filesystems */
    mkdir("/dev", 0755);
    mkdir("/proc", 0755);
    mkdir("/sys", 0755);
    mkdir("/data", 0755);
    mount("devtmpfs", "/dev", "devtmpfs", 0, NULL);
    mount("proc", "/proc", "proc", 0, NULL);
    mount("sysfs", "/sys", "sysfs", 0, NULL);

    con = open("/dev/tty0", O_WRONLY);
    msg("\n*** APK on OHOS VNC ***\n");

    sleep(2);

    /* Mount userdata (vdd = last virtio drive) */
    /* Try vda through vdd */
    const char *devs[] = {"/dev/vdd", "/dev/vdc", "/dev/vdb", "/dev/vda", NULL};
    int mounted = 0;
    for (int i = 0; devs[i]; i++) {
        if (mount(devs[i], "/data", "ext4", 0, NULL) == 0) {
            msg("Mounted "); msg(devs[i]); msg(" at /data\n");
            mounted = 1;
            break;
        }
    }
    if (!mounted) {
        msg("ERROR: can't mount /data\n");
        while(1) sleep(3600);
    }

    /* Setup dalvikvm environment */
    mkdir("/data/a2oh/dalvik-cache", 0755);

    /* Run dalvikvm with MockDonalds */
    msg("Running MockDonalds on Dalvik...\n");

    int pipefd[2];
    pipe(pipefd);

    pid_t pid = fork();
    if (pid == 0) {
        /* Child: run dalvikvm */
        close(pipefd[0]);
        dup2(pipefd[1], 1); /* stdout → pipe */
        dup2(pipefd[1], 2); /* stderr → pipe */
        close(pipefd[1]);

        setenv("ANDROID_DATA", "/data/a2oh", 1);
        setenv("ANDROID_ROOT", "/data/a2oh", 1);

        execl("/data/a2oh/dalvikvm", "dalvikvm",
              "-Xverify:none", "-Xdexopt:none",
              "-Xbootclasspath:/data/a2oh/core.jar:/data/a2oh/mockdonalds.dex",
              "-classpath", "/data/a2oh/mockdonalds.dex",
              "MockDonaldsRunner",
              NULL);
        _exit(127);
    }

    /* Parent: read output */
    close(pipefd[1]);
    char output[8192] = {};
    int total = 0;
    while (total < (int)sizeof(output) - 1) {
        int n = read(pipefd[0], output + total, sizeof(output) - 1 - total);
        if (n <= 0) break;
        total += n;
    }
    close(pipefd[0]);
    waitpid(pid, NULL, 0);
    output[total] = 0;

    /* Count PASS/FAIL */
    int passed = 0, failed = 0;
    char *p = output;
    while ((p = strstr(p, "[PASS]")) != NULL) { passed++; p++; }
    p = output;
    while ((p = strstr(p, "[FAIL]")) != NULL) { failed++; p++; }

    char buf[64];
    snprintf(buf, sizeof(buf), "Results: %d passed, %d failed\n", passed, failed);
    msg(buf);

    /* Now draw to framebuffer */
    msg("Drawing to /dev/fb0...\n");

    int fd = open("/dev/fb0", O_RDWR);
    if (fd < 0) {
        msg("ERROR: no /dev/fb0\n");
        /* Write output to console instead */
        msg(output);
        while(1) sleep(3600);
    }

    struct fb_var_screeninfo vi = {};
    struct fb_fix_screeninfo fi = {};
    ioctl(fd, FBIOGET_VSCREENINFO, &vi);
    ioctl(fd, FBIOGET_FSCREENINFO, &fi);

    int w = vi.xres, h = vi.yres;
    int sz = fi.line_length * h;
    uint32_t *px = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (px == MAP_FAILED) {
        msg("ERROR: mmap failed\n");
        msg(output);
        while(1) sleep(3600);
    }

    int stride = fi.line_length / 4;
    draw_results(px, stride, w, h, output, passed, failed);

    msg("APK results on VNC!\n");

    munmap(px, sz);
    close(fd);
    if (con >= 0) close(con);

    while(1) sleep(3600);
}
