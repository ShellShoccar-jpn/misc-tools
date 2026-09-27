/*####################################################################
#
# SLEEP - Sleep Command Which Supported Non-Integer Numbers
#
# USAGE   : sleep duration
# Args    : duration ... The length of the time to sleep for. You can
#                        give not only an integer number but also a
#                        non-integer number here.
#                        You can use the format "[-]A[.B][u]" for it.
#                          "A" is the integer part of the time.
#                          "B" is the decimal part of the time.
#                          "u" is the unit for the time. You can choose
#                              one of the followings.
#                              "s" (default), "ms", "us", "ns", "m",
#                              "h" and "d."
#                          If you give it a negative value (with a
#                          leading "-"), this command does not sleep
#                          at all; it just exits immediately with 0.
# Retuen  : Return 0 only when succeeded to sleep
#
# How to compile : cc -O3 -std=c99 -o __CMDNAME__ __SRCNAME__
#
# Written by Shell-Shoccar Japan (@shellshoccarjpn) on 2026-09-24
#
# This is a public-domain software (CC0). It means that all of the
# people can use this for any purposes with no restrictions at all.
# By the way, We are fed up with the side effects which are brought
# about by the major licenses.
#
# The latest version is distributed at the following page.
# https://github.com/ShellShoccar-jpn/tokideli
#
####################################################################*/


/*####################################################################
# Initial Configuration
####################################################################*/

/*=== Initial Setting ==============================================*/
/* Solaris 11.3's <sys/feature_tests.h> only recognizes the exact
 * values _XOPEN_SOURCE==600 / _POSIX_C_SOURCE==200112L for its UNIX 03
 * detection and has no notion of POSIX.1-2008/SUSv4 at all; requesting
 * 700/200809L there trips its strict conformance-level check and
 * aborts the build, so __EXTENSIONS__ (which sidesteps that check
 * entirely and exposes every POSIX/XSI/BSD interface regardless of C
 * standard level) is used there instead. Everywhere else, we ask for
 * _XOPEN_SOURCE 700 rather than _POSIX_C_SOURCE 200809L alone: on
 * FreeBSD, _POSIX_C_SOURCE alone leaves __XSI_VISIBLE unset, hiding
 * XSI interfaces (e.g. SA_SIGINFO/sa_sigaction, S_IFMT/S_IFREG) that
 * some of these commands need; _XOPEN_SOURCE 700 enables both.       */
#if defined(__sun) || defined(__SVR4)
  #define __EXTENSIONS__
#else
  #define _XOPEN_SOURCE 700 /* for nanosleep() */
#endif
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

/*--- macro constants ----------------------------------------------*/
/* Buffer size for the unit string of the "duration" argument */
#define OPT_PARM_BUF 32

/*--- prototype functions ------------------------------------------*/
int64_t parse_periodictime(char *pszArg);

char* gpszCmdname;

