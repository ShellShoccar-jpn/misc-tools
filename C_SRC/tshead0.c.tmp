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
#           -u .......... Set the date in UTC when -c option is set
#                         (same as that of date command)
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
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <unistd.h>
extern char *optarg;
extern int optind, opterr, optopt;
#include <sys/select.h>
#include <time.h>
#include <fcntl.h>
#include <signal.h>
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

/*--- prototype functions ------------------------------------------*/
void get_time_data_arrived(int iFd, struct timespec *ptsTime);
int  read_1st_field_as_a_timestamp(FILE *fp, char *pszTime);
int  read_and_write_a_line(FILE *fp);
int  skip_over_a_line(FILE *fp);
int  parse_calendartime(char* pszTime, struct timespec *ptsTime);
int  parse_unixtime(char* pszTime, struct timespec *ptsTime);
void spend_my_spare_time(struct timespec *ptsTo, struct timespec *ptsOffset);
int64_t parse_periodictime(char *pszArg);


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
    "          -x .......... An additional option for -i and -t. It will\n"
    "                        exclude the endpoint itself from the range.\n"
    "                        (See the pattern (b), (d) and (f) above)\n"
    "          -Z .......... Define the time when the first line came as 0.\n"
    "                        For instance, imagine that the first field of\n"
    "                        the first line is "20200229235959," and the\n"
    "                        second line's one is "20200301000004." when\n"
    "                        \"-c\" option is given. In this case, the first\n"
    "                        line is sent to stdout immediately, and after\n"
    "                        five seconds, the second line is sent.\n"
    "          -u .......... Set the date in UTC when -c option is set\n"
    "                        (same as that of date command)\n"
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
int      iTopbase;        /* Interval is based on the time 1st line comes */
int      iFromtop;        /* The time range start from the (1:top 0:end)  */
int      iRet;            /* return code                                  */
int      iGotOffset;      /* 0:NotYet 1:GetZeroPoint 2:Done               */
char     szTime[33];      /* Buffer for the 1st field of lines            */
struct timespec tsOptTime;/* Time defined with "-t" or "-i"               */
struct timespec tsTime;   /* Parsed time for the 1st field                */
struct timespec tsOffset; /* Zero-point time to adjust the 1st field one  */
char    *pszPath;         /* filepath on arguments                        */
char    *pszFilename;     /* filepath (for message)                       */
int      iFileno;         /* file# of filepath                            */
int      iFd;             /* file descriptor                              */
FILE    *fp;              /* file handle                                  */
int      i;               /* all-purpose int                              */
int64_t  i8;              /* all-purpose int64                            */
uint64_t ui8;             /* all-purpose uint64                           */

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
iTopbase  = 0; /* 0:The 0-time is based on the time the command begins
                  1:The 0-time is based on the time 1st line comes     */
iEndp     = 1; /* 0:Exclude the time range endpoint
                  1:Include the time range endpoint (default)          */
iFromtop  = 1; /* 1:The time range start from the top 0:from the end   */
giVerbose = 0;
/*--- Parse options which start by "-" -----------------------------*/
while ((i=getopt(argc, argv, "cehi:t:uvxzZ")) != -1) {
  switch (i) {
    case 'u': (void)setenv("TZ", "UTC0", 1); break;
    case 'c': iTfmt    = 0;                  break;
    case 'e': iTfmt    = 1;                  break;
    case 'z': iTfmt    = 2;                  break;
    case 'Z': iTopbase = 1;                  break;
    case 'x': iEndp    = 0;                  break;
    case 'i': if (&optarg=='-') {iFromtop = 0; optarg++;}
              else              {iFromtop = 1;          }
              i8 = parse_periodictime(optarg);
              if (i<0) {print_usage_and_exit();}
              iMode = 1;
              tsOptTime.tv_sec  = (time_t)(i8/1000000000);
              tsOptTime.tv_nsec =   (long)(i8%1000000000);
                                             break;
    case 't': if (&optarg=='-') {iFromtop = 0; optarg++;}
              else              {iFromtop = 1;          }
              iMode = 2;
              switch (iTfmt) {
                case 0 : if (! parse_calendartime(optarg, &tsOptTime)) {
                           print_usage_and_exit();
                         }
                         break;
                default: if (! parse_unixtime(    optarg, &tsOptTime)) {
                           print_usage_and_exit();
                         }
                         break;
              }
                                             break;
    case 'v': giVerbose++;                   break;
    case 'h': print_usage_and_exit();
    default : print_usage_and_exit();
  }
}
argc -= optind-1;
argv += optind  ;
if (giVerbose>0) {warning("verbose mode (level %d)\n",giVerbose);}

