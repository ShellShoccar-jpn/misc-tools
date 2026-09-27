/*####################################################################
#
# TSTAIL - A "tail" Command Which Considers Timestamp Instead of
#          the Number of Lines
#
# USAGE   : (a) tstail [options] -d    duration      [file ...]
#           (b) tstail [options] -xd   duration      [file ...]
#           (c) tstail [options] -d   -duration      [file ...]
#           (d) tstail [options] -xd  -duration      [file ...]
#           (e) tstail [options] -t    date-and-time [file ...]
#           (f) tstail [options] -xt   date-and-time [file ...]
#           ("-x" must not be fused after "-d"/"-t" in the same token,
#            e.g. "-dx" is NOT "-d" + "-x" but "-d" with the argument
#            "x". Write it as "-xd"/"-xt", or as two separate options
#            such as "-d -x" or "-x -d".)
#
#           The lines that can pass through this command will be chosen
#           by making sure the timestamp at the first field of each line
#           is in one of the following ranges.
#             (a) [ <command start time>-<duration>, <bottom> ]
#             (b) ( <command start time>-<duration>, <bottom> ]
#             (c) [ <1st line's time>  +<duration>,  <bottom> ]
#             (d) ( <1st line's time>  +<duration>,  <bottom> ]
#             (e) [ date-and-time,                   <bottom> ]
#             (f) ( date-and-time,                   <bottom> ]
#           When two or more files are given, they are treated as if
#           they were a single concatenated stream (like "cat"), so
#           <1st line's time> means the first line of the FIRST file,
#           and <bottom> means the last line of the LAST file.
#
#           NOTE: Since <bottom> is always the last line of the input,
#           this command cannot produce any output until it reaches the
#           end of the input (EOF), no matter which pattern is used --
#           even pattern (a)/(b), whose border time itself is already
#           fixed the moment this command starts. Therefore, this
#           command is NOT suitable for an unbounded, never-ending
#           stream such as one from "tail -f".
#
# Args    : file ........ Filepath to be send ("-" means STDIN)
#                         The file MUST be a textfile and MUST have
#                         a timestamp at the first field to make the
#                         timing of flow. The first space character
#                         <0x20> of every line will be regarded as
#                         the field delimiter.
# Options : -c,-e,-I,-z . Specify the format for timestamp and -t option
#                         parameter. You can choose one of the following.
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
#           -d duration . This is one of options to specify the timestamp
#                         range. (See the pattern (a) to (d) above)
#                         You can use the format "A[.B][u]" as the
#                         option's parameter "duration."
#                           "A" is the integer part of the time.
#                           "B" is the decimal part of the time.
#                           "u" is the unit for the time. You can choose
#                               one of the followings.
#                               "s", "ms", "us", "ns", "m", "h" and "d."
#           -t date-and-time
#                         This is one of options to specify the timestamp
#                         range. (See the pattern (e) and (f) above)
#                         The format of "date-and-time" depends on
#                         which of the option "-c", "-e," "-I," or "-z"
#                         you choose.
#                           "-c" ... "YYYYMMDDhhmmss[.n]" (cal. time)
#                           "-e" ... "n[.n]" (UNIX time)
#                           "-I" ... "YYYY-MM-DDThh:mm:ss[,n][{{+|-}hh:mm|Z}]"
#                                    (ext. ISO 8601 time)
#                           "-z" ... "n[.n]" (elapsed sec. since start)
#           -q .......... Suppresses printing filenames when two or more
#                         files are given.
#           -u .......... Set the date in UTC when -c option is set
#                         (same as that of date command)
#           -x .......... An additional option for -d and -t. It will
#                         exclude the border endpoint itself (the start
#                         of the range) from the output.
#                         (See the pattern (b), (d) and (f) above)
#           -Z .......... Define the time when the LAST line came as
#                         the origin, instead of the time this command
#                         started. This option is only meaningful for
#                         the pattern (a) and (b) (i.e. "-d" without a
#                         leading "-"). Without this option, the origin
#                         for pattern (a)/(b) is the moment this command
#                         started, which is not reproducible from run to
#                         run; with this option, the origin instead
#                         becomes the timestamp of the very last line of
#                         the input, which makes the result reproducible.
# Retuen  : Return 0 only when finished successfully for all files
#
# How to compile : cc -O3 -std=c99 -o __CMDNAME__ __SRCNAME__ -lrt
#                  (if it doesn't work)
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
#define MY_REV "2026-09-24 01:09:03 JST"

/*--- headers ------------------------------------------------------*/
/* Solaris 11.3's <sys/feature_tests.h> only recognizes the exact
 * values _XOPEN_SOURCE==600 / _POSIX_C_SOURCE==200112L for its UNIX 03
 * detection and has no notion of POSIX.1-2008/SUSv4 at all; requesting
 * 700 there trips its strict conformance-level check and aborts the
 * build, so __EXTENSIONS__ (which sidesteps that check entirely and
 * exposes every POSIX/XSI/BSD interface regardless of C standard
 * level) is used there instead. Everywhere else, _XOPEN_SOURCE 700 is
 * used directly, for strptime().                                    */
#if defined(__sun) || defined(__SVR4)
  #define __EXTENSIONS__
#else
  #define _XOPEN_SOURCE 700
#endif
#include <limits.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
extern char *optarg;
extern int optind, opterr, optopt;
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
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
/* Buffer size for the timestamp field which is copied out of a line
   before parsing it (long enough for the longest calendar-time or
   UNIX-time representation this command accepts)                   */
#define FIELD_BUF 40

/*--- macro functions (timespec arithmetic, "ts" is a variable) ----*/
#define tsadd(ts,i8) ts.tv_nsec+=i8%1000000000;ts.tv_sec+=ts.tv_nsec/1000000000+i8/1000000000;ts.tv_nsec%=1000000000
#define tssub(ts,i8) ts.tv_nsec-=i8%1000000000;ts.tv_sec-=(ts.tv_nsec<0)+i8/1000000000;ts.tv_nsec+=(ts.tv_nsec<0)*1000000000

