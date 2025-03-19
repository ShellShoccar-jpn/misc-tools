/*####################################################################
#
# TSCAT - A "cat" Command Which Can Reprodude the Timing of Flow
#
# USAGE   : tscat [-c|-e|-I|-z] [-Z] [-1kuy] [-p n] [file [...]]
# Args    : file ........ Filepath to be send ("-" means STDIN)
#                         The file MUST be a textfile and MUST have
#                         a timestamp at the first field to make the
#                         timing of flow. The first space character
#                         <0x20> of every line will be regarded as
#                         the field delimiter.
#                         And, the string from the top of the line to
#                         the charater will be cut before outgoing to
#                         the stdout.
# Options : -c,-e,-I,-z . Specify the format for timestamp. You can choose
#                         one of them.
#                           -c ... "YYYYMMDDhhmmss[.n]" (default)
#                                  Calendar time (standard time) in your
#                                  timezone (".n" is the digits under
#                                  second. You can specify up to nano
#                                  second.)
#                           -e ... "n[.n]"
#                                  The number of seconds since the UNIX
#                                  epoch (".n" is the same as -c)
#                           -I ... "YYYY-MM-DDThh:mm:ss[,n][{{+|-}hh:mm|Z}]"
#                                  Ext. ISO 8601 formatted time in your
#                                  timezone (".n" is the same as -c)
#                           -z ... "n[.n]"
#                                  The number of seconds since this
#                                  command has startrd (".n" is the same
#                                  as -c)
#           -Z .......... Define the time when the first line came as 0.
#                         For instance, imagine that the first field of
#                         the first line is "20200229235959," and the
#                         second line's one is "20200301000004." when
#                         "-c" option is given. In this case, the first
#                         line is sent to stdout immediately, and after
#                         five seconds, the second line is sent.
#           -1 .......... * Output one character/line (LF) at first before
#                           outputting the incoming data.
#                         * This option might work as a starter of the
#                           system embedding this command.
#           -k .......... Keep the timestamp at the head of each line
#                         when outputting the line to the stdout.
#           -u .......... Set the date in UTC when -c option is set
#                         (same as that of date command)
#           -y .......... "Typing mode": Do not output the LF character
#                         at the end of each line in the input file unless
#                         the line has no other letters. This mode is
#                         useful to resconstruct the timing of key typing
#                         recorded by as in the following.
#                           $ typeliner -e | linets -c3 > mytyping.txt
#                           $ tscat -ycZ mytyping.txt
#           [The following option is for professional]
#           -p n ........ Process priority setting [0-3] (if possible)
#                          0: Normal process
#                          1: Weakest realtime process (default)
#                          2: Strongest realtime process for generic users
#                             (for only Linux, equivalent 1 for otheres)
#                          3: Strongest realtime process of this host
#                         Larger numbers maybe require a privileged user,
#                         but if failed, it will try the smaller numbers.
# Return  : Return 0 only when finished successfully
#
# How to compile : cc -O3 -o __CMDNAME__ __SRCNAME__ -lrt
#                  (if it doesn't work)
# How to compile : cc -O3 -o __CMDNAME__ __SRCNAME__
#
# Written by Shell-Shoccar Japan (@shellshoccarjpn) on 2025-03-19
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/select.h>
#include <time.h>
#include <fcntl.h>
#include <signal.h>
#include <locale.h>
#if defined(_POSIX_PRIORITY_SCHEDULING) && !defined(__OpenBSD__) && !defined(__APPLE__)
  #include <sched.h>
  #include <sys/resource.h>
#endif

/*--- macro constants ----------------------------------------------*/
/* Some OSes, such as HP-UX, may not know the following macros whenever
   <limit.h> is included. So, this source code defines them by itself. */
#ifndef LONG_MAX
  #define LONG_MAX           2147483647
#endif
#ifndef LLONG_MAX
  #define LLONG_MAX 9223372036854775807
#endif
/* Buffer size for the read_and_write_a_line() */
#define LINE_BUF 1024