/*=== Switch buffer mode ===========================================*/
if (setvbuf(stdout,NULL,_IOLBF,0)!=0) {
  error_exit(255,"Failed to switch to line-buffered mode\n");
}

/*=== Each file loop ===============================================*/
iMode      =  0;
iEndp      =  1;
iRet       =  0;
iGotOffset =  0;
iFileno    =  0;
iFd        = -1;
while ((pszPath = argv[iFileno]) != NULL || iFileno == 0) {

  /*--- Open one of the input files --------------------------------*/
  if (pszPath == NULL || strcmp(pszPath, "-") == 0) {
    pszFilename = "stdin"                ;
    iFd         = STDIN_FILENO           ;
  } else                                            {
    pszFilename = pszPath                ;
    while ((iFd=open(pszPath, O_RDONLY)) < 0) {
      iRet = 1;
      warning("%s: %s\n", pszFilename, strerror(errno));
      iFileno++;
      break;
    }
    if (iFd < 0) {continue;}
  }
  if (iFd == STDIN_FILENO) {
    fp = stdin;
    if (feof(stdin)) {clearerr(stdin);} /* Reset EOF condition when stdin */
  } else                   {
    fp = fdopen(iFd, "r");
  }

  /*--- Reading and writing loop -----------------------------------*/
  switch (iTfmt) {
    case 0: /* "-c" Calendar time mode */
             while (1) {
               switch (read_1st_field_as_a_timestamp(fp, szTime)) {
                 case  1: /* read successfully */
                          if (! parse_calendartime(szTime, &tsTime)) {
                            warning("%s: %s: Invalid calendar-time, "
                                    "skip this line\n", pszFilename, szTime);
                            iRet = 1;
                            switch (skip_over_a_line(fp)) {
                              case  1: /* expected LF */
                                       break;
                              case -1: /* expected EOF */
                                       goto CLOSE_THISFILE;
                              case -2: /* file access error */
                                       warning("%s: File access error, "
                                               "skip it\n", pszFilename);
                                       goto CLOSE_THISFILE;
                                       break;
                              default: /* bug of system error */
                                       error_exit(1,"Unexpected error at %d\n",
                                                  __LINE__);
                                       break;
                            }
                            break;
                          }
                          spend_my_spare_time(&tsTime, NULL);
                          switch (read_and_write_a_line(fp)) {
                            case  1: /* expected LF */
                                     break;
                            case -1: /* expected EOF */
                                     goto CLOSE_THISFILE;
                            case -2: /* file access error */
                                     warning("%s: File access error, "
                                             "skip it\n", pszFilename);
                                     iRet = 1;
                                     goto CLOSE_THISFILE;
                                     break;
                            default: /* bug of system error */
                                     error_exit(1,"Unexpected error at %d\n",
                                                __LINE__);
                                     break;
                          }
                          break;
                 case  0: /* unexpected LF */
                          warning("%s: %s: Invalid timestamp field found, "
                                  "skip this line.\n", pszFilename, szTime);
                          iRet = 1;
                          break;
                 case -2: /* unexpected EOF */
                          warning("%s: Came to EOF suddenly\n", pszFilename);
                          iRet = 1;
                 case -1: /*   expected EOF */
                          goto CLOSE_THISFILE;
                          break;
                 case -3: /* file access error */
                          warning("%s: File access error, skip it\n",
                                  pszFilename);
                          iRet = 1;
                          goto CLOSE_THISFILE;
                 default: /* bug or system error */
                          error_exit(1,"Unexpected error at %d\n", __LINE__);
               }
             }
             break;
    case 1: /* "-e" UNIX epoch time mode */
             while (1) {
               switch (read_1st_field_as_a_timestamp(fp, szTime)) {
                 case  1: /* read successfully */
                          if (! parse_unixtime(szTime, &tsTime)) {
                            warning("%s: %s: Invalid UNIX-time, "
                                    "skip this line\n", pszFilename, szTime);
                            iRet = 1;
                            switch (skip_over_a_line(fp)) {
                              case  1: /* expected LF */
                                       break;
                              case -1: /* expected EOF */
                                       goto CLOSE_THISFILE;
                              case -2: /* file access error */
                                       warning("%s: File access error, "
                                               "skip it\n", pszFilename);
                                       goto CLOSE_THISFILE;
                                       break;
                              default: /* bug of system error */
                                       error_exit(1,"Unexpected error at %d\n",
                                                  __LINE__);
                                       break;
                            }
                            break;
                          }
                          spend_my_spare_time(&tsTime, NULL);
                          switch (read_and_write_a_line(fp)) {
                            case  1: /* expected LF */
                                     break;
                            case -1: /* expected EOF */
                                     goto CLOSE_THISFILE;
                            case -2: /* file access error */
                                     warning("%s: File access error, "
                                             "skip it\n", pszFilename);
                                     iRet = 1;
                                     goto CLOSE_THISFILE;
                                     break;
                            default: /* bug of system error */
                                     error_exit(1,"Unexpected error at %d\n",
                                                __LINE__);
                                     break;
                          }
                          break;
                 case  0: /* unexpected LF */
                          warning("%s: %s: Invalid timestamp field found, "
                                  "skip this line.\n", pszFilename, szTime);
                          iRet = 1;
                          break;
                 case -2: /* unexpected EOF */
                          warning("%s: Came to EOF suddenly\n", pszFilename);
                          iRet = 1;
                 case -1: /*   expected EOF */
                          goto CLOSE_THISFILE;
                          break;
                 case -3: /* file access error */
                          warning("%s: File access error, skip it\n",
                                  pszFilename);
                          iRet = 1;
                          goto CLOSE_THISFILE;
                 default: /* bug or system error */
                          error_exit(1,"Unexpected error at %d\n", __LINE__);
               }
             }
             break;
    case 2: /* "-z" Zero time mode */
             while (1) {
               switch (read_1st_field_as_a_timestamp(fp, szTime)) {
                 case  1: /* read successfully */
                          if (! parse_unixtime(szTime, &tsTime)) {
                            warning("%s: %s: Invalid number of seconds, "
                                    "skip this line\n", pszFilename, szTime);
                            iRet = 1;
                            switch (skip_over_a_line(fp)) {
                              case  1: /* expected LF */
                                       break;
                              case -1: /* expected EOF */
                                       goto CLOSE_THISFILE;
                              case -2: /* file access error */
                                       warning("%s: File access error, "
                                               "skip it\n", pszFilename);
                                       goto CLOSE_THISFILE;
                                       break;
                              default: /* bug of system error */
                                       error_exit(1,"Unexpected error at %d\n",
                                                  __LINE__);
                                       break;
                            }
                            break;
                          }
                          if (iGotOffset<2) {
                            /* tsOffset = gtsZero - tsTime */
                            if ((gtsZero.tv_nsec - tsTime.tv_nsec) < 0) {
                              tsOffset.tv_sec  = gtsZero.tv_sec 
                                                 - tsTime.tv_sec  -          1;
                              tsOffset.tv_nsec = gtsZero.tv_nsec
                                                 - tsTime.tv_nsec + 1000000000;
                            } else {
                              tsOffset.tv_sec  = gtsZero.tv_sec -tsTime.tv_sec ;
                              tsOffset.tv_nsec = gtsZero.tv_nsec-tsTime.tv_nsec;
                            }
                            iGotOffset=2;
                          }
                          spend_my_spare_time(&tsTime, &tsOffset);
                          switch (read_and_write_a_line(fp)) {
                            case  1: /* expected LF */
                                     break;
                            case -1: /* expected EOF */
                                     goto CLOSE_THISFILE;
                            case -2: /* file access error */
                                     warning("%s: File access error, "
                                             "skip it\n", pszFilename);
                                     iRet = 1;
                                     goto CLOSE_THISFILE;
                                     break;
                            default: /* bug of system error */
                                     error_exit(1,"Unexpected error at %d\n",
                                                __LINE__);
                                     break;
                          }
                          break;
                 case  0: /* unexpected LF */
                          warning("%s: %s: Invalid timestamp field found, "
                                  "skip this line.\n", pszFilename, szTime);
                          iRet = 1;
                          break;
                 case -2: /* unexpected EOF */
                          warning("%s: Came to EOF suddenly\n", pszFilename);
                          iRet = 1;
                 case -1: /*   expected EOF */
                          goto CLOSE_THISFILE;
                          break;
                 case -3: /* file access error */
                          warning("%s: File access error, skip it\n",
                                  pszFilename);
                          iRet = 1;
                          goto CLOSE_THISFILE;
                 default: /* bug or system error */
                          error_exit(1,"Unexpected error at %d\n", __LINE__);
               }
             }
             break;
    case 4: /* "-cZ" Calendar time with immediate outgoing mode */
             if (iGotOffset==0) {
               get_time_data_arrived(iFd, &gtsZero);
               iGotOffset=1;
             }
             while (1) {
               switch (read_1st_field_as_a_timestamp(fp, szTime)) {
                 case  1: /* read successfully */
                          if (! parse_calendartime(szTime, &tsTime)) {
                            warning("%s: %s: Invalid calendar-time, "
                                    "skip this line\n", pszFilename, szTime);
                            iRet = 1;
                            switch (skip_over_a_line(fp)) {
                              case  1: /* expected LF */
                                       break;
                              case -1: /* expected EOF */
                                       goto CLOSE_THISFILE;
                              case -2: /* file access error */
                                       warning("%s: File access error, "
                                               "skip it\n", pszFilename);
                                       goto CLOSE_THISFILE;
                                       break;
                              default: /* bug of system error */
                                       error_exit(1,"Unexpected error at %d\n",
                                                  __LINE__);
                                       break;
                            }
                            break;
                          }
                          if (iGotOffset==1) {
                            /* tsOffset = gtsZero - tsTime */
                            if ((gtsZero.tv_nsec - tsTime.tv_nsec) < 0) {
                              tsOffset.tv_sec  = gtsZero.tv_sec 
                                                 - tsTime.tv_sec  -          1;
                              tsOffset.tv_nsec = gtsZero.tv_nsec
                                                 - tsTime.tv_nsec + 1000000000;
                            } else {
                              tsOffset.tv_sec  = gtsZero.tv_sec -tsTime.tv_sec ;
                              tsOffset.tv_nsec = gtsZero.tv_nsec-tsTime.tv_nsec;
                            }
                            iGotOffset=2;
                          }
                          spend_my_spare_time(&tsTime, &tsOffset);
                          switch (read_and_write_a_line(fp)) {
                            case  1: /* expected LF */
                                     break;
                            case -1: /* expected EOF */
                                     goto CLOSE_THISFILE;
                            case -2: /* file access error */
                                     warning("%s: File access error, "
                                             "skip it\n", pszFilename);
                                     iRet = 1;
                                     goto CLOSE_THISFILE;
                                     break;
                            default: /* bug of system error */
                                     error_exit(1,"Unexpected error at %d\n",
                                                __LINE__);
                                     break;
                          }
                          break;
                 case  0: /* unexpected LF */
                          warning("%s: %s: Invalid timestamp field found, "
                                  "skip this line.\n", pszFilename, szTime);
                          iRet = 1;
                          break;
                 case -2: /* unexpected EOF */
                          warning("%s: Came to EOF suddenly\n", pszFilename);
                          iRet = 1;
                 case -1: /*   expected EOF */
                          goto CLOSE_THISFILE;
                          break;
                 case -3: /* file access error */
                          warning("%s: File access error, skip it\n",
                                  pszFilename);
                          goto CLOSE_THISFILE;
                 default: /* bug or system error */
                          error_exit(1,"Unexpected error at %d\n", __LINE__);
               }
             }
             break;
    case 5: /* "-eZ" UNIX epoch time with immediate outgoing mode */
    case 6: /* "-zZ" Zero time with immediate outgoing mode */
             if (iGotOffset==0) {
               get_time_data_arrived(iFd, &gtsZero);
               iGotOffset=1;
             }
             while (1) {
               switch (read_1st_field_as_a_timestamp(fp, szTime)) {
                 case  1: /* read successfully */
                          if (! parse_unixtime(szTime, &tsTime)) {
                            warning("%s: %s: Invalid timestamp, "
                                    "skip this line\n", pszFilename, szTime);
                            iRet = 1;
                            switch (skip_over_a_line(fp)) {
                              case  1: /* expected LF */
                                       break;
                              case -1: /* expected EOF */
                                       goto CLOSE_THISFILE;
                              case -2: /* file access error */
                                       warning("%s: File access error, "
                                               "skip it\n", pszFilename);
                                       goto CLOSE_THISFILE;
                                       break;
                              default: /* bug of system error */
                                       error_exit(1,"Unexpected error at %d\n",
                                                  __LINE__);
                                       break;
                            }
                            break;
                          }
                          if (iGotOffset==1) {
                            /* tsOffset = gtsZero - tsTime */
                            if ((gtsZero.tv_nsec - tsTime.tv_nsec) < 0) {
                              tsOffset.tv_sec  = gtsZero.tv_sec 
                                                 - tsTime.tv_sec  -          1;
                              tsOffset.tv_nsec = gtsZero.tv_nsec
                                                 - tsTime.tv_nsec + 1000000000;
                            } else {
                              tsOffset.tv_sec  = gtsZero.tv_sec -tsTime.tv_sec ;
                              tsOffset.tv_nsec = gtsZero.tv_nsec-tsTime.tv_nsec;
                            }
                            iGotOffset=2;
                          }
                          spend_my_spare_time(&tsTime, &tsOffset);
                          switch (read_and_write_a_line(fp)) {
                            case  1: /* expected LF */
                                     break;
                            case -1: /* expected EOF */
                                     goto CLOSE_THISFILE;
                            case -2: /* file access error */
                                     warning("%s: File access error, "
                                             "skip it\n", pszFilename);
                                     iRet = 1;
                                     goto CLOSE_THISFILE;
                                     break;
                            default: /* bug of system error */
                                     error_exit(1,"Unexpected error at %d\n",
                                                __LINE__);
                                     break;
                          }
                          break;
                 case  0: /* unexpected LF */
                          warning("%s: %s: Invalid timestamp field found, "
                                  "skip this file.\n", pszFilename, szTime);
                          iRet = 1;
                          break;
                 case -2: /* unexpected EOF */
                          warning("%s: Came to EOF suddenly\n", pszFilename);
                          iRet = 1;
                 case -1: /*   expected EOF */
                          goto CLOSE_THISFILE;
                          break;
                 case -3: /* file access error */
                          warning("%s: File access error, skip it\n",
                                  pszFilename);
                          iRet = 1;
                          goto CLOSE_THISFILE;
                 default: /* bug or system error */
                          error_exit(1,"Unexpected error at %d\n", __LINE__);
               }
             }
             break;
    default:
             error_exit(255,"main() #L1: Invalid mode number\n");
  }

CLOSE_THISFILE:
  /*--- Close the input file ---------------------------------------*/
  if (fp != stdin) {fclose(fp);}

  /*--- End loop ---------------------------------------------------*/
  if (pszPath == NULL) {break;}
  iFileno++;
}