/*=== Define the functions for printing usage and error ============*/
void print_usage_and_exit(void) {
  fprintf(stderr,
    "USAGE   : %s duration\n"
    "Args    : duration ... The length of the time to sleep for. You can\n"
    "                       give not only an integer number but also a\n"
    "                       non-integer number here.\n"
    "                       You can use the format \"[-]A[.B][u]\" for it.\n"
    "                         \"A\" is the integer part of the time.\n"
    "                         \"B\" is the decimal part of the time.\n"
    "                         \"u\" is the unit for the time. You can choose\n"
    "                             one of the followings.\n"
    "                             \"s\" (default), \"ms\", \"us\", \"ns\", \"m\",\n"
    "                             \"h\" and \"d.\"\n"
    "                         If you give it a negative value (with a\n"
    "                         leading \"-\"), this command does not sleep\n"
    "                         at all; it just exits immediately with 0.\n"
    "Retuen  : Return 0 only when succeeded to sleep\n"
    "Version : 2026-09-24 01:09:03 JST\n"
    "          (POSIX C language)\n"
    "\n"
    "Shell-Shoccar Japan (@shellshoccarjpn), No rights reserved.\n"
    "This is public domain software. (CC0)\n"
    "\n"
    "The latest version is distributed at the following page.\n"
    "https://github.com/ShellShoccar-jpn/tokideli\n"
    ,gpszCmdname);
  exit(1);
}
void error_exit(int iErrno, const char* szFormat, ...) {
  va_list va;
  va_start(va, szFormat);
  fprintf(stderr,"%s: ",gpszCmdname);
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
  char    *pszArg;
  int64_t  i8Dur;
  int      iNeg;
  int      i,iRet;

  gpszCmdname = argv[0];
  for (i=0; *(gpszCmdname+i)!='\0'; i++) {
    if (*(gpszCmdname+i)=='/') {gpszCmdname=gpszCmdname+i+1;}
  }

  /*=== Parse options ==============================================*/
  if (argc != 2) {print_usage_and_exit();}
  pszArg = argv[1];
  iNeg   = 0;
  if (*pszArg=='-') {iNeg=1; pszArg++;}
  /* "pszArg" is never negative here, so the "-2" that parse_periodictime()
     returns on failure can never collide with a legitimate negative
     duration value (which is only ever reintroduced below by "iNeg"). */
  i8Dur = parse_periodictime(pszArg);
  if (i8Dur < 0) {print_usage_and_exit();}
  if (iNeg) {i8Dur = -i8Dur;}

  /*=== Sleep =======================================================
   * A duration of zero or less (including a negative one, given with
   * a leading "-") is not an error; it just means "sleep for no time."*/
  if (i8Dur <= 0                                  ) {exit(0);               }
  tspcSleeping_time.tv_sec  = (time_t)(i8Dur / 1000000000);
  tspcSleeping_time.tv_nsec =   (long)(i8Dur % 1000000000);

  iRet = nanosleep(&tspcSleeping_time, NULL);
  if (iRet != 0) {error_exit(iRet,"Error happend while nanosleeping\n");}

  /*=== Finish =====================================================*/
  return 0;
}


/*####################################################################
# Functions
####################################################################*/

/*=== Parse the periodic time ========================================
 * [ret] >= 0  : Interval value (in nanosecound)
 *       <=-1  : (undefined)
 *       <=-2  : It is not a value                                  */
int64_t parse_periodictime(char *pszArg) {

  /*--- Variables --------------------------------------------------*/
  char   szUnit[OPT_PARM_BUF];
  double dNum;

  /*--- Check the lengths of the argument --------------------------*/
  if (strlen(pszArg) >= OPT_PARM_BUF) {return -2;}

  /*--- Try to interpret the argument as "<value>"[+"unit"] --------*/
  switch (sscanf(pszArg, "%lf%s", &dNum, szUnit)) {
    case   2:                      break;
    case   1: strcpy(szUnit, "s"); break;
    default : return -2;
  }
  if (dNum < 0                     ) {return -2;}

  /* as a second value */
  if (strcmp(szUnit, "s" )==0) {
    if (dNum > ((double)INT_MAX             )) {return -2;}
    return       (int64_t)(dNum * 1000000000);
  }

  /* as a millisecond value */
  if (strcmp(szUnit, "ms")==0) {
    if (dNum > ((double)INT_MAX *       1000)) {return -2;}
    return       (int64_t)(dNum *    1000000);
  }

  /* as a microsecond value */
  if (strcmp(szUnit, "us")==0) {
    if (dNum > ((double)INT_MAX *    1000000)) {return -2;}
    return       (int64_t)(dNum *       1000);
  }

  /* as a nanosecond value */
  if (strcmp(szUnit, "ns")==0) {
    if (dNum > ((double)INT_MAX * 1000000000)) {return -2;}
    return       (int64_t)(dNum *          1);
  }

  /* as a minute value */
  if (strcmp(szUnit, "m" )==0) {
    if (dNum > ((double)INT_MAX /         60)) {return -2;}
    return       (int64_t)(dNum *   60000000000LL);
  }

  /* as an hour value */
  if (strcmp(szUnit, "h" )==0) {
    if (dNum > ((double)INT_MAX /       3600)) {return -2;}
    return       (int64_t)(dNum * 3600000000000LL);
  }

  /* as a day value */
  if (strcmp(szUnit, "d" )==0) {
    if (dNum > ((double)INT_MAX /      86400)) {return -2;}
    return       (int64_t)(dNum * 86400000000000LL);
  }

  /*--- Otherwise, it is not a value -------------------------------*/
  return -2;
}
