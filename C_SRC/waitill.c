/*####################################################################
#
# WAITILL - Wait till the Specified Absolute Time
#
# USAGE   : waitill [-lu] [-p n] abstime
#           waitill [-lu] [-p n] -e length
# Args    : abstime ..... * Absolute time (time point) to wait till.
#                         * This command will wait for the specified
#                           time to arrive. And then exit.
#                         * The formats for the time you can use are
#                           one of the following.
#                           + YYYYMMDDhhmmss[.d]
#                             - Calendar time
#                             - ".d" is the decimal part. You can
#                               specify it up to nanoseconds, or omit it.
#                             - The timezone for the time is set to the
#                               one used by the computer running this
#                               command. (The env "TZ" and the option
#                               -u can change the timezone)
#                           + YYYY-MM-DDThh:mm:ss[,d][+hh:mm|Z]
#                             - ISO 8601 (extended)
#                             - You can specify or omit the decimal part
#                               (up to nanoseconds) and the timezone part.
#                             - The timezone, in case you omitted it, is
#                               set to the one used by the computer
#                               running this command. (The env "TZ" and
#                               the option -u can change the timezone)
#                           + {+|-}n[.d]
#                             - UNIX time
#                             - To distinguish it from the other formats,
#                               you must write the sign "+" or "-"
#                               right before the time value. For example:
#                                 "+123"
#                                 "+123.45"
#                                 "-12.345"
#                             - ".d" is the decimal part. You can
#                               specify it up to nanoseconds, or omit it.
#                           + hhmmss[.d]
#                           + hhmm
#                           + mm
#                           + mmss.d
#                           + ss.d
#                           + .[d]
#                             - Abbreviations of the calendar time
#                             - The higher digits you omitted will be
#                               complemented by the nearest future time
#                               when this command was executed. For
#                               example:
#                                 a. abstime="57"
#                                    Executed at 2025-04-12 23:56:55,
#                                      --> "2025-04-12 23:57:00"
#                                 b. abstime="57"
#                                    Executed at 2025-04-12 23:57:05,
#                                      --> "2025-04-13 00:57:00"
#                             - When you use the "ss" and "mmss"
#                               formats, you cannot omit the decimal point
#                               because the "ss" and "mmss" have
#                               exactly the same look as "mm" and
#                               "hhmm." Thus, if you give "57" as the
#                               abstime to this command, this command
#                               will regard it as 57 minutes, not 57
#                               seconds.
#           length ...... * Length of time in seconds from "WT_EPOCH"
#                         * The format for the time you can use is the
#                           following.
#                           + n[.d]
#                             - "n" is the integer part.
#                             - ".d" is the decimal part. You can
#                               specify it up to nanoseconds, or omit it.
#                         * See the modes section for details.
# Options : -e .......... * Switch to "epoch mode"
#                         * See the modes section for details.
#           -l .......... * List the times this command waits till
#                         * The time is listed in three different formats.
#                           Those all mean the same.
#                           + ISO 8601 formatted time
#                           + Unix time
#                           + Calendar time
#           -u .......... * Assume the abstime is in the UTC timezone
#                         * This option works when the abstime you gave
#                           is a calendar time or ISO 8601 format without
#                           a timezone.
#           [The following option is for professional]
#           -p n ........ Process priority setting [0-3] (if possible)
#                          0: Normal process
#                          1: Weakest realtime process (default)
#                          2: Strongest realtime process for generic users
#                             (for only Linux, equivalent 1 for otheres)
#                          3: Strongest realtime process of this host
#                         Larger numbers maybe require a privileged user,
#                         but if failed, it will try the smaller numbers.
# Env-vars: WT_EPOCH .... * Absolute time for the "epoch mode" (See the
#                           modes section for details)
#                         * The formats for the time you can use are
#                           one of the following.
#                           + YYYYMMDDhhmmss[.d]
#                             - Calendar time
#                             - ".d" is the decimal part. You can
#                               specify it up to nanoseconds, or omit it.
#                             - The timezone for the time is set to the
#                               one used by the computer running this
#                               command.
#                           + YYYY-MM-DDThh:mm:ss[,d][+hh:mm|Z]
#                             - ISO 8601 (extended)
#                             - You can specify or omit the decimal part
#                               (up to nanoseconds) and the timezone part.
#                           + {+|-}n[.d]
#                             - UNIX time
#                             - To distinguish it from the other formats,
#                               you must write the sign "+" or "-"
#                               right before the time value. For example:
#                                 "+123"
#                                 "+123.45"
#                                 "-12.345"
#                             - ".d" is the decimal part. You can
#                               specify it up to nanoseconds, or omit it.
# Modes   : This command has the following two modes.
#             1. Basic mode
#                * When you run this command WITHOUT the -e option, this
#                  command works in this mode.
#                * In this mode, the command waits till the absolute time
#                  you specified in the first argument "abstime."
#                * It is simple and easy to use.
#             2. Epoch mode
#                * When you run this command WITH the -e option, this
#                  command works in this mode.
#                * In this mode, the command gets the followint two times
#                  at fitst.
#                    (1) reference time (epoch time) from the environment
#                        variable "WT_EPOCH" (See also in the env-vars
#                        section)
#                    (2) length of time in seconds from the first argument
#                        "length." (See also in the args section)
#                * Then, the command calculates the absolute time to wait
#                  by adding the value of "length" to the value of
#                  "WT_EPOCH" and starts waiting till that time.
#                * This mode is useful when you want to specify the end
#                  time of the wait as a time relative to another time,
#                  and gives your program a simpler look.
# Return  : Return 0 only when finished successfully
#
# How to compile : cc -O3 -o __CMDNAME__ __SRCNAME__ -lrt
#
# Written by Shell-Shoccar Japan (@shellshoccarjpn) on 2025-04-20
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

/*--- headers ------------------------------------------------------*/
#if defined(__linux) || defined(__linux__)
  /* This definition is for strptime() on Linux */
  #define _XOPEN_SOURCE 700
#endif
#include <errno.h>
#include <inttypes.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <time.h>
#if defined(_POSIX_PRIORITY_SCHEDULING) && !defined(__OpenBSD__) && !defined(__APPLE__)
  #include <sched.h>
  #include <sys/resource.h>
#endif

/*--- macro constants ----------------------------------------------*/
#define ENV_NAME "WT_EPOCH"

