/*####################################################################
#
# RELVAL - Limit the Flow Rate of the UNIX Pipeline Like a Relief Valve
#
# USAGE   : relval [-c|-e|-z] [-u] [-k] [-d fd|file] ratelimit [file [...]]
# Args    : file ........ Filepath to be sent ("-" means STDIN)
#                         The file MUST be a textfile and MUST have
#                         a timestamp at the first field to make the
#                         timing of flow. The first space character
#                         <0x20> of every line will be regarded as
#                         the field delimiter.
#                         And, the string from the top of the line to
#                         the charater will be cut before outgoing to
#                         the stdout.
#           ratelimit ... Dataflow limit. You can specify it by the following
#                         two methods.
#                           1. interval time
#                              * One line will be allowed to pass through
#                                in the time you specified.
#                              * The usage is "time[unit]."
#                                - "time" is the numerical part. You can
#                                  use an integer or a decimal.
#                                - "unit" is the part of the unit of time.
#                                  You can choose one of "s," "ms," "us,"
#                                  or "ns." The default is "s."
#                              * If you set "1.24ms," this command allows
#                                up to one line of the source textdata
#                                to pass through every 1.24 milliseconds.
#                           2. number per time
#                              * Text data of a specified number of lines
#                                are allowed to pass through in a specified
#                                time.
#                              * The usage is "number/time."
#                                - "number" is the part to specify the
#                                  numner of lines. You can set only a
#                                  natural number from 1 to 65535.
#                                - "/" is the delimiter to seperate parts.
#                                  You must insert any whitespace characters
#                                  before and after this slash letter.
#                                - "time" is the part that specifies the
#                                  period. The usage is the same as
#                                  the interval time we explained above.
#                              * If you set "10/1.5," this command allows
#                                up to 10 lines to pass through every 1.5
#                                seconds.
# Options : -c,-e,-z .... Specify the format for timestamp. You can choose
#                         one of them.
#                           -c ... "YYYYMMDDhhmmss[.n]" (default)
#                                  Calendar time (standard time) in your
#                                  timezone (".n" is the digits under
#                                  second. You can specify up to nano
#                                  second.)
#                           -e ... "n[.n]"
#                                  The number of seconds since the UNIX
#                                  epoch (".n" is the same as -x)
#                           -z ... "n[.n]"
#                                  The number of seconds since this
#                                  command has startrd (".n" is the same
#                                  as -x)
#           -u .......... Set the date in UTC when -c option is set
#                         (same as that of date command)
#           -k .......... Keep the timestamp when outputting each line.\n
#           -d fd/file .. If you set this option, the lines that will be
#                         dropped will be sent to the specified file
#                         descriptor or file.
#                         * When you set an integer, this command regards
#                           it as a file descriptor number. If you want
#                           to specify the file in the current directory
#                           that has a numerical filename, you have to
#                           add "./" before the name, like "./3."
#                         * When you set another type of string, this
#                           command regards it as a filename.
#
# Return  : Return 0 only when finished successfully
#
# How to compile : cc -O3 -o __CMDNAME__ __SRCNAME__ -lrt
#                  (if it doesn't work)
# How to compile : cc -O3 -o __CMDNAME__ __SRCNAME__
#
# Written by Shell-Shoccar Japan (@shellshoccarjpn) on 2024-06-19
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
#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>


/*--- headers ------------------------------------------------------*/
/* Buffer size for the control file */
#define CTRL_FILE_BUF 64
#define BILLION 1000000000

/*--- data type definitions ----------------------------------------*/
typedef struct timespec tmsp;

/*--- prototype functions ------------------------------------------*/
int     erase_stale_items_in_the_ring_buffer(
          int iBufsize, tmsp* ptsBuf, int iLast, tmsp tsRef);
int     parse_calendartime(char* pszTime, tmsp *ptsTime);
int     parse_unixtime(char* pszTime, tmsp *ptsTime);
int     read_1st_field_as_a_timestamp(FILE *fp, char *pszTime);
int     read_and_drain_a_line(FILE *fp, FILE *fpDrain);
int     read_and_write_a_line(FILE *fp);
int     skip_over_a_line(FILE *fp);
int64_t parse_duration(char* pszDuration);
tmsp*   generate_ring_buf(int iSize);
void    release_ring_buf(tmsp* pts);

