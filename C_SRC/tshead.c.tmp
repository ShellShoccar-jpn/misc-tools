/*####################################################################
#
# TSHEAD - A "head" Command Which Considers Timestamp Instead of
#          the Number of Lines
#
# USAGE   : (a) tshead [options] -i   interval      [file ...]
#           (b) tshead [options] -ix  interval      [file ...]
#           (c) tshead [options] -i  -interval      [file ...]
#           (d) tshead [options] -ix -interval      [file ...]
#           (e) tshead [options] -t   date-and-time [file ...]
#           (f) tshead [options] -tx  date-and-time [file ...]
#
#           The lines that can pass through this command will be chosen
#           by making sure the timestamp at the first field of each line
#           is in one of the following ranges.
#             (a) [ <top>, <command start time>+<interval> ]
#             (b) [ <top>, <command start time>+<interval> )
#             (c) [ <top>, <last line's time>  -<interval> ]
#             (d) [ <top>, <last line's time>  -<interval> )
#             (e) [ <top>, <date-and-time>                 ]
#             (f) [ <top>, <date-and-time>                 )
#
# Args    : file ........ Filepath to be send ("-" means STDIN)
#                         The file MUST be a textfile and MUST have
#                         a timestamp at the first field to make the
#                         timing of flow. The first space character
#                         <0x20> of every line will be regarded as
#                         the field delimiter.
# Options : -c,-e,-z .... Specify the format for timestamp and -t option
#                         parameter. You can choose one of the following.
#                           -c ... "YYYYMMDDhhmmss[.n]" (default)
#                                  Calendar time (standard time) in your
#                                  timezone (".n" is the digits under
#                                  second. You can specify up to nano
#                                  second.)
#                           -e ... "n[.n]"
#                                  The number of seconds since the UNIX
#                                  epoch (".n" is the same as -c)
#                           -z ... "n[.n]"
#                                  The number of seconds since this
#                                  command has startrd (".n" is the same
#                                  as -c)
#           -i interval . This is one of options to specify the timestamp
#                         range. (See the pattern (a) to (d) above)
#                         You can use the format "A[.B][u]" as the
#                         option's parameter "interval."
#                           "A" is the integer part of the time.
#                           "B" is the decimal part of the time.
#                           "u" is the unit for the time. You can choose
#                               one of the followings.
#                               "s", "ms", "us" and "ns."
#           -t date-and-time
#                         This is one of options to specify the timestamp
#                         range. (See the pattern (e) and (f) above)
#                         The format of "date-and-time" depends on
#                         which of the option "-c", "-e," or "-z"
#                         you choose.
#                           "-c" ... "YYYYMMDDhhmmss[.n]" (cal. time)
#                           "-e" ... "n[.n]" (UNIX time)
#                           "-z" ... "n[.n]" (the number of seconds)
#           -q .......... Suppresses printing filenames when two or more
#                         files are given.
#           -u .......... Set the date in UTC when -c option is set
#                         (same as that of date command)
#           -x .......... An additional option for -i and -t. It will
#                         exclude the endpoint itself from the range.
#                         (See the pattern (b), (d) and (f) above)
#           -Z .......... Define the time when the first line came as 0.
#                         For instance, imagine that the first field of
#                         the first line is "20200229235959," and the
#                         second line's one is "20200301000004." when
#                         "-c" option is given. In this case, the first
#                         line is sent to stdout immediately, and after
#                         five seconds, the second line is sent.
# Retuen  : Return 0 only when finished successfully for all files
#
# How to compile : cc -O3 -o __CMDNAME__ __SRCNAME__ -lrt
#                  (if it doesn't work)
# How to compile : cc -O3 -o __CMDNAME__ __SRCNAME__
#
# Written by Shell-Shoccar Japan (@shellshoccarjpn) on 2021-03-22
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
#define MY_REV "2021-03-22 12:31:01 JST"

/*--- headers ------------------------------------------------------*/
#if defined(__linux) || defined(__linux__)
  /* This definition is for strptime() on Linux */
  #define _XOPEN_SOURCE 700
