/*####################################################################
#
# LINETS - Print the Current Timestamp at the Top of Each Line
#
#          This command attaches the current time to the top of each line.
#          (as the first field of the line) Strictly speaking, "the
#          current time" means the instant when this command starts
#          writing the line which has been read.
#
# USAGE   : linets [-0|-3|-6|-9] [-c|-e|-z] [-du] [file ...]
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
#           -c,-e,-z .. Specify the format for timestamp. You can choose
#                       one of them.
#                         -c ... "YYYYMMDDhhmmss[.n]" (default)
#                                Civil time (standard time) in your
#                                timezone (".n" is the digits under
#                                second. It will be attached when -3 or
#                                -6 or -9 option is specified)
#                         -e ... "n[.n]"
#                                The number of seconds since the UNIX
#                                epoch (".n" is the same as -s)
#                         -z ... "n[.n]"
#                                The number of seconds since this command
#                                started (".n" is the same as -s)
#           -d ........ Insert "delta-t" (the number of seconds since
#                       started writing the previous line) into the next
#                       to the current timestamp. So, two fields will
#                       be attatched when using this option.
#           -u ........ Set the date in UTC when -c option is set
#                       (same as that of date command)
# Retuen  : Return 0 only when finished successfully
#
# How to compile : cc -O3 -o __CMDNAME__ __SRCNAME__ -lrt
#
# Written by Shell-Shoccar Japan (@shellshoccarjpn) on 2019-04-12
#
# This is a public-domain software (CC0). It means that all of the
# people can use this for any purposes with no restrictions at all.
# By the way, We are fed up with the side effects which are brought
# about by the major licenses.
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

/*--- prototype functions ------------------------------------------*/
int read_1line(FILE *fp);
void print_cur_timestamp(void);

/*--- global variables ---------------------------------------------*/
char* gpszCmdname; /* The name of this command                        */
int   giVerbose;   /* speaks more verbosely by the greater number     */
int   giFmtType;   /* 'c':Civil-time
                      'e':UNIX-epoch-time
                      'z':Command-running-sec                         */
clockid_t gclkId;  /* CLOCK_MONOTONIC will be set when giFmtType is 'z'.
                      Or CLOCK_REALTIME */
int   giTimeResol; /* 0:second 3:millisec 6:microsec 9:nanosec        */
int   giDeltaMode; /* attach the number of seconds since printing the 
                      previous line after the timestamp when >0       */

/*=== Define the functions for printing usage and error ============*/

