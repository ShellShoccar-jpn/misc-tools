/*####################################################################
#
# TSCAT - A "cat" Command Which Can Reprodude the Timing of Flow
#
# USAGE   : tscat [-c|-e|-z] [-Z] [-p n] [file ...]
# Args    : file ........ Filepath to be send ("-" means STDIN)
#                         The file MUST be a textfile and MUST have
#                         a timestamp at the first field to make the
#                         timing of flow. The first space character
#                         <0x20> of every line will be regarded as
#                         the field delimiter.
#                         And, the string from the top of the line to
#                         the charater will be cut before outgoing to
#                         the stdout.
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
#           -Z .......... Define the time when the first line came as 0.
#                         For instance, imagine that the first field of
#                         the first line is "20200229235959," and the
#                         second line's one is "20200301000004." when
#                         "-c" option is given. In this case, the first
#                         line is sent to stdout immediately, and after
#                         five seconds, the second line is sent.
#           [The following option is for professional]
#           -p n ........ Process priority setting [0-3] (if possible)
#                          0: Normal process
#                          1: Weakest realtime process (default)
#                          2: Strongest realtime process for generic users
#                             (for only Linux, equivalent 1 for otheres)
#                          3: Strongest realtime process of this host
#                         Larger numbers maybe require a privileged user,
#                         but if failed, it will try the smaller numbers.
# Retuen  : Return 0 only when finished successfully
#
# How to compile : cc -O3 -o __CMDNAME__ __SRCNAME__ -lrt
#                  (if it doesn't work)
# How to compile : cc -O3 -o __CMDNAME__ __SRCNAME__
#
# Written by Shell-Shoccar Japan (@shellshoccarjpn) on 2020-03-19
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

/*--- prototype functions ------------------------------------------*/
void get_time_data_arrived(int iFd, struct timespec *ptsTime);
int  read_1st_field_as_a_timestamp(FILE *fp, char *pszTime);
int  read_and_write_a_line(FILE *fp);
int  parse_calendartime(char* pszTime, struct timespec *ptsTime);
int  parse_unixtime(char* pszTime, struct timespec *ptsTime);
void spend_my_spare_time(struct timespec *ptsTo, struct timespec *ptsOffset);
int  change_to_rtprocess(int iPrio);

/*--- global variables ---------------------------------------------*/
char*           gpszCmdname; /* The name of this command                    */
int             giVerbose;   /* speaks more verbosely by the greater number */
struct timespec gtsZero;     /* The zero-point time                         */

/*=== Define the functions for printing usage and error ============*/