#endif
#include <limits.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
//#include <stdint.h>
#include <stdarg.h>
#include <unistd.h>
extern char *optarg;
extern int optind, opterr, optopt;
//#include <sys/select.h>
#include <time.h>
//#include <fcntl.h>
#include <locale.h>

/*--- macro constants ----------------------------------------------*/
/* Some OSes, such as HP-UX, may not know the following macros whenever
   <limit.h> is included. So, this source code defines them by itself. */
#ifndef LONG_MAX
  #define LONG_MAX           2147483647
#endif
#ifndef LLONG_MAX
  #define LLONG_MAX 9223372036854775807
#endif
/* Buffer size for the option parameter */
#define OPT_PARM_BUF 32

/*--- structure definitions ----------------------------------------*/
typedef struct listedline {
  char              *pszStr;
  size_t             sizStr;
  struct timespec    ts;
  struct listedline *pstNext;
} lline_t;

/*--- prototype functions ------------------------------------------*/
int parse_calendartime(char* pszTime, struct timespec *ptsTime);
int parse_unixtime(char* pszTime, struct timespec *ptsTime);
int64_t parse_periodictime(char *pszArg);
int push_a_line_into_lline(char *szHead, struct timespec *pts,
                           FILE *fp    , lline_t *pllLast     );
void flush_lline(lline_t *pllFirst);

/*--- global variables ---------------------------------------------*/
char*           gpszCmdname; /* The name of this command                    */
int             giVerbose;   /* speaks more verbosely by the greater number */
struct timespec gtsZero;     /* The zero-point time                         */

/*=== Define the functions for printing usage and error ============*/

/*--- exit with usage ----------------------------------------------*/
void print_usage_and_exit(void) {
  fprintf(stderr,
    "USAGE   : (a) %s [options] -i   interval      [file ...]\n"
    "          (b) %s [options] -ix  interval      [file ...]\n"
    "          (c) %s [options] -i  -interval      [file ...]\n"
    "          (d) %s [options] -ix -interval      [file ...]\n"
    "          (e) %s [options] -t   date-and-time [file ...]\n"
    "          (f) %s [options] -tx  date-and-time [file ...]\n"
    "\n"
    "          The lines that can pass through this command will be chosen\n"
    "          by making sure the timestamp at the first field of each line\n"
    "          is in one of the following ranges.\n"
    "            (a) [ <top>, <command start time>+<interval> ]\n"
    "            (b) [ <top>, <command start time>+<interval> )\n"
    "            (c) [ <top>, <last line's time>  -<interval> ]\n"
    "            (d) [ <top>, <last line's time>  -<interval> )\n"
    "            (e) [ <top>, <date-and-time>                 ]\n"
    "            (f) [ <top>, <date-and-time>                 )\n"
    "\n"
    "Args    : file ........ Filepath to be send (\"-\" means STDIN)\n"
    "                        The file MUST be a textfile and MUST have\n"
    "                        a timestamp at the first field to make the\n"
    "                        timing of flow. The first space character\n"
    "                        <0x20> of every line will be regarded as\n"
    "                        the field delimiter.\n"
    "Options : -c,-e,-z .... Specify the format for timestamp and -t option\n"
    "                        parameter. You can choose one of the following.\n"
    "                          -c ... \"YYYYMMDDhhmmss[.n]\" (default)\n"
    "                                 Calendar time (standard time) in your\n"
    "                                 timezone (\".n\" is the digits under\n"
    "                                 second. You can specify up to nano\n"
    "                                 second.)\n"
    "                          -e ... \"n[.n]\"\n"
    "                                 The number of seconds since the UNIX\n"
    "                                 epoch (\".n\" is the same as -c)\n"
    "                          -z ... \"n[.n]\"\n"
    "                                 The number of seconds since this\n"
    "                                 command has startrd (\".n\" is the same\n"
    "                                 as -c)\n"
    "          -i interval . This is one of options to specify the timestamp\n"
    "                        range. (See the pattern (a) to (d) above)\n"
    "                        You can use the format \"A[.B][u]\" as the\n"
    "                        option's parameter \"interval.\"\n"
    "                          \"A\" is the integer part of the time.\n"
    "                          \"B\" is the decimal part of the time.\n"
    "                          \"u\" is the unit for the time. You can choose\n"
    "                              one of the followings.\n"
    "                              \"s\", \"ms\", \"us\" and \"ns.\"\n"
    "          -t date-and-time\n"
    "                        This is one of options to specify the timestamp\n"
    "                        range. (See the pattern (e) and (f) above)\n"
    "                        The format of \"date-and-time\" depends on\n"
    "                        which of the option \"-c\", \"-e,\" or \"-z\"\n"
    "                        you choose.\n"
    "                          \"-c\" ... \"YYYYMMDDhhmmss[.n]\" (cal. time)\n"
    "                          \"-e\" ... \"n[.n]\" (UNIX time)\n"
    "                          \"-z\" ... \"n[.n]\" (the number of seconds)\n"
    "          -q .......... Suppresses printing filenames when two or more\n"
    "                        files are given.\n"
    "          -u .......... Set the date in UTC when -c option is set\n"
    "                        (same as that of date command)\n"
    "          -x .......... An additional option for -i and -t. It will\n"
    "                        exclude the endpoint itself from the range.\n"
    "                        (See the pattern (b), (d) and (f) above)\n"
    "          -Z .......... Define the time when the first line came as 0.\n"
    "                        For instance, imagine that the first field of\n"
    "                        the first line is \"20200229235959,\" and the\n"
    "                        second line's one is \"20200301000004.\" when\n"
    "                        \"-c\" option is given. In this case, the first\n"
    "                        line is sent to stdout immediately, and after\n"
    "                        five seconds, the second line is sent.\n"
    "Version : " MY_REV "\n"
    "          (POSIX C language)\n"
    "\n"
    "Shell-Shoccar Japan (@shellshoccarjpn), No rights reserved.\n"
    "This is public domain software. (CC0)\n"
    "\n"
    "The latest version is distributed at the following page.\n"
    "https://github.com/ShellShoccar-jpn/misc-tools\n"
    ,gpszCmdname,gpszCmdname,gpszCmdname,gpszCmdname,gpszCmdname,gpszCmdname);
  exit(1);
}

