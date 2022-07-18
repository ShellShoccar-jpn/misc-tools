/*####################################################################
#
# GETFILETS - Get Timestamps of Each File
#
# USAGE   : getftimes [options] file [file [...]]
# Options : -9 ... Prints the timestamps to the nanosecond if supported
#           -c ... Prints the timestamps in Calendar-time (YYYYMMDDhhmmss)
#                  in yout timezone (default)
#           -e ... Prints the timestamps in UNIX Epoch time
#           -I ... Prints the timestamps in ISO8601 format
#           -u ... Set the date in UTC when -c option is set
#                  (same as that of date command)
#           -- ... Finishes parsing arguments as options
# Output  : * Print the following 4 fields by each file
#             <atime> <mtime> <ctime> <filename>
#           * The format of each time is either <YYYYMMDDhhmmss> or
#             <YYYY-MM-DDThh:mm:ss+hhmm>.
#           * The latter format is set by -l option.
# Retuen  : Return 0 only when timestamps of all files were able to be
#           gotten.
#
# How to compile : cc -O3 -o __CMDNAME__ __SRCNAME__
#
# Written by Shell-Shoccar Japan (@shellshoccarjpn) on 2022-07-19
#
# This is a public-domain software (CC0). It means that all of the
# people can use this for any purposes with no restrictions at all.
# By the way, We are fed up with the side effects which are brought
# about by the major licenses.
#
# The latest version is distributed at the following page.
# https://github.com/ShellShoccar-jpn/misc-tools
#
####################################################################*/


/*####################################################################
# Initial Configuration
####################################################################*/

/*=== Initial Setting ==============================================*/

/*--- macro constants ----------------------------------------------*/
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/*--- global variables ---------------------------------------------*/
char* gpszCmdname;
int   giVerbose;     /* speaks more verbosely by the greater number */