/*--- global variables ---------------------------------------------*/
int     giVerbose;   /* speaks more verbosely by the greater number */
char*   gpszCmdname; /* The name of this command                    */


/*=== Define the functions for printing usage and error ============*/

/*--- exit with usage ----------------------------------------------*/
void print_usage_and_exit(void) {
  fprintf(stderr,
    " USAGE   : %s [-c|-e|-z] [-u] [-k] [-d fd|file] ratelimit [file [...]]\n"
    " Args    : file ........ Filepath to be sent (\"-\" means STDIN)\n"
    "                         The file MUST be a textfile and MUST have\n"
    "                         a timestamp at the first field to make the\n"
    "                         timing of flow. The first space character\n"
    "                         <0x20> of every line will be regarded as\n"
    "                         the field delimiter.\n"
    "                         And, the string from the top of the line to\n"
    "                         the charater will be cut before outgoing to\n"
    "                         the stdout.\n"
    "           ratelimit ... Dataflow limit. You can specify it by the following\n"
    "                         two methods.\n"
    "                           1. interval time\n"
    "                              * One line will be allowed to pass through\n"
    "                                in the time you specified.\n"
    "                              * The usage is \"time[unit].\"\n"
    "                                - \"time\" is the numerical part. You can\n"
    "                                  use an integer or a decimal.\n"
    "                                - \"unit\" is the part of the unit of time.\n"
    "                                  You can choose one of \"s,\" \"ms,\" \"us,\"\n"
    "                                  or \"ns.\" The default is \"s.\"\n"
    "                              * If you set \"1.24ms,\" this command allows\n"
    "                                up to one line of the source textdata\n"
    "                                to pass through every 1.24 milliseconds.\n"
    "                           2. number per time\n"
    "                              * Text data of a specified number of lines\n"
    "                                are allowed to pass through in a specified\n"
    "                                time.\n"
    "                              * The usage is \"number/time.\"\n"
    "                                - \"number\" is the part to specify the\n"
    "                                  numner of lines. You can set only a\n"
    "                                  natural number from 1 to 65535.\n"
    "                                - \"/\" is the delimiter to seperate parts.\n"
    "                                  You must insert any whitespace characters\n"
    "                                  before and after this slash letter.\n"
    "                                - \"time\" is the part that specifies the\n"
    "                                  period. The usage is the same as\n"
    "                                  the interval time we explained above.\n"
    "                              * If you set \"10/1.5,\" this command allows\n"
    "                                up to 10 lines to pass through every 1.5\n"
    "                                seconds.\n"
    " Options : -c,-e,-z .... Specify the format for timestamp. You can choose\n"
    "                         one of them.\n"
    "                           -c ... \"YYYYMMDDhhmmss[.n]\" (default)\n"
    "                                  Calendar time (standard time) in your\n"
    "                                  timezone (\".n\" is the digits under\n"
    "                                  second. You can specify up to nano\n"
    "                                  second.)\n"
    "                           -e ... \"n[.n]\"\n"
    "                                  The number of seconds since the UNIX\n"
    "                                  epoch (\".n\" is the same as -x)\n"
    "                           -z ... \"n[.n]\"\n"
    "                                  The number of seconds since this\n"
    "                                  command has startrd (\".n\" is the same\n"
    "                                  as -x)\n"
    "           -u .......... Set the date in UTC when -c option is set\n"
    "                         (same as that of date command)\n"
    "           -k .......... Keep the timestamp when outputting each line.\n"
    "           -d fd/file .. If you set this option, the lines that will be\n"
    "                         dropped will be sent to the specified file\n"
    "                         descriptor or file.\n"
    "                         * When you set an integer, this command regards\n"
    "                           it as a file descriptor number. If you want\n"
    "                           to specify the file in the current directory\n"
    "                           that has a numerical filename, you have to\n"
    "                           add \"./\" before the name, like \"./3.\"\n"
    "                         * When you set another type of string, this\n"
    "                           command regards it as a filename.\n"
    "\n"
    "Version : 2024-06-19 10:50:00 JST\n"
    "          (POSIX C language)\n"
    "\n"
    "USP-NCNT prj. / Shell-Shoccar Japan (@shellshoccarjpn),\n"
    "No rights reserved. This is public domain software. (CC0)\n"
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
int      i;               /* all-purpose int                        */
int      iDrainFd;        /* File Descriptor for Drain              */
int      iFd;             /* file descriptor                        */
int      iFileno;         /* file# of filepath                      */
int      iLastitemCode;   /* last memorized item in the buffer      */
int      iMaxlines;       /* the max number of lines allowed        */
int      iMode;           /* 0:"-c" 1:"-e" 2:"-z"                   */
int      iNumVacantBuf;   /* Number of vacant items in the buffer   */
int      iRet;            /* return code                            */
int      iUnstamped;      /* 0:"-k" 1:default stop outputting time  */
int64_t  i8Duration;      /* the time to reset iMaxlines            */
char     szTime[34];      /* Buffer for the 1st field of lines      */
char*    psz;             /* all-purpose char pointer               */
char*    pszDrainname;    /* Filename for Drain                     */
char*    pszFilename;     /* filepath (for message)                 */
char*    pszPath;         /* filepath on arguments                  */
FILE*    fp;              /* file handle                            */
FILE*    fpDrain;         /* file handle for the drain              */
tmsp     tsTime;          /* Parsed time for the 1st field          */
tmsp     tsRef;           /* Ref-time to erase the stale items      */
tmsp*    ptsRingBuf;      /* To memorize timestamp of passed lines  */

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
iMode        = 0; /* 0:"-c"(default) 1:"-e" 2:"-z" */
giVerbose    = 0;
i8Duration   = BILLION;
iDrainFd     = -1;
iMaxlines    = 1;
iUnstamped   = 1;
pszDrainname = NULL;

/*--- Parse options which start with "-" ---------------------------*/
while((i=getopt(argc, argv, "cezukd:hv")) != -1) {
  switch(i){
    case 'c': iMode=0;                       break;
    case 'e': iMode=1;                       break;
    case 'z': iMode=2;                       break;
    case 'u': (void)setenv("TZ", "UTC0", 1); break;
    case 'k': iUnstamped=0;                  break;
    case 'd': if (sscanf(optarg,"%d",&iDrainFd) != 1) {iDrainFd=-1;}
              if (iDrainFd>=0) {pszDrainname=NULL;} else {pszDrainname=optarg;}
              break;
    case 'v': giVerbose++; break;
    case 'h': print_usage_and_exit();
    default : print_usage_and_exit();
  }
}
if (giVerbose>0) {warning("verbose mode (level %d)\n",giVerbose);}
argc -= optind;
argv += optind;

/*--- Parse the ratelimit argument ---------------------------------*/
if (argc < 1){print_usage_and_exit();}
if ((psz = strchr(argv[0],'/')) != NULL){
  if (sscanf(argv[0],"%d",&iMaxlines) != 1){print_usage_and_exit();}
  psz++;
}else{
  psz = argv[0];
}
i8Duration = parse_duration(psz);
if (iMaxlines < 1 || 65535 < iMaxlines){print_usage_and_exit();} 
if (i8Duration <= -2)                  {print_usage_and_exit();}
argc--;
argv++;

/*=== Pre-Operation of the each file loop ==========================*/

/*--- Open the drain file if specified -----------------------------*/
if (pszDrainname != NULL) {
  while ((iDrainFd=open(pszDrainname,O_WRONLY|O_CREAT,0644))<0) {
    if (errno == EINTR) {continue;}
    error_exit(errno, "%s: %s\n", pszDrainname   ,
                                  strerror(errno) );
  };                       fpDrain = fdopen(iDrainFd, "w");  }
else if (iDrainFd != -1 ) {fpDrain = fdopen(iDrainFd, "w");  }
else                      {fpDrain = NULL;                   }
if (fpDrain) {
  if (setvbuf(fpDrain,NULL,_IOLBF,0)!=0) {
    error_exit(255,"Failed to switch to line-buffered mode (drain)\n");
  }
}

/*--- generate a ring buffer ----------------------------------------*/
if ((ptsRingBuf=generate_ring_buf(iMaxlines)) == NULL) {
  error_exit(errno, "%s: %s\n", pszDrainname, strerror(errno));
}

/*=== Each file loop ===============================================*/
iRet          =  0;
iFileno       =  0;
iFd           = -1;
iLastitemCode =  0;
while (argc > 0 || iFileno == 0) {
  /*--- Read the filepath ------------------------------------------*/
  if (argc == 0) {pszPath="-";          }
  else           {pszPath=argv[iFileno];}
  argc--;
  /*--- Open one of the input files --------------------------------*/
  if (strcmp(pszPath, "-") == 0) {
    pszFilename = "stdin"                ;
    iFd         = STDIN_FILENO           ;
  } else                         {
    pszFilename = pszPath                ;
    while ((iFd=open(pszPath, O_RDONLY)) < 0) {
      if (errno == EINTR) {continue;}
      iRet = 1;
      warning("%s: %s\n",pszFilename,strerror(errno));
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
  iFileno++;

  /*--- relief valve -----------------------------------------------*/
  switch(iMode){
    case 0: /* "-c" Calendar time mode */
            while(1){
              switch(read_1st_field_as_a_timestamp(fp,szTime)){
                 case  1: /* read successfully */
                          if (! parse_calendartime(szTime, &tsTime)) {
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
                          tsRef.tv_nsec = tsTime.tv_nsec
                                          - (long)i8Duration%BILLION;
                          if (tsRef.tv_nsec < 0) {
                            tsRef.tv_nsec += BILLION;
                            tsRef.tv_sec   = tsTime.tv_sec - 1
                                             - (time_t)i8Duration/BILLION;
                          } else {
                            tsRef.tv_sec = tsTime.tv_sec
                                           - (time_t)i8Duration/BILLION;
                          }

                          iNumVacantBuf = erase_stale_items_in_the_ring_buffer(
                                            iMaxlines, ptsRingBuf,
                                            iLastitemCode, tsRef);
                          if (iNumVacantBuf < 0) {
                            error_exit(1,"Unexpected error at %d\n", __LINE__);
                          }
                          if (iNumVacantBuf == 0) {
                            if (fpDrain == NULL) {
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
                                         error_exit(1,
                                                    "Unexpected error at %d\n",
                                                    __LINE__);
                                         break;
                              }
                              break;
                            } else {
                              if (iUnstamped == 0) {
                                if (fputs(szTime, fpDrain) == EOF) {
                                  error_exit(1, "Access error for the drain\n");
                                }
                              }
                              switch (read_and_drain_a_line(fp,fpDrain)) {
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
                                         error_exit(1,
                                                    "Unexpected error at %d\n",
                                                    __LINE__);
                                         break;
                              }
                              break;
                            }
                            break;
                          }
                          iLastitemCode = (iLastitemCode+1) % iMaxlines;
                          ptsRingBuf[iLastitemCode].tv_sec  = tsTime.tv_sec;
                          ptsRingBuf[iLastitemCode].tv_nsec = tsTime.tv_nsec;
                          if (iUnstamped == 0) {
                            if (fputs(szTime, stdout) == EOF) {
                              error_exit(1, "Access error for the stdout\n");
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
                                     error_exit(1,
                                                "Unexpected error at %d\n",
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
    case 1: /* "-e" Unix time mode */
    case 2: /* "-z" Zero time mode */
            while(1){
              switch(read_1st_field_as_a_timestamp(fp,szTime)){
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
                          tsRef.tv_nsec = tsTime.tv_nsec
                                          - (long)i8Duration%BILLION;
                          if (tsRef.tv_nsec < 0) {
                            tsRef.tv_nsec += BILLION;
                            tsRef.tv_sec   = tsTime.tv_sec - 1
                                             - (time_t)i8Duration/BILLION;
                          } else {
                            tsRef.tv_sec = tsTime.tv_sec
                                           - (time_t)i8Duration/BILLION;
                          }

                          iNumVacantBuf = erase_stale_items_in_the_ring_buffer(
                                            iMaxlines, ptsRingBuf,
                                            iLastitemCode, tsRef);
                          if (iNumVacantBuf < 0) {
                            error_exit(1,"Unexpected error at %d\n", __LINE__);
                          }
                          if (iNumVacantBuf == 0) {
                            if (fpDrain == NULL) {
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
                                         error_exit(1,
                                                    "Unexpected error at %d\n",
                                                    __LINE__);
                                         break;
                              }
                              break;
                            } else {
                              if (iUnstamped == 0) {
                                if (fputs(szTime, fpDrain) == EOF) {
                                  error_exit(1, "Access error for the drain\n");
                                }
                              }
                              switch (read_and_drain_a_line(fp,fpDrain)) {
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
                                         error_exit(1,
                                                    "Unexpected error at %d\n",
                                                    __LINE__);
                                         break;
                              }
                              break;
                            }
                            break;
                          }
                          iLastitemCode = (iLastitemCode+1) % iMaxlines;
                          ptsRingBuf[iLastitemCode].tv_sec  = tsTime.tv_sec;
                          ptsRingBuf[iLastitemCode].tv_nsec = tsTime.tv_nsec;
                          if (iUnstamped == 0) {
                            if (fputs(szTime, stdout) == EOF) {
                              error_exit(1, "Access error for the stdout\n");
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
                                     error_exit(1,
                                                "Unexpected error at %d\n",
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
    default:
            error_exit(1,"Undefined mode\n");
  }

CLOSE_THISFILE:
  /*--- Close the input file ---------------------------------------*/
  if (fp != stdin) {fclose(fp);}

  /*--- End loop ---------------------------------------------------*/
}
/*=== Finish normally ==============================================*/
return(iRet);}

/*####################################################################
# Functions
####################################################################*/


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
    if      (('0'<=c) && (c<='9')     ) {szDate[i]=c;                       }
    else if (c=='.'                   ) {szDate[i]=0; iStatus=2; i++; break;}
    else if (c==0 || c==' ' || c=='\t') {szDate[i]=0; iStatus=1;      break;}
    else                                {if (giVerbose>0) {
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
               if      (('0'<=c) && (c<='9')     ) {szNsec[k]=c; k++;}
               else if (c==0 || c==' ' || c=='\t') {break;           }
               else                                {
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
  szDate2[k] = 0;
  memset(&tmDate, 0, sizeof(tmDate));
  if(! strptime(szDate2, "%Y-%m-%dT%H:%M:%S", &tmDate)) {return 0;}
  ptsTime->tv_sec = mktime(&tmDate);
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
    if      (('0'<=c) && (c<='9')     ) {szSec[i]=c;                       }
    else if (c=='.'                   ) {szSec[i]=0; iStatus=2; i++; break;}
    else if (c==0 || c==' ' || c=='\t') {szSec[i]=0; iStatus=1;      break;}
    else                                {if (giVerbose>0) {
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
               if      (('0'<=c) && (c<='9')     ) {szNsec[k]=c; k++;}
               else if (c==0 || c==' ' || c=='\t') {break;           }
               else                                {
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


/*=== Read and write only one line having a timestamp ================
 * [in] fp      : Filehandle for read
 *      pszTime : Pointer for the string buffer to get the timestamp on
 *                the 1st field with a field separator
 *                (Size of the buffer you give MUST BE 34 BYTES or more!)
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
                 pszTime[iTslen  ] = iChar;
                 pszTime[iTslen+1] = 0;
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
                 if (iTslen>32) {                                 continue;}
                 else           {pszTime[iTslen]=iChar; iTslen++; continue;}
    }
  }
}


/*=== Read and write only one line to the drain ======================
 * [in] fp      : Filehandle for read
 *      fpDrain : Filehandle for drain
 * [ret] == 1   : Finished reading/writing due to '\n', which is the last
 *                char of the file
 *       ==-1   : Finished reading due to the end of file
 *       ==-2   : Finished reading due to a file reading error
 *       ==-3   : Finished reading due to a system error            */
int read_and_drain_a_line(FILE *fp, FILE *fpDrain) {

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
                 if (putc('\n', fpDrain)==EOF) {
                   error_exit(errno,"write error #1: %s\n",
                              strerror(errno));
                 }
                 return 1;
      default  :
                 if (putc(iChar, fpDrain)==EOF) {
                   error_exit(errno,"write error #2: %s\n",
                              strerror(errno));
                 }
                 break;
    }
  }
  return -3;
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
  return -3;
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

/*=== Parse the duration =============================================
 * [in] pszDuration : The string to be parsed as a duration
 * [ret] >= 0  : Interval value (in nanosecound)
 *       <=-1  : Means infinity (completely shut the valve)
 *       <=-2  : It is not a value                                  */
int64_t parse_duration(char *pszDuration) {

  /*--- Variables --------------------------------------------------*/
  char   szUnit[CTRL_FILE_BUF];
  double dNum;

  /*--- Check the lengths of the argument --------------------------*/
  if (strlen(pszDuration)>=CTRL_FILE_BUF) {return -2;}

  /*--- Try to interpret the argument as "<value>"[+"unit"] --------*/
  switch (sscanf(pszDuration, "%lf%s", &dNum, szUnit)) {
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

/*=== Allocate memory for a ring buffer ==============================
 * [in]  iSize : The size of the ring buffer
 * [ret] Pointer for the ring buffer when it succeeded in memory
         allocation. If it failed, it returns NULL.                 */
tmsp* generate_ring_buf(int iSize) {
  
  /*--- Variables --------------------------------------------------*/
  tmsp* ptsRet;
  int   i;

  /*--- Allocate memory with malloc --------------------------------*/
  ptsRet = (tmsp*) malloc((sizeof(tmsp)) * iSize);
  if (ptsRet == NULL) { return NULL; }

  /*--- Initialize the ring buffer ---------------------------------*/
  for (i=0;i<iSize;i++) { ptsRet[i].tv_nsec=-1; }

  /*--- Return the pointer of the buffer ---------------------------*/
  return ptsRet;
}

/*=== Free memory of the ring buffer =================================
 * [in] pts : The pointer to be released                            */
void release_ring_buf(tmsp* pts) {
  if (pts==NULL) { return; }
  free(pts);
  return;
}

/*=== Erase items that are deemed stale ==============================
 * [in]  iBufsize : The size of the ring buffer
         ptsBuf   : The pointer of the ring buffer
         iLast    : The last written item number 
         tsRef    : The reference time to erase the stale items
 * [ret] the number of vacancies (Success)
         A negative number       (Failure)                          */
int erase_stale_items_in_the_ring_buffer(
          int iBufsize, tmsp* ptsBuf, int iLast, tmsp tsRef) {
  
  /*--- Variables --------------------------------------------------*/
  int i;
  int iNum;
  int iVacancies = 0;

  /*--- Validate the arguments -------------------------------------*/
  if (iBufsize <= 0              ) { return -1; }
  if (ptsBuf   == NULL           ) { return -1; }
  if (iLast<0  || iLast>=iBufsize) { return -1; }

  /*--- Erase items that are deemed stale --------------------------*/
  for (i=1;i<=iBufsize;i++) {
    iNum = (iLast+i) % iBufsize;
    if (ptsBuf[iNum].tv_nsec < 0) { iVacancies++; continue; }
    if (ptsBuf[iNum].tv_sec < tsRef.tv_sec) {
      ptsBuf[iNum].tv_nsec=-1; iVacancies++; continue;
    }
    if (ptsBuf[iNum].tv_sec  == tsRef.tv_sec &&
        ptsBuf[iNum].tv_nsec <= tsRef.tv_nsec  ) {
      ptsBuf[iNum].tv_nsec=-1; iVacancies++; continue;
    }
    break;
  }

  /*--- Return the number of vacant items --------------------------*/
  return iVacancies;
}