/*--- data type ------------------------------------------------------*/
typedef struct timespec tmsp;
typedef struct _lineentry_t {
  char*   pszLine;   /* the raw line data (ownership is transferred
                        from read_one_line()'s buffer when a node is
                        created)                                        */
  size_t  sizLine;
  tmsp    tsTime;    /* the parsed timestamp of this line               */
  struct _lineentry_t* pNext;
} lineentry_t;
typedef struct _filerange_t {
  char*        pszDisp;     /* the name to print in the "==> ... <=="
                                header                                  */
  int          iIsLinelist; /* 1: this entry uses "pLineHead" (built by
                                   the slow, line-by-line path)
                                0: this entry uses "pBuf"/"sizBuf"/
                                   "sizCutin"/"iMapped" (built by the
                                   fast, mmap'ed path)                  */
  char*        pBuf;        /* (iIsLinelist==0) the whole file content  */
  size_t       sizBuf;      /* (iIsLinelist==0) the length of *pBuf     */
  size_t       sizCutin;    /* (iIsLinelist==0) the offset in *pBuf from
                                which the content is within the border
                                (i.e. printable)                        */
  int          iMapped;     /* (iIsLinelist==0) 1:*pBuf came from mmap()
                                (needs munmap()) 0:came from malloc()
                                (needs free())                          */
  lineentry_t* pLineHead;   /* (iIsLinelist==1) the head of the list of
                                the lines within the border             */
} filerange_t;

/*--- prototype functions ------------------------------------------*/
int          parse_calendartime(char* pszTime, tmsp *ptsTime);
int          parse_unixtime(char* pszTime, tmsp *ptsTime);
int          parse_iso8601time(char* pszTime, tmsp *ptsTime);
int64_t      parse_periodictime(char *pszArg);
int          extract_timestamp_field(char *pszLine, size_t sizLine, tmsp *ptsTime);
int          is_within_border(tmsp *ptsTime);
int          mmap_regularfile(int iFd, char **ppMap, size_t *psizMap);
int          get_lastline_time(char *pBuf, size_t sizBuf, tmsp *ptsOut);
int          get_firstline_time(char *pBuf, size_t sizBuf, tmsp *ptsOut);
size_t       find_cutin_offset(char *pBuf, size_t sizBuf, int *piHit);
int          read_one_line(FILE *fp, char **ppLine, size_t *psizBuf, size_t *psizLine);
lineentry_t* scan_stream_lines(FILE *fp, int iZpending, int *piHit,
                                tmsp *ptsLastline);
void         write_all(const char *pBuf, size_t sizBuf);
void         print_header(const char *pszDispname, int iFirst);

/*--- global variables ---------------------------------------------*/
char* gpszCmdname; /* The name of this command                          */
int   giVerbose;   /* speaks more verbosely by the greater number       */
tmsp  gtsZero;      /* The time when this command started (also used as
                       the epoch for "-z" formatted fields)             */
int   giTfmt;      /* 0:"-c"(calendar) 1:"-e"(UNIX) 2:"-z"(elapsed)
                      3:"-I"(ISO 8601)                                  */
int   giTZoffs;    /* Offset in second of the local timezone from UTC,
                       used by parse_iso8601time() (only when "-I")     */
int   giEndp;      /* 1:Include the border endpoint 0:Exclude it ("-x") */
tmsp  gtsBorder;   /* The border time (finalized before it is used)     */

/*=== Define the functions for printing usage and error ============*/