/*=== Finish normally ==============================================*/
return(iRet);}



/*####################################################################
# Functions
####################################################################*/

/*=== Read and write only one line having a timestamp ================
 * [in] iFd     : File descriptor number for read
 *      ptsTime : Pointer to return the time when data arrived
 * [ret] (none) : This function alway calls error_exit() if any error
                  occured.                                          */
void get_time_data_arrived(int iFd, struct timespec *ptsTime) {

  /*--- Variables --------------------------------------------------*/
  fd_set fdsRead;

  /*--- Wait for data arriving -------------------------------------*/
  FD_ZERO(&fdsRead);
  FD_SET(iFd, &fdsRead);
  if (select(iFd+1, &fdsRead, NULL, NULL, NULL) == -1) {
    error_exit(errno,"select() in get_time_data_arrived(): %s\n",
               strerror(errno));
  }

  /*--- Set the time -----------------------------------------------*/
  if (clock_gettime(CLOCK_REALTIME,ptsTime) != 0) {
    error_exit(errno,"clock_gettime() in get_time_data_arrived(): %s\n",
               strerror(errno));
  }
}

/*=== Read and write only one line having a timestamp ================
 * [in] fp      : Filehandle for read
 *      pszTime : Pointer for the string buffer to get the timestamp on
 *                the 1st field and it is followed by a field delimiter
 *                if exists
 *                (Size of the buffer you give MUST BE 32 BYTES or more!)
 * [ret] == 0 : Finished reading due to '\n'
 *       == 1 : Finished reading successfully, you may use the result in
 *              the buffer
 *       ==-1 : Finished reading because no more data in the "fp"
 *       ==-2 : Finished reading due to the end of file
 *       ==-3 : Finished reading due to a file reading error
 *       other: Finished reading due to a system error              */