/*--- data type definitions ----------------------------------------*/
typedef struct timespec tmsp;

/*--- prototype functions ------------------------------------------*/
void parse_abstime_env(char* pszTime, tmsp *ptsTime);
void parse_abstime(char* pszTime, tmsp *ptsTime);
int  parse_calendartime(char* pszTime, tmsp *ptsTime);
int  parse_unixtime(char* pszTime, tmsp *ptsTime);
int  parse_iso8601time(char* pszTime, tmsp *ptsTime);
int  change_to_rtprocess(int iPrio);

/*--- global variables ---------------------------------------------*/
char* gpszCmdname;  /* The name of this command                    */
int   giVerbose;    /* speaks more verbosely by the greater number */

/*=== Define the functions for printing usage and error ============*/

/*--- exit with usage ----------------------------------------------*/
void print_usage_and_exit(void) {
  fprintf(stderr,
#if defined(_POSIX_PRIORITY_SCHEDULING) && !defined(__OpenBSD__) && !defined(__APPLE__)
    "USAGE   : %s [-lu] [-p n] abstime\n"
    "          %s [-lu] [-p n] -e length\n"
#else
    "USAGE   : %s [-lu] abstime\n"
    "          %s [-lu] -e length\n"
#endif
    "Args    : abstime ..... * Absolute time (time point) to wait till.\n"
    "                        * This command will wait for the specified\n"
    "                          time to arrive. And then exit.\n"
    "                        * The formats for the time you can use are\n"
    "                          one of the following.\n"
    "                          + YYYYMMDDhhmmss[.d]\n"
    "                            - Calendar time\n"
    "                            - \".d\" is the decimal part. You can\n"
    "                              specify it up to nanoseconds, or omit it.\n"
    "                            - The timezone for the time is set to the\n"
    "                              one used by the computer running this\n"
    "                              command. (The env \"TZ\" and the option\n"
    "                              -u can change the timezone)\n"
    "                          + YYYY-MM-DDThh:mm:ss[,d][+hh:mm|Z]\n"
    "                            - ISO 8601 (extended)\n"
    "                            - You can specify or omit the decimal part\n"
    "                              (up to nanoseconds) and the timezone part.\n"
    "                             - The timezone, in case you omitted it, is\n"
    "                               set to the one used by the computer\n"
    "                               running this command. (The env \"TZ\" and\n"
    "                               the option -u can change the timezone)\n"
    "                          + {+|-}n[.d]\n"
    "                            - UNIX time\n"
    "                            - To distinguish it from the other formats,\n"
    "                              you must write the sign \"+\" or \"-\"\n"
    "                              right before the time value. For example:\n"
    "                                \"+123\"\n"
    "                                \"+123.45\"\n"
    "                                \"-12.345\"\n"
    "                            - \".d\" is the decimal part. You can\n"
    "                              specify it up to nanoseconds, or omit it.\n"
    "                          + hhmmss[.d]\n"
    "                          + hhmm\n"
    "                          + mm\n"
    "                          + mmss.[d]\n"
    "                          + ss.[d]\n"
    "                          + .[d]\n"
    "                            - Abbreviations of the calendar time\n"
    "                            - The higher digits you omitted will be\n"
    "                              complemented by the nearest future time\n"
    "                              when this command was executed. For\n"
    "                              example:\n"
    "                                a. abstime=\"57\"\n"
    "                                   Executed at 2025-04-12 23:56:55,\n"
    "                                     --> \"2025-04-12 23:57:00\"\n"
    "                                b. abstime=\"57\"\n"
    "                                   Executed at 2025-04-12 23:57:05,\n"
    "                                     --> \"2025-04-13 00:57:00\"\n"
    "                            - When you use the \"ss\" and \"mmss\"\n"
    "                              formats, you cannot omit the decimal point\n"
    "                              because the \"ss\" and \"mmss\" have\n"
    "                              exactly the same look as \"mm\" and\n"
    "                              \"hhmm.\" Thus, if you give \"57\" as the\n"
    "                              abstime to this command, this command\n"
    "                              will regard it as 57 minutes, not 57\n"
    "                              seconds.\n"
    "          length ...... * Length of time in seconds from \"WT_EPOCH\"\n"
    "                        * The format for the time you can use is the\n"
    "                          following.\n"
    "                          + n[.d]\n"
    "                            - \"n\" is the integer part.\n"
    "                            - \".d\" is the decimal part. You can\n"
    "                              specify it up to nanoseconds, or omit it.\n"
    "                        * See the modes section for details.\n"
    "Options : -e .......... * Switch to \"epoch mode\"\n"
    "                        * See the modes section for details.\n"
    "          -l .......... * List the times this command waits till\n"
    "                        * The time is listed in three different formats.\n"
    "                          Those all mean the same.\n"
    "                          + ISO 8601 formatted time\n"
    "                          + Unix time\n"
    "                          + Calendar time\n"
    "          -u .......... * Assume the abstime is in the UTC timezone\n"
    "                        * This option works when the abstime you gave\n"
    "                          is a calendar time or ISO 8601 format without\n"
    "                          a timezone.\n"
#if defined(_POSIX_PRIORITY_SCHEDULING) && !defined(__OpenBSD__) && !defined(__APPLE__)
    "          [The following option is for professional]\n"
    "          -p n ........ Process priority setting [0-3] (if possible)\n"
    "                         0: Normal process\n"
    "                         1: Weakest realtime process (default)\n"
    "                         2: Strongest realtime process for generic users\n"
    "                            (for only Linux, equivalent 1 for otheres)\n"
    "                         3: Strongest realtime process of this host\n"
    "                        Larger numbers maybe require a privileged user,\n"
    "                        but if failed, it will try the smaller numbers.\n"
#endif
    "Env-vars: WT_EPOCH .... * Reference time for the \"epoch mode\" (See the\n"
    "                          modes section for details)\n"
    "                        * The formats for the time you can use are\n"
    "                          one of the following.\n"
    "                          + YYYYMMDDhhmmss[.d]\n"
    "                            - Calendar time\n"
    "                            - \".d\" is the decimal part. You can\n"
    "                              specify it up to nanoseconds, or omit it.\n"
    "                            - The timezone for the time is set to the\n"
    "                              one used by the computer running this\n"
    "                              command.\n"
    "                          + YYYY-MM-DDThh:mm:ss[,d][+hh:mm|Z]\n"
    "                            - ISO 8601 (extended)\n"
    "                            - You can specify or omit the decimal part\n"
    "                              (up to nanoseconds) and the timezone part.\n"
    "                          + {+|-}n[.d]\n"
    "                            - UNIX time\n"
    "                            - To distinguish it from the other formats,\n"
    "                              you must write the sign \"+\" or \"-\"\n"
    "                              right before the time value. For example:\n"
    "                                \"+123\"\n"
    "                                \"+123.45\"\n"
    "                                \"-12.345\"\n"
    "                            - \".d\" is the decimal part. You can\n"
    "                              specify it up to nanoseconds, or omit it.\n"
    "Modes   : This command has the following two modes.\n"
    "            1. Basic mode\n"
    "               * When you run this command WITHOUT the -e option, this\n"
    "                 command works in this mode.\n"
    "               * In this mode, the command waits till the absolute time\n"
    "                 you specified in the first argument \"abstime.\"\n"
    "               * It is simple and easy to use.\n"
    "            2. Epoch mode\n"
    "               * When you run this command WITH the -e option, this\n"
    "                 command works in this mode.\n"
    "               * In this mode, the command gets the followint two times\n"
    "                 at fitst.\n"
    "                   (1) reference time (epoch time) from the environment\n"
    "                       variable \"WT_EPOCH\" (See also in the env-vars\n"
    "                       section)\n"
    "                   (2) length of time in seconds from the first argument\n"
    "                       \"length.\" (See also in the args section)\n"
    "               * Then, the command calculates the absolute time to wait\n"
    "                 by adding the value of \"length\" to the value of\n"
    "                 \"WT_EPOCH\" and starts waiting till that time.\n"
    "               * This mode is useful when you want to specify the end\n"
    "                 time of the wait as a time relative to another time,\n"
    "                 and gives your program a simpler look.\n"
    "Return  : Return 0 only when finished successfully\n"
    "Version : 2025-04-20 02:54:37 JST\n"
    "          (POSIX C language)\n"
    "\n"
    "Shell-Shoccar Japan (@shellshoccarjpn), No rights reserved.\n"
    "This is public domain software. (CC0)\n"
    "\n"
    "The latest version is distributed at the following page.\n"
    "https://github.com/ShellShoccar-jpn/tokideli\n"
    ,gpszCmdname,gpszCmdname);
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
int        iOpt_e;     /* -e option flag                            */
int        iOpt_l;     /* -l option flag                            */
int        iPrio;      /* -p option number (default 1)              */
tmsp       tsAbstime;  /* Parsed abstime                            */
tmsp       tsLength;   /* Length of time from the abstime Length of time from the abstime           */
struct tm* ptmAbstime; /* Parsed abstime                            */
char       szTmz[7];   /* timezone string                           */
int        i;          /* all-purpose int                           */

/*--- Initialize ---------------------------------------------------*/
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
iOpt_e=0;
iOpt_l=0;
iPrio =1;
/*--- Parse options which start by "-" -----------------------------*/
while ((i=getopt(argc, argv, "elup:vh")) != -1) {
  switch (i) {
    case 'e': iOpt_e=1;                     break;
    case 'l': iOpt_l=1;                     break;
    case 'u': (void)setenv("TZ", "UTC", 1); break;
    #if defined(_POSIX_PRIORITY_SCHEDULING) && !defined(__OpenBSD__) && !defined(__APPLE__)
      case 'p': if (sscanf(optarg,"%d",&iPrio) != 1) {print_usage_and_exit();}
                                              break;
    #endif
    case 'v': giVerbose++;                  break;
    case 'h': print_usage_and_exit();
    default : print_usage_and_exit();
  }
}
argc -= optind;
argv += optind;
if (argc != 1  ) {print_usage_and_exit();}
if (giVerbose>0) {warning("verbose mode (level %d)\n",giVerbose);}

/*=== Basic mode routine ===========================================*/
if (iOpt_e==0) {
  if (giVerbose>0) {warning("Basic mode:\n");}
  parse_abstime(argv[0], &tsAbstime);
}

/*=== Epoch mode routine ===========================================*/
else           {
  if (giVerbose>0) {warning("Epoch mode:\n");}
  if (getenv(ENV_NAME) == NULL) {
    error_exit(1, "The environment variable \"%s\" is missing\n", ENV_NAME);
  }
  parse_abstime_env(getenv(ENV_NAME), &tsAbstime);
  if (! parse_unixtime(argv[0], &tsLength)) {
    error_exit(1, "%s: Invalid length of time\n", argv[0]);
  }
  tsAbstime.tv_sec  += tsLength.tv_sec;
  tsAbstime.tv_nsec += tsLength.tv_nsec;
  if (tsAbstime.tv_nsec >= 1000000000L) {
    tsAbstime.tv_sec++;
    tsAbstime.tv_nsec -= 1000000000L;
  }
}

/*=== Display the abstime to wait till (only in the verbose mode) ==*/
if (iOpt_l) {
  if ((ptmAbstime=localtime(&tsAbstime.tv_sec)) == NULL) {
    error_exit(255,"localtime(): returned NULL at %d\n",__LINE__);
  }
  strftime(szTmz, 7, "%z", ptmAbstime);
  szTmz[6]=0; szTmz[5]=szTmz[4]; szTmz[4]=szTmz[3]; szTmz[3]=':';
  printf("abstime_iso %04d-%02d-%02dT%02d:%02d:%02d,%09ld%s\n"
         "abstime_uni %+jd.%09ld\n"                           
         "abstime_cal %04d%02d%02d%02d%02d%02d.%09ld\n"       ,
         ptmAbstime->tm_year+1900, ptmAbstime->tm_mon+1, ptmAbstime->tm_mday,
         ptmAbstime->tm_hour     , ptmAbstime->tm_min  , ptmAbstime->tm_sec ,
         tsAbstime.tv_nsec       , szTmz                                    ,
         (intmax_t)tsAbstime.tv_sec, tsAbstime.tv_nsec,
         ptmAbstime->tm_year+1900, ptmAbstime->tm_mon+1, ptmAbstime->tm_mday,
         ptmAbstime->tm_hour     , ptmAbstime->tm_min  , ptmAbstime->tm_sec ,
         tsAbstime.tv_nsec                                                   );
}

/*=== Wait for the abstime to arrive ===============================*/
if (change_to_rtprocess(iPrio)==-1) {print_usage_and_exit();}
clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &tsAbstime, NULL);

