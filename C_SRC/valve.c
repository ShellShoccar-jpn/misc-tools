/*####################################################################
#
# VALVE - Adjust the UNIX Pipe Streaming Speed
#
# USAGE   : valve [-cl] perioictime [file ...]
# Args    : periodictime  Periodic time from start sending the current
#                         block (means a character or a line) to start
#                         sending the next block.
#                         The unit of the periodic time is millisecond
#                         defaultly. You can also designate the unit
#                         like '100ms'. Available units are 's', 'ms',
#                         'us', 'ns', and 'bps' or 'cps'.
#                         The maximum value is 2147483647 for all units.
#           file ........ filepath to be send ("-" means STDIN)
# Options : -c .......... (This is default.) Changes the periodic unit
#                         to character. This option defines that the
#                         periodic time is the time from sending the
#                         current character to sending the next one.
#           -l .......... Changes the periodic unit to line. This
#                         option defines that the periodic time is the
#                         time from sending the top character of the
#                         current line to sending the top character of
#                         the next line.
# Retuen  : Return 0 only when finished successfully
#
# Written by Shell-Shoccar Japan (@shellshoccarjpn) on 2019-02-27
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <limits.h>
#include <locale.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>

/* --- macro functions ---------------------------------------------*/
#define WRN(message) fprintf(stderr,message)
#define WRV(fmt,...) fprintf(stderr,fmt,__VA_ARGS__)

/*--- macro constants ----------------------------------------------*/
#define MAX_PERIODICVAL 2147483647

/*--- prototype functions ------------------------------------------*/
int64_t parse_periodictime(char *pszArg);
void spend_my_spare_time(int64_t i8Periodic_nsec);
int read_1line(FILE *fp);

/*--- global variables ---------------------------------------------*/
char* pszMypath;

/*=== Define the functions for printing usage and error ============*/

/*--- exit with usage ----------------------------------------------*/
void print_usage_and_exit(void) {
  int  i;
  int  iPos = 0;
  for (i=0; *(pszMypath+i)!='\0'; i++) {
    if (*(pszMypath+i)=='/') {iPos=i+1;}
  }
  WRV(
    "USAGE   : %s [-cl] periodictime [file ...]\n"
    "Args    : periodictime  Periodic time from start sending the current\n"
    "                        block (means a character or a line) to start\n"
    "                        sending the next block.\n"
    "                        The unit of the periodic time is millisecond\n"
    "                        defaultly. You can also designate the unit\n"
    "                        like '100ms'. Available units are 's', 'ms',\n"
    "                        'us', 'ns', and 'bps' or 'cps'.\n"
    "                        The maximum value is 2147483647 for all units.\n"
    "          file ........ filepath to be send (\"-\" means STDIN)\n"
    "Options : -c .......... (This is default.) Changes the periodic unit\n"
    "                        to character. This option defines that the\n"
    "                        periodic time is the time from sending the\n"
    "                        current character to sending the next one.\n"
    "          -l .......... Changes the periodic unit to line. This\n"
    "                        option defines that the periodic time is the\n"
    "                        time from sending the top character of the\n"
    "                        current line to sending the top character of\n"
    "                        the next line.\n"
    "Version : 2019-02-27 15:58:29 JST\n"
    "          (POSIX C language)\n"
    ,pszMypath+iPos);
  exit(1);
}

/*--- print warning message ----------------------------------------*/
void warning(const char* szFormat, ...) {
  va_list va      ;
  int     i       ;
  int     iPos = 0;
  for (i=0; *(pszMypath+i)!='\0'; i++) {
    if (*(pszMypath+i)=='/') {iPos=i+1;}
  }
  va_start(va, szFormat);
  WRV("%s: ",pszMypath+iPos);
  vfprintf(stderr,szFormat,va);
  va_end(va);
  return;
}

