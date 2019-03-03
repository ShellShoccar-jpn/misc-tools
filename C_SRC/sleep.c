/*####################################################################
#
# SLEEP - Sleep Command Which Supported Non-Integer Numbers
#
# USAGE   : sleep seconds
# Args    : seconds ... The number of second to sleep for. You can
#                       give not only an integer number but also a
#                       non-integer number here.
# Retuen  : Return 0 only when succeeded to sleep
#
# Written by Shell-Shoccar Japan (@shellshoccarjpn) on 2019-03-04
#
# This is a public-domain software (CC0). It means that all of the
# people can use this for any purposes with no restrictions at all.
# By the way, We are fed up with the side effects which are brought
# about by the major licenses.
#
####################################################################*/


/*####################################################################
# Initial Configuration
####################################################################*/

/*=== Initial Setting ==============================================*/
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#define WRN(message) fprintf(stderr,message)
#define WRV(fmt,...) fprintf(stderr,fmt,__VA_ARGS__)

char* gpszCmdname;

/*=== Define the functions for printing usage and error ============*/
void print_usage_and_exit(void) {
  WRV(
    "USAGE   : %s seconds\n"
    "Args    : seconds ... The number of second to sleep for. You can\n"
    "                      give not only an integer number but also a\n"
    "                      non-integer number here.\n"
    "Retuen  : Return 0 only when succeeded to sleep\n"
    "Version : 2019-03-04 00:24:33 JST\n"
    "          (POSIX C language)\n"
    ,gpszCmdname);
  exit(1);
}
void error_exit(int iErrno, const char* szFormat, ...) {
  va_list va;
  va_start(va, szFormat);
  WRV("%s: ",gpszCmdname);
  vfprintf(stderr,szFormat,va);
  va_end(va);
  exit(iErrno);
}


/*####################################################################
# Main
####################################################################*/

int main(int argc, char *argv[]) {

  /*=== Initial Setting ============================================*/
  struct timespec tspcSleeping_time;
  double dNum;
  int    i,iRet;

  gpszCmdname = argv[0];
  for (i=0; *(gpszCmdname+i)!='\0'; i++) {
    if (*(gpszCmdname+i)=='/') {gpszCmdname=gpszCmdname+i+1;}
  }

  /*=== Parse options ==============================================*/
  if (argc != 2                            ) {print_usage_and_exit();}
  if (sscanf(argv[1], "%lf%1s", &dNum) != 1) {print_usage_and_exit();}
  if (dNum > INT_MAX                       ) {print_usage_and_exit();}

  /*=== Sleep ======================================================*/
  if (dNum <= 0                                   ) {exit(0);               }
  tspcSleeping_time.tv_sec  = (time_t)dNum;
  tspcSleeping_time.tv_nsec = (dNum - tspcSleeping_time.tv_sec) * 1000000000;

  iRet = nanosleep(&tspcSleeping_time, NULL);
  if (iRet != 0) {error_exit(iRet,"Error happend while nanosleeping\n");}

  /*=== Finish =====================================================*/
  exit(0);
}