/*=== Finish normally ==============================================*/
return 0;}


/*####################################################################
# Functions
####################################################################*/

/*=== Parse the absolute time (for environment variable) =============
 * [in]  pszTime : absolute time string
 *                 The following patterns are supported:
 *                 - YYYYMMDDhhmmss[.d]
 *                 - YYYY-MM-DDThh:mm:ss[,d][+hh:mm|Z]
 *                 - {+|-}n[.d]
 *       ptsTime : To be set the parsed time ("timespec" structure)
 * [ret] Return only when success (exit when some error occurs)     */
void parse_abstime_env(char* pszTime, tmsp *ptsTime) {
  /*--- Variables --------------------------------------------------*/
  tmsp       tsRef;     /* current time or the next interval */
  struct tm* ptmRef;    /* pointer to the current time or the next interval */
  int        iLen;      /* length of pszTime */
  int        iPpos;     /* position of '.' in pszTime */
  char       szHh[3],szMm[3],szSs[3],szNsec[10]; /* bufs for the parsed time */
  struct tm  tm;        /* struct tm for the parsed time */
  char*      psz;       /* all-purpose string pointer */
  int        i;         /* all-purpose int */
  char       c;         /* all-purpose char */
  double     d;         /* all-purpose double */

  /*--- Main -------------------------------------------------------*/
  if (strchr(pszTime,'T')        != NULL) {
    // YYYY-MM-DDThh:mm:ss[,d][+hh:mm|Z]
    if (! parse_iso8601time(pszTime, ptsTime)) {
      error_exit(
        1,
        "%s: Tne string in the env \"%s\" is an invalid ISO 8601 time.\n",
        pszTime,
        ENV_NAME
      );
    }
    return;
  }
  if ((pszTime[0]<'0'||pszTime[0]>'9') && pszTime[0]!='+' && pszTime[0]!='+') {
    error_exit(
      1,
      "%s: Tne string in the env \"%s\" does not mean an absolute time.\n",
      pszTime,
      ENV_NAME
    );
  }

  if (pszTime[0]=='+' || pszTime[0]=='-') {
    // {+|-}n[.d]
    if (! parse_unixtime(pszTime, ptsTime)) {
      error_exit(
        1,
        "%s: Tne string in the env \"%s\" is an invalid Unix time.\n",
        pszTime,
        ENV_NAME
      );
    }
    return;
  }
  if (parse_calendartime(pszTime, ptsTime)) {return;}
  if (parse_unixtime(    pszTime, ptsTime)) {return;}
  error_exit(
    1,
    "%s: Tne string in the env \"%s\" is neither a calendar time"
    " nor a Unix time.\n",
    pszTime,
    ENV_NAME
  );
}


