/*
 * Real APK → VNC: Runs Dalvik with ViewDumper, parses View geometry,
 * draws actual Android View tree to /dev/fb0.
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

static void msg(const char *s) {
    int fd = open("/dev/tty0", O_WRONLY);
    if (fd >= 0) { write(fd, s, strlen(s)); close(fd); }
}

static void fill(uint32_t *fb, int stride, int scrW, int scrH,
                 int x, int y, int w, int h, uint32_t c) {
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    for (int r = y; r < y + h && r < scrH; r++)
        for (int col = x; col < x + w && col < scrW; col++)
            fb[r * stride + col] = c;
}

int main() {
    mkdir("/dev", 0755);
    mkdir("/proc", 0755);
    mkdir("/sys", 0755);
    mkdir("/data", 0755);
    mount("devtmpfs", "/dev", "devtmpfs", 0, NULL);
    mount("proc", "/proc", "proc", 0, NULL);
    mount("sysfs", "/sys", "sysfs", 0, NULL);

    msg("\n*** Real APK on VNC ***\n");
    sleep(2);

    /* Mount userdata */
    const char *devs[] = {"/dev/vda", "/dev/vdb", "/dev/vdc", "/dev/vdd", NULL};
    for (int i = 0; devs[i]; i++) {
        if (mount(devs[i], "/data", "ext4", 0, NULL) == 0) {
            msg("Mounted "); msg(devs[i]); msg("\n");
            break;
        }
    }
    mkdir("/data/a2oh/dalvik-cache", 0755);

    /* Run ViewDumper via dalvikvm */
    msg("Running ViewDumper...\n");

    /* Write output to file, not pipe (avoids buffer deadlock) */
    pid_t pid = fork();
    if (pid == 0) {
        int out = open("/data/a2oh/viewdump.txt", O_WRONLY|O_CREAT|O_TRUNC, 0666);
        if (out >= 0) { dup2(out, 1); close(out); }
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) { dup2(devnull, 2); close(devnull); }
        setenv("ANDROID_DATA", "/data/a2oh", 1);
        setenv("ANDROID_ROOT", "/data/a2oh", 1);
        execl("/data/a2oh/dalvikvm", "dalvikvm",
              "-Xverify:none", "-Xdexopt:none",
              "-Xbootclasspath:/data/a2oh/core.jar:/data/a2oh/viewdumper.dex",
              "-classpath", "/data/a2oh/viewdumper.dex",
              "ViewDumper", NULL);
        /* If ViewDumper DEX not found, try MockDonaldsRunner */
        execl("/data/a2oh/dalvikvm", "dalvikvm",
              "-Xverify:none", "-Xdexopt:none",
              "-Xbootclasspath:/data/a2oh/core.jar:/data/a2oh/mockdonalds.dex",
              "-classpath", "/data/a2oh/mockdonalds.dex",
              "MockDonaldsRunner", NULL);
        _exit(127);
    }

    msg("Waiting for Dalvik (this takes ~90s)...\n");
    int status = 0;
    waitpid(pid, &status, 0);
    char sbuf[64];
    snprintf(sbuf, sizeof(sbuf), "Dalvik exit status: %d\n", status);
    msg(sbuf);

    /* Read output from file */
    char output[65536] = {};
    int total = 0;
    int ofd = open("/data/a2oh/viewdump.txt", O_RDONLY);
    if (ofd >= 0) {
        total = read(ofd, output, sizeof(output) - 1);
        if (total < 0) total = 0;
        close(ofd);
    }
    output[total] = 0;

    msg("ViewDumper done. Parsing...\n");

    /* Open framebuffer */
    int fd = open("/dev/fb0", O_RDWR);
    if (fd < 0) {
        msg("No /dev/fb0\n");
        msg(output);
        while(1) sleep(3600);
    }

    struct fb_var_screeninfo vi = {};
    struct fb_fix_screeninfo fi = {};
    ioctl(fd, FBIOGET_VSCREENINFO, &vi);
    ioctl(fd, FBIOGET_FSCREENINFO, &fi);
    int scrW = vi.xres, scrH = vi.yres;
    int sz = fi.line_length * scrH;
    uint32_t *px = mmap(NULL, sz, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if (px == MAP_FAILED) {
        msg("mmap fail\n");
        msg(output);
        while(1) sleep(3600);
    }
    int stride = fi.line_length / 4;

    /* Clear to dark background */
    for (int i = 0; i < scrH * stride; i++) px[i] = 0xFF1A1A2E;

    /* Title bar */
    fill(px, stride, scrW, scrH, 0, 0, scrW, 40, 0xFFE53935);

    /* Parse RECT lines from ViewDumper output */
    int rectCount = 0;
    char *line = output;
    while (*line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;

        if (strncmp(line, "RECT ", 5) == 0) {
            int x, y, w, h;
            unsigned int color;
            char type[32] = {}, text[128] = {};
            if (sscanf(line + 5, "%d %d %d %d %x %31s %127[^\n]",
                       &x, &y, &w, &h, &color, type, text) >= 5) {
                if (color != 0)
                    fill(px, stride, scrW, scrH, x, y + 40, w, h, color | 0xFF000000);
                rectCount++;
            }
        }

        line = nl ? nl + 1 : line + strlen(line);
    }

    /* Footer with stats */
    char buf[64];
    snprintf(buf, sizeof(buf), "Views drawn: %d\n", rectCount);
    msg(buf);
    msg("Real APK UI on VNC!\n");

    /* If no RECTs found, show raw output for debugging */
    if (rectCount == 0) {
        msg("No RECT data. Raw output:\n");
        msg(output);
    }

    munmap(px, sz);
    close(fd);
    while(1) sleep(3600);
}