/*--- print warning message ----------------------------------------*/
void warning(const char* szFormat, ...) {
  va_list va;
  va_start(va, szFormat);
  fprintf(stderr,"%s: ",gpszCmdname);
  vfprintf(stderr, szFormat, va);
  va_end(va);
  return;
}

/*--- exit with error message --------------------------------------*/
void error_exit(int iErrno, const char* szFormat, ...) {
  va_list va;
  va_start(va, szFormat);
  fprintf(stderr,"%s: ",gpszCmdname);
  vfprintf(stderr, szFormat, va);
  va_end(va);
  exit(iErrno);
}


/*####################################################################
# Main
####################################################################*/

/*=== Initialization ===============================================*/
int main(int argc, char *argv[]) {

/*--- Variables ----------------------------------------------------*/
int      iTfmt;           /* 0:"-c"  1:"-e"  2:"-z"                       */
int      iMode;           /* 1:"-i"  2:"-t"  0:(undefined)                */
int      iEndp;           /* Including the endpoint or not (1:include)    */
int      iL1zero;         /* Interval is based on the time 1st line comes */
int      iFromtop;        /* The time range start from the (1:top 0:end)  */
int      iPrnhdr;         /* 1:Print 2 or more filenames 0:none           */
char     szOptbuf[OPT_PARM_BUF];
struct timespec tsBorder; /* Border time                                  */
struct timespec tsDelta;  /* delta-T (defined by "-i" option)             */
int      iRet;            /* return code                                  */
int      i;               /* all-purpose int                              */
int64_t  i8;              /* all-purpose int64                            */

/*--- Initialize ---------------------------------------------------*/
if (clock_gettime(CLOCK_REALTIME,&gtsZero) != 0) {
  error_exit(errno,"clock_gettime() at initialize: %s\n",strerror(errno));
}
gpszCmdname = argv[0];
for (i=0; *(gpszCmdname+i)!='\0'; i++) {
  if (*(gpszCmdname+i)=='/') {gpszCmdname=gpszCmdname+i+1;}
}
if (setenv("POSIXLY_CORRECT","1",1) < 0) {
  error_exit(errno,"setenv() at initialization: \n", strerror(errno));
}
setlocale(LC_CTYPE, "");

/*=== Parse arguments ==============================================*/

/*--- Set default parameters of the arguments ----------------------*/
iTfmt     = 0; /* 0:"-c"(default) 1:"-e" 2:"-z" */
iMode     = 0; /* 1:interval(-i) 2:time(-t)     */
iL1zero   = 0; /* 0:The 0-time is based on the time the command begins
                  1:The 0-time is based on the time 1st line comes     */
iEndp     = 1; /* 0:Exclude the time range endpoint
                  1:Include the time range endpoint (default)          */
iFromtop  = 1; /* 1:The time range start from the top 0:from the end   */
iPrnhdr   = 1; /* 1:Print 2 or more filenames 0:none                   */
giVerbose = 0;

/*--- Parse and validate options -----------------------------------*/
if (argc<2) {print_usage_and_exit();}
while ((i=getopt(argc, argv, "cehi:qt:uvxzZ")) != -1) {
  switch (i) {
    case 'u': (void)setenv("TZ", "UTC0", 1); break;
    case 'c': iTfmt    = 0;                  break;
    case 'e': iTfmt    = 1;                  break;
    case 'z': iTfmt    = 2;                  break;
    case 'Z': iL1zero = 1;                   break;
    case 'x': iEndp    = 0;                  break;
    case 'i': if (*optarg=='-') {iFromtop = 0; optarg++;}
              else              {iFromtop = 1;          }
              i8 = parse_periodictime(optarg);
              if (i8<0) {print_usage_and_exit();}
              iMode = 1;
              tsDelta.tv_sec  = (time_t)(i8/1000000000);
              tsDelta.tv_nsec =   (long)(i8%1000000000);
                                             break;
    case 't': if (*optarg=='-') {iFromtop = 0; optarg++;}
              else              {iFromtop = 1;          }
              iMode = 2;
              if (strlen(optarg)>=OPT_PARM_BUF) {
                error_exit(1,"date-and-time for the \"-t\" is too long\n");
              }
              strcpy(szOptbuf, optarg);      break;
    case 'q': iPrnhdr  = 0;                  break;
    case 'v': giVerbose++;                   break;
    case 'h': print_usage_and_exit();
    default : print_usage_and_exit();
  }
}
argc -= optind;
argv += optind;
if (argc     <3) {iPrnhdr=0;}
if (giVerbose>0) {warning("verbose mode (level %d)\n",giVerbose);}
switch (iMode) {
  case  1: break;
  case  2: switch (iTfmt) {
             case 0 : if (! parse_calendartime(szOptbuf, &tsBorder)) {
                        error_exit(
                          1,
                          "%s: Timestamp format is calendar time by "
                          "\"-c\" option, but the string for \"-t\" "
                          "is wrong. See usage.\n",
                          szOptbuf);
                      }
                      break;
             default: if (! parse_unixtime(    szOptbuf, &tsBorder)) {
                        error_exit(
                          1,
                          "%s: Timestamp format is the number of seconds "
                          "by \"-e\" or \"-z\" option, but the string "
                          "for \"-t\" is wrong. See usage.\n",
                          szOptbuf);
                      }
                      break;
           }
           break;
  default: error_exit(1,"Either \"-i\" or \"-t\" option is required\n");
}
/*--- DEBUG ----------------------*/
printf("iTfmt   =%d\n",iTfmt);
printf("iMode   =%d\n",iMode);
printf("iL1zero =%d\n",iL1zero);
printf("iEndp   =%d\n",iEndp);
printf("iFromtop=%d\n",iFromtop);
printf("iPrnhdr =%d\n",iPrnhdr);
printf("tsBorder =(%010lld,%09ld)\n",(int64_t)tsBorder.tv_sec,tsBorder.tv_nsec);
printf("argc    =%d\n",argc);
for (i=0; i<argc; i++) {
  printf("file1=%s\n",argv[i]);
}

/*=== Each file loop ===============================================*/
iRet=0;



/*=== Finish normally ==============================================*/
return(iRet);}