int read_1st_field_as_a_timestamp(FILE *fp, char *pszTime) {

  /*--- Variables --------------------------------------------------*/
  int        iTslen = 0; /* length of the timestamp string          */
  int        iChar;

  /*--- Reading and writing a line ---------------------------------*/
  while (1) {
    iChar = getc(fp);
    switch (iChar) {
      case ' ' :
      case '\t':
                 pszTime[iTslen  ]=iChar;
                 pszTime[iTslen+1]=    0;
                 return 1;
      case EOF :
                 if         (feof(  fp)) {
                   if (iTslen==0) {
                     return -1;
                   } else         {
                     if (giVerbose>0) {
                       warning("EOF came while reading 1st field\n");
                     }
                     return -2;
                   }
                 } else  if (ferror(fp)) {
                   if (giVerbose>0) {
                     warning("error while reading 1st field\n");
                   }
                   return -3;
                 } else                  {
                   return -4;
                 }
      case '\n':
                 pszTime[iTslen]=0;
                 return 0;
      default  :
                 if (iTslen>30) {                                 continue;}
                 else           {pszTime[iTslen]=iChar; iTslen++; continue;}
    }
  }
}

/*=== Read and write only one line ===================================
 * [in] fp    : Filehandle for read
 * [ret] == 1 : Finished reading/writing due to '\n', which is the last
 *              char of the file
 *       ==-1 : Finished reading due to the end of file
 *       ==-2 : Finished reading due to a file reading error
 *       ==-3 : Finished reading due to a system error              */