/*--- exit with usage ----------------------------------------------*/
void print_usage_and_exit(void) {
  fprintf(stderr,
    "USAGE   : (a) %s [options] -d    duration      [file ...]\n"
    "          (b) %s [options] -xd   duration      [file ...]\n"
    "          (c) %s [options] -d   -duration      [file ...]\n"
    "          (d) %s [options] -xd  -duration      [file ...]\n"
    "          (e) %s [options] -t    date-and-time [file ...]\n"
    "          (f) %s [options] -xt   date-and-time [file ...]\n"
    "          (\"-x\" must not be fused after \"-d\"/\"-t\" in the same\n"
    "           token, e.g. \"-dx\" is NOT \"-d\"+\"-x\" but \"-d\" with the\n"
    "           argument \"x\". Write it as \"-xd\"/\"-xt\", or as two\n"
    "           separate options such as \"-d -x\" or \"-x -d\".)\n"
    "\n"
    "          The lines that can pass through this command will be chosen\n"
    "          by making sure the timestamp at the first field of each line\n"
    "          is in one of the following ranges.\n"
    "            (a) [ <command start time>-<duration>, <bottom> ]\n"
    "            (b) ( <command start time>-<duration>, <bottom> ]\n"
    "            (c) [ <1st line's time>  +<duration>,  <bottom> ]\n"
    "            (d) ( <1st line's time>  +<duration>,  <bottom> ]\n"
    "            (e) [ date-and-time,                   <bottom> ]\n"
    "            (f) ( date-and-time,                   <bottom> ]\n"
    "          When two or more files are given, they are treated as if\n"
    "          they were a single concatenated stream (like \"cat\"), so\n"
    "          <1st line's time> means the first line of the FIRST file,\n"
    "          and <bottom> means the last line of the LAST file.\n"
    "\n"
    "          NOTE: Since <bottom> is always the last line of the input,\n"
    "          this command cannot produce any output until it reaches\n"
    "          the end of the input (EOF), no matter which pattern is\n"
    "          used -- even pattern (a)/(b), whose border time itself is\n"
    "          already fixed the moment this command starts. Therefore,\n"
    "          this command is NOT suitable for an unbounded, never-\n"
    "          ending stream such as one from \"tail -f\".\n"
    "\n"
    "Args    : file ........ Filepath to be send (\"-\" means STDIN)\n"
    "                        The file MUST be a textfile and MUST have\n"
    "                        a timestamp at the first field to make the\n"
    "                        timing of flow. The first space character\n"
    "                        <0x20> of every line will be regarded as\n"
    "                        the field delimiter.\n"
    "Options : -c,-e,-I,-z  Specify the format for timestamp and -t option\n"
    "                        parameter. You can choose one of the following.\n"
    "                          -c ... \"YYYYMMDDhhmmss[.n]\" (default)\n"
    "                                 Calendar time (standard time) in your\n"
    "                                 timezone (\".n\" is the digits under\n"
    "                                 second. You can specify up to nano\n"
    "                                 second.)\n"
    "                          -e ... \"n[.n]\"\n"
    "                                 The number of seconds since the UNIX\n"
    "                                 epoch (\".n\" is the same as -c)\n"
    "                          -I ... \"YYYY-MM-DDThh:mm:ss[,n][{{+|-}hh:mm|Z}]\"\n"
    "                                 Ext. ISO 8601 formatted time in your\n"
    "                                 timezone (\".n\" is the same as -c)\n"
    "                          -z ... \"n[.n]\"\n"
    "                                 The number of seconds since this\n"
    "                                 command has startrd (\".n\" is the same\n"
    "                                 as -c)\n"
    "          -d duration . This is one of options to specify the timestamp\n"
    "                        range. (See the pattern (a) to (d) above)\n"
    "                        You can use the format \"A[.B][u]\" as the\n"
    "                        option's parameter \"duration.\"\n"
    "                          \"A\" is the integer part of the time.\n"
    "                          \"B\" is the decimal part of the time.\n"
    "                          \"u\" is the unit for the time. You can choose\n"
    "                              one of the followings.\n"
    "                              \"s\", \"ms\", \"us\", \"ns\", \"m\", \"h\"\n"
    "                              and \"d.\"\n"
    "          -t date-and-time\n"
    "                        This is one of options to specify the timestamp\n"
    "                        range. (See the pattern (e) and (f) above)\n"
    "                        The format of \"date-and-time\" depends on\n"
    "                        which of the option \"-c\", \"-e,\" \"-I,\" or\n"
    "                        \"-z\" you choose.\n"
    "                          \"-c\" ... \"YYYYMMDDhhmmss[.n]\" (cal. time)\n"
    "                          \"-e\" ... \"n[.n]\" (UNIX time)\n"
    "                          \"-I\" ... \"YYYY-MM-DDThh:mm:ss[,n][{{+|-}hh:mm|Z}]\"\n"
    "                                   (ext. ISO 8601 time)\n"
    "                          \"-z\" ... \"n[.n]\" (elapsed sec. since start)\n"
    "          -q .......... Suppresses printing filenames when two or more\n"
    "                        files are given.\n"
    "          -u .......... Set the date in UTC when -c option is set\n"
    "                        (same as that of date command)\n"
    "          -x .......... An additional option for -d and -t. It will\n"
    "                        exclude the border endpoint itself (the start\n"
    "                        of the range) from the output.\n"
    "                        (See the pattern (b), (d) and (f) above)\n"
    "          -Z .......... Define the time when the LAST line came as\n"
    "                        the origin, instead of the time this command\n"
    "                        started. This option is only meaningful for\n"
    "                        the pattern (a) and (b) (i.e. \"-d\" without a\n"
    "                        leading \"-\"). Without this option, the origin\n"
    "                        for pattern (a)/(b) is the moment this command\n"
    "                        started, which is not reproducible from run\n"
    "                        to run; with this option, the origin instead\n"
    "                        becomes the timestamp of the very last line\n"
    "                        of the input, which makes the result\n"
    "                        reproducible.\n"
    "Version : " MY_REV "\n"
    "          (POSIX C language)\n"
    "\n"
    "Shell-Shoccar Japan (@shellshoccarjpn), No rights reserved.\n"
    "This is public domain software. (CC0)\n"
    "\n"
    "The latest version is distributed at the following page.\n"
    "https://github.com/ShellShoccar-jpn/tokideli\n"
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
int          iMode;           /* 1:"-d"  2:"-t"  0:(undefined)               */
int          iLLzero;         /* 1:"-Z" (border origin is the LAST line's
                                  time, instead of the command start time;
                                  only meaningful for pattern (a)/(b))       */
int          iOriginIsFirst;  /* The sign of "-d"'s argument determines
                                  which pattern is used.
                                    1: the argument had a leading "-"
                                       -> pattern (c)/(d), origin is the
                                          1st line's time
                                    0: no leading "-"
                                       -> pattern (a)/(b), origin is the
                                          command start time
                                  NOTE: this is the OPPOSITE of tshead.c's
                                  "iFromtop" (which meant "no leading
                                  dash was given"). Here it means "a
                                  leading dash WAS given," i.e. exactly
                                  reversed in meaning.                       */
int          iPrnhdr;         /* 1:Print 2 or more filenames 0:none          */
int          iZpending;       /* 1:gtsBorder is not fixed yet ("-Z" pending) */
char         szOptbuf[OPT_PARM_BUF];
int64_t      i8Delta = 0;     /* delta-T in nanoseconds (defined by "-d")    */
int          iRet;            /* return code                                 */
int          i;               /* all-purpose int                             */
int          iNfiles;         /* the number of the files to be processed     */
char*        pszDashOnly[1];  /* used when no file is given (means stdin)    */
char**       ppszFiles;       /* the file list to be processed               */
tmsp         tsTmp;
int          iFd, iHit;
FILE*        fpFirstKept   = NULL; /* pattern (c)/(d): kept open across the
                                       border-determination phase and the
                                       main loop, when the 1st file turned
                                       out to be unseekable                  */
lineentry_t* pFirstNode    = NULL; /* pattern (c)/(d): the 1st line itself,
                                       already read while determining the
                                       border, kept as a list node to be
                                       prepended later if it is within the
                                       border                                */
char*        pMap;
size_t       sizMap;
filerange_t* astFiles;        /* printable ranges, accumulated in the
                                  order they were found (last file first)    */
int          iNincluded;      /* the number of entries actually stored in
                                  astFiles[]                                 */

/*--- Initialize ---------------------------------------------------*/
if (clock_gettime(CLOCK_REALTIME,&gtsZero) != 0) {
  error_exit(errno,"clock_gettime() at initialize: %s\n",strerror(errno));
}
gpszCmdname = argv[0];
for (i=0; *(gpszCmdname+i)!='\0'; i++) {
  if (*(gpszCmdname+i)=='/') {gpszCmdname=gpszCmdname+i+1;}
}
if (setenv("POSIXLY_CORRECT","1",1) < 0) {
  error_exit(errno,"setenv() at initialization: %s\n", strerror(errno));
}
setlocale(LC_CTYPE, "");

/*=== Parse arguments ==============================================*/

/*--- Set default parameters of the arguments ----------------------*/
giTfmt         = 0; /* 0:"-c"(default) 1:"-e" 2:"-z" 3:"-I" */
iMode          = 0; /* 1:duration(-d) 2:time(-t)     */
iLLzero        = 0; /* 0:The origin for (a)/(b) is the time the command
                        begins  1:...is the time the last line came     */
giEndp         = 1; /* 0:Exclude the time range endpoint
                        1:Include the time range endpoint (default)     */
iOriginIsFirst = 0; /* 0:"-d" had no leading "-" (pattern a/b)
                        1:"-d" had a leading "-"  (pattern c/d)         */
iPrnhdr        = 1; /* 1:Print 2 or more filenames 0:none               */
giVerbose      = 0;

/*--- Parse and validate options -----------------------------------*/
if (argc<2) {print_usage_and_exit();}
while ((i=getopt(argc, argv, "cd:ehIqt:uvxzZ")) != -1) {
  switch (i) {
    case 'u': (void)setenv("TZ", "UTC0", 1); break;
    case 'c': giTfmt   = 0;                  break;
    case 'e': giTfmt   = 1;                  break;
    case 'I': giTfmt   = 3;                  break;
    case 'z': giTfmt   = 2;                  break;
    case 'Z': iLLzero  = 1;                  break;
    case 'x': giEndp   = 0;                  break;
    case 'd': if (*optarg=='-') {iOriginIsFirst = 1; optarg++;}
              else              {iOriginIsFirst = 0;          }
              i8Delta = parse_periodictime(optarg);
              if (i8Delta<0) {print_usage_and_exit();}
              iMode = 1;
                                             break;
    case 't': iMode = 2;
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
if (giVerbose>0) {warning("verbose mode (level %d)\n",giVerbose);}

/*--- Calculate the timezone offset if the "-I" option is enabled --*/
if (giTfmt==3) {
  /* "giTZoffs" means "localtime - UTCtime" */
  giTZoffs = (int)difftime(mktime(localtime((time_t[]){0})),
                           mktime(   gmtime((time_t[]){0})) );
}

/*--- Build the file list (no file means stdin) ---------------------*/
if (argc == 0) {
  pszDashOnly[0] = "-";
  iNfiles        = 1;
  ppszFiles      = pszDashOnly;
} else {
  iNfiles        = argc;
  ppszFiles      = argv;
}
if (iNfiles < 2) {iPrnhdr = 0;}

/*=== Determine the border time (gtsBorder) ========================*/
iZpending      = 0;
switch (iMode) {
  case 1: /* "-d" */
          if (! iOriginIsFirst) {
            /* --- pattern (a)/(b): border = command start - duration -*/
            if (iLLzero) {
              iZpending = 1; /* fixed later, at the last file (the 1st
                                 iteration of the backward loop below) */
            } else {
              gtsBorder = gtsZero;
              tssub(gtsBorder, i8Delta);
              if (gtsBorder.tv_sec<0) {gtsBorder.tv_sec=0; gtsBorder.tv_nsec=0;}
            }
          } else {
            /* --- pattern (c)/(d): border = 1st line's time + duration */
            iFd = (strcmp(ppszFiles[0],"-")==0)
                  ? STDIN_FILENO : open(ppszFiles[0], O_RDONLY);
            if (iFd < 0) {
              error_exit(errno,"%s: %s\n",ppszFiles[0],strerror(errno));
            }
            if (mmap_regularfile(iFd, &pMap, &sizMap)) {
              if (! get_firstline_time(pMap, sizMap, &tsTmp)) {
                error_exit(1,"%s: Cannot determine the 1st line's time\n",
                           ppszFiles[0]);
              }
              munmap(pMap, sizMap);
              if (iFd != STDIN_FILENO) {close(iFd);}
              gtsBorder = tsTmp;
              tsadd(gtsBorder, i8Delta);
            } else {
              /* Unseekable: read only the 1st line now, and keep the
                 stream open so the main loop (when it reaches i==0)
                 can resume reading right where this left off, instead
                 of re-opening/re-reading the file from the top.       */
              char*  pFirstLine    = NULL;
              size_t sizFirstBuf   = 0, sizFirstLine;
              fpFirstKept = (iFd==STDIN_FILENO) ? stdin : fdopen(iFd,"r");
              if (! read_one_line(fpFirstKept, &pFirstLine, &sizFirstBuf,
                                   &sizFirstLine)) {
                error_exit(1,"%s: Cannot determine the 1st line's time\n",
                           ppszFiles[0]);
              }
              if (! extract_timestamp_field(pFirstLine, sizFirstLine, &tsTmp)) {
                error_exit(1,"%s: Cannot determine the 1st line's time\n",
                           ppszFiles[0]);
              }
              gtsBorder = tsTmp;
              tsadd(gtsBorder, i8Delta);
              /* Keep the 1st line as a list node only if it is itself
                 within the border (this only happens when duration==0,
                 in which case the border equals the 1st line's own
                 time); otherwise it is discarded.                     */
              if (is_within_border(&tsTmp)) {
                pFirstNode = (lineentry_t*)malloc(sizeof(lineentry_t));
                if (pFirstNode == NULL) {
                  error_exit(errno,"malloc() for the 1st line: %s\n",
                             strerror(errno));
                }
                pFirstNode->pszLine = pFirstLine;
                pFirstNode->sizLine = sizFirstLine;
                pFirstNode->tsTime  = tsTmp;
                pFirstNode->pNext   = NULL;
              } else {
                free(pFirstLine);
              }
            }
          }
          break;
  case 2: /* "-t" */
          switch (giTfmt) {
            case 0 : if (! parse_calendartime(szOptbuf, &gtsBorder)) {
                       error_exit(1,
                         "%s: Timestamp format is calendar time by "
                         "\"-c\" option, but the string for \"-t\" "
                         "is wrong. See usage.\n", szOptbuf);
                     }
                     break;
            case 1 : if (! parse_unixtime(szOptbuf, &gtsBorder)) {
                       error_exit(1,
                         "%s: Timestamp format is the number of seconds "
                         "by \"-e\" option, but the string for \"-t\" "
                         "is wrong. See usage.\n", szOptbuf);
                     }
                     break;
            case 3 : if (! parse_iso8601time(szOptbuf, &gtsBorder)) {
                       error_exit(1,
                         "%s: Timestamp format is ext. ISO 8601 time by "
                         "\"-I\" option, but the string for \"-t\" "
                         "is wrong. See usage.\n", szOptbuf);
                     }
                     break;
            default: /* "-z": elapsed seconds since the command started */
                     if (! parse_unixtime(szOptbuf, &tsTmp)) {
                       error_exit(1,
                         "%s: Timestamp format is the number of seconds "
                         "by \"-z\" option, but the string for \"-t\" "
                         "is wrong. See usage.\n", szOptbuf);
                     }
                     gtsBorder = gtsZero;
                     tsadd(gtsBorder, (int64_t)tsTmp.tv_sec*1000000000
                                      +(int64_t)tsTmp.tv_nsec           );
                     break;
          }
          break;
  default: error_exit(1,"Either \"-d\" or \"-t\" option is required\n");
}

/*=== Each file loop (backward: from the LAST file to the FIRST) ===
 * NOTE: The files are handled as if they were one concatenated stream.
 *       Since <bottom> is always the last line of the input, this
 *       command scans the files BACKWARD (starting from the last one)
 *       and, as soon as it finds the border, stops opening any more
 *       (earlier) files. Because the scan order (last-to-first) is the
 *       reverse of the required output order (first-to-last), the
 *       printable range of each file is accumulated into astFiles[]
 *       instead of being written out immediately; the actual output
 *       happens afterward, in a separate pass in the original order. */
astFiles = (filerange_t*)malloc(sizeof(filerange_t) * (size_t)iNfiles);
if (astFiles == NULL) {
  error_exit(errno,"malloc() for the file list: %s\n",strerror(errno));
}
iNincluded = 0;
iRet       = 0;

for (i=iNfiles-1; i>=0; i--) {
  char* pszPath = ppszFiles[i];
  char* pszDisp = (strcmp(pszPath,"-")==0) ? "standard input" : pszPath;
  int   iMapped;
  lineentry_t* pLineHead = NULL;

  if (fpFirstKept != NULL && i==0) {
    /*--- Pattern (c)/(d): resume the unseekable 1st file's stream, --*
     *    which is still open from the border-determination phase.  --*/
    { lineentry_t* pRest = scan_stream_lines(fpFirstKept, 0, &iHit, NULL);
      if (fpFirstKept != stdin) {fclose(fpFirstKept);}
      if (pFirstNode != NULL) {
        pFirstNode->pNext = pRest;
        pLineHead = pFirstNode;
        if (iHit == 0) {iHit = 1;} /* the 1st line itself was within the
                                       border, so the border was found in
                                       this file regardless of "pRest".  */
      } else {
        pLineHead = pRest;
      }
    }
    astFiles[iNincluded].pszDisp     = pszDisp;
    astFiles[iNincluded].iIsLinelist = 1;
    astFiles[iNincluded].pLineHead   = pLineHead;
    iNincluded++;
    if (iHit==2) {iRet=1;}
    if (iHit   ) {break;}
    continue;
  }

  iFd = (strcmp(pszPath,"-")==0) ? STDIN_FILENO : open(pszPath, O_RDONLY);
  if (iFd < 0) {
    warning("%s: %s\n",pszPath,strerror(errno));
    iRet = 1;
    continue;
  }

  if (mmap_regularfile(iFd, &pMap, &sizMap)) {
    /*--- Fast path: a regular file, mapped via mmap ------------------*/
    iMapped = 1;
    /* munmap() is deferred to the final output pass below; the file
       descriptor itself, however, is no longer needed once mmap() has
       succeeded, so it is safe to close it here.                      */
    if (iFd != STDIN_FILENO) {close(iFd);}

    if (i==iNfiles-1 && iZpending) {
      /* "-Z": fix the border using the LAST line of the LAST file     */
      if (! get_lastline_time(pMap, sizMap, &tsTmp)) {
        error_exit(1,"%s: Cannot read the last line to fix \"-Z\"\n",pszDisp);
      }
      gtsBorder = tsTmp;
      tssub(gtsBorder, i8Delta);
      if (gtsBorder.tv_sec < 0) {gtsBorder.tv_sec=0; gtsBorder.tv_nsec=0;}
      iZpending = 0;
    }

    { size_t sizCutin = find_cutin_offset(pMap, sizMap, &iHit);
      astFiles[iNincluded].pszDisp     = pszDisp;
      astFiles[iNincluded].iIsLinelist = 0;
      astFiles[iNincluded].pBuf        = pMap;
      astFiles[iNincluded].sizBuf      = sizMap;
      astFiles[iNincluded].sizCutin    = sizCutin;
      astFiles[iNincluded].iMapped     = iMapped;
      iNincluded++;
      if (iHit==2) {iRet=1;}
      if (iHit   ) {break;} /* the border (or an invalid line) was found;
                                files before this one are never opened.  */
    }
  } else {
    /*--- Slow path: a pipe/stdin/etc., scanned line by line ---------*/
    FILE* fp = (iFd==STDIN_FILENO) ? stdin : fdopen(iFd,"r");

    if (i==iNfiles-1 && iZpending) {
      /* "-Z": the border depends on the LAST line, which is not known
         until EOF; keep every line for now, and fix the border and
         trim the list afterward.                                     */
      tmsp tsLastline;
      pLineHead = scan_stream_lines(fp, 1, &iHit, &tsLastline);
      if (fp!=stdin) {fclose(fp);}
      if (pLineHead == NULL) {
        error_exit(1,"%s: Cannot read the last line to fix \"-Z\"\n",pszDisp);
      }
      gtsBorder = tsLastline;
      tssub(gtsBorder, i8Delta);
      if (gtsBorder.tv_sec < 0) {gtsBorder.tv_sec=0; gtsBorder.tv_nsec=0;}
      iZpending = 0;
      /* trim the lines that turned out to be earlier than the border */
      while (pLineHead != NULL && ! is_within_border(&pLineHead->tsTime)) {
        lineentry_t* pOld = pLineHead;
        pLineHead = pLineHead->pNext;
        free(pOld->pszLine); free(pOld);
      }
      iHit = 1; /* the border is within this (the last) file by definition */
    } else {
      pLineHead = scan_stream_lines(fp, 0, &iHit, NULL);
      if (fp!=stdin) {fclose(fp);}
    }

    astFiles[iNincluded].pszDisp     = pszDisp;
    astFiles[iNincluded].iIsLinelist = 1;
    astFiles[iNincluded].pLineHead   = pLineHead;
    iNincluded++;
    if (iHit==2) {iRet=1;}
    if (iHit   ) {break;}
  }
}

/*=== Output the accumulated ranges, in the original chronological
      order (first file to last, top to bottom within each) ========*/
for (i=iNincluded-1; i>=0; i--) {
  if (iPrnhdr) {print_header(astFiles[i].pszDisp, i==iNincluded-1);}
  if (astFiles[i].iIsLinelist) {
    lineentry_t* p = astFiles[i].pLineHead;
    while (p != NULL) {
      lineentry_t* pNext = p->pNext;
      write_all(p->pszLine, p->sizLine);
      free(p->pszLine);
      free(p);
      p = pNext;
    }
  } else {
    write_all(astFiles[i].pBuf + astFiles[i].sizCutin,
              astFiles[i].sizBuf - astFiles[i].sizCutin);
    if (astFiles[i].iMapped) {munmap(astFiles[i].pBuf, astFiles[i].sizBuf);}
    else                     {free(astFiles[i].pBuf);}
  }
}
free(astFiles);

/*=== Finish normally ================================================*/
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
int parse_calendartime(char* pszTime, tmsp *ptsTime) {

  /*--- Variables --------------------------------------------------*/
  char szDate[21], szNsec[10], szDate2[26];
  int  i, j, k;             /* +-- 0:(reading integer part)          */
  char c;                   /* +-- 1:finish reading without_decimals */
  int  iStatus = 0;  /* <--------- 2:to_be_started reading decimals  */
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
  if (ptsTime->tv_sec == (time_t)-1) {
    /* mktime() is only specified to return (time_t)-1 on failure; it
     * is NOT specified to leave errno unchanged on success (and in
     * practice it can be left set by unrelated internal work, such
     * as loading timezone data), so errno must not be used here.   */
    if (giVerbose>1) {warning("%s: Invalid calendar-time string\n",pszTime);}
    return 0;
  }
  ptsTime->tv_nsec = atol(szNsec);

  return 1;
}

/*=== Parse a UNIX-time ==============================================
 * [in]  pszTime : UNIX-time string (/[+-]?[0-9]{1,19}(\.[0-9]{1,9})?/)
 *                 It is also OK to be followed by a ' ' or '\t'.
 *       ptsTime : To be set the parsed time ("timespec" structure)
 * [ret] > 0 : success
 *       ==0 : error (failure to parse)                             */
int parse_unixtime(char* pszTime, tmsp *ptsTime) {

  /*--- Variables --------------------------------------------------*/
  char szSec[21], szNsec[10];
  int  i, j, k;              /* +-- 0:(reading integer part)          */
  char c;                    /* +-- 1:finish reading without_decimals */
  int  iStatus = 0;  /* <--------- 2:to_be_started reading decimals  */

  /*--- Separate pszTime into seconds and nanoseconds --------------*/
  i=0; j=19;
  if (pszTime[i]=='+' || pszTime[i]=='-') {szSec[i]=pszTime[i]; i++; j++;}
  for (   ; i<j; i++) {
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
  if ((iStatus==0) && (i==j)) {
    switch (pszTime[j]) {
      case '.' : szSec[j]=0; iStatus=2; i++; break;
      case  0  :
      case ' ' :
      case '\t': szSec[j]=0; iStatus=1;      break;
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
  if (ptsTime->tv_sec<0 && szSec[0]!='-') {
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
  memset(&tmDate, 0, sizeof(tmDate));
  if (! strptime(szDate, "%Y-%m-%dT%H:%M:%S", &tmDate)) {
    if (giVerbose>1) {
      warning("Unexpect error at strptime() #1 in parse_iso8601time()\n");
    }
    return 0;
  }
  ptsTime->tv_sec  = mktime(&tmDate);
  if (ptsTime->tv_sec == (time_t)-1) {
    /* see the comment on the same check in parse_calendartime()     */
    if (giVerbose>1) {warning("%s: Invalid ISO 8601 string\n", pszTime);}
    return 0;
  }
  ptsTime->tv_nsec = atol(szNsec);
  if (j!=0) {ptsTime->tv_sec = ptsTime->tv_sec + giTZoffs - iTZoffs;}

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


/*=== Extract and parse the 1st field (the timestamp) of a line =====
 * [in]  pszLine : the line (does NOT have to be 0x00-terminated)
 *       sizLine : the length of the line, in bytes
 *       ptsTime : To be set the parsed, absolutized time
 * [ret] > 0 : success
 *       ==0 : error (failure to parse)                             */
int extract_timestamp_field(char *pszLine, size_t sizLine, tmsp *ptsTime) {

  char   szField[FIELD_BUF];
  size_t i;
  tmsp   tsElapsed;

  for (i=0; i<sizLine && i<FIELD_BUF-1 &&
            pszLine[i]!=' ' && pszLine[i]!='\t'; i++) {
    szField[i] = pszLine[i];
  }
  szField[i] = '\0';

  switch (giTfmt) {
    case 0 : return parse_calendartime(szField, ptsTime);
    case 1 : return parse_unixtime(    szField, ptsTime);
    case 3 : return parse_iso8601time( szField, ptsTime);
    default: /* "-z": elapsed seconds since the command started */
             if (! parse_unixtime(szField, &tsElapsed)) {return 0;}
             *ptsTime = gtsZero;
             tsadd((*ptsTime), ( (int64_t)tsElapsed.tv_sec*1000000000
                                 +(int64_t)tsElapsed.tv_nsec        ));
             return 1;
  }
}

/*=== Judge whether a time is within the border or not ==============
 * [in]  ptsTime : the time to be judged
 * [ret] !=0 : within the border (the line can be printed)
 *       ==0 : out of the border                                    *
 * NOTE: The comparison direction here is the OPPOSITE of tshead.c's
 *       is_within_border(), because tstail's border is the LOWER end
 *       of the printable range (everything from the border onward,
 *       up to the last line), whereas tshead's border is the UPPER
 *       end (everything from the top up to the border).            */
int is_within_border(tmsp *ptsTime) {
  if (ptsTime->tv_sec  > gtsBorder.tv_sec ) {return 1;}
  if (ptsTime->tv_sec  < gtsBorder.tv_sec ) {return 0;}
  if (ptsTime->tv_nsec > gtsBorder.tv_nsec) {return 1;}
  if (ptsTime->tv_nsec < gtsBorder.tv_nsec) {return 0;}
  return giEndp; /* exactly the same time as the border */
}

/*=== Try to mmap() a file descriptor as a regular file ==============
 * [in]  iFd     : the file descriptor to be mapped
 *       ppMap   : *ppMap will be set to the mapped address
 *       psizMap : *psizMap will be set to the size of the mapped area
 * [ret] !=0 : success (the caller must munmap(*ppMap,*psizMap) later)
 *       ==0 : failure (not a regular file, empty, or mmap() failed) */
int mmap_regularfile(int iFd, char **ppMap, size_t *psizMap) {
  struct stat st;
  void*       p;

  if (fstat(iFd, &st) != 0    ) {return 0;}
  if (! S_ISREG(st.st_mode)   ) {return 0;}
  if (st.st_size <= 0         ) {return 0;}
  p = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, iFd, 0);
  if (p == MAP_FAILED         ) {return 0;}
  *ppMap   = (char*)p;
  *psizMap = (size_t)st.st_size;
  return 1;
}

/*=== Get the timestamp of the last line in a buffer =================
 * [in]  pBuf    : the buffer (a whole file mmap'ed)
 *       sizBuf  : the length of the buffer
 *       ptsOut  : To be set the parsed time of the last line
 * [ret] > 0 : success
 *       ==0 : error (empty buffer or failure to parse)              */
int get_lastline_time(char *pBuf, size_t sizBuf, tmsp *ptsOut) {
  size_t iEnd, iStart;

  if (sizBuf == 0) {return 0;}
  iEnd = sizBuf;
  if (pBuf[iEnd-1] == '\n') {iEnd--;}
  if (iEnd == 0            ) {return 0;}
  iStart = iEnd;
  while (iStart > 0 && pBuf[iStart-1] != '\n') {iStart--;}

  return extract_timestamp_field(pBuf+iStart, iEnd-iStart, ptsOut);
}

/*=== Get the timestamp of the first line in a buffer =================
 * [in]  pBuf    : the buffer (a whole file mmap'ed)
 *       sizBuf  : the length of the buffer
 *       ptsOut  : To be set the parsed time of the first line
 * [ret] > 0 : success
 *       ==0 : error (empty buffer or failure to parse)              */
int get_firstline_time(char *pBuf, size_t sizBuf, tmsp *ptsOut) {
  char*  pNl;
  size_t sizLine;

  if (sizBuf == 0) {return 0;}
  pNl     = memchr(pBuf, '\n', sizBuf);
  sizLine = pNl ? (size_t)(pNl-pBuf) : sizBuf;
  return extract_timestamp_field(pBuf, sizLine, ptsOut);
}

/*=== Find the offset from which the buffer can be printed as is =====
 * [in]  pBuf    : the buffer (a whole file mmap'ed)
 *       sizBuf  : the length of the buffer
 *       piHit   : *piHit is set to 0 if the whole buffer is out of the
 *                 border (i.e. entirely earlier than it; the caller
 *                 should keep looking at an even earlier file), 1 if
 *                 a line within the border was found (the whole
 *                 program should stop opening any earlier file after
 *                 this one), 2 if an invalid timestamp field was
 *                 found (ditto, but it should be treated as an error)
 * [ret]         : the offset (from the top of *pBuf) from which the
 *                 content is within the border, i.e. printable       */
size_t find_cutin_offset(char *pBuf, size_t sizBuf, int *piHit) {
  size_t iEnd, iLineStart, iCutin;
  tmsp   ts;

  *piHit = 0;
  iCutin = sizBuf;
  iEnd   = sizBuf;
  if (iEnd>0 && pBuf[iEnd-1]=='\n') {iEnd--;}

  while (iEnd > 0) {
    iLineStart = iEnd;
    while (iLineStart > 0 && pBuf[iLineStart-1] != '\n') {iLineStart--;}

    if (! extract_timestamp_field(pBuf+iLineStart, iEnd-iLineStart, &ts)) {
      warning("Invalid timestamp field, "
              "stop reading the rest of this input\n");
      *piHit = 2;
      return iCutin;
    }
    if (! is_within_border(&ts)) {*piHit = 1; return iCutin;}

    iCutin = iLineStart;
    if (iLineStart == 0) {break;}
    iEnd = iLineStart - 1;
  }
  return iCutin;
}

/*=== Read one line from a stream, growing the buffer if necessary ===
 * [in]  fp      : the stream to read from
 *       ppLine  : the (possibly NULL) buffer pointer to reuse/grow
 *       psizBuf : the current size of *ppLine
 *       psizLine: To be set the length of the line read (0 on EOF)
 * [ret] !=0 : a line (which may lack a trailing "\n" at EOF) was read
 *       ==0 : EOF, nothing was read                                 */
int read_one_line(FILE *fp, char **ppLine, size_t *psizBuf, size_t *psizLine) {
  size_t sizUsed = 0;
  int    iChar;

  if (*ppLine == NULL) {
    *psizBuf = 256;
    if ((*ppLine = (char*)malloc(*psizBuf)) == NULL) {
      error_exit(errno,"read_one_line() #1: %s\n",strerror(errno));
    }
  }
  while ((iChar = getc(fp)) != EOF) {
    if (sizUsed >= *psizBuf) {
      *psizBuf *= 2;
      if ((*ppLine = (char*)realloc(*ppLine, *psizBuf)) == NULL) {
        error_exit(errno,"read_one_line() #2: %s\n",strerror(errno));
      }
    }
    (*ppLine)[sizUsed] = (char)iChar; sizUsed++;
    if (iChar == '\n') {break;}
  }
  if (ferror(fp)) {error_exit(errno,"read_one_line(): %s\n",strerror(errno));}
  *psizLine = sizUsed;
  return (sizUsed>0) ? 1 : 0;
}

/*=== Scan an unseekable stream line by line, keeping only the lines
      that are within the border ====================================
 * [in]  fp         : the stream to read to its EOF
 *       iZpending  : 1 if the border is not fixed yet (only possible
 *                    for the LAST file, when "-Z" was given). While
 *                    this is set, every line is kept regardless of
 *                    the border, because the border itself cannot be
 *                    determined until the last line (i.e. EOF) is
 *                    known; the caller is responsible for trimming
 *                    the returned list afterward, once "gtsBorder"
 *                    has been fixed with the help of "*ptsLastline."
 * [out] piHit      : 0 = reached EOF without ever entering the border
 *                        (this whole file turned out to be entirely
 *                        earlier than the border; the caller should
 *                        keep looking at an even earlier file)
 *                    1 = entered the border, and (only if the
 *                        monotonicity of the timestamps is violated)
 *                        possibly left it again; the caller must not
 *                        open any earlier file after this one
 *                    2 = an invalid timestamp field was found
 *                    (not meaningful when iZpending was given; the
 *                    caller decides "piHit" by itself in that case)
 *       ptsLastline: if iZpending was given, set to the timestamp of
 *                    the very last line read (ignored otherwise)
 * [ret]             the head of the linked list of the lines that are
 *                    within the border (NULL if none). Ownership of
 *                    every node, and of the line buffer inside it, is
 *                    transferred to the caller.                      */
lineentry_t* scan_stream_lines(FILE *fp, int iZpending, int *piHit,
                                tmsp *ptsLastline) {

  /*--- Variables --------------------------------------------------*/
  lineentry_t *pHead=NULL, *pTail=NULL, *pNew;
  char*        pLine = NULL;
  size_t       sizBuf = 0, sizLine;
  tmsp         tsLine;
  int          iEnteredBorder = 0;

  /*--- Read the stream to its EOF, line by line --------------------*/
  *piHit = 0;
  while (read_one_line(fp, &pLine, &sizBuf, &sizLine)) {
    if (! extract_timestamp_field(pLine, sizLine, &tsLine)) {
      warning("Invalid timestamp field, "
              "stop reading the rest of this input\n");
      *piHit = 2;
      break;
    }
    if (iZpending) {
      /* the border is still unknown: keep everything for now */
      if (ptsLastline) {*ptsLastline = tsLine;}
    } else if (! is_within_border(&tsLine)) {
      if (iEnteredBorder) {
        /* was within the border, but not anymore: the monotonicity
           assumption is violated. Stop here, following the same
           policy as tshead.c's forward scan (a one-way cutoff).      */
        *piHit = 1;
        break;
      }
      continue; /* not yet reached the border: discard this line */
    } else {
      iEnteredBorder = 1;
    }

    /*--- Keep this line: move it into a new list node --------------*/
    pNew = (lineentry_t*)malloc(sizeof(lineentry_t));
    if (pNew == NULL) {
      error_exit(errno,"malloc() for a line: %s\n",strerror(errno));
    }
    pNew->pszLine = pLine; pNew->sizLine = sizLine; pNew->tsTime = tsLine;
    pNew->pNext   = NULL;
    if (pTail) {pTail->pNext = pNew;} else {pHead = pNew;}
    pTail = pNew;
    pLine = NULL; sizBuf = 0; /* ownership moved; read_one_line() will
                                 allocate a fresh buffer next time     */
  }
  free(pLine);

  if (*piHit != 2 && ! iZpending) {*piHit = iEnteredBorder ? 1 : 0;}
  return pHead;
}

/*=== Write a buffer to stdout, retrying on short writes =============
 * [in]  pBuf, sizBuf : the buffer and its length
 * [ret] (none, exits the program on an unrecoverable write error)    */
void write_all(const char *pBuf, size_t sizBuf) {
  size_t  sizDone = 0;
  ssize_t ssRes;

  while (sizDone < sizBuf) {
    ssRes = write(STDOUT_FILENO, pBuf+sizDone, sizBuf-sizDone);
    if (ssRes < 0) {
      if (errno==EINTR) {continue;}
      error_exit(errno,"write(): %s\n",strerror(errno));
    }
    sizDone += (size_t)ssRes;
  }
}

/*=== Print the GNU-head-style filename header =======================
 * [in]  pszDispname : the name to be printed
 *       iFirst      : 1 if this is the first file (no blank line before)
 * [ret] (none)                                                       */
void print_header(const char *pszDispname, int iFirst) {
  char szBuf[PATH_MAX+32];
  int  iLen;

  iLen = snprintf(szBuf, sizeof(szBuf), "%s==> %s <==\n",
                   iFirst?"":"\n", pszDispname);
  if (iLen < 0) {error_exit(1,"print_header(): snprintf() failed\n");}
  if ((size_t)iLen >= sizeof(szBuf)) {iLen = sizeof(szBuf)-1;}
  write_all(szBuf, (size_t)iLen);
}