/*####################################################################
# Functions
####################################################################*/

/*=== Parse a local calendar time ====================================
 * [in]  pszTime : calendar-time string in the localtime
 *                 (/[0-9]{11,20}(\.[0-9]{1,9})?/)
 *                 It is also OK to be followed by a ' ' or '\t'.
 *       ptsTime : To be set the parsed time ("timespec" structure)
 * [ret] > 0 : success
 *       ==0 : error (failure to parse)                             */
int parse_calendartime(char* pszTime, struct timespec *ptsTime) {

  /*--- Variables --------------------------------------------------*/
  char szDate[21], szNsec[10], szDate2[26];
  unsigned int i, j, k;    /* +-- 0:(reading integer part)          */
  char c;                  /* +-- 1:finish reading without_decimals */
  int  iStatus = 0; /* <--------- 2:to_be_started reading decimals  */
  struct tm tmDate;

  /*--- Separate pszTime into date and nanoseconds -----------------*/
  for (i=0; i<20; i++) {
    c = pszTime[i];
    if      (('0'<=c) && (c<='9')) {szDate[i]=c;                       }
    else if (c=='.'              ) {szDate[i]=0; iStatus=2; i++; break;}
    else if ((c==0) || (c==' ' )
                    || (c=='\t') ) {szDate[i]=0; iStatus=1;      break;}
    else                           {if (giVerbose>0) {
                                      warning("%c: Unexpected chr. in "
                                              "the integer part\n",c);
                                    }
                                    return 0;
                                   }
  }
  if ((iStatus==0) && (i==20)) {
    switch (pszTime[20]) {
      case '.' : szDate[20]=0; iStatus=2; i++; break;
      case  0  :
      case ' ' :
      case '\t': szDate[20]=0; iStatus=1;      break;
      default  : warning("The integer part of the timestamp is too big "
                         "as a calendar-time\n");
                 return 0;
    }
  }
  switch (iStatus) {
    case 1 : strcpy(szNsec,"000000000");
             break;
    case 2 : j=i+9;
             k=0;
             for (; i<j; i++) {
               c = pszTime[i];
               if      (('0'<=c) && (c<='9')) {szNsec[k]=c; k++;}
               else if ((c==0) || (c==' ' )
                               || (c=='\t') ) {break;           }
               else                           {
                 if (giVerbose>0) {
                   warning("%c: Unexpected chr. in the decimal part\n",c);
                 }
                 return 0;
               }
             }
             for (; k<9; k++) {szNsec[k]='0';}
             szNsec[9]=0;
             break;
    default: warning("Unexpected error in parse_calendartime(), "
                     "maybe a bug?\n");
             return 0;
  }

  /*--- Pack the time-string into the timespec structure -----------*/
  i = strlen(szDate);
  if (i <= 10) {return 0;}
  i -= 10;
  k =0; for (j=0; j<i; j++) {szDate2[k]=szDate[j];k++;} /* Y */
  szDate2[k]='-'; k++;
  i+=2; for (   ; j<i; j++) {szDate2[k]=szDate[j];k++;} /* m */
  szDate2[k]='-'; k++;
  i+=2; for (   ; j<i; j++) {szDate2[k]=szDate[j];k++;} /* d */
  szDate2[k]='T'; k++;
  i+=2; for (   ; j<i; j++) {szDate2[k]=szDate[j];k++;} /* H */
  szDate2[k]=':'; k++;
  i+=2; for (   ; j<i; j++) {szDate2[k]=szDate[j];k++;} /* M */
  szDate2[k]=':'; k++;
  i+=2; for (   ; j<i; j++) {szDate2[k]=szDate[j];k++;} /* S */
  szDate2[k]=  0;
  memset(&tmDate, 0, sizeof(tmDate));
  if(! strptime(szDate2, "%Y-%m-%dT%H:%M:%S", &tmDate)) {return 0;}
  ptsTime->tv_sec = mktime(&tmDate);
  ptsTime->tv_nsec = atol(szNsec);

  return 1;
}