/*--- exit with usage ----------------------------------------------*/
void print_usage_and_exit(void) {
  fprintf(stderr,
#if defined(_POSIX_PRIORITY_SCHEDULING) && !defined(__OpenBSD__) && !defined(__APPLE__)
    "USAGE   : %s [-c|-e|-z] [-Z] [-p n] [file ...]\n"
#else
    "USAGE   : %s [-c|-e|-z] [-Z] [file ...]\n"
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
    "Options : -c,-e,-z .... Specify the format for timestamp. You can choose\n"
    "                        one of them.\n"
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
    "                                 command has started (\".n\" is the same\n"
    "                                 as -c)\n"
    "          -Z .......... Define the time when the first line came as 0.\n"
    "                        For instance, imagine that the first field of\n"
    "                        the first line is \"20200229235959,\" and the\n"
    "                        second line's one is \"20200301000004.\" when\n"
    "                        \"-c\" option is given. In this case, the first\n"
    "                        line is sent to stdout immediately, and after\n"
    "                        five seconds, the second line is sent.\n"
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
    "Version : 2020-03-19 12:18:14 JST\n"
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

/*--- print warning message ----------------------------------------*/
void warning(const char* szFormat, ...) {
  va_list va;
  va_start(va, szFormat);
  fprintf(stderr,"%s: ",gpszCmdname);
  vfprintf(stderr,szFormat,va);
  va_end(va);
  return;
}

/*--- exit with error message --------------------------------------*/
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

/*=== Initialization ===============================================*/
int main(int argc, char *argv[]) {

/*--- Variables ----------------------------------------------------*/
int      iMode;           /* 0:"-c"  1:"-e"  2:"-z",
                             4:"-cZ" 5:"-eZ" 6:"-zZ"                     */
int      iPrio;           /* -p option number (default 1)                */
int      iRet;            /* return code                                 */
int      iGotOffset;      /* 0:NotYet 1:GetZeroPoint 2:Done              */
char     szTime[33];      /* Buffer for the 1st field of lines           */
struct timespec tsTime;   /* Parsed time for the 1st field               */
struct timespec tsOffset; /* Zero-point time to adjust the 1st field one */
char    *pszPath;         /* filepath on arguments                       */
char    *pszFilename;     /* filepath (for message)                      */
int      iFileno;         /* file# of filepath                           */
int      iFd;             /* file descriptor                             */
FILE    *fp;              /* file handle                                 */
int      i;               /* all-purpose int                             */

/*--- Initialize ---------------------------------------------------*/
gpszCmdname = argv[0];
for (i=0; *(gpszCmdname+i)!='\0'; i++) {
  if (*(gpszCmdname+i)=='/') {gpszCmdname=gpszCmdname+i+1;}
}
if (setenv("POSIXLY_CORRECT","1",1) < 0) {
  error_exit(errno,"setenv() at initialization: \n", strerror(errno));
}
setlocale(LC_CTYPE, "");
if (clock_gettime(CLOCK_REALTIME,&gtsZero) != 0) {
  error_exit(errno,"clock_gettime() at initialize: %s\n",strerror(errno));
}

/*=== Parse arguments ==============================================*/

/*--- Set default parameters of the arguments ----------------------*/
iMode     = 2; /* 0:"-c" 1:"-e" 2:"-z"(default) 4:"-cZ" 5:"-eZ" 6:"-zZ" */
iPrio     = 1;
giVerbose = 0;
/*--- Parse options which start by "-" -----------------------------*/
while ((i=getopt(argc, argv, "cep:vhZz")) != -1) {
  switch (i) {
    case 'c': iMode&=4; iMode+=0; break;
    case 'e': iMode&=4; iMode+=1; break;
    case 'z': iMode&=4; iMode+=2; break;
    case 'Z': iMode&=3; iMode+=4; break;
    #if defined(_POSIX_PRIORITY_SCHEDULING) && !defined(__OpenBSD__) && !defined(__APPLE__)
      case 'p': if (sscanf(optarg,"%d",&iPrio) != 1) {print_usage_and_exit();}
                                    break;
    #endif
    case 'v': giVerbose++;        break;
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

/*=== Try to make me a realtime process ============================*/
if (change_to_rtprocess(iPrio)==-1) {print_usage_and_exit();}

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

  /*--- Reading and writing loop -----------------------------------*/
  switch (iMode) {
    case 0: /* "-c" Calendar time mode */
             while (1) {
               switch (read_1st_field_as_a_timestamp(fp, szTime)) {
                 case  1: /* read successfully */
                          if (! parse_calendartime(szTime, &tsTime)) {
                            goto CLOSE_THISFILE;
                          }
                          spend_my_spare_time(&tsTime, NULL);
                          switch (read_and_write_a_line(fp)) {
                            case  1: /* expected LF */
                                     break;
                            case -1: /* expected EOF */
                                     goto CLOSE_THISFILE;
                            case -2: /* file access error */
                                     warning("%s: File access error, "
                                             "skipping it\n",pszFilename);
                                     goto CLOSE_THISFILE;
                                     break;
                            default: /* bug of system error */
                                     error_exit(1,"Unexpected error at %d\n",
                                                __LINE__);
                                     break;
                          }
                          break;
                 case  0: /* unexpected LF */
                          warning("%s: No first field which contains the line, "
                                  "skipping it\n",pszFilename);
                          goto CLOSE_THISFILE;
                 case -2: /* unexpected EOF */
                          warning("%s: Came to EOF suddenly\n",pszFilename);
                 case -1: /*   expected EOF */
                          goto CLOSE_THISFILE;
                          break;
                 case -3: /* file access error */
                          warning("%s: File access error, skipping it\n",
                                  pszFilename);
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
                            goto CLOSE_THISFILE;
                          }
                          spend_my_spare_time(&tsTime, NULL);
                          switch (read_and_write_a_line(fp)) {
                            case  1: /* expected LF */
                                     break;
                            case -1: /* expected EOF */
                                     goto CLOSE_THISFILE;
                            case -2: /* file access error */
                                     warning("%s: File access error, "
                                             "skipping it\n",pszFilename);
                                     goto CLOSE_THISFILE;
                                     break;
                            default: /* bug of system error */
                                     error_exit(1,"Unexpected error at %d\n",
                                                __LINE__);
                                     break;
                          }
                          break;
                 case  0: /* unexpected LF */
                          warning("%s: No first field which contains the line, "
                                  "skipping it\n",pszFilename);
                          goto CLOSE_THISFILE;
                 case -2: /* unexpected EOF */
                          warning("%s: Came to EOF suddenly\n",pszFilename);
                 case -1: /*   expected EOF */
                          goto CLOSE_THISFILE;
                          break;
                 case -3: /* file access error */
                          warning("%s: File access error, skipping it\n",
                                  pszFilename);
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
                            goto CLOSE_THISFILE;
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
                                             "skipping it\n",pszFilename);
                                     goto CLOSE_THISFILE;
                                     break;
                            default: /* bug of system error */
                                     error_exit(1,"Unexpected error at %d\n",
                                                __LINE__);
                                     break;
                          }
                          break;
                 case  0: /* unexpected LF */
                          warning("%s: No first field which contains the line, "
                                  "skipping it\n",pszFilename);
                          goto CLOSE_THISFILE;
                 case -2: /* unexpected EOF */
                          warning("%s: Came to EOF suddenly\n",pszFilename);
                 case -1: /*   expected EOF */
                          goto CLOSE_THISFILE;
                          break;
                 case -3: /* file access error */
                          warning("%s: File access error, skipping it\n",
                                  pszFilename);
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
                            goto CLOSE_THISFILE;
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
                                             "skipping it\n",pszFilename);
                                     goto CLOSE_THISFILE;
                                     break;
                            default: /* bug of system error */
                                     error_exit(1,"Unexpected error at %d\n",
                                                __LINE__);
                                     break;
                          }
                          break;
                 case  0: /* unexpected LF */
                          warning("%s: No first field which contains the line, "
                                  "skipping it\n",pszFilename);
                          goto CLOSE_THISFILE;
                 case -2: /* unexpected EOF */
                          warning("%s: Came to EOF suddenly\n",pszFilename);
                 case -1: /*   expected EOF */
                          goto CLOSE_THISFILE;
                          break;
                 case -3: /* file access error */
                          warning("%s: File access error, skipping it\n",
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
                            goto CLOSE_THISFILE;
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
                                             "skipping it\n",pszFilename);
                                     goto CLOSE_THISFILE;
                                     break;
                            default: /* bug of system error */
                                     error_exit(1,"Unexpected error at %d\n",
                                                __LINE__);
                                     break;
                          }
                          break;
                 case  0: /* unexpected LF */
                          warning("%s: No first field which contains the line, "
                                  "skipping it\n",pszFilename);
                          goto CLOSE_THISFILE;
                 case -2: /* unexpected EOF */
                          warning("%s: Came to EOF suddenly\n",pszFilename);
                 case -1: /*   expected EOF */
                          goto CLOSE_THISFILE;
                          break;
                 case -3: /* file access error */
                          warning("%s: File access error, skipping it\n",
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
 *                the 1st field
 *                (Size of the buffer you give MUST BE 33 BYTES or more!)
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
                 pszTime[iTslen]=0;
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
                 if (iTslen>31) {                                 continue;}
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

/*=== Parse a local calendar time ====================================
 * [in]  pszTime : calendar-time string in the localtime
 *                 (/[0-9]{11,20}(\.[0-9]{1,9})?/)
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
    else if (c==0                ) {szDate[i]=0; iStatus=1;      break;}
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
               if      (('0'<=c) && (c<='9')) {szNsec[k]=c; k++;}
               else if (c==0                ) {break;           }
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
  if (ptsTime->tv_sec<0) {
    ptsTime->tv_sec = (sizeof(time_t)>=8) ? LLONG_MAX : LONG_MAX;
  }
  ptsTime->tv_nsec = atol(szNsec);

  return 1;
}

/*=== Parse a UNIX-time ==============================================
 * [in]  pszTime : UNIX-time string (/[0-9]{1,19}(\.[0-9]{1,9})?/)
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
    else if (c==0                ) {szSec[i]=0; iStatus=1;      break;}
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
               if      (('0'<=c) && (c<='9')) {szNsec[k]=c; k++;}
               else if (c==0                ) {break;           }
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