int read_and_write_a_line(FILE *fp) {

  /*--- Variables --------------------------------------------------*/
  int        iChar;

  /*--- Reading and writing a line ---------------------------------*/
  while (1) {
    iChar = getc(fp);
    switch (iChar) {
      case EOF :
                 if (feof(  fp)) {return -1;}
                 if (ferror(fp)) {return -2;}
                 else            {return -3;}
      case '\n':
                 if (putchar('\n' )==EOF) {
                   error_exit(errno,"stdout write error #1: %s\n",
                              strerror(errno));
                 }
                 return 1;
      default  :
                 if (putchar(iChar)==EOF) {
                   error_exit(errno,"stdout write error #2: %s\n",
                              strerror(errno));
                 }
                 break;
    }
  }
}

/*=== Read and throw away one line ===================================
 * [in] fp    : Filehandle for read
 * [ret] == 1 : Finished reading/writing due to '\n', which is the last
 *              char of the file
 *       ==-1 : Finished reading due to the end of file
 *       ==-2 : Finished reading due to a file reading error
 *       ==-3 : Finished reading due to a system error              */
int skip_over_a_line(FILE *fp) {

  /*--- Variables --------------------------------------------------*/
  int        iChar;

  /*--- Reading and writing a line ---------------------------------*/
  while (1) {
    iChar = getc(fp);
    switch (iChar) {
      case EOF :
                 if (feof(  fp)) {return -1;}
                 if (ferror(fp)) {return -2;}
                 else            {return -3;}
      case '\n':
                 return 1;
      default  :
                 break;
    }
  }
}

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
  int  i, j, k;            /* +-- 0:(reading integer part)          */
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
  int  i, j, k;            /* +-- 0:(reading integer part)          */
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