/*=== Parse a UNIX-time ==============================================
 * [in]  pszTime : UNIX-time string (/[0-9]{1,19}(\.[0-9]{1,9})?/)
 *                 It is also OK to be followed by a ' ' or '\t'.
 *       ptsTime : To be set the parsed time ("timespec" structure)
 * [ret] > 0 : success
 *       ==0 : error (failure to parse)                             */
int parse_unixtime(char* pszTime, struct timespec *ptsTime) {

  /*--- Variables --------------------------------------------------*/
  char szSec[20], szNsec[10];
  unsigned int i, j, k;    /* +-- 0:(reading integer part)          */
  char c;                  /* +-- 1:finish reading without_decimals */
  int  iStatus = 0; /* <--------- 2:to_be_started reading decimals  */

  /*--- Separate pszTime into seconds and nanoseconds --------------*/
  for (i=0; i<19; i++) {
    c = pszTime[i];
    if      (('0'<=c) && (c<='9')) {szSec[i]=c;                       }
    else if (c=='.'              ) {szSec[i]=0; iStatus=2; i++; break;}
    else if ((c==0) || (c==' ' )
                    || (c=='\t') ) {szSec[i]=0; iStatus=1;      break;}
    else                           {if (giVerbose>0) {
                                      warning("%c: Unexpected chr. in "
                                              "the integer part\n",c);
                                    }
                                    return 0;
                                   }
  }
  if ((iStatus==0) && (i==19)) {
    switch (pszTime[19]) {
      case '.' : szSec[19]=0; iStatus=2; i++; break;
      case  0  :
      case ' ' :
      case '\t': szSec[19]=0; iStatus=1;      break;
      default  : warning("The integer part of the timestamp is too big "
                         "as a UNIX-time\n");
                return 0;
    }
  }
  switch (iStatus) {
    case 1 : strcpy(szNsec,"000000000");
             break;
    case 2 : j=i+9;
             k=0;
             for (; i<j; i++) {
               c = pszTime[i];
               if      (('0'<=c) && (c<='9')) {szNsec[k]=c; k++;}
               else if ((c==0) || (c==' ' )
                               || (c=='\t') ) {break;           }
               else                           {
                 if (giVerbose>0) {
                   warning("%c: Unexpected chr. in the decimal part\n",c);
                 }
                 return 0;
               }
             }
             for (; k<9; k++) {szNsec[k]='0';}
             szNsec[9]=0;
             break;
    default: warning("Unexpected error in parse_unixtime(), maybe a bug?\n");
             return 0;
  }

  /*--- Pack the time-string into the timespec structure -----------*/
  ptsTime->tv_sec = (time_t)atoll(szSec);
  if (ptsTime->tv_sec<0) {
    ptsTime->tv_sec = (sizeof(time_t)>=8) ? LLONG_MAX : LONG_MAX;
  }
  ptsTime->tv_nsec = atol(szNsec);

  return 1;
}


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

  /*--- Otherwise, it is not a value -------------------------------*/
  return -2;
}