/*=== Define the functions for printing usage and error ============*/
void print_usage_and_exit(void) {
  fprintf(stderr,
    "Usage   : %s [options] file [file [...]]\n"
    "Options : -9 ... Prints the timestamps to the nanosecond if supported\n"
    "          -c ... Prints the timestamps in Calendar-time (YYYYMMDDhhmmss)\n"
    "                 in yout timezone (default)\n"
    "          -e ... Prints the timestamps in UNIX Epoch time\n"
    "          -I ... Prints the timestamps in ISO8601 format\n"
    "          -u ... Set the date in UTC when -c option is set\n"
    "                 (same as that of date command)\n"
    "          -- ... Finishes parsing arguments as options\n"
    "Output  : * Print the following 4 fields by each file\n"
    "            <atime> <mtime> <ctime> <filename>\n"
    "          * The format of each time is either <YYYYMMDDhhmmss> or\n"
    "            <YYYY-MM-DDThh:mm:ss+hhmm>.\n"
    "          * The latter format is set by -l option.\n"
    "Retuen  : Return 0 only when timestamps of all files were able to be\n"
    "          gotten. \n"
    "Version : 2022-07-19 04:33:38 JST\n"
    "          (POSIX C language)\n"
    "\n"
    "Shell-Shoccar Japan (@shellshoccarjpn), No rights reserved.\n"
    "This is public domain software. (CC0)\n"
    "\n"
    "The latest version is distributed at the following page.\n"
    "https://github.com/ShellShoccar-jpn/misc-tools\n"
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
void warning(const char* szFormat, ...) {
  va_list va;
  va_start(va, szFormat);
  fprintf(stderr,"%s: ",gpszCmdname);
  vfprintf(stderr,szFormat,va);
  va_end(va);
  return;
}


/*####################################################################
# Main
####################################################################*/

/*=== Initial Setting ==============================================*/
int main(int argc, char *argv[]) {

/*--- Variables ----------------------------------------------------*/
struct stat stFileinfo;
struct tm*  pstTm;
char        szBuf[256];
char        szAtim[256], szMtim[256], szCtim[256], szFmt[256], szDummy[256];
int         iFmttype;    /* Long option switch */
int         iNanosec;    /* "in nanosec" flag */
int         i;           /* It means the argument position */
int         iNerror = 0; /* The number of error to get timestamps */

/*--- Initialize ---------------------------------------------------*/
gpszCmdname = argv[0];
giVerbose   = 0;
for (i=0; *(gpszCmdname+i)!='\0'; i++) {
  if (*(gpszCmdname+i)=='/') {gpszCmdname=gpszCmdname+i+1;}
}
if (setenv("POSIXLY_CORRECT","1",1) < 0) {
  error_exit(errno,"setenv() at initialization: \n", strerror(errno));
}

/*=== Parse options ================================================*/

/*--- Set default parameters of the arguments ----------------------*/
iFmttype = 0; /* 0:YYYYMMDDhhmmss 1:UnixTime 2:ISO8601 */
iNanosec = 0; /* 0:second only 1:nanosecond */

/*--- Parse options which start by "-" -----------------------------*/
while ((i=getopt(argc, argv, "9cehIuv")) != -1) {
  switch (i) {
    case '9': iNanosec = 1;                  break;
    case 'c': iFmttype = 0;                  break;
    case 'e': iFmttype = 1;                  break;
    case 'I': iFmttype = 2;                  break;
    case 'u': (void)setenv("TZ", "UTC0", 1); break;
    case 'v': giVerbose++;                   break;
    case 'h': print_usage_and_exit();
    default : print_usage_and_exit();
  }
}
argc -= optind;
argv += optind;

/*--- print the usage if no filename has given ---------------------*/
if (argc < 1) { print_usage_and_exit(); }

/*--- Warn if use "-9" option on nanosecond timestamp non-supported OS */
#ifndef st_atime
if (iNanosec) {
  warning("Warning: This OS does't support the nanosec timestamp\n");
}
#endif

/*=== Swtich to the long format mode if -l option has been set =====*/
switch (iFmttype*2+iNanosec) {
  case  0*2+0: strcpy(szFmt  ,"%Y%m%d%H%M%S"              );
               strcpy(szDummy,"%-14s %-14s %-14s "        );
               break;
  case  0*2+1: strcpy(szFmt  ,"%Y%m%d%H%M%S.%%09ld"       );
               strcpy(szDummy,"%-24s %-24s %-24s "        );
               break;
  case  1*2+0: strcpy(szFmt  ,"%s"                        );
               strcpy(szDummy,"%-10s %-10s %-10s "        );
               break;
  case  1*2+1: strcpy(szFmt  ,"%s.%%09ld"                 );
               strcpy(szDummy,"%-20s %-20s %-20s "        );
               break;
  case  2*2+0: strcpy(szFmt  ,"%Y-%m-%dT%H:%M:%S%z"       );
               strcpy(szDummy,"%-24s %-24s %-24s "        );
               break;
  case  2*2+1: strcpy(szFmt  ,"%Y-%m-%dT%H:%M:%S,%%09ld%z");
               strcpy(szDummy,"%-34s %-34s %-34s "        );
               break;
  default: error_exit(1, "Unexpected Error!\n");
}

/*=== Main loop ====================================================*/
for (i=0; i<argc; i++) {
  if (stat(argv[i],&stFileinfo)==0) {
    if (!iNanosec) {
      pstTm = localtime(&stFileinfo.st_atime);
      strftime(szAtim, 256, szFmt, pstTm);
      pstTm = localtime(&stFileinfo.st_mtime);
      strftime(szMtim, 256, szFmt, pstTm);
      pstTm = localtime(&stFileinfo.st_ctime);
      strftime(szCtim, 256, szFmt, pstTm);
      printf("%s %s %s ",szAtim,szMtim,szCtim);
    } else         {
      pstTm = localtime(&stFileinfo.st_atime);
      strftime(szBuf, 256, szFmt, pstTm);
      #ifdef st_atime
        sprintf(szAtim, szBuf, stFileinfo.st_atim.tv_nsec);
      #else
        sprintf(szAtim, szBuf,                          0);
      #endif
      pstTm = localtime(&stFileinfo.st_mtime);
      strftime(szBuf, 256, szFmt, pstTm);
      #ifdef st_mtime
        sprintf(szMtim, szBuf, stFileinfo.st_mtim.tv_nsec);
      #else
        sprintf(szMtim, szBuf,                          0);
      #endif
      pstTm = localtime(&stFileinfo.st_ctime);
      strftime(szBuf, 256, szFmt, pstTm);
      #ifdef st_ctime
        sprintf(szCtim, szBuf, stFileinfo.st_ctim.tv_nsec);
      #else
        sprintf(szCtim, szBuf,                          0);
      #endif
      printf("%s %s %s ",szAtim,szMtim,szCtim);
    }
  } else {
    if (giVerbose>0) {warning("%s: Failed to get its timestamp\n",argv[i]);}
    iNerror++;
    printf(szDummy, "-", "-", "-");
  }
  printf("%s\n",argv[i]);
}

/*=== Finish =======================================================*/
if (iNerror>0) {
  warning("Warning: Couldn't get timestamps of 1 file(s).\n",iNerror);
  return 1;
}
return 0;}