/*--- data type definitions ----------------------------------------*/
typedef struct timespec tmsp;

/*--- prototype functions ------------------------------------------*/
void get_time_data_arrived(int iFd, tmsp *ptsTime);
int  read_1st_field_as_a_timestamp(FILE *fp, char *pszTime);
int  read_and_write_a_line(FILE *fp);
int  skip_over_a_line(FILE *fp);
int  parse_calendartime(char* pszTime, tmsp *ptsTime);
int  parse_unixtime(char* pszTime, tmsp *ptsTime);
int  parse_iso8601time(char* pszTime, tmsp *ptsTime);
void spend_my_spare_time(tmsp *ptsTo, tmsp *ptsOffset);
int  change_to_rtprocess(int iPrio);

/*--- global variables ---------------------------------------------*/
char* gpszCmdname;  /* The name of this command                    */
int   giTypingmode; /* Typing mode by option -y is on if >0        */
int   giVerbose;    /* speaks more verbosely by the greater number */
int   giTZoffs;     /* Offset in second in the local timezone      */
tmsp  gtsZero;      /* The zero-point time                         */

/*=== Define the functions for printing usage and error ============*/

/*--- exit with usage ----------------------------------------------*/
void print_usage_and_exit(void) {
  fprintf(stderr,
#if defined(_POSIX_PRIORITY_SCHEDULING) && !defined(__OpenBSD__) && !defined(__APPLE__)
    "USAGE   : %s [-c|-e|-I|-z] [-Z] [-1kuy] [-p n] [file [...]]\n"
#else
    "USAGE   : %s [-c|-e|-I|-z] [-Z] [-1kuy] [file [...]]\n"
#endif
    "Args    : file ........ Filepath to be send (\"-\" means STDIN)\n"
    "                        The file MUST be a textfile and MUST have\n"
    "                        a timestamp at the first field to make the\n"
    "                        timing of flow. The first space character\n"
    "                        <0x20> of every line will be regarded as\n"
    "                        the field delimiter.\n"
    "                        And, the string from the top of the line to\n"
    "                        the charater will be cut before outgoing to\n"
    "                        the stdout.\n"
    "Options : -c,-e,-I,-z . Specify the format for timestamp. You can choose\n"
    "                        one of them.\n"
    "                          -c ... \"YYYYMMDDhhmmss[.n]\" (default)\n"
    "                                 Calendar time (standard time) in your\n"
    "                                 timezone (\".n\" is the digits under\n"
    "                                 second. You can specify up to nano\n"
    "                                 second.)\n"
    "                          -e ... \"n[.n]\"\n"
    "                                 The number of seconds since the UNIX\n"
    "                                 epoch (\".n\" is the same as -c)\n"
    "                          -I ... \"YYYY-MM-DDThh:mm:ss[,n][{{+|-}hh:mm|Z}]"
                                                                          "\"\n"
    "                                 Ext. ISO 8601 formatted time in your\n"
    "                                 timezone (\".n\" is the same as -c)\n"
    "                          -z ... \"n[.n]\"\n"
    "                                 The number of seconds since this\n"
    "                                 command has started (\".n\" is the same\n"
    "                                 as -c)\n"
    "          -Z .......... Define the time when the first line came as 0.\n"
    "                        For instance, imagine that the first field of\n"
    "                        the first line is \"20200229235959,\" and the\n"
    "                        second line's one is \"20200301000004.\" when\n"
    "                        \"-c\" option is given. In this case, the first\n"
    "                        line is sent to stdout immediately, and after\n"
    "                        five seconds, the second line is sent.\n"
    "          -1 .......... * Output one character/line (LF) at first before\n"
    "                          outputting the incoming data.\n"
    "                        * This option might work as a starter of the\n"
    "                          system embedding this command.\n"
    "          -k .......... Keep the timestamp at the head of each line\n"
    "                        when outputting the line to the stdout.\n"
    "          -u .......... Set the date in UTC when -c option is set\n"
    "                        (same as that of date command)\n"
    "          -y .......... \"Typing mode\": Do not output the LF character\n"
    "                        at the end of each line in the input file unless\n"
    "                        the line has no other letters. This mode is\n"
    "                        useful to resconstruct the timing of key typing\n"
    "                        recorded by as in the following.\n"
    "                          $ typeliner -e | linets -c3 > mytyping.txt\n"
    "                          $ tscat -ycZ mytyping.txt\n"
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
    "Version : 2025-03-19 19:42:00 JST\n"
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
int   iMode;         /* 0:"-c",  1:"-e",  2:"-z",  3:"-I",
                        4:"-cZ", 5:"-eZ", 6:"-zZ", 7:"-IZ"          */
int   iOpt_1;        /* -1 option flag (default 0)                  */
int   iKeepTs;       /* -k option flag (0>:Keep timestamps, =0:Drop)*/
int   iPrio;         /* -p option number (default 1)                */
int   iRet;          /* return code                                 */
int   iGotOffset;    /* 0:NotYet 1:GetZeroPoint 2:Done              */
char  szTime[43];    /* Buffer for the 1st field of lines           */
tmsp  tsTime;        /* Parsed time for the 1st field               */
tmsp  tsOffset;      /* Zero-point time to adjust the 1st field one */
char *pszPath;       /* filepath on arguments                       */
char *pszFilename;   /* filepath (for message)                      */
int   iFileno;       /* file# of filepath                           */
int   iFd;           /* file descriptor                             */
FILE *fp;            /* file handle                                 */
int   i;             /* all-purpose int                             */

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
iMode        = 0; /* 0:"-c"(default) 1:"-e" 2:"-z" 4:"-cZ" 5:"-eZ" 6:"-zZ" */
iOpt_1       = 0; /* 0:Normal 1:Output one character/line at first         */
iKeepTs      = 0; /* 0>:Keep timestamps, =0:Drop(default) */
giTypingmode = 0;
iPrio        = 1;
giVerbose    = 0;
/*--- Parse options which start by "-" -----------------------------*/
while ((i=getopt(argc, argv, "ceIp:1kuyvhZz")) != -1) {
  switch (i) {
    case 'c': iMode&=4; iMode+=0;            break;
    case 'e': iMode&=4; iMode+=1;            break;
    case 'z': iMode&=4; iMode+=2;            break;
    case 'I': iMode&=4; iMode+=3;            break;
    case 'Z': iMode&=3; iMode+=4;            break;
    case '1': iOpt_1=1;                      break;
    case 'k': iKeepTs=1;                     break;
    case 'u': (void)setenv("TZ", "UTC0", 1); break;
    case 'y': giTypingmode=1;                break;
    #if defined(_POSIX_PRIORITY_SCHEDULING) && !defined(__OpenBSD__) && !defined(__APPLE__)
      case 'p': if (sscanf(optarg,"%d",&iPrio) != 1) {print_usage_and_exit();}
                                               break;
    #endif
    case 'v': giVerbose++;                   break;
    case 'h': print_usage_and_exit();
    default : print_usage_and_exit();
  }
}
argc -= optind-1;
argv += optind  ;
if (giVerbose>0) {warning("verbose mode (level %d)\n",giVerbose);}

/*=== Switch buffer mode ===========================================*/
if (setvbuf(stdout,NULL,(giTypingmode>0)?_IONBF:_IOLBF,0)!=0) {
  error_exit(255,"Failed to switch to line-buffered mode\n");
}

/*=== Try to make me a realtime process ============================*/
if (change_to_rtprocess(iPrio)==-1) {print_usage_and_exit();}

/*=== Calculate the timezone offset if the -I option is enabled ====*/
if (iMode%4==3) {
  /* "giTZoffset" means "localtime - UTCtime" */
  giTZoffs = (int)difftime(mktime(localtime((time_t[]){0})),
                           mktime(   gmtime((time_t[]){0})) );
}

/*=== Output the starter charater/line when -1 is enabled ==========*/
if (iOpt_1 && putchar('\n')==EOF) {
  error_exit(errno, "putchar() in main(): %s\n", strerror(errno));
}

/*=== Each file loop ===============================================*/
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
  switch (iMode) {
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
                          if (iKeepTs) {
                            if (fputs(szTime, stdout)==EOF) {
                              error_exit(errno,"stdout write error #m1: %s\n",
                                         strerror(errno));
                            }
                          }
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
                          if (iKeepTs) {
                            if (fputs(szTime, stdout)==EOF) {
                              error_exit(errno,"stdout write error #m2: %s\n",
                                         strerror(errno));
                            }
                          }
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
                            /* tsOffset = gtsZero */
                            tsOffset.tv_sec  = gtsZero.tv_sec ;
                            tsOffset.tv_nsec = gtsZero.tv_nsec;
                            iGotOffset=2;
                          }
                          spend_my_spare_time(&tsTime, &tsOffset);
                          if (iKeepTs) {
                            if (fputs(szTime, stdout)==EOF) {
                              error_exit(errno,"stdout write error #m3: %s\n",
                                         strerror(errno));
                            }
                          }
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
    case 3: /* "-I" Extended ISO 8601 formatted time mode */
             while (1) {
               switch (read_1st_field_as_a_timestamp(fp, szTime)) {
                 case  1: /* read successfully */
                          if (! parse_iso8601time(szTime, &tsTime)) {
                            warning("%s: %s: Invalid ISO8601-time, "
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
                          if (iKeepTs) {
                            if (fputs(szTime, stdout)==EOF) {
                              error_exit(errno,"stdout write error #m1: %s\n",
                                         strerror(errno));
                            }
                          }
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
                          if (iKeepTs) {
                            if (fputs(szTime, stdout)==EOF) {
                              error_exit(errno,"stdout write error #m4: %s\n",
                                         strerror(errno));
                            }
                          }
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
                          if (iKeepTs) {
                            if (fputs(szTime, stdout)==EOF) {
                              error_exit(errno,"stdout write error #m5: %s\n",
                                         strerror(errno));
                            }
                          }
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
    case 7: /* "-IZ" Ext.ISO 8601 formatted time with immediate outgoing mode */
             if (iGotOffset==0) {
               get_time_data_arrived(iFd, &gtsZero);
               iGotOffset=1;
             }
             while (1) {
               switch (read_1st_field_as_a_timestamp(fp, szTime)) {
                 case  1: /* read successfully */
                          if (! parse_iso8601time(szTime, &tsTime)) {
                            warning("%s: %s: Invalid ISO8601-time, "
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
                          if (iKeepTs) {
                            if (fputs(szTime, stdout)==EOF) {
                              error_exit(errno,"stdout write error #m4: %s\n",
                                         strerror(errno));
                            }
                          }
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
void get_time_data_arrived(int iFd, tmsp *ptsTime) {

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
 *                the 1st field with a white space (if it exists)
 *                (Size of the buffer you give MUST BE 43 BYTES or more!)
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
                 return 0;
      default  :
                 if (iTslen>41) {                                 continue;}
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
  char       szBuf[LINE_BUF]; /* Buffer for reading 1-line */
  int        iLen;            /* Actual size of string in the buffer */
  int        iChar;           /* Buffer for reading 1-char */

  /*--- Reading and writing a line (normal mode) -------------------*/
  if (! giTypingmode) {
    while (fgets(szBuf,LINE_BUF,fp) != NULL) {
      if (fputs(szBuf,stdout) < 0) {
        error_exit(errno,"fputs() #RW1L-1: %s\n",strerror(errno));
      }
      iLen = strnlen(szBuf, LINE_BUF);
      if (szBuf[iLen-1] == '\n') {return 1;}
      if (iLen < LINE_BUF-1) {
        iChar=getc(fp);
        if (iChar==EOF ) {if (feof(  fp)) {return -1;}
                          if (ferror(fp)) {return -2;}
                          else            {return -3;}}
        while (putchar(iChar)==EOF) {
          error_exit(errno,"putchar() #RW1L-1: %s\n",strerror(errno));
        }
        if (iChar=='\n') {return 1;                   }
      }
    }
    if (feof(  fp)) {return -1;}
    if (ferror(fp)) {return -2;}
    else            {return -3;}
  }

  /*--- Reading and writing a line (typing mode) -------------------*/
  iChar = getc(fp);
  switch (iChar) {
    case EOF :
               if (feof(  fp)) {return -1;}
               if (ferror(fp)) {return -2;}
               else            {return -3;}
    case '\n':
               if (putchar('\n' )==EOF) {
                 error_exit(errno,"putchar() #RW1L-2: %s\n",
                            strerror(errno));
               }
               return 1;
    default  :
               if (putchar(iChar)==EOF) {
                 error_exit(errno,"putchar() #RW1L-3: %s\n",
                            strerror(errno));
               }
               break;
  }
  while (fgets(szBuf,LINE_BUF,fp) != NULL) {
    iLen = strnlen(szBuf, LINE_BUF);
    if (szBuf[iLen-1] == '\n') {
      szBuf[iLen-1] = '\0';
      if (fputs(szBuf,stdout) < 0) {
        error_exit(errno,"fputs() #RW1L-2: %s\n",strerror(errno));
      }
      return 1;
    }
    if (fputs(szBuf,stdout) < 0) {
      error_exit(errno,"fputs() #RW1L-3: %s\n",strerror(errno));
    }
    if (iLen < LINE_BUF-1) {
      iChar=getc(fp);
      if (iChar==EOF ) {if (feof(  fp)) {return -1;}
                        if (ferror(fp)) {return -2;}
                        else            {return -3;}}
      if (iChar=='\n') {return 1;                   }
      while (putchar(iChar)==EOF) {
        error_exit(errno,"putchar() #RW1L-4: %s\n",strerror(errno));
      }
    }
  }
  if (feof(  fp)) {return -1;}
  if (ferror(fp)) {return -2;}
  else            {return -3;}
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
  ptsTime->tv_sec  = mktime(&tmDate);
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
  char szSec[20], szNsec[10];
  int  i, j, k;            /* +-- 0:(reading integer part)          */
  char c;                  /* +-- 1:finish reading without_decimals */
  int  iStatus = 0; /* <--------- 2:to_be_started reading decimals  */

  /*--- Separate pszTime into seconds and nanoseconds --------------*/
  for (i=0; i<19; i++) {
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
  if ((iStatus==0) && (i==19)) {
    switch (pszTime[19]) {
      case '.': szSec[19]=0; iStatus=2; i++; break;
      case  0 : szSec[19]=0; iStatus=1;      break;
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
  ptsTime->tv_sec = (time_t)atoll(szSec);
  if (ptsTime->tv_sec<0) {
    ptsTime->tv_sec = (sizeof(time_t)>=8) ? LLONG_MAX : LONG_MAX;
  }
  ptsTime->tv_nsec = atol(szNsec);

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
  ptsTime->tv_sec  = mktime(&tmDate);
  ptsTime->tv_nsec = atol(szNsec);
  if (j!=0) {ptsTime->tv_sec = ptsTime->tv_sec + giTZoffs - iTZoffs;}

  return 1;
}

/*=== Sleep until the next interval period ===========================
 * [in] ptsTo     : Time until which this function wait
                    (given from the 1st field of a line, which not adjusted yet)
        ptsOffset : Offset for ptsTo (set NULL if unnecessary)      */
void spend_my_spare_time(tmsp *ptsTo, tmsp *ptsOffset) {

  /*--- Variables --------------------------------------------------*/
  tmsp tsTo  ;
  tmsp tsDiff;
  tmsp tsNow ;

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
