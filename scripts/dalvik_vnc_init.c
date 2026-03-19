#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <linux/fb.h>
#include <sys/mman.h>

static void msg(const char *s){int fd=open("/dev/tty0",O_WRONLY);if(fd>=0){write(fd,s,strlen(s));close(fd);}}

static void fill(uint32_t *fb,int s,int x,int y,int w,int h,uint32_t c){
    for(int r=y;r<y+h;r++)for(int col=x;col<x+w;col++)fb[r*s+col]=c;
}

/* Draw text line at position (block font) */
static void draw_line(uint32_t *fb, int stride, int y, const char *text, uint32_t color) {
    for (int i = 0; text[i] && i < 200; i++) {
        if (text[i] != ' ' && text[i] != '\n')
            fill(fb, stride, 10 + i * 8, y, 6, 10, color);
    }
}

int main() {
    mount("devtmpfs","/dev","devtmpfs",0,NULL);
    mount("proc","/proc","proc",0,NULL);
    mount("sysfs","/sys","sysfs",0,NULL);
    mkdir("/data",0755);
    msg("\n*** Dalvik on VNC ***\n");
    sleep(2);

    /* Mount userdata */
    const char *devs[]={"/dev/vda","/dev/vdb","/dev/vdc","/dev/vdd",NULL};
    int ok=0;
    for(int i=0;devs[i];i++){if(mount(devs[i],"/data","ext4",0,NULL)==0){msg("Mounted ");msg(devs[i]);msg("\n");ok=1;break;}}
    if(!ok){msg("No data mount\n");while(1)sleep(3600);}

    mkdir("/data/a2oh/dalvik-cache",0755);

    /* Run dalvikvm, write stdout+stderr to file */
    msg("Starting Dalvik...\n");
    pid_t pid=fork();
    if(pid==0){
        int out=open("/data/a2oh/dalvik_out.txt",O_WRONLY|O_CREAT|O_TRUNC,0666);
        if(out>=0){dup2(out,1);dup2(out,2);close(out);}
        setenv("ANDROID_DATA","/data/a2oh",1);
        setenv("ANDROID_ROOT","/data/a2oh",1);
        execl("/data/a2oh/dalvikvm","dalvikvm",
            "-Xverify:none","-Xdexopt:none",
            "-Xbootclasspath:/data/a2oh/core.jar:/data/a2oh/viewdumper.dex",
            "-classpath","/data/a2oh/viewdumper.dex",
            "ViewDumper",NULL);
        _exit(127);
    }

    /* Wait with timeout — check every 5 seconds */
    msg("Waiting for Dalvik (max 120s)...\n");
    int elapsed=0;
    while(elapsed<120){
        int st;
        int r=waitpid(pid,&st,WNOHANG);
        if(r>0){
            char buf[32];
            int n=0;
            buf[n++]='E';buf[n++]='x';buf[n++]='i';buf[n++]='t';buf[n++]=':';
            buf[n++]='0'+((st>>8)%10);buf[n++]='\n';buf[n]=0;
            msg(buf);
            break;
        }
        sleep(5);
        elapsed+=5;
        msg(".");
    }
    if(elapsed>=120){msg("\nTimeout! Killing...\n");kill(pid,9);waitpid(pid,NULL,0);}

    /* Read output */
    msg("\nReading output...\n");
    char output[65536]={};
    int total=0;
    int ofd=open("/data/a2oh/dalvik_out.txt",O_RDONLY);
    if(ofd>=0){total=read(ofd,output,sizeof(output)-1);close(ofd);}
    if(total<0)total=0;
    output[total]=0;

    char lenbuf[32];
    snprintf(lenbuf,sizeof(lenbuf),"Output: %d bytes\n",total);
    msg(lenbuf);

    /* Open framebuffer */
    int fb=open("/dev/fb0",O_RDWR);
    if(fb<0){msg("No fb0. Output:\n");msg(output);while(1)sleep(3600);}

    struct fb_var_screeninfo vi={};
    struct fb_fix_screeninfo fi={};
    ioctl(fb,FBIOGET_VSCREENINFO,&vi);
    ioctl(fb,FBIOGET_FSCREENINFO,&fi);
    int W=vi.xres,H=vi.yres,sz=fi.line_length*H;
    uint32_t *px=mmap(NULL,sz,PROT_READ|PROT_WRITE,MAP_SHARED,fb,0);
    if(px==MAP_FAILED){msg("mmap fail\n");msg(output);while(1)sleep(3600);}
    int stride=fi.line_length/4;

    /* Clear */
    for(int i=0;i<H*stride;i++)px[i]=0xFF1A1A2E;

    /* Title */
    fill(px,stride,0,0,W,30,0xFF2196F3);
    draw_line(px,stride,8,"MockDonalds - Android View Tree on OHOS ARM32 QEMU",0xFFFFFFFF);

    /* Parse RECT lines */
    int y_pos=40;
    int rect_count=0;
    char *line=output;
    while(*line){
        char *nl=strchr(line,'\n');
        if(nl)*nl=0;

        if(strncmp(line,"RECT ",5)==0){
            int x,y,w,h;
            unsigned int color;
            char type[32]={};
            if(sscanf(line+5,"%d %d %d %d %x %31s",&x,&y,&w,&h,&color,type)>=5){
                if(color!=0)
                    fill(px,stride,x,y+30,w,h,color|0xFF000000);
                rect_count++;
            }
        } else if(strncmp(line,"SCREEN",6)==0 || strncmp(line,"===",3)==0){
            /* skip */
        } else if(strlen(line)>0 && rect_count==0){
            /* Show raw output lines if no RECTs found */
            draw_line(px,stride,y_pos,line,0xFFCCCCCC);
            y_pos+=14;
            if(y_pos>H-40) break;
        }

        line=nl?nl+1:line+strlen(line);
    }

    /* Footer */
    fill(px,stride,0,H-25,W,25,0xFF0D1117);
    char footer[64];
    snprintf(footer,sizeof(footer),"Views: %d | Output: %d bytes | ARM32 QEMU",rect_count,total);
    draw_line(px,stride,H-18,footer,0xFF888888);

    munmap(px,sz);close(fb);
    msg("Done!\n");
    while(1)sleep(3600);
}