/*--- exit with error message --------------------------------------*/
void error_exit(int iErrno, const char* szFormat, ...) {
  va_list va      ;
  int     i       ;
  int     iPos = 0;
  for (i=0; *(pszMypath+i)!='\0'; i++) {
    if (*(pszMypath+i)=='/') {iPos=i+1;}
  }
  va_start(va, szFormat);
  WRV("%s: ",pszMypath+iPos);
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
int64_t  i8Peritime;   /* Periodic time in nanosecond     */
int      iUnit;        /* 0:character 1:line 2-:undefined */
int      iRet;         /* return code                     */
char     *pszPath;     /* filepath on arguments           */
char     *pszFilename; /* filepath (for message)          */
int      iFileno;      /* file# of filepath               */
int      iFd;          /* file descriptor                 */
FILE     *fp;          /* file handle                     */
char     szBuf[256];   /* all-purpose char                */
int      i;            /* all-purpose int                 */

/*--- Initialize ---------------------------------------------------*/
pszMypath = argv[0];
setlocale(LC_CTYPE, "");

/*=== Parse arguments ==============================================*/
iUnit=0;

/*--- Parse options which start by "-" -----------------------------*/
while ((i=getopt(argc, argv, "cl")) != -1) {
  switch (i) {
    case 'c': iUnit = 0; break;
    case 'l': iUnit = 1; break;
    default : print_usage_and_exit();
  }
}
argc -= optind-1;
argv += optind  ;

/*--- Parse the interval -------------------------------------------*/
if (argc < 2                                  ) {print_usage_and_exit();}
if ((i8Peritime=parse_periodictime(argv[0]))<0) {print_usage_and_exit();}
argc--;
argv++;

/*=== Switch buffer mode ===========================================*/
switch (iUnit) {
  case 0:
            if (setvbuf(stdout,NULL,_IONBF,0)!=0) {
              error_exit(1,"Failed to switch to unbuffered mode\n");
            }
            break;
  case 1:
            if (setvbuf(stdout,NULL,_IOLBF,0)!=0) {
              error_exit(1,"Failed to switch to line-buffered mode\n");
            }
            break;
  default:
            error_exit(1,"FATAL: Invalid unit type\n");
            break;
}

/*=== Each file loop ===============================================*/
iRet     =  0;
iFileno  =  0;
iFd      = -1;
while ((pszPath = argv[iFileno]) != NULL || iFileno == 0) {

  /*--- Open the input file ----------------------------------------*/
  if (pszPath == NULL || strcmp(pszPath, "-") == 0) {
    pszFilename = "stdin"                ;
    iFd         = STDIN_FILENO           ;
  } else                                            {
    pszFilename = pszPath                ;
    iFd         = open(pszPath, O_RDONLY);
  }
  if (iFd < 0) {
    iRet = 1;
    warning("%s: File open error\n",pszFilename);
    iFileno++;
    continue;
  }
  if (iFd == STDIN_FILENO) {
    fp = stdin;
    if (feof(stdin)) {clearerr(stdin);} /* Reset EOF condition when stdin */
  } else                   {
    fp = fdopen(iFd, "r");
  }

  /*--- Reading and writing loop -----------------------------------*/
  switch (iUnit) {
    case 0:
              while ((i=getc(fp)) != EOF) {
                spend_my_spare_time(i8Peritime);
                if (putchar(i)==EOF) {
                  error_exit(1,"Cannot write to STDOUT\n");
                }
              }
              break;
    case 1:
              while (1) {
                spend_my_spare_time(i8Peritime);
                if (read_1line(fp)==EOF) {break;}
              }
              break;
    default:
              error_exit(1,"FATAL: Invalid unit type\n");
  }

  if (pszPath == NULL) {break;}
  iFileno++;
}

/*=== Finish normally ==============================================*/
return(iRet);}



/*####################################################################
# Functions
####################################################################*/

/*=== Parse the periodic time ========================================
 * [ret] >= 0  : Interval value (in nanosecound)
 *       <  0  : It is not a value                                  */