/*=== Read a line from FH and push it into the line buffer list =====
 * [in]  *szHead : Header string to be written at the top of the buffer
 *                 - You should give me the timestamp string that is
 *                   terminated with 0x20,0x00.
 *                 - The length must be less than 1024.
 *       *fp     : File handle
 *       *pllLast: The Latest lline_t pointer
 * [ret] ==1 : Finished normally 
         ==2 : Finished normally (reached the EOF)
 *       ==0 : error                                                */
int push_a_line_into_lline(char *szHead, struct timespec *pts,
                           FILE *fp    , lline_t *pllLast     ) {

  int      iChar  , iRet;
  char    *pszBuf, *psz;
  size_t   sizBuf, sizStr;
  lline_t *pll;

  if (pts == NULL) {error_exit(1,"Error at %d, " MY_REV "\n", __LINE__);}
  if (fp  == NULL) {error_exit(1,"Error at %d, " MY_REV "\n", __LINE__);}

  pszBuf = NULL;
  sizBuf = sizStr = 0;

  if ((pszBuf = (char *)malloc(1024)) == NULL) {
    error_exit(errno,"push_a_line_into_lline() #1: %s\n",strerror(errno));
  }
  psz = pszBuf; sizBuf = 1024;
  if (szHead != NULL) {
    for (; szHead[sizStr]&&sizStr<1024; sizStr++) {psz[sizStr]=szHead[sizStr];}
  }
  while ((iChar = getc(fp)) != EOF) {
    if (sizStr >= sizBuf) {
      if ((pszBuf = (char *)realloc(pszBuf, sizBuf+=1024)) == NULL) {
        error_exit(errno,"push_a_line_into_lline() #2: %s\n",strerror(errno));
      }
      psz = pszBuf + sizStr;
    }
    *psz = iChar; psz++; sizStr++;
    if (iChar == '\n') {iRet=1; goto PUSH_LLIST;}
  }
  if (feof(  fp)) {iRet   =2;                                          }
  if (ferror(fp)) {iRet   =0;                                          }
  else            {error_exit(1,"Error at %d, " MY_REV "\n", __LINE__);}

  PUSH_LLIST:
  if (sizStr < sizBuf) {
    if ((pszBuf = (char *)realloc(pszBuf, sizStr)) == NULL) {
      error_exit(errno,"push_a_line_into_lline() #3: %s\n",strerror(errno));
    }
  }
  if ((pll = (lline_t *)malloc(sizeof(lline_t))) == NULL) {
    error_exit(errno,"push_a_line_into_lline() #4: %s\n",strerror(errno));
  }
  pll->pszStr=pszBuf; pll->sizStr=sizStr; pll->pstNext=NULL;
  if (pllLast!=NULL) {pllLast->pstNext=pll;}
  memcpy(pll->ts, pts, sizeof(struct timespec));

  return iRet;
}



