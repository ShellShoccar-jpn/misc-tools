/*####################################################################
#
# LINETS - Print the Current Timestamp at the Top of Each Line
#
#          This command attaches the current time to the top of each line.
#          (as the first field of the line) Strictly speaking, "the
#          current time" means the instant when this command starts
#          writing the line which has been read.
#
# USAGE   : linets [-0|-3|-6|-9] [-c|-e|-z|-Z] [-du] [file [...]]
# Args    : file ...... Filepath to be attached the current timestamp
#                       ("-" means STDIN)
# Options : -0,-3,-6,-9 Specify resolution unit of the time. For instance,
#                       timestamp becomes "YYYYMMDDhhmmss.nnn" when
#                       "-3" option is set. 
#                       You have to set one of them.
#                         -0 ... second (default)
#                         -3 ... millisecond
#                         -6 ... microsecond
#                         -9 ... nanosecond
#           -c,-e,-z,-Z Specify the format for timestamp. You can choose
#                       one of them.
#                         -c ... "YYYYMMDDhhmmss[.n]" (default)
#                                Calendar-time (standard time) in your
#                                timezone (".n" is the digits under
#                                second. It will be attached when -3 or
#                                -6 or -9 option is specified)
#                         -e ... "n[.n]"
#                                The number of seconds since the UNIX
#                                epoch (".n" is the same as -c)
#                         -z ... "n[.n]"
#                                The number of seconds since this command
#                                started (".n" is the same as -c)
#                         -Z ... "n[.n]"
#                                The number of seconds since the first
#                                line came (".n" is the same as -c)
#           -d ........ Insert "delta-t" (the number of seconds since
#                       started writing the previous line) into the next
#                       to the current timestamp. So, two fields will
#                       be attatched when using this option.
#           -u ........ Set the date in UTC when -c option is set
#                       (same as that of date command)
# Retuen  : Return 0 only when finished successfully
#
# How to compile : cc -O3 -o __CMDNAME__ __SRCNAME__ -lrt
#                  (if it doesn't work)
# How to compile : cc -O3 -o __CMDNAME__ __SRCNAME__
#
# Written by Shell-Shoccar Japan (@shellshoccarjpn) on 2022-07-18
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
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>

/*--- macro constants ----------------------------------------------*/
/* Buffer size for a timestamp string */
#define LINE_BUF         80
#ifndef CLOCK_MONOTONIC
  #define CLOCK_MONOTONIC CLOCK_REALTIME /* for HP-UX */
#endif

/*--- data type definitions ----------------------------------------*/
typedef struct timespec tmsp;

/*--- prototype functions ------------------------------------------*/
int read_1line(FILE *fp);
int read_c1st_1line(FILE *fp);
int read_e1st_1line(FILE *fp);
int read_Z1st_1line(FILE *fp);
void print_cur_timestamp(void);

/*--- global variables ---------------------------------------------*/
char* gpszCmdname      ; /* The name of this command                      */
int   giVerbose   =  0 ; /* speaks more verbosely by the greater number   */
int   giFmtType   = 'c'; /* 'c':calendar-time (default)
                            'e':UNIX-epoch-time
                            'z':command-running-sec                       */
int   giTimeResol =  0 ; /* 0:second(def) 3:millisec 6:microsec 9:nanosec */
int   giDeltaMode =  0 ; /* attach the number of seconds since printing
                            the previous line after the timestamp when >0 */
tmsp  gtsZero     = {0}; /* Time this command booted                      */
tmsp  gtsPrev     = {0}; /* Time the previous line has come (-gtsZero)    */
int   giHold      =  0 ; /* for read_1line(): 1 if next character exists  */
int   giNextchar       ; /* for read_1line(): the next character          */

/*=== Define the functions for printing usage and error ============*/

