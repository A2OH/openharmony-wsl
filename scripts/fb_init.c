#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <linux/fb.h>

static void msg(const char *s){int fd=open("/dev/tty0",O_WRONLY);if(fd>=0){write(fd,s,strlen(s));close(fd);}}

static void fill(uint32_t *fb,int s,int x,int y,int w,int h,uint32_t c){
    for(int r=y;r<y+h;r++)for(int col=x;col<x+w;col++)fb[r*s+col]=c;
}

int main(){
    mount("devtmpfs","/dev","devtmpfs",0,NULL);
    mount("proc","/proc","proc",0,NULL);
    mount("sysfs","/sys","sysfs",0,NULL);
    msg("\n*** FB TEST ***\n");
    sleep(3);
    int fd=open("/dev/fb0",O_RDWR);
    if(fd<0){msg("no fb0\n");while(1)sleep(3600);}
    msg("fb0 opened!\n");
    struct fb_var_screeninfo vi={};
    struct fb_fix_screeninfo fi={};
    ioctl(fd,FBIOGET_VSCREENINFO,&vi);
    ioctl(fd,FBIOGET_FSCREENINFO,&fi);
    int w=vi.xres,h=vi.yres;
    msg("Drawing...\n");
    int sz=fi.line_length*h;
    uint32_t *px=mmap(NULL,sz,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    if(px==MAP_FAILED){msg("mmap fail\n");while(1)sleep(3600);}
    int s=fi.line_length/4;
    for(int i=0;i<h*s;i++)px[i]=0xFF303030;
    fill(px,s,0,0,w,48,0xFF2196F3);
    fill(px,s,10,58,w-20,60,0xFF1E1E1E);
    fill(px,s,w-60,75,30,28,0xFFFFFFFF);
    int bw=(w-50)/4,bh=60;
    uint32_t cl[]={0xFF616161,0xFF616161,0xFF616161,0xFFFF9800,0xFF616161,0xFF616161,0xFF616161,0xFFFF9800,0xFF616161,0xFF616161,0xFF616161,0xFFFF9800,0xFFF44336,0xFF616161,0xFF4CAF50,0xFFFF9800};
    for(int r=0;r<4;r++)for(int c=0;c<4;c++){int bx=10+c*(bw+10),by=130+r*(bh+10);fill(px,s,bx,by,bw,bh,cl[r*4+c]);fill(px,s,bx+bw/2-5,by+bh/2-5,10,10,0xFFFFFFFF);}
    msg("CALCULATOR ON VNC!\n");
    munmap(px,sz);close(fd);
    while(1)sleep(3600);
}