/*=== Flush the line buffer list =====================================
 * [in]  *pllFirst: The first item pointer of the line buffer list
 * [ret]          : (none)                                          */
void flush_lline(lline_t *pllFirst) {

  lline_t *pll, *pll0;
  ssize_t ssRes;

  if (pllFirst == NULL) {error_exit(1,"Error at %d, " MY_REV "\n", __LINE__);}

  pll = pllFirst;
  do {
    ssRes = write(STDOUT_FILENO, pll->pszStr, pll->sizStr);
    if (ssRes != (ssize_t)pll->sizStr) {
      if (ssRes==-1) {error_exit(errno,"flush_lline(): %s\n",strerror(errno));}
      else           {error_exit(errno,"flush_lline(): write error\n");       }
    }
    pll0 = pll;
    pll  = pll0->pstNext;
    free(pll0->pszStr);
    free(pll0);
  } while (pll != NULL);

  return;
}

/*

-i <i> .. 時間の長さ（無単位の場合は秒）
-t <t> .. 時刻（単位は-c,-e,-zオプションによる）
-c ...... 1列目と-tオプションはカレンダー時間(default)
-e ...... 1列目と-tオプションはUNIX時間
-z ...... 1列目と-tオプションは経過秒数
-l ...... 行バッファリング
-x ...... 端点を含めない（より後、より前）
-Z ...... 0を起動日時から1行目記載日時に変更

tshead -i   <i>  [1行目, cmd起動日時+<i>秒 ]
tshead -ix  <i>  [1行目, cmd起動日時+<i>秒 )
tshead -i  -<i>  [1行目, 最終行日時-<i>秒  ]
tshead -ix -<i>  [1行目, 最終行日時-<i>秒  )
tshead -t   <t>  [1行目, 日時<t>           ]
tshead -tx  <t>  [1行目, 日時<t>           )

tstail -i   <i>  [最終行日時-<i>秒 , 最終行]
tstail -ix  <i>  (最終行日時-<i>秒 , 最終行]
tstail -i  +<i>  [cmd起動日時+<i>秒, 最終行]
tstail -ix +<i>  (cmd起動日時+<i>秒, 最終行]
tstail -t   <t>  [日時<t>          , 最終行]
tstail -tx  <t>  (日時<t>          , 最終行]



○ルーチンの種類
  * ①オプションの種類
    + -t（単に大小比較すればよし）
    + -i
      - ②最終行日時が必要か否か
        ・不要（単に大小比較すればよし）
        ・必要
          - ③ファイルの種類（statでなくfstatを使う）
            ・通常ファイル（mmap使用可能）
            ・特殊ファイル（テキスト蓄積が必要）


*/



/* ===== メモ領域 ================================================= */