/*=== Sleep until the next interval period ===========================
 * [in] ptsTo     : Time until which this function wait
                    (given from the 1st field of a line, which not adjusted yet)
        ptsOffset : Offset for ptsTo (set NULL if unnecessary)      */
void spend_my_spare_time(struct timespec *ptsTo, struct timespec *ptsOffset) {

  /*--- Variables --------------------------------------------------*/
  struct timespec tsTo  ;
  struct timespec tsDiff;
  struct timespec tsNow ;

  /*--- Calculate how long I wait ----------------------------------*/
  if (! ptsOffset) {
    /* tsTo = ptsTo */
    tsTo.tv_sec  = ptsTo->tv_sec ;
    tsTo.tv_nsec = ptsTo->tv_nsec;
  } else           {
    /* tsTo = ptsTo + ptsOffset */
    tsTo.tv_nsec = ptsTo->tv_nsec + ptsOffset->tv_nsec;
    if (tsTo.tv_nsec > 999999999) {
      tsTo.tv_nsec -= 1000000000;
      tsTo.tv_sec   = ptsTo->tv_sec + ptsOffset->tv_sec + 1;
    } else {
      tsTo.tv_sec   = ptsTo->tv_sec + ptsOffset->tv_sec;
    }
  }
  /* tsNow = (current_time) */
  if (clock_gettime(CLOCK_REALTIME,&tsNow) != 0) {
    error_exit(errno,"clock_gettime() in spend_my_spare_time(): %s\n",
               strerror(errno));
  }
  /* tsDiff = tsTo - tsNow */
  if ((tsTo.tv_nsec - tsNow.tv_nsec) < 0) {
    tsDiff.tv_sec  = tsTo.tv_sec  - tsNow.tv_sec  -          1;
    tsDiff.tv_nsec = tsTo.tv_nsec - tsNow.tv_nsec + 1000000000;
  } else {
    tsDiff.tv_sec  = tsTo.tv_sec  - tsNow.tv_sec ;
    tsDiff.tv_nsec = tsTo.tv_nsec - tsNow.tv_nsec;
  }

  /*--- Sleeping for tsDiff ----------------------------------------*/
  while (nanosleep(&tsDiff,NULL) != 0) {
    if (errno == EINVAL) { /* It means ptsNow is a past time, doesn't matter */
      if (giVerbose>1) {warning("Waiting time is negative\n");}
      break;
    }
    error_exit(errno,"nanosleep() in spend_my_spare_time(): %s\n",
               strerror(errno));
  }
}