/*--- exit with usage ----------------------------------------------*/
void print_usage_and_exit(void) {
  fprintf(stderr,
    "USAGE   : %s [-0|-3|-6|-9] [-c|-e|-z] [-du] [file ...]\n"
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
    "          -c,-e,-z .. Specify the format for timestamp. You can choose\n"
    "                      one of them.\n"
    "                        -c ... \"YYYYMMDDhhmmss[.n]\" (default)\n"
    "                               Civil time (standard time) in your\n"
    "                               timezone (\".n\" is the digits under\n"
    "                               second. It will be attached when -3 or\n"
    "                               -6 or -9 option is specified)\n"
    "                        -e ... \"n[.n]\"\n"
    "                               The number of seconds since the UNIX\n"
    "                               epoch (\".n\" is the same as -s)\n"
    "                        -z ... \"n[.n]\"\n"
    "                               The number of seconds since this command\n"
    "                               started (\".n\" is the same as -s)\n"
    "          -d ........ Insert \"delta-t\" (the number of seconds since\n"
    "                      started writing the previous line) into the next\n"
    "                      to the current timestamp. So, two fields will\n"
    "                      be attatched when using this option.\n"
    "          -u ........ Set the date in UTC when -c option is set\n"
    "                      (same as that of date command)\n"
    "Retuen  : Return 0 only when finished successfully\n"
    "Version : 2019-04-12 15:15:56 JST\n"
    "          (POSIX C language)\n"
    "\n"
    "Shell-Shoccar Japan (@shellshoccarjpn), No rights reserved.\n"
    "This is public domain software. (CC0)\n"
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
int      i;               /* all-purpose int                       */

/*--- Initialize ---------------------------------------------------*/
gpszCmdname = argv[0];
for (i=0; *(gpszCmdname+i)!='\0'; i++) {
  if (*(gpszCmdname+i)=='/') {gpszCmdname=gpszCmdname+i+1;}
}

/*=== Parse arguments ==============================================*/

/*--- Set default parameters of the arguments ----------------------*/
giFmtType  ='c';
giTimeResol= 0 ;
giDeltaMode= 0 ;
giVerbose  = 0 ;

/*--- Parse options which start by "-" -----------------------------*/
while ((i=getopt(argc, argv, "0369cezduvh")) != -1) {
  switch (i) {
    case '0': giTimeResol = 0  ;                           break;
    case '3': giTimeResol = 3  ;                           break;
    case '6': giTimeResol = 6  ;                           break;
    case '9': giTimeResol = 9  ;                           break;
    case 'c': giFmtType   = 'c'; gclkId = CLOCK_REALTIME ; break;
    case 'e': giFmtType   = 'e'; gclkId = CLOCK_REALTIME ; break;
    case 'z': giFmtType   = 'z'; gclkId = CLOCK_MONOTONIC; break;
    case 'd': giDeltaMode = 1  ;                           break;
    case 'u': (void)setenv("TZ", "UTC0", 1)              ; break;
    case 'v': giVerbose++      ;                           break;
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
  if (feof(fp)==0) {
    while (1) {
      if ((iRet_r1l=read_1line(fp)) !=   0) {break;}
    }
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
 * [ret] 0   : Finished reading/writing due to '\n'
 *       1   : Finished reading/writing due to '\n', which is the last
 *             char of the file
 *       EOF : Finished reading/writing due to EOF              */
int read_1line(FILE *fp) {

  /*--- Variables --------------------------------------------------*/
  static int iHold = 0; /* set 1 if next character is currently held */
  static int iNextchar; /* variable for the next character           */
  int        iChar;

  /*--- Reading and writing a line (1st letter of the line) --------*/
  if (iHold) {iChar=iNextchar; iHold=0;} else {iChar=getc(fp);}
  switch (iChar) {
    case EOF:
                return(EOF);
    case '\n':
                print_cur_timestamp();
                while (putchar('\n' )==EOF) {
                  if (errno == EINTR) {continue;}
                  error_exit(errno,"putchar() #R1L-1: %s\n",strerror(errno));
                }
                iNextchar = getc(fp);
                if (iNextchar!=EOF) {iHold=1;return 0;}
                else                {        return 1;}
    default:
                print_cur_timestamp();
                while (putchar(iChar)==EOF) {
                  if (errno == EINTR) {continue;}
                  error_exit(errno,"putchar() #R1L-2: %s\n",strerror(errno));
                }
  }

  /*--- Reading and writing a line ---------------------------------*/
  while (1) {
    if (iHold) {iChar=iNextchar; iHold=0;} else {iChar=getc(fp);}
    switch (iChar) {
      case EOF:
                  return(EOF);
      case '\n':
                  while (putchar('\n' )==EOF) {
                    if (errno == EINTR) {continue;}
                    error_exit(errno,"putchar() #R1L-3: %s\n",strerror(errno));
                  }
                  iNextchar = getc(fp);
                  if (iNextchar!=EOF) {iHold=1;return 0;}
                  else                {        return 1;}
      default:
                  while (putchar(iChar)==EOF) {
                    if (errno == EINTR) {continue;}
                    error_exit(errno,"putchar() #R1L-4: %s\n",strerror(errno));
                  }
    }
  }
}



/*=== Write the current timestamp to stdout ==========================
 * [in] giFmtType   : (must be defined as a global variable)
 *      gclkId      : (must be defined as a global variable)
 *      giTimeResol : (must be defined as a global variable)
 *      giDeltaMode : (must be defined as a global variable)        */
void print_cur_timestamp(void) {

  /*--- Variables --------------------------------------------------*/
  static int             iFirst = 1     ; /* >0 when called for the 1st time */
  static struct timespec tsZero = {0,0} ;
  static struct timespec tsPrev = {0,0} ;
  struct timespec        tsNow          ;
  struct timespec        tsDiff         ;
  struct tm             *ptm            ;
  char                   szBuf[LINE_BUF];

  /*--- Get the current time ---------------------------------------*/
  if (clock_gettime(gclkId,&tsNow) != 0) {
    error_exit(errno,"clock_gettime()#1: %s\n",strerror(errno));
  }

  /*--- Print the current timestamp ("YYYYMMDDhhmmss" part) --------*/
  switch (giFmtType) {
    case 'c':
              if (iFirst) {
                tsZero.tv_sec=tsNow.tv_sec; tsZero.tv_nsec=tsNow.tv_nsec;
                tsPrev.tv_sec=tsNow.tv_sec; tsPrev.tv_nsec=tsNow.tv_nsec;
                iFirst = 0;
              }
              ptm = localtime(&tsNow.tv_sec);
              if (ptm==NULL) {error_exit(255,"localtime(): returned NULL\n");}
              printf("%04d%02d%02d%02d%02d%02d",
                ptm->tm_year+1900, ptm->tm_mon+1, ptm->tm_mday,
                ptm->tm_hour     , ptm->tm_min  , ptm->tm_sec  );
              break;
    case 'e':
              if (iFirst) {
                tsZero.tv_sec=tsNow.tv_sec; tsZero.tv_nsec=tsNow.tv_nsec;
                tsPrev.tv_sec=tsNow.tv_sec; tsPrev.tv_nsec=tsNow.tv_nsec;
                iFirst = 0;
              }
              ptm = localtime(&tsNow.tv_sec);
              strftime(szBuf, LINE_BUF, "%s", ptm);
              printf("%s", szBuf);
              break;
    case 'z':
              if (iFirst) {
                tsZero.tv_sec=tsNow.tv_sec; tsZero.tv_nsec=tsNow.tv_nsec;
                tsPrev.tv_sec=0           ; tsPrev.tv_nsec=0            ;
                iFirst = 0;
              }
              if ((tsNow.tv_nsec - tsZero.tv_nsec) < 0) {
                tsNow.tv_sec  = tsNow.tv_sec  - tsZero.tv_sec  -          1;
                tsNow.tv_nsec = tsNow.tv_nsec - tsZero.tv_nsec + 1000000000;
              } else {
                tsNow.tv_sec  = tsNow.tv_sec  - tsZero.tv_sec ;
                tsNow.tv_nsec = tsNow.tv_nsec - tsZero.tv_nsec;
              }
              ptm = localtime(&tsNow.tv_sec);
              strftime(szBuf, LINE_BUF, "%s", ptm);
              printf("%s", szBuf);
              break;
    default : error_exit(255,"print_cur_timestamp(): Unknown format\n");
  }

  /*--- Print the current timestamp (".n" part) --------------------*/
  if (tsNow.tv_nsec==0) {
    putchar(' ');
  } else                {
    switch (giTimeResol) {
      case 0:
              putchar(' ');
              break;
      case 3:
              printf(".%03d " , (int)(tsNow.tv_nsec+500000)/1000000);
              break;
      case 6:
              printf(".%06d " , (int)(tsNow.tv_nsec+   500)/   1000);
              break;
      case 9:
              printf(".%09ld ",       tsNow.tv_nsec                );
              break;
      default : error_exit(255,"print_cur_timestamp(): Unknown resolution\n");
    }
  }

  /*--- Print the delta-t if required ------------------------------*/
  if (giDeltaMode) {
    if ((tsNow.tv_nsec - tsPrev.tv_nsec) < 0) {
      tsDiff.tv_sec  = tsNow.tv_sec  - tsPrev.tv_sec  -          1;
      tsDiff.tv_nsec = tsNow.tv_nsec - tsPrev.tv_nsec + 1000000000;
    } else {
      tsDiff.tv_sec  = tsNow.tv_sec  - tsPrev.tv_sec ;
      tsDiff.tv_nsec = tsNow.tv_nsec - tsPrev.tv_nsec;
    }
    tsPrev.tv_sec=tsNow.tv_sec; tsPrev.tv_nsec=tsNow.tv_nsec;
    ptm = localtime(&tsDiff.tv_sec);
    strftime(szBuf, LINE_BUF, "%s", ptm);
    printf("%s", szBuf);
    if (tsDiff.tv_nsec==0) {
      putchar(' ');
    } else                 {
      switch (giTimeResol) {
        case 0:
                putchar(' ');
                break;
        case 3:
                printf(".%03d " , (int)(tsDiff.tv_nsec+500000)/1000000);
                break;
        case 6:
                printf(".%06d " , (int)(tsDiff.tv_nsec+   500)/   1000);
                break;
        case 9:
                printf(".%09ld ",       tsDiff.tv_nsec                );
                break;
      }
    }
  }

  /*--- Finish -----------------------------------------------------*/
  return;
}