int64_t parse_periodictime(char *pszArg) {

  /*--- Variables --------------------------------------------------*/
  char szVal[256]       ;
  char *pszUnit         ;
  int  iLen, iVlen, iVal;
  int  iVlen_max        ;
  int  i                ;

  /*--- Get the lengths for the argument ---------------------------*/
  if ((iLen=strlen(pszArg))>=256) {return -1;}
  iVlen_max=sprintf(szVal,"%d",MAX_PERIODICVAL);

  /*--- Try to interpret the argument as "<value>"+"unit" ----------*/
  for (iVlen=0; iVlen<iLen; iVlen++) {
    if (pszArg[iVlen]<'0' || pszArg[iVlen]>'9'){break;}
    szVal[iVlen] = pszArg[iVlen];
  }
  szVal[iVlen] = (int)NULL;
  if (iVlen==0 || iVlen>iVlen_max                             ) {return -1;}
  if (sscanf(szVal,"%d",&iVal) != 1                           ) {return -1;}
  if ((strlen(szVal)==iVlen_max) && (iVal<(MAX_PERIODICVAL/2))) {return -1;}
  pszUnit = pszArg + iVlen;

  /* as a second value */
  if (strcmp(pszUnit, "s"  )==0) {return (int64_t)iVal*1000000000;           }

  /* as a millisecond value */
  if (strlen(pszUnit)==0 || strcmp(pszUnit, "ms")==0) {
                                  return (int64_t)iVal*1000000;              }

  /* as a microsecond value */
  if (strcmp(pszUnit, "us" )==0) {return (int64_t)iVal*1000;                 }

  /* as a nanosecond value */
  if (strcmp(pszUnit, "ns" )==0) {return (int64_t)iVal;                      }

  /* as a bps value (1charater=8bit) */
  if (strcmp(pszUnit, "bps")==0) {return ( 80000000000LL/(int64_t)iVal+5)/10;}

  /* as a cps value (1charater=10bit) */
  if (strcmp(pszUnit, "cps")==0) {return (100000000000LL/(int64_t)iVal+5)/10;}

  /*--- Otherwise, it is not a value -------------------------------*/
  return -1;
}

/*=== Read and write only one line ===================================
 * [ret] 0   : Finished reading and writing by reading a '\n'
 *       EOF : Finished reading and writing due to EOF              */
int read_1line(FILE *fp) {

  /*--- Variables --------------------------------------------------*/
  static int iHold = 0; /* set 1 if next character is currently held */
  static int iNextchar; /* variable for the next character           */
  int        iChar0, iChar;

  /*--- Reading and writing a line ---------------------------------*/
  while (1) {
    if (iHold) {iChar=iNextchar; iHold=0;} else {iChar=getc(fp);}
    switch (iChar) {
      case EOF:
                  return(EOF);
                  break;
      case '\n':
                  if (putchar('\n' )==EOF) {
                    error_exit(1,"Cannot write to STDOUT\n");
                  }
                  iNextchar = getc(fp);
                  if (iNextchar==EOF) {        return(EOF);}
                  else                {iHold=1;return(  0);}
                  break;
      default:
                  if (putchar(iChar)==EOF) {
                    error_exit(1,"Cannot write to STDOUT\n");
                  }
    }
  }
}

/*=== Sleep until the next interval period =========================*/
void spend_my_spare_time(int64_t i8Periodic_nsec) {

  /*--- Variables --------------------------------------------------*/
  static struct timespec tsPrev = {0,0}; /* the time when this func called last time */
  struct timespec        tsNow         ;
  struct timespec        tsTo          ;
  struct timespec        tsDiff        ;

  int                    iRet          ;
  uint64_t               ui8           ;

  /*--- Calculate "tsTo", the time until which I have to wait ------*/
  ui8 = (uint64_t)tsPrev.tv_nsec + i8Periodic_nsec;
  tsTo.tv_sec  = tsPrev.tv_sec + (time_t)(ui8/1000000000);
  tsTo.tv_nsec = (long)(ui8%1000000000);

  /*--- If the "tsTo" has been already past, set the current time into
   *    "tsPrev" and return immediately                             */
  if (clock_gettime(CLOCK_MONOTONIC, &tsNow) != 0) {
    error_exit(1,"Error happend while clock_gettime()\n");
  }
  if ((tsTo.tv_nsec - tsNow.tv_nsec) < 0) {
    tsDiff.tv_sec  = tsTo.tv_sec  - tsNow.tv_sec  -          1;
    tsDiff.tv_nsec = tsTo.tv_nsec - tsNow.tv_nsec + 1000000000;
  } else {
    tsDiff.tv_sec  = tsTo.tv_sec  - tsNow.tv_sec ;
    tsDiff.tv_nsec = tsTo.tv_nsec - tsNow.tv_nsec;
  }
  if (tsDiff.tv_sec < 0) {
    tsPrev.tv_sec  = tsNow.tv_sec ;
    tsPrev.tv_nsec = tsNow.tv_nsec;
    return;
  }

  /*--- Sleep until the next interval period -----------------------*/
  iRet = nanosleep(&tsDiff, NULL);
  if (iRet != 0) {error_exit(1,"Error happend while nanosleeping\n");}

  /*--- Finish this function ---------------------------------------*/
  tsPrev.tv_sec  = tsTo.tv_sec ;
  tsPrev.tv_nsec = tsTo.tv_nsec;
  return;
}
