/*####################################################################
#
# GETFTIMES - Get Timestamps of Each File (Outdated)
#
# USAGE   : getftimes [options] file [file ...]
# Options : -l ... Prints the timestamps in ISO8601 format
#           -u ... Prints the timestamps in UNIX time
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
# Written by Shell-Shoccar Japan (@shellshoccarjpn) on 2022-06-26
#
# This is a public-domain software (CC0). It means that all of the
# people can use this for any purposes with no restrictions at all.
# By the way, We are fed up with the side effects which are brought
# about by the major licenses.
#
# WE WON'T UPDATE THIS COMMEND ANY LONGER.
# USE "getfilets" COMMAND INSTEAD.
#
####################################################################*/


/*####################################################################
# Initial Configuration
####################################################################*/

/*=== Initial Setting ==============================================*/
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <sys/stat.h>

char* gpszCmdname;

/*=== Define the functions for printing usage and error ============*/
void print_usage_and_exit(void) {
  fprintf(stderr,
    "Usage   : %s [options] file [file ...]\n"
    "Options : -l ... Prints the timestamps in ISO8601 format\n"
    "          -u ... Prints the timestamps in UNIX time\n"
    "          -- ... Finishes parsing arguments as options\n"
    "Output  : * Print the following 4 fields by each file\n"
    "            <atime> <mtime> <ctime> <filename>\n"
    "          * The format of each time is either <YYYYMMDDhhmmss> or\n"
    "            <YYYY-MM-DDThh:mm:ss+hhmm>.\n"
    "          * The latter format is set by -l option.\n"
    "Retuen  : Return 0 only when timestamps of all files were able to be\n"
    "          gotten. \n"
    "Version : 2022-06-26 00:50:36 JST\n"
    "          (POSIX C language)\n"
    "\n"
    "Shell-Shoccar Japan (@shellshoccarjpn), No rights reserved.\n"
    "This is public domain software. (CC0)\n"
    "\n"
    "WE WON'T UPDATE THIS COMMEND ANY LONGER.\n"
    "USE \"getfilets\" COMMAND INSTEAD.\n"
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

int main(int argc, char *argv[]){

  /*=== Initial Setting ============================================*/
  struct stat stFileinfo;
  struct tm*  pstTm;
  char        szAtim[256], szMtim[256], szCtim[256], szFmt[256], szDummy[256];
  int         iFmttype;    /* Long option switch */
  int         i;           /* It means the argument position */
  int         iNerror = 0; /* The number of error to get timestamps */

  gpszCmdname = argv[0];
  for (i=0; *(gpszCmdname+i)!='\0'; i++) {
    if (*(gpszCmdname+i)=='/') {gpszCmdname=gpszCmdname+i+1;}
  }
  if (setenv("POSIXLY_CORRECT","1",1) < 0) {
    error_exit(errno,"setenv() at initialization: \n", strerror(errno));
  }

  /*=== Parse options ==============================================*/
  /*--- initialize option parameters -------------------------------*/
  iFmttype = 0; /* 0:YYYYMMDDhhmmss 1:ISO8601 2:UnixTime */
  /*--- get them ---------------------------------------------------*/
  for (i=1; i<argc; i++) {
    if        (strcmp (argv[i], "--"  )==0) {
      i++;
      break;
    } else if (strcmp (argv[i], "-l"  )==0) {
      iFmttype = 1;
    } else if (strcmp (argv[i], "-u"  )==0) {
      iFmttype = 2;
    } else if (strncmp(argv[i], "-" ,1)==0) {
      print_usage_and_exit();
    } else {
      break;
    }
  }
  /*--- print the usage if no filename has given -------------------*/
  if (i >= argc) { print_usage_and_exit(); }

  /*=== Swtich to the long format mode if -l option has been set ===*/
  switch (iFmttype) {
    case  0: strcpy(szFmt  ,"%Y%m%d%H%M%S"       );
             strcpy(szDummy,"%-14s %-14s %-14s " );
             break;
    case  1: strcpy(szFmt  ,"%Y-%m-%dT%H:%M:%S%z");
             strcpy(szDummy,"%-24s %-24s %-24s " );
             break;
    case  2: strcpy(szFmt  ,"%s"                 );
             strcpy(szDummy,"%-10s %-10s %-10s " );
             break;
    default: error_exit(1, "Unexpected Error!\n");
  }

  /*=== Main loop ==================================================*/
  for (   ; i<argc; i++) {
    if (stat(argv[i],&stFileinfo)==0) {
      pstTm = localtime(&stFileinfo.st_atime); /* Not use st_*tim.tv_sec for */
      strftime(szAtim, 256, szFmt, pstTm);     /* compatibility with earlier */
      pstTm = localtime(&stFileinfo.st_mtime); /* versions */
      strftime(szMtim, 256, szFmt, pstTm);
      pstTm = localtime(&stFileinfo.st_ctime);
      strftime(szCtim, 256, szFmt, pstTm);
      printf("%s %s %s ",szAtim,szMtim,szCtim);
    } else {
      iNerror++;
       printf(szDummy, "-", "-", "-");
    }
    printf("%s\n",argv[i]);
  }

  /*=== Finish =====================================================*/
  return (iNerror==0) ? 0 : 1;
}
