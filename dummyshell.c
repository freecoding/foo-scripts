#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <time.h>
#ifdef _HAS_SIGNAL
#include <signal.h>      /* signal(...) */
#else
#warn "SIGNAL disabled!"
#endif
#if defined(_HAS_UTMP) || defined(_HAS_SYSINFO)
#include <string.h>      /* memset */
#endif
#ifdef _HAS_UTMP
#include <utmp.h>        /* utmp structure */
#else
#warn "UTMP disabled!"
#endif
#ifdef _HAS_SYSINFO
#include "sys/types.h"   /* sysinfo structture */
#include "sys/sysinfo.h" /* sysinfo(...) */
#else
#warn "SYSINFO disabled!"
#endif

/* for print_memusage() and print cpuusage() see: http://stackoverflow.com/questions/63166/how-to-determine-cpu-and-memory-consumption-from-inside-a-process */


static const char keymsg[] = " [PRESS ANY KEY TO QUIT] ";
static const char txtheader[] =
  "**************\n"
  "* dummyshell *\n"
  "**************\n";

static volatile unsigned char doLoop = 1;


#ifdef _HAS_UTMP
static void print_utmp(void)
{
  int utmpfd = open("/var/run/utmp", O_RDONLY);
  if (utmpfd >= 0) {
    struct utmp ut;
    memset(&ut, '\0', sizeof(struct utmp));
    printf("\33[2K\ruser online...: ");
    while (read(utmpfd, &ut, sizeof(struct utmp)) == sizeof(struct utmp)) {
      printf("%.*s ", UT_NAMESIZE, ut.ut_user);
    }
    printf("\n");
  }
}
#endif

#ifdef _HAS_SYSINFO
static unsigned long long lastTotalUser, lastTotalUserLow, lastTotalSys, lastTotalIdle;

static void init_cpuusage(){
  FILE* file = fopen("/proc/stat", "r");
  if (file) {
    fscanf(file, "cpu %llu %llu %llu %llu", &lastTotalUser, &lastTotalUserLow,
      &lastTotalSys, &lastTotalIdle);
    fclose(file);
  }
}

static void print_cpuusage(){
  double percent;
  FILE* file;
  unsigned long long totalUser, totalUserLow, totalSys, totalIdle, total;

  file = fopen("/proc/stat", "r");
  fscanf(file, "cpu %llu %llu %llu %llu", &totalUser, &totalUserLow,
    &totalSys, &totalIdle);
  fclose(file);

  if (totalUser < lastTotalUser || totalUserLow < lastTotalUserLow ||
    totalSys < lastTotalSys || totalIdle < lastTotalIdle){
    //Overflow detection. Just skip this value.
    percent = -1.0;
  } else{
    total = (totalUser - lastTotalUser) + (totalUserLow - lastTotalUserLow) +
      (totalSys - lastTotalSys);
    percent = total;
    total += (totalIdle - lastTotalIdle);
    percent /= total;
    percent *= 100;
  }

  lastTotalUser = totalUser;
  lastTotalUserLow = totalUserLow;
  lastTotalSys = totalSys;
  lastTotalIdle = totalIdle;

  printf("CPU...........: %.02f%%\n", percent);
}

static void print_memusage(void)
{
  struct sysinfo meminfo;
  memset(&meminfo, '\0', sizeof(struct sysinfo));
  if (sysinfo(&meminfo) == 0) {
    unsigned long long totalvmem = meminfo.totalram;
    totalvmem += meminfo.totalswap;
    totalvmem *= meminfo.mem_unit;
    unsigned long long usedvmem = meminfo.totalram - meminfo.freeram;
    usedvmem += meminfo.totalswap - meminfo.freeswap;
    usedvmem *= meminfo.mem_unit;
    printf("VMEM(used/max): %llu/%lld (Mb)\n", (usedvmem/(1024*1024)), (totalvmem/(1024*1024)));
  }
}
#endif

#ifdef _HAS_SIGNAL
void SigIntHandler(int signum)
{
  if (signum == SIGINT) {
    doLoop = 0;
  }
}
#endif

int main(int argc, char** argv)
{
  struct timeval tv;
  tv.tv_sec = 1;
  tv.tv_usec = 0;

#ifdef _HAS_SIGNAL
  signal(SIGINT, SigIntHandler);
#endif
#ifdef _HAS_SYSINFO
  init_cpuusage();
#endif
  int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
  fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

  static struct termios oldt, newt;
  tcgetattr(STDIN_FILENO, &oldt);
  newt = oldt;
  newt.c_lflag &= ~(ICANON | ECHO);
  tcsetattr(STDIN_FILENO, TCSANOW, &newt);

  printf("%s\n", txtheader);
  fd_set fds;
  time_t start = time(NULL);
  time_t cur;
  while (1) {
    cur = time(NULL);
    double diff = difftime(cur, start);
#if defined(_HAS_UTMP) || defined(_HAS_SYSINFO)
    if ((unsigned int)diff % 60 == 0) {
      struct tm localtime;
      if (localtime_r(&cur, &localtime) != NULL) {
        printf("--- %02d:%02d:%02d ---\n", localtime.tm_hour, localtime.tm_min, localtime.tm_sec);
      }
#ifdef _HAS_UTMP
      print_utmp();
#endif
#ifdef _HAS_SYSINFO
      print_memusage();
      print_cpuusage();
#endif
    }
#endif
    printf("\r( %0.fs )%s", diff, keymsg);
    fflush(stdout);
    fflush(stdin);
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    int ret = select(FD_SETSIZE, &fds, NULL, NULL, &tv);
    if (doLoop == 1 && ret == 0) {
      tv.tv_sec = 1;
      tv.tv_usec = 0;
    } else {
#ifdef _HAS_SIGNAL
      signal(SIGINT, SIG_IGN);
#endif
      printf("quit in 3 .. ");
      fflush(stdout);
      sleep(1);
      printf("2 .. ");
      fflush(stdout);
      sleep(1);
      printf("1 .. ");
      fflush(stdout);
      sleep(1);
      printf("\n");
      break;
    }
    if (FD_ISSET(STDIN_FILENO,&fds)) break;
  }
  while (getchar() != EOF) {}

  tcsetattr( STDIN_FILENO, TCSANOW, &oldt);

  return 0;
}