/*=== Parse the periodic time ========================================
 * [ret] >= 0  : Interval value (in nanosecound)
 *       <=-1  : (undefined)
 *       <=-2  : It is not a value                                  */
int64_t parse_periodictime(char *pszArg) {

  /*--- Variables --------------------------------------------------*/
  char   szUnit[CTRL_FILE_BUF];
  double dNum;

  /*--- Check the lengths of the argument --------------------------*/
  if (strlen(pszArg)>=CTRL_FILE_BUF) {return -2;}

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

１．テキスト蓄積の方法


/* listed line struct */
typedef struct listedline {
  char              *pszStr;
  size_t             sizStr;
  struct timespec    ts;
  struct listedline *pstNext;
} lline_t;




/*=== Read a line from FH and push it into the line buffer list =====
 * [in]  *szHead : Header string to be written at the top of the buffer
 *                 - You should give me the timestamp string that is
 *                   terminated with 0x20.
 *                 - The length must be less than 1024.
 *       *fp     : File handle
 *       *pllLast: The Latest lline_t pointer
 * [ret] ==1 : Finished normally 
         ==2 : Finished normally (reached the EOF)
 *       ==0 : error                                                */
int push_a_line_into_lline(char *szHead, struct timespec *pts,
                           FILE *fp    , lline_t *pllLast     ) {

  int      i, iChar  , iRet;
  char    *pszBuf, *psz;
  size_t   sizBuf, sizStr;
  lline_t *pll;

  if (fp == NULL) {error_exit(1,"Error at %d, " MY_REV "\n", __LINE__);}

  pszBuf = NULL;
  sizBuf = sizStr = 0;

  if (szHead != NULL) {
    if ((pszBuf = (char *)malloc(pszBuf, 1024)) == NULL) {
      error_exit(errno,"push_a_line_into_lline() #1: %s\n",strerror(errno));
    }
    sizBuf = 1024;
    psz = pszBuf;
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

  return iRet;
}



/*=== Flush the line buffer list =====================================
 * [in]  *pllFirst: The first item pointer of the line buffer list
 * [ret]          : (none)
 * NOTICE: This function does NOT release any allocated memory      */
void flush_lline(lline_t *pllFirst) {

  lline_t *pll;
  ssize_t ssRes;

  if (pllFirst == NULL) {error_exit(1,"Error at %d, " MY_REV "\n", __LINE__);}

  pll = pllFirst;
  do {
    ssRes = write(STDOUT_FILENO, pll->pszStr, pll->sizStr);
    if (ssRes != (ssize_t)pll->sizStr) {
      if (ssRes==-1) {error_exit(errno,"flush_lline(): %s\n",strerror(errno));}
      else           {error_exit(errno,"flush_lline(): write error\n";        }
    }
    pll = pll->pstNext;
  } while (pll != NULL)

  return;
}



２．テキスト蓄積の方法

元の read_1st_field_as_a_timestamp() は、1バイトずつ読み込んで列区切り（' 'または'\t'）に
到達した場合、列区切りを含めず、そこまでのタイムスタンプ文字列を第二引数の文字配列に入れて返す。
しかし、これから作ろうとしているものでは、列区切りが何の文字なのかも把握しなければならない。
したがって、列区切りに到達した場合の処理に手を加えなければならない。
