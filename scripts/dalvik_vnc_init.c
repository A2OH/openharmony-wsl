#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <stdio.h>

static void msg(const char *s){int fd=open("/dev/tty0",O_WRONLY);if(fd>=0){write(fd,s,strlen(s));close(fd);}}

static void fill(uint32_t *fb,int s,int scrW,int scrH,int x,int y,int w,int h,uint32_t c){
    if(x<0)x=0; if(y<0)y=0;
    for(int r=y;r<y+h&&r<scrH;r++)
        for(int col=x;col<x+w&&col<scrW;col++)
            fb[r*s+col]=c;
}

static void draw_line(uint32_t *fb,int stride,int scrW,int scrH,int y,const char *text,uint32_t color){
    for(int i=0;text[i]&&i<200;i++){
        if(text[i]!=' '&&text[i]!='\n')
            fill(fb,stride,scrW,scrH,10+i*8,y,6,10,color);
    }
}

int main(){
    mount("devtmpfs","/dev","devtmpfs",0,NULL);
    mount("proc","/proc","proc",0,NULL);
    mount("sysfs","/sys","sysfs",0,NULL);
    mkdir("/data",0755);
    msg("\n*** Dalvik ViewDumper on VNC ***\n");
    sleep(2);

    /* Mount userdata */
    const char *devs[]={"/dev/vda","/dev/vdb","/dev/vdc","/dev/vdd",NULL};
    int ok=0;
    for(int i=0;devs[i];i++){
        if(mount(devs[i],"/data","ext4",0,NULL)==0){
            msg("Mounted "); msg(devs[i]); msg("\n");
            ok=1; break;
        }
    }
    if(!ok){msg("No data mount\n");while(1)sleep(3600);}

    mkdir("/data/a2oh/dalvik-cache",0755);

    /* Clear old output */
    unlink("/data/a2oh/viewdump.txt");

    /* Run dalvikvm — stderr to /dev/null, stdout to dalvik_stdout.txt */
    msg("Starting Dalvik ViewDumper...\n");
    pid_t pid=fork();
    if(pid==0){
        /* Redirect stdout to file (for any System.out.println) */
        int out=open("/data/a2oh/dalvik_stdout.txt",O_WRONLY|O_CREAT|O_TRUNC,0666);
        if(out>=0){dup2(out,1);close(out);}
        /* Redirect stderr to /dev/null (suppress verbose dalvikvm logs) */
        int devnull=open("/dev/null",O_WRONLY);
        if(devnull>=0){dup2(devnull,2);close(devnull);}

        setenv("ANDROID_DATA","/data/a2oh",1);
        setenv("ANDROID_ROOT","/data/a2oh",1);
        execl("/data/a2oh/dalvikvm","dalvikvm",
            "-Xverify:none","-Xdexopt:none",
            "-Xbootclasspath:/data/a2oh/core.jar:/data/a2oh/viewdumper.dex",
            "-classpath","/data/a2oh/viewdumper.dex",
            "ViewDumper",NULL);
        _exit(127);
    }

    /* Wait with timeout */
    msg("Waiting for Dalvik (max 180s)...\n");
    int elapsed=0;
    while(elapsed<180){
        int st;
        int r=waitpid(pid,&st,WNOHANG);
        if(r>0){
            char buf[32];
            snprintf(buf,sizeof(buf),"Dalvik exit: %d\n",WEXITSTATUS(st));
            msg(buf);
            break;
        }
        sleep(5);
        elapsed+=5;
        if(elapsed%30==0) msg(".");
    }
    if(elapsed>=180){msg("\nTimeout!\n");kill(pid,9);waitpid(pid,NULL,0);}

    /* Read ViewDumper output (written by ViewDumper via FileOutputStream) */
    msg("\nReading viewdump.txt...\n");
    char output[65536]={};
    int total=0;
    int ofd=open("/data/a2oh/viewdump.txt",O_RDONLY);
    if(ofd>=0){
        total=read(ofd,output,sizeof(output)-1);
        if(total<0)total=0;
        close(ofd);
    }
    output[total]=0;

    /* Strip null bytes (ViewDumper's manual byte conversion may include them) */
    int clean=0;
    for(int i=0;i<total;i++){
        if(output[i]!=0) output[clean++]=output[i];
    }
    output[clean]=0;
    total=clean;

    char lenbuf[64];
    snprintf(lenbuf,sizeof(lenbuf),"ViewDumper output: %d bytes\n",total);
    msg(lenbuf);

    /* Show first few lines on tty for debug */
    if(total>0){
        char preview[512]={};
        int n=total<500?total:500;
        memcpy(preview,output,n);
        preview[n]=0;
        msg("Preview:\n");
        msg(preview);
        msg("\n---\n");
    }

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

    /* Clear to dark background */
    for(int i=0;i<H*stride;i++) px[i]=0xFF1A1A2E;

    /* Title bar */
    fill(px,stride,W,H,0,0,W,30,0xFF2196F3);
    draw_line(px,stride,W,H,8,"MockDonalds - Android View Tree on OHOS ARM32 QEMU",0xFFFFFFFF);

    /* Parse RECT lines from ViewDumper output */
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
                    fill(px,stride,W,H,x,y+30,w,h,color|0xFF000000);
                rect_count++;
            }
        } else if(strlen(line)>0 && rect_count==0 && y_pos<H-40){
            /* Show text lines on screen if no RECTs found yet */
            draw_line(px,stride,W,H,y_pos,line,0xFFCCCCCC);
            y_pos+=14;
        }

        line=nl?nl+1:line+strlen(line);
    }

    /* Footer */
    fill(px,stride,W,H,0,H-25,W,25,0xFF0D1117);
    char footer[96];
    snprintf(footer,sizeof(footer),"Views: %d | Output: %d bytes | ARM32 QEMU VNC",rect_count,total);
    draw_line(px,stride,W,H,H-18,footer,0xFF888888);

    munmap(px,sz);close(fb);
    msg("Done!\n");
    while(1)sleep(3600);
}