/*=== Parse the absolute time (for argument) =========================
 * [in]  pszTime : absolute time string
 *                 The following patterns are supported:
 *                 - YYYYMMDDhhmmss[.d]
 *                 - YYYY-MM-DDThh:mm:ss[,d][+hh:mm|Z]
 *                 - {+|-}n[.d]
 *                 - hhmm
 *                 - hhmmss[.d]
 *                 - mm
 *                 - ss.d
 *                 - mmss.d
 *       ptsTime : To be set the parsed time ("timespec" structure)
 * [ret] Return only when success (exit when some error occurs)     */
void parse_abstime(char* pszTime, tmsp *ptsTime) {
  /*--- Variables --------------------------------------------------*/
  tmsp       tsRef;     /* current time or the next interval */
  struct tm* ptmRef;    /* pointer to the current time or the next interval */
  int        iLen;      /* length of pszTime */
  int        iPpos;     /* position of '.' in pszTime */
  char       szHh[3],szMm[3],szSs[3],szNsec[10]; /* bufs for the parsed time */
  struct tm  tm;        /* struct tm for the parsed time */
  char*      psz;       /* all-purpose string pointer */
  int        i;         /* all-purpose int */
  char       c;         /* all-purpose char */
  double     d;         /* all-purpose double */

  /*--- Main -------------------------------------------------------*/
  if (strchr(pszTime,'T')        != NULL) {
    // YYYY-MM-DDThh:mm:ss[,d][+hh:mm|Z]
    if (! parse_iso8601time(pszTime, ptsTime)) {
      error_exit(1, "%s: Invalid abstime (ISO 8601 time)\n"     , pszTime );
    }
    return;
  }
  if (pszTime[0]=='+' || pszTime[0]=='-') {
      // {+|-}n[.d]
    if (! parse_unixtime(pszTime, ptsTime)) {
      error_exit(1, "%s: Invalid abstime (Unix time)\n"     , pszTime );
    }
    return;
  }
  //
  iLen  = strlen(pszTime);
  psz   = strchr(pszTime,'.');
  iPpos = (psz!=NULL) ? (int)(psz-pszTime) : -1;
  if        (iLen <=2 && iPpos< 0) {
    // mm
    memset(&tm, 0, sizeof(tm));
    errno=0;
    tm.tm_min  = (int)strtol(pszTime, NULL, 10);
    if (errno) {
      error_exit(errno, "%s: Invalid abstime (min)\n"         , pszTime );
    }
    if (tm.tm_min >59) {
      error_exit(errno, "%s: abstime is out of range (min)\n" , pszTime );
    }
    if (clock_gettime(CLOCK_REALTIME, &tsRef) != 0) {
      error_exit(errno, "clock_gettime() failed at %d\n"      , __LINE__);
    }
    ptmRef = localtime(&tsRef.tv_sec);
    tm.tm_year = ptmRef->tm_year; tm.tm_mon  = ptmRef->tm_mon ;
    tm.tm_mday = ptmRef->tm_mday; tm.tm_hour = ptmRef->tm_hour;
    tm.tm_sec  = 0;
    ptsTime->tv_sec = mktime(&tm);
    if (difftime(ptsTime->tv_sec,tsRef.tv_sec) < 0) {
      tsRef.tv_sec+=3600;
      ptmRef = localtime(&tsRef.tv_sec);
      tm.tm_year = ptmRef->tm_year; tm.tm_mon  = ptmRef->tm_mon ;
      tm.tm_mday = ptmRef->tm_mday; tm.tm_hour = ptmRef->tm_hour;
      ptsTime->tv_sec = mktime(&tm);
    }
    ptsTime->tv_nsec = 0;
    return;
  } else if (iLen <=4 && iPpos< 0) {
    // hhmm
    strncpy(szMm, pszTime+iLen-2, 3     );
    strncpy(szHh, pszTime       , iLen-2); szHh[iLen-2]='\0';
    memset(&tm, 0, sizeof(tm));
    errno=0;
    tm.tm_min  = (int)strtol(szMm, NULL, 10);
    if (errno) {
      error_exit(errno, "%s: Invalid abstime (min)\n"         , pszTime );
    }
    if (tm.tm_min >59) {
      error_exit(errno, "%s: abstime is out of range (min)\n" , pszTime );
    }
    errno=0;
    tm.tm_hour = (int)strtol(szHh, NULL, 10);
    if (errno) {
      error_exit(errno, "%s: Invalid abstime (hour)\n"        , pszTime );
    }
    if (tm.tm_hour>23) {
      error_exit(errno, "%s: abstime is out of range (hour)\n", pszTime );
    }
    if (clock_gettime(CLOCK_REALTIME, &tsRef) != 0) {
      error_exit(errno, "clock_gettime() failed at %d\n"      , __LINE__);
    }
    ptmRef = localtime(&tsRef.tv_sec);
    tm.tm_year = ptmRef->tm_year; tm.tm_mon  = ptmRef->tm_mon ;
    tm.tm_mday = ptmRef->tm_mday;
    tm.tm_sec  = 0;
    ptsTime->tv_sec = mktime(&tm);
    if (difftime(ptsTime->tv_sec,tsRef.tv_sec) < 0) {
      tsRef.tv_sec+=86400;
      ptmRef = localtime(&tsRef.tv_sec);
      tm.tm_year = ptmRef->tm_year; tm.tm_mon  = ptmRef->tm_mon ;
      tm.tm_mday = ptmRef->tm_mday;
      ptsTime->tv_sec = mktime(&tm);
    }
    ptsTime->tv_nsec = 0;
    return;
  } else if (iLen <=6 && iPpos< 0) {
    // hhmmss
    strncpy(szSs, pszTime+iLen-2, 3     );
    strncpy(szMm, pszTime+iLen-4, 2     ); szMm[2     ]='\0';
    strncpy(szHh, pszTime       , iLen-4); szHh[iLen-4]='\0';
    memset(&tm, 0, sizeof(tm));
    errno=0;
    tm.tm_sec  = (int)strtol(szSs, NULL, 10);
    if (errno) {
      error_exit(errno, "%s: Invalid abstime (sec)\n"         , pszTime );
    }
    if (tm.tm_sec >60) {
      error_exit(errno, "%s: abstime is out of range (sec)\n" , pszTime );
    }
    errno=0;
    tm.tm_min  = (int)strtol(szMm, NULL, 10);
    if (errno) {
      error_exit(errno, "%s: Invalid abstime (min)\n"         , pszTime );
    }
    if (tm.tm_min >59) {
      error_exit(errno, "%s: abstime is out of range (min)\n" , pszTime );
    }
    errno=0;
    tm.tm_hour = (int)strtol(szHh, NULL, 10);
    if (errno) {
      error_exit(errno, "%s: Invalid abstime (hour)\n"        , pszTime );
    }
    if (tm.tm_hour>23) {
      error_exit(errno, "%s: abstime is out of range (hour)\n", pszTime );
    }
    if (clock_gettime(CLOCK_REALTIME, &tsRef) != 0) {
      error_exit(errno, "clock_gettime() failed at %d\n"      , __LINE__);
    }
    ptmRef = localtime(&tsRef.tv_sec);
    tm.tm_year = ptmRef->tm_year; tm.tm_mon  = ptmRef->tm_mon ;
    tm.tm_mday = ptmRef->tm_mday;
    ptsTime->tv_sec = mktime(&tm);
    if (difftime(ptsTime->tv_sec,tsRef.tv_sec) < 0) {
      tsRef.tv_sec+=86400;
      ptmRef = localtime(&tsRef.tv_sec);
      tm.tm_year = ptmRef->tm_year; tm.tm_mon  = ptmRef->tm_mon ;
      tm.tm_mday = ptmRef->tm_mday;
      ptsTime->tv_sec = mktime(&tm);
    }
    ptsTime->tv_nsec = 0;
    return;
  }
  // (Parse the decimal part)
  for (i=0; i<10; i++) {
    c = pszTime[iPpos+1+i];
    if (c>='0' && c<='9') {szNsec[i]=c;} else {break;}
  }
  for (   ; i<10; i++) {szNsec[i]='0';}
  szNsec[9] = '\0';
  ptsTime->tv_nsec = atol(szNsec);
  //
  if        (iPpos==0            ) {
    // .d
    if (clock_gettime(CLOCK_REALTIME, &tsRef) != 0) {
      error_exit(errno, "clock_gettime() failed at %d\n"      , __LINE__);
    }
    ptsTime->tv_sec = (ptsTime->tv_nsec > tsRef.tv_nsec) ? tsRef.tv_sec
                                                         : tsRef.tv_sec+1;
    return;
  } else if (iPpos==1 || iPpos==2) {
    // ss.d
    memset(&tm, 0, sizeof(tm));
    strncpy(szSs, pszTime, iPpos); szSs[iPpos]='\0';
    errno=0;
    tm.tm_sec  = (int)strtol(szSs, NULL, 10);
    if (errno) {
      error_exit(errno, "%s: Invalid abstime (sec)\n"         , pszTime );
    }
    if (tm.tm_sec>60) {
      error_exit(errno, "%s: abstime is out of range (sec)\n" , pszTime );
    }
    if (clock_gettime(CLOCK_REALTIME, &tsRef) != 0) {
      error_exit(errno, "clock_gettime() failed at %d\n"      , __LINE__);
    }
    ptmRef = localtime(&tsRef.tv_sec);
    tm.tm_year = ptmRef->tm_year; tm.tm_mon  = ptmRef->tm_mon ;
    tm.tm_mday = ptmRef->tm_mday; tm.tm_hour = ptmRef->tm_hour;
    tm.tm_min  = ptmRef->tm_min ;
    ptsTime->tv_sec = mktime(&tm);
    d = difftime(ptsTime->tv_sec, tsRef.tv_sec);
    if ((d==0 && ptsTime->tv_nsec<=tsRef.tv_nsec) || d<0) {
      tsRef.tv_sec+=60;
      ptmRef = localtime(&tsRef.tv_sec);
      tm.tm_year = ptmRef->tm_year; tm.tm_mon  = ptmRef->tm_mon ;
      tm.tm_mday = ptmRef->tm_mday; tm.tm_hour = ptmRef->tm_hour;
      tm.tm_min  = ptmRef->tm_min ;
      ptsTime->tv_sec = mktime(&tm);
    }
    return;
  } else if (iPpos==3 || iPpos==4) {
    // mmss.d
    strncpy(szSs, pszTime+iPpos-2, 2      ); szSs[2      ]='\0';
    strncpy(szMm, pszTime        , iPpos-2); szMm[iPpos-2]='\0';
    memset(&tm, 0, sizeof(tm));
    errno=0;
    tm.tm_sec  = (int)strtol(szSs, NULL, 10);
    if (errno) {
      error_exit(errno, "%s: Invalid abstime (sec)\n"         , pszTime );
    }
    if (tm.tm_sec >60) {
      error_exit(errno, "%s: abstime is out of range (sec)\n" , pszTime );
    }
    errno=0;
    tm.tm_min  = (int)strtol(szMm, NULL, 10);
    if (errno) {
      error_exit(errno, "%s: Invalid abstime (min)\n"         , pszTime );
    }
    if (tm.tm_min >59) {
      error_exit(errno, "%s: abstime is out of range (min)\n" , pszTime );
    }
    if (clock_gettime(CLOCK_REALTIME, &tsRef) != 0) {
      error_exit(errno, "clock_gettime() failed at %d\n"      , __LINE__);
    }
    ptmRef = localtime(&tsRef.tv_sec);
    tm.tm_year = ptmRef->tm_year; tm.tm_mon  = ptmRef->tm_mon ;
    tm.tm_mday = ptmRef->tm_mday; tm.tm_hour = ptmRef->tm_hour;
    ptsTime->tv_sec = mktime(&tm);
    d = difftime(ptsTime->tv_sec, tsRef.tv_sec);
    if ((d==0 && ptsTime->tv_nsec<=tsRef.tv_nsec) || d<0) {
      tsRef.tv_sec+=3600;
      ptmRef = localtime(&tsRef.tv_sec);
      tm.tm_year = ptmRef->tm_year; tm.tm_mon  = ptmRef->tm_mon ;
      tm.tm_mday = ptmRef->tm_mday; tm.tm_hour = ptmRef->tm_hour;
      ptsTime->tv_sec = mktime(&tm);
    }
    return;
  } else if (iPpos==5 || iPpos==6) {
    // hhmmss.d
    strncpy(szSs, pszTime+iPpos-2, 2      ); szSs[2      ]='\0';
    strncpy(szMm, pszTime+iPpos-4, 2      ); szMm[2      ]='\0';
    strncpy(szHh, pszTime        , iPpos-4); szHh[iPpos-4]='\0';
    memset(&tm, 0, sizeof(tm));
    errno=0;
    tm.tm_sec  = (int)strtol(szSs, NULL, 10);
    if (errno) {
      error_exit(errno, "%s: Invalid abstime (sec)\n"         , pszTime );
    }
    if (tm.tm_sec >60) {
      error_exit(errno, "%s: abstime is out of range (sec)\n" , pszTime );
    }
    errno=0;
    tm.tm_min  = (int)strtol(szMm, NULL, 10);
    if (errno) {
      error_exit(errno, "%s: Invalid abstime (min)\n"         , pszTime );
    }
    if (tm.tm_min >59) {
      error_exit(errno, "%s: abstime is out of range (min)\n" , pszTime );
    }
    errno=0;
    tm.tm_hour = (int)strtol(szHh, NULL, 10);
    if (errno) {
      error_exit(errno, "%s: Invalid abstime (hour)\n"        , pszTime );
    }
    if (tm.tm_hour>23) {
      error_exit(errno, "%s: abstime is out of range (hour)\n", pszTime );
    }
    if (clock_gettime(CLOCK_REALTIME, &tsRef) != 0) {
      error_exit(errno, "clock_gettime() failed at %d\n"      , __LINE__);
    }
    ptmRef = localtime(&tsRef.tv_sec);
    tm.tm_year = ptmRef->tm_year; tm.tm_mon  = ptmRef->tm_mon;
    tm.tm_mday = ptmRef->tm_mday;
    ptsTime->tv_sec = mktime(&tm);
    d = difftime(ptsTime->tv_sec, tsRef.tv_sec);
    if ((d==0 && ptsTime->tv_nsec<=tsRef.tv_nsec) || d<0) {
      tsRef.tv_sec+=86400;
      ptmRef = localtime(&tsRef.tv_sec);
      tm.tm_year = ptmRef->tm_year; tm.tm_mon  = ptmRef->tm_mon;
      tm.tm_mday = ptmRef->tm_mday;
      ptsTime->tv_sec = mktime(&tm);
    }
    return;
  } else                           {
    // YYYYMMDDhhmmss[.d]
    if (! parse_calendartime(pszTime, ptsTime)) {
      error_exit(1, "%s: Invalid abstime (calendar-time)\n"     , pszTime );
    }
  }
}