/*--- exit with usage ----------------------------------------------*/
void print_usage_and_exit(void) {
  fprintf(stderr,
    "USAGE   : %s [-0|-3|-6|-9] [-c|-e|-z|-Z] [-du] [file [...]]\n"
    "Args    : file ...... Filepath to be attached the current timestamp\n"
    "                      (\"-\" means STDIN)\n"
    "Options : -0,-3,-6,-9 Specify resolution unit of the time. For instance,\n"
    "                      timestamp becomes \"YYYYMMDDhhmmss.nnn\" when\n"
    "                      \"-3\" option is set. \n"
    "                      You have to set one of them.\n"
    "                        -0 ... second (default)\n"
    "                        -3 ... millisecond\n"
    "                        -6 ... microsecond\n"
    "                        -9 ... nanosecond\n"
    "          -c,-e,-z,-Z Specify the format for timestamp. You can choose\n"
    "                      one of them.\n"
    "                        -c ... \"YYYYMMDDhhmmss[.n]\" (default)\n"
    "                               Calendar-time (standard time) in your\n"
    "                               timezone (\".n\" is the digits under\n"
    "                               second. It will be attached when -3 or\n"
    "                               -6 or -9 option is specified)\n"
    "                        -e ... \"n[.n]\"\n"
    "                               The number of seconds since the UNIX\n"
    "                               epoch (\".n\" is the same as -c)\n"
    "                        -z ... \"n[.n]\"\n"
    "                               The number of seconds since this command\n"
    "                               started (\".n\" is the same as -c)\n"
    "                        -Z ... \"n[.n]\"\n"
    "                               The number of seconds since the fisrt\n"
    "                               line came (\".n\" is the same as -c)\n"
    "          -d ........ Insert \"delta-t\" (the number of seconds since\n"
    "                      started writing the previous line) into the next\n"
    "                      to the current timestamp. So, two fields will\n"
    "                      be attatched when using this option.\n"
    "          -u ........ Set the date in UTC when -c option is set\n"
    "                      (same as that of date command)\n"
    "Retuen  : Return 0 only when finished successfully\n"
    "Version : 2022-07-18 23:34:16 JST\n"
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
int      iRet;            /* return code                           */
int      iRet_r1l;        /* return value by read_1line()          */
char    *pszPath;         /* filepath on arguments                 */
char    *pszFilename;     /* filepath (for message)                */
int      iFileno;         /* file# of filepath                     */
int      iFd;             /* file descriptor                       */
FILE    *fp;              /* file handle                           */
int      iFirstline='c';  /* >0 when x-opt and no line come yet    */
int      i;               /* all-purpose int                       */

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

/*=== Parse arguments ==============================================*/

/*--- Parse options which start by "-" -----------------------------*/
while ((i=getopt(argc, argv, "0369cezZduvh")) != -1) {
  switch (i) {
    case '0': giTimeResol =  0 ;                 break;
    case '3': giTimeResol =  3 ;                 break;
    case '6': giTimeResol =  6 ;                 break;
    case '9': giTimeResol =  9 ;                 break;
    case 'c': giFmtType   = 'c'; iFirstline='c'; break;
    case 'e': giFmtType   = 'e'; iFirstline='e'; break;
    case 'Z': giFmtType   = 'Z'; iFirstline='Z'; break;
    case 'z': giFmtType   = 'z'; iFirstline='z'; break;
    case 'd': giDeltaMode =  1 ;                 break;
    case 'u': (void)setenv("TZ", "UTC0", 1);     break;
    case 'v': giVerbose++      ;                 break;
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
iRet     =  0;
iFileno  =  0;
iFd      = -1;
iRet_r1l =  0;
while ((pszPath = argv[iFileno]) != NULL || iFileno == 0) {

  /*--- Open one of the input files --------------------------------*/
  if (pszPath == NULL || strcmp(pszPath, "-") == 0) {
    pszFilename = "stdin"                ;
    iFd         = STDIN_FILENO           ;
  } else                                            {
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

  /*--- Reading and writing loop -----------------------------------*/
  if (! feof(fp)) {
    switch(iFirstline) {
      case 'c': iRet_r1l=read_c1st_1line(fp); iFirstline=0;               break;
      case 'e': iRet_r1l=read_e1st_1line(fp); iFirstline=0;               break;
      case 'z': gtsPrev.tv_sec =gtsZero.tv_sec;
                gtsPrev.tv_nsec=gtsZero.tv_nsec;
                iRet_r1l=0;                   iFirstline=0;               break;
      case 'Z': iRet_r1l=read_Z1st_1line(fp); iFirstline=0;giFmtType='z'; break;
      default : iRet_r1l=0;
    }
    while (iRet_r1l==0) {iRet_r1l=read_1line(fp);}
  }

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

/*=== Read and write only one line ===================================
 * [in]  giHold     : (must be defined as a global variable)
 *       giNextchar : (must be defined as a global variable)
 * [ret] 0          : Finished reading/writing due to '\n'
 *       1          : Finished reading/writing due to '\n',
 *                    which is the last char of the file
 *       EOF        : Finished reading/writing due to EOF           */
int read_1line(FILE *fp) {

  /*--- Variables --------------------------------------------------*/
  int        iChar;

  /*--- Reading and writing a line (1st letter of the line) --------*/
  if (giHold) {iChar=giNextchar; giHold=0;} else {iChar=getc(fp);}
  if (iChar == EOF) {return(EOF);}
  print_cur_timestamp();
  while (putchar(iChar)==EOF) {
    if (errno == EINTR) {continue;}
    error_exit(errno,"read_1line(): putchar() #1: %s\n",strerror(errno));
  }
  if (iChar == '\n') {
    giNextchar = getc(fp);
    if (giNextchar!=EOF) {giHold=1;return 0;}
    else                 {         return 1;}
  }

  /*--- Reading and writing a line ---------------------------------*/
  while (1) {
    if (giHold) {iChar=giNextchar; giHold=0;} else {iChar=getc(fp);}
    if (iChar == EOF) {return(EOF);}
    while (putchar(iChar)==EOF) {
      if (errno == EINTR) {continue;}
      error_exit(errno,"read_1line(): putchar() #2: %s\n",strerror(errno));
    }
    if (iChar == '\n') {
      giNextchar = getc(fp);
      if (giNextchar!=EOF) {giHold=1;return 0;}
      else                 {         return 1;}
    }
  }
}


/*=== Read and write only one line (for c-option and 1st line ) ======
 * [in]  giHold     : (must be defined as a global variable)
 *       giNextchar : (must be defined as a global variable)
 * [ret] 0          : Finished reading/writing due to '\n'
 *       1          : Finished reading/writing due to '\n',
 *                    which is the last char of the file
 *       EOF        : Finished reading/writing due to EOF           */
int read_c1st_1line(FILE *fp) {

  /*--- Variables --------------------------------------------------*/
  tmsp       tsNow;
  struct tm *ptm  ;
  int        iChar;

  /*--- Reading and writing a line (1st letter of the line) --------*/
  if (giHold) {iChar=giNextchar; giHold=0;} else {iChar=getc(fp);}
  if (iChar == EOF) {return(EOF);}
  if (clock_gettime(CLOCK_REALTIME,&tsNow) != 0) {
    error_exit(errno,"read_c1st_1line(): clock_gettime(): %s\n",
                     strerror(errno)                            );
  }
  if ((ptm=localtime(&tsNow.tv_sec)) == NULL) {
    error_exit(255,"read_c1st_1line(): localtime(): returned NULL\n");
  }
  printf("%04d%02d%02d%02d%02d%02d",
    ptm->tm_year+1900, ptm->tm_mon+1, ptm->tm_mday,
    ptm->tm_hour     , ptm->tm_min  , ptm->tm_sec  );
  switch (giTimeResol) {
    case 0 : putchar(' ')                                          ; break;
    case 3 : printf(".%03d " , (int)(tsNow.tv_nsec+500000)/1000000); break;
    case 6 : printf(".%06d " , (int)(tsNow.tv_nsec+   500)/   1000); break;
    case 9 : printf(".%09ld ",       tsNow.tv_nsec                ); break;
    default: error_exit(255,"read_e1st_1line(): Unknown resolution\n");
  }
  if (giDeltaMode) {
    printf("0 ");
    gtsPrev.tv_sec=tsNow.tv_sec; gtsPrev.tv_nsec=tsNow.tv_nsec;
  }
  while (putchar(iChar)==EOF) {
    if (errno == EINTR) {continue;}
    error_exit(errno,"read_c1st_1line(): putchar() #1: %s\n",strerror(errno));
  }
  if (iChar == '\n') {
    giNextchar = getc(fp);
    if (giNextchar!=EOF) {giHold=1;return 0;}
    else                 {         return 1;}
  }

  /*--- Reading and writing a line ---------------------------------*/
  while (1) {
    if (giHold) {iChar=giNextchar; giHold=0;} else {iChar=getc(fp);}
    if (iChar == EOF) {return(EOF);}
    while (putchar(iChar)==EOF) {
      if (errno == EINTR) {continue;}
      error_exit(errno,"read_c1st_1line(): putchar() #2: %s\n",strerror(errno));
    }
    if (iChar == '\n') {
      giNextchar = getc(fp);
      if (giNextchar!=EOF) {giHold=1;return 0;}
      else                 {         return 1;}
    }
  }
}


/*=== Read and write only one line (for e-option and 1st line ) ======
 * [in]  giHold     : (must be defined as a global variable)
 *       giNextchar : (must be defined as a global variable)
 * [ret] 0          : Finished reading/writing due to '\n'
 *       1          : Finished reading/writing due to '\n',
 *                    which is the last char of the file
 *       EOF        : Finished reading/writing due to EOF           */
int read_e1st_1line(FILE *fp) {

  /*--- Variables --------------------------------------------------*/
  tmsp       tsNow          ;
  struct tm *ptm            ;
  char       szBuf[LINE_BUF];
  int        iChar          ;

  /*--- Reading and writing a line (1st letter of the line) --------*/
  if (giHold) {iChar=giNextchar; giHold=0;} else {iChar=getc(fp);}
  if (iChar == EOF) {return(EOF);}
  if (clock_gettime(CLOCK_REALTIME,&tsNow) != 0) {
    error_exit(errno,"read_e1st_1line(): clock_gettime(): %s\n",
                     strerror(errno)                            );
  }
  if ((ptm=localtime(&tsNow.tv_sec)) == NULL) {
    error_exit(255,"read_e1st_1line(): localtime(): returned NULL\n");
  }
  strftime(szBuf, LINE_BUF, "%s", ptm);
  printf("%s", szBuf);
  switch (giTimeResol) {
    case 0 : putchar(' ')                                          ; break;
    case 3 : printf(".%03d " , (int)(tsNow.tv_nsec+500000)/1000000); break;
    case 6 : printf(".%06d " , (int)(tsNow.tv_nsec+   500)/   1000); break;
    case 9 : printf(".%09ld ",       tsNow.tv_nsec                ); break;
    default: error_exit(255,"read_e1st_1line(): Unknown resolution\n");
  }
  if (giDeltaMode) {
    printf("0 ");
    gtsPrev.tv_sec=tsNow.tv_sec; gtsPrev.tv_nsec=tsNow.tv_nsec;
  }
  while (putchar(iChar)==EOF) {
    if (errno == EINTR) {continue;}
    error_exit(errno,"read_e1st_1line(): putchar() #1: %s\n",strerror(errno));
  }
  if (iChar == '\n') {
    giNextchar = getc(fp);
    if (giNextchar!=EOF) {giHold=1;return 0;}
    else                 {         return 1;}
  }

  /*--- Reading and writing a line ---------------------------------*/
  while (1) {
    if (giHold) {iChar=giNextchar; giHold=0;} else {iChar=getc(fp);}
    if (iChar == EOF) {return(EOF);}
    while (putchar(iChar)==EOF) {
      if (errno == EINTR) {continue;}
      error_exit(errno,"read_e1st_1line(): putchar() #2: %s\n",strerror(errno));
    }
    if (iChar == '\n') {
      giNextchar = getc(fp);
      if (giNextchar!=EOF) {giHold=1;return 0;}
      else                 {         return 1;}
    }
  }
}


/*=== Read and write only one line (for Z-option and 1st line ) ======
 * [in]  giHold     : (must be defined as a global variable)
 *       giNextchar : (must be defined as a global variable)
 * [ret] 0          : Finished reading/writing due to '\n'
 *       1          : Finished reading/writing due to '\n',
 *                    which is the last char of the file
 *       EOF        : Finished reading/writing due to EOF           */
int read_Z1st_1line(FILE *fp) {

  /*--- Variables --------------------------------------------------*/
  int        iChar;

  /*--- Reading and writing a line (1st letter of the line) --------*/
  if (giHold) {iChar=giNextchar; giHold=0;} else {iChar=getc(fp);}
  if (iChar == EOF) {return(EOF);}
  if (clock_gettime(CLOCK_REALTIME,&gtsZero) != 0) {
    error_exit(errno,"read_Z1st_1line(): clock_gettime(): %s\n",
                     strerror(errno)                            );
  }
  if (giDeltaMode) {
    printf("0 0 ");
    gtsPrev.tv_sec=gtsZero.tv_sec; gtsPrev.tv_nsec=gtsZero.tv_nsec;
  } else           {
    printf("0 ");
  }
  while (putchar(iChar)==EOF) {
    if (errno == EINTR) {continue;}
    error_exit(errno,"read_Z1st_1line(): putchar() #1: %s\n",strerror(errno));
  }
  if (iChar == '\n') {
    giNextchar = getc(fp);
    if (giNextchar!=EOF) {giHold=1;return 0;}
    else                 {         return 1;}
  }

  /*--- Reading and writing a line ---------------------------------*/
  while (1) {
    if (giHold) {iChar=giNextchar; giHold=0;} else {iChar=getc(fp);}
    if (iChar == EOF) {return(EOF);}
    while (putchar(iChar)==EOF) {
      if (errno == EINTR) {continue;}
      error_exit(errno,"read_Z1st_1line(): putchar() #2: %s\n",strerror(errno));
    }
    if (iChar == '\n') {
      giNextchar = getc(fp);
      if (giNextchar!=EOF) {giHold=1;return 0;}
      else                 {         return 1;}
    }
  }
}


/*=== Write the current timestamp to stdout ==========================
 * [in] giFmtType   : (must be defined as a global variable)
 *      giTimeResol : (must be defined as a global variable)
 *      giDeltaMode : (must be defined as a global variable)
 *      gtsZero     : (must be defined as a global variable)
 *      gtsPrev     : (must be defined as a global variable) */
void print_cur_timestamp(void) {

  /*--- Variables --------------------------------------------------*/
  tmsp        tsNow          ; /* Current time but substructed by gtsZero */
  tmsp        tsDiff         ;
  tmsp        ts             ;
  struct tm  *ptm            ;
  char        szBuf[LINE_BUF];
  char        szDec[LINE_BUF]; /* for the Decimal part */

  /*--- Get the current time ---------------------------------------*/
  if (clock_gettime(CLOCK_REALTIME,&tsNow) != 0) {
    error_exit(errno,"clock_gettime()#1: %s\n",strerror(errno));
  }

  /*--- Print the current timestamp --------------------------------*/
  switch (giFmtType) {
    case 'c':
              ts.tv_sec=tsNow.tv_sec; ts.tv_nsec=tsNow.tv_nsec;
              switch (giTimeResol) {
                case 0 : if (ts.tv_nsec>=500000000L) {ts.tv_sec++;ts.tv_nsec=0;}
                         szDec[0]=0;
                         break;
                case 3 : if (ts.tv_nsec>=999500000L) {ts.tv_sec++;ts.tv_nsec=0;}
                         snprintf(szDec,LINE_BUF,".%03ld",ts.tv_nsec/1000000);
                         break;
                case 6 : if (ts.tv_nsec>=999999500L) {ts.tv_sec++;ts.tv_nsec=0;}
                         snprintf(szDec,LINE_BUF,".%06ld",ts.tv_nsec/   1000);
                         break;
                default: snprintf(szDec,LINE_BUF,".%09ld",ts.tv_nsec        );
                         break;
              }
              ptm = localtime(&ts.tv_sec);
              if (ptm==NULL) {error_exit(255,"localtime(): returned NULL\n");}
              printf("%04d%02d%02d%02d%02d%02d%s ",
                ptm->tm_year+1900, ptm->tm_mon+1, ptm->tm_mday,
                ptm->tm_hour     , ptm->tm_min  , ptm->tm_sec , szDec);
              break;
    case 'e':
              ts.tv_sec=tsNow.tv_sec; ts.tv_nsec=tsNow.tv_nsec;
              switch (giTimeResol) {
                case 0 : if (ts.tv_nsec>=500000000L) {ts.tv_sec++;ts.tv_nsec=0;}
                         szDec[0]=0;
                         break;
                case 3 : if (ts.tv_nsec>=999500000L) {ts.tv_sec++;ts.tv_nsec=0;}
                         snprintf(szDec,LINE_BUF,".%03ld",ts.tv_nsec/1000000);
                         break;
                case 6 : if (ts.tv_nsec>=999999500L) {ts.tv_sec++;ts.tv_nsec=0;}
                         snprintf(szDec,LINE_BUF,".%06ld",ts.tv_nsec/   1000);
                         break;
                default: snprintf(szDec,LINE_BUF,".%09ld",ts.tv_nsec        );
                         break;
              }
              ptm = localtime(&ts.tv_sec);
              strftime(szBuf, LINE_BUF, "%s", ptm);
              printf("%s%s ", szBuf, szDec);
              break;
    case 'z':
              if ((tsNow.tv_nsec - gtsZero.tv_nsec) < 0) {
                ts.tv_sec  = tsNow.tv_sec  - gtsZero.tv_sec  -          1;
                ts.tv_nsec = tsNow.tv_nsec - gtsZero.tv_nsec + 1000000000;
              } else {
                ts.tv_sec  = tsNow.tv_sec  - gtsZero.tv_sec ;
                ts.tv_nsec = tsNow.tv_nsec - gtsZero.tv_nsec;
              }
              switch (giTimeResol) {
                case 0 : if (ts.tv_nsec>=500000000L) {ts.tv_sec++;ts.tv_nsec=0;}
                         szDec[0]=0;
                         break;
                case 3 : if (ts.tv_nsec>=999500000L) {ts.tv_sec++;ts.tv_nsec=0;}
                         snprintf(szDec,LINE_BUF,".%03ld",ts.tv_nsec/1000000);
                         break;
                case 6 : if (ts.tv_nsec>=999999500L) {ts.tv_sec++;ts.tv_nsec=0;}
                         snprintf(szDec,LINE_BUF,".%06ld",ts.tv_nsec/   1000);
                         break;
                default: snprintf(szDec,LINE_BUF,".%09ld",ts.tv_nsec        );
                         break;
              }
              ptm = localtime(&ts.tv_sec);
              strftime(szBuf, LINE_BUF, "%s", ptm);
              printf("%s%s ", szBuf, szDec);
              break;
    default : error_exit(255,"print_cur_timestamp(): Unknown format\n");
  }

  /*--- Print the delta-t if required ------------------------------*/
  if (giDeltaMode) {
    if ((tsNow.tv_nsec - gtsPrev.tv_nsec) < 0) {
      tsDiff.tv_sec  = tsNow.tv_sec  - gtsPrev.tv_sec  -          1;
      tsDiff.tv_nsec = tsNow.tv_nsec - gtsPrev.tv_nsec + 1000000000;
    } else {
      tsDiff.tv_sec  = tsNow.tv_sec  - gtsPrev.tv_sec ;
      tsDiff.tv_nsec = tsNow.tv_nsec - gtsPrev.tv_nsec;
    }
    gtsPrev.tv_sec=tsNow.tv_sec; gtsPrev.tv_nsec=tsNow.tv_nsec;
    switch (giTimeResol) {
      case 0 : if(tsDiff.tv_nsec>=500000000L){tsDiff.tv_sec++;tsDiff.tv_nsec=0;}
               szDec[0]=0;
               break;
      case 3 : if(tsDiff.tv_nsec>=999500000L){tsDiff.tv_sec++;tsDiff.tv_nsec=0;}
               snprintf(szDec,LINE_BUF,".%03ld",tsDiff.tv_nsec/1000000);
               break;
      case 6 : if(tsDiff.tv_nsec>=999999500L){tsDiff.tv_sec++;tsDiff.tv_nsec=0;}
               snprintf(szDec,LINE_BUF,".%06ld",tsDiff.tv_nsec/   1000);
               break;
      default: snprintf(szDec,LINE_BUF,".%09ld",tsDiff.tv_nsec        );
               break;
    }
    ptm = localtime(&tsDiff.tv_sec);
    strftime(szBuf, LINE_BUF, "%s", ptm);
    printf("%s%s ", szBuf, szDec);
  }

  /*--- Finish -----------------------------------------------------*/
  return;
}