/*=== Parse a local calendar time ====================================
 * [in]  pszTime : calendar-time string in the localtime
 *                 (/[0-9]{11,20}(\.[0-9]{1,9})?/)
 *       ptsTime : To be set the parsed time ("timespec" structure)
 * [ret] > 0 : success
 *       ==0 : error (failure to parse)                             */
int parse_calendartime(char* pszTime, tmsp *ptsTime) {

  /*--- Variables --------------------------------------------------*/
  char szDate[21], szNsec[10], szDate2[26];
  int  i, j, k;            /* +-- 0:(reading integer part)          */
  char c;                  /* +-- 1:finish reading without_decimals */
  int  iStatus = 0; /* <--------- 2:to_be_started reading decimals  */
  struct tm tmDate;

  /*--- Separate pszTime into date and nanoseconds -----------------*/
  for (i=0; i<20; i++) {
    c = pszTime[i];
    if      ('0'<=c && c<='9'         ) {szDate[i]=c;                       }
    else if (c=='.'                   ) {szDate[i]=0; iStatus=2; i++; break;}
    else if (c==0 || c=='\t' || c==' ') {szDate[i]=0; iStatus=1;      break;}
    else                           {if (giVerbose>0) {
                                      warning("%c: Unexpected chr. in "
                                              "the integer part\n",c);
                                    }
                                    return 0;
                                   }
  }
  if ((iStatus==0) && (i==20)) {
    switch (pszTime[20]) {
      case '.': szDate[20]=0; iStatus=2; i++; break;
      case  0 : szDate[20]=0; iStatus=1;      break;
      default : warning("The integer part of the timestamp is too big "
                        "as a calendar-time\n",c);
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
               if      ('0'<=c && c<='9'         ) {szNsec[k]=c; k++;}
               else if (c==0 || c=='\t' || c==' ') {break;           }
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
  i = strlen(szDate)-10;
  if (i<=0) {return 0;}
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
  if(! strptime(szDate2, "%Y-%m-%dT%H:%M:%S", &tmDate)) {
    if (giVerbose>1) {
      warning("Unexpect error at strptime() in parse_calendartime()\n");
    }
    return 0;
  }
  errno=0;
  ptsTime->tv_sec  = mktime(&tmDate);
  if (errno) {
    if (giVerbose>1) {
      warning("%s: Invalid calendartime string: %s\n", pszTime,strerror(errno));
    }
    return 0;
  }
  ptsTime->tv_nsec = atol(szNsec);

  return 1;
}

/*=== Parse a UNIX-time ==============================================
 * [in]  pszTime : UNIX-time string (/[0-9]{1,19}(\.[0-9]{1,9})?/)
 *       ptsTime : To be set the parsed time ("timespec" structure)
 * [ret] > 0 : success
 *       ==0 : error (failure to parse)                             */
int parse_unixtime(char* pszTime, tmsp *ptsTime) {

  /*--- Variables --------------------------------------------------*/
  char szSec[21], szNsec[10];
  int  i, j, k;            /* +-- 0:(reading integer part)          */
  char c;                  /* +-- 1:finish reading without_decimals */
  int  iStatus = 0; /* <--------- 2:to_be_started reading decimals  */

  /*--- Separate pszTime into seconds and nanoseconds --------------*/
  i=0; j=19;
  if (pszTime[i]=='+'||pszTime[i]=='-') {szSec[i]=pszTime[i];i++;j++;}
  for (   ; i<j; i++) {
    c = pszTime[i];
    if      ('0'<=c && c<='9'         ) {szSec[i]=c;                       }
    else if (c=='.'                   ) {szSec[i]=0; iStatus=2; i++; break;}
    else if (c==0 || c=='\t' || c==' ') {szSec[i]=0; iStatus=1;      break;}
    else                           {if (giVerbose>0) {
                                      warning("%c: Unexpected chr. in "
                                              "the integer part\n",c);
                                    }
                                    return 0;
                                   }
  }
  if ((iStatus==0) && (i==j)) {
    switch (pszTime[j]) {
      case '.': szSec[j]=0; iStatus=2; i++; break;
      case  0 : szSec[j]=0; iStatus=1;      break;
      default : warning("The integer part of the timestamp is too big "
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
               if      ('0'<=c && c<='9'         ) {szNsec[k]=c; k++;}
               else if (c==0 || c=='\t' || c==' ') {break;           }
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
  ptsTime->tv_sec  = (time_t)atoll(szSec);
  ptsTime->tv_nsec =         atol(szNsec);

  return 1;
}

/*=== Parse an extended ISO 8601 time ================================
 * [in]  pszTime : ISO 8601 (ext.) string in the localtime
   (/[0-9]{1,10}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}([,.][0-9]{1,9})?([+-][0-9]{2}:?[0-9]{2}|Z)?/)
 *       ptsTime : To be set the parsed time ("timespec" structure)
 * [ret] > 0 : success
 *       ==0 : error (failure to parse)                             */
int parse_iso8601time(char* pszTime, tmsp *ptsTime) {

  /*--- Variables --------------------------------------------------*/
  char szDate[26], szNsec[10];
  int  iTZoffs;            /* +-- 0:now reading integer part or invalid string*/
  int  i, j, k;            /* +-- 1:to_be_started reading decimals  */
  char c;                  /* +-- 2:to_be_started reading timezone  */
  int  iStatus = 0; /* <--------- 3:finished reading                */
  struct tm tmDate;

  /*--- Read the string (integer part) -----------------------------*/
  iStatus=0;
  while (1) {
    i=0;
    if (pszTime[i]<'0' || '9'<pszTime[i]) {break;} else {i++;} /* Y     */
    while ('0'<=pszTime[i] && pszTime[i]<='9' && i<10 ) {i++;} /* Y{,9} */
    if (pszTime[i] != '-'               ) {break;} else {i++;} /* -     */
    if (pszTime[i]<'0' || '9'<pszTime[i]) {break;} else {i++;} /* M     */
    if (pszTime[i]<'0' || '9'<pszTime[i]) {break;} else {i++;} /* M     */
    if (pszTime[i] != '-'               ) {break;} else {i++;} /* -     */
    if (pszTime[i]<'0' || '9'<pszTime[i]) {break;} else {i++;} /* D     */
    if (pszTime[i]<'0' || '9'<pszTime[i]) {break;} else {i++;} /* D     */
    if (pszTime[i] != 'T'               ) {break;} else {i++;} /* T     */
    if (pszTime[i]<'0' || '9'<pszTime[i]) {break;} else {i++;} /* h     */
    if (pszTime[i]<'0' || '9'<pszTime[i]) {break;} else {i++;} /* h     */
    if (pszTime[i] != ':'               ) {break;} else {i++;} /* :     */
    if (pszTime[i]<'0' || '9'<pszTime[i]) {break;} else {i++;} /* m     */
    if (pszTime[i]<'0' || '9'<pszTime[i]) {break;} else {i++;} /* m     */
    if (pszTime[i] != ':'               ) {break;} else {i++;} /* :     */
    if (pszTime[i]<'0' || '9'<pszTime[i]) {break;} else {i++;} /* s     */
    if (pszTime[i]<'0' || '9'<pszTime[i]) {break;} else {i++;} /* s     */
    switch (pszTime[i]) {
      case ',' :
      case '.' : iStatus = 1; break;
      case 'Z' :
      case '+' :
      case '-' : iStatus = 2; break;
      case 0   :
      case ' ' :
      case '\t': iStatus = 3; break;
      default  : break;
    }
    break;
  }
  if (iStatus==0) {
    if (giVerbose>0) {warning("%s: Invalid ISO 8601 string\n",pszTime);}
    return 0;
  }
  memcpy(szDate, pszTime, i); szDate[i]=0;

  /*--- Read the string (decimal part) -----------------------------*/
  switch (iStatus) {
    case 1 : i++;
             j=i+9;
             k=0;
             for (; i<j; i++) {
               c = pszTime[i];
               if      ('0'<=c  && c<='9'          ) {szNsec[k]=c; k++;}
               else if (c=='+'  || c=='-' || c=='Z') {iStatus=2; break;}
               else if (c=='\t' || c==' ' || c==0  ) {iStatus=3; break;}
               else                                  {
                 if (giVerbose>0) {
                   warning("%s: Invalid ISO 8601 string (decimal part)\n",
                           pszTime                                        );
                 }
                 return 0;
               }
             }
             if (i==j) {
               c = pszTime[i];
               if (c=='+'  || c=='-'        ) {iStatus=2; break;}
               if (c=='\t' || c==' ' || c==0) {iStatus=3; break;}
             }
             for (; k<9; k++) {szNsec[k]='0';}
             szNsec[9]=0;
             break;
    case 2 :
    case 3 : strcpy(szNsec,"000000000");
             break;
  }

  /*--- Read the string (timezone part) ----------------------------*/
  if (iStatus==2) {
    k=0;
    while (k==0) {
      iTZoffs  = 0;
      if (pszTime[i]=='Z') {j=1; k=1; break;}                    /* Z    */
      j = (pszTime[i]=='+') ? 1 : -1; i++;                       /* [+-] */
      if (pszTime[i]<'0' || '9'<pszTime[i]) {break;}
      iTZoffs += (pszTime[i]-'0')*36000; i++;                    /* h    */
      if (pszTime[i]<'0' || '9'<pszTime[i]) {break;}
      iTZoffs += (pszTime[i]-'0')* 3600; i++;                    /* h    */
      if (pszTime[i]==':'                 ) {i++;  }             /* :    */
      if (pszTime[i]<'0' || '9'<pszTime[i]) {break;}
      iTZoffs += (pszTime[i]-'0')*  600; i++;                    /* m    */
      if (pszTime[i]<'0' || '9'<pszTime[i]) {break;}
      iTZoffs += (pszTime[i]-'0')*   60; i++;                    /* m    */
      if (pszTime[i]!=' ' && pszTime[i]!='\t' && pszTime[i]!=0) {break;}
      iTZoffs *= j;
      k=1;
    }
    if (k==0) {
      if (iStatus==0) {
        warning("%s: Invalid ISO 8601 string (timezone part)\n",
                pszTime                                         );
        return 0;
      }
    }
  } else          {
    j=0;
  }

  /*--- Pack the time-string into the timespec structure -----------*/
  if (! strptime(szDate, "%Y-%m-%dT%H:%M:%S", &tmDate)) {
    if (giVerbose>1) {
      warning("Unexpect error at strptime() #1 in parse_iso8601time()\n");
    }
    return 0;
  }
  errno=0;
  ptsTime->tv_sec  = mktime(&tmDate);
  if (errno) {
    if (giVerbose>1) {
      warning("%s: Invalid ISO 8601 string: %s\n", pszTime, strerror(errno));
    }
    return 0;
  }
  ptsTime->tv_nsec = atol(szNsec);
  if (j!=0) {
    i = (int)difftime(mktime(localtime((time_t[]){0})),
                      mktime(   gmtime((time_t[]){0})) );
    ptsTime->tv_sec = ptsTime->tv_sec + i - iTZoffs;
  }

  return 1;
}


/*=== Try to make me a realtime process ==============================
 * [in]  iPrio : 0:will not change (just return normally)
 *               1:minimum priority
 *               2:maximun priority for non-privileged users (only Linux)
 *               3:maximun priority of this host
 * [ret] = 0 : success, or errno
 *       =-1 : error (by this function)
 *       > 0 : error (by system call, and the value means "errno")       */
int change_to_rtprocess(int iPrio) {

#if defined(_POSIX_PRIORITY_SCHEDULING) && !defined(__OpenBSD__) && !defined(__APPLE__)
  /*--- Variables --------------------------------------------------*/
  struct sched_param spPrio;
  #ifdef RLIMIT_RTPRIO
    struct rlimit      rlInfo;
  #endif

  /*--- Initialize -------------------------------------------------*/
  memset(&spPrio, 0, sizeof(spPrio));

  /*--- Decide the priority number ---------------------------------*/
  switch (iPrio) {
    case 3 : if ((spPrio.sched_priority=sched_get_priority_max(SCHED_RR))==-1) {
               return errno;
             }
             if (sched_setscheduler(0, SCHED_RR, &spPrio)==0) {
               if (giVerbose>0) {warning("\"-p3\": succeeded\n"         );}
               return 0;
             } else                                           {
               if (giVerbose>0) {warning("\"-p3\": %s\n",strerror(errno));}
             }
    case 2 : 
      #ifdef RLIMIT_RTPRIO
             if ((getrlimit(RLIMIT_RTPRIO,&rlInfo))==-1) {
               return errno;
             }
             if (rlInfo.rlim_cur > 0) {
               spPrio.sched_priority=rlInfo.rlim_cur;
               if (sched_setscheduler(0, SCHED_RR, &spPrio)==0) {
                 if (giVerbose>0){warning("\"-p2\" succeeded\n"          );}
                 return 0;
               } else                                           {
                 if (giVerbose>0){warning("\"-p2\": %s\n",strerror(errno));}
               }
               if (giVerbose>0) {warning("\"-p2\": %s\n",strerror(errno));}
             } else if (giVerbose>0) {
               warning("RLIMIT_RTPRIO isn't set\n");
             }
      #endif
    case 1 : if ((spPrio.sched_priority=sched_get_priority_min(SCHED_RR))==-1) {
               return errno;
             }
             if (sched_setscheduler(0, SCHED_RR, &spPrio)==0) {
               if (giVerbose>0) {warning("\"-p1\": succeeded\n"         );}
               return 0;
             } else                                           {
               if (giVerbose>0) {warning("\"-p1\": %s\n",strerror(errno));}
             }
             return errno;
    case 0 : if (giVerbose>0) {warning("\"-p0\": succeeded\n");}
             return  0;
    default: return -1;
  }
#endif
  /*--- Return successfully ----------------------------------------*/
  return 0;
}
