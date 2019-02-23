/*####################################################################
#
# VALVE - Adjust the Flow Rate of UNIX Pipe Stream
#
# USAGE   : valve [-cl] perioictime [file ...]
# Args    : perioictime . Perioic time in millisecond from start
#                         sending the current unit (means a character
#                         or a line) to start sending the next unit.
#                         The range of it is from 0 to 2147483647
#                         (about 597 hours).
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
# Written by Shell-Shoccar Japan (@shellshoccarjpn) on 2019-02-24
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
#define MAX_INTERVAL 2147483647

/*--- prototype functions ------------------------------------------*/
void wait_intervally(uint64_t iInterval_msec);

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
  WRV("USAGE   : %s [-cl] perioictime [file ...]",pszMypath+iPos              );
  WRN("Args    : perioictime . Perioic time in millisecond from start\n"      );
  WRN("                        sending the current unit (means a character\n" );
  WRN("                        or a line) to start sending the next unit.\n"  );
  WRN("                        The range of it is from 0 to 2147483647\n"     );
  WRN("                        (about 597 hours).\n"                          );
  WRN("          file ........ filepath to be send (\"-\" means STDIN)\n"     );
  WRN("Options : -c .......... (This is default.) Changes the periodic unit\n");
  WRN("                        to character. This option defines that the\n"  );
  WRN("                        periodic time is the time from sending the\n"  );
  WRN("                        current character to sending the next one.\n"  );
  WRN("          -l .......... Changes the periodic unit to line. This\n"     );
  WRN("                        option defines that the periodic time is the\n");
  WRN("                        time from sending the top character of the\n"  );
  WRN("                        current line to sending the top character of\n");
  WRN("                        the next line.\n"                              );
  WRN("Version : 2019-02-24 00:38:57 JST\n"                                   );
  WRN("          (POSIX C language)\n"                                        );
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
int  iInterval;
int  iUnit;        /* 0:character 1:line 2-:undefined */
int  iRet;         /* return code                     */
char *pszPath;     /* filepath on arguments           */
char *pszFilename; /* filepath (for message)          */
int  iFileno;      /* file# of filepath               */
int  iFd;          /* file descriptor                 */
FILE *fp;          /* file handle                     */
char szBuf[256];   /* all-purpose char                */
int  i;            /* all-purpose int                 */

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
if (argc < 2                            ) {print_usage_and_exit();}
i=sprintf(szBuf,"%d",MAX_INTERVAL);
if (strlen(argv[0]) > i                 ) {print_usage_and_exit();}
if (sscanf(argv[0],"%d",&iInterval) != 1) {print_usage_and_exit();}
if ((strlen(argv[0])==i        )&&
    (iInterval<(MAX_INTERVAL/2))        ) {print_usage_and_exit();}
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
                wait_intervally(iInterval);
                if (putchar(i)==EOF) {
                  error_exit(1,"Cannot write to STDOUT\n");
                }
              }
              break;
    case 1:
              while (1) {
                wait_intervally(iInterval);
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

/*=== Wait until the next interval =================================*/
void wait_intervally(uint64_t iInterval_msec) {

  /*--- Variables --------------------------------------------------*/
  static uint64_t i8Prev = 0; /* the time when this func called last time */
  uint64_t        i8Now     ;
  uint64_t        i8To      ;

  int             iRet;

  struct timespec ts;
  const struct timespec tsSleep = {0, 100000}; /* 0.1msec */

  /*--- Calculate "i8To", the time until which I have to wait ------*/
  i8To = i8Prev + iInterval_msec;

  /*--- If the "i8To" has been already past, set the current time into
   *    "i8Prev" and return immediately                             */
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
    error_exit(1,"Error happend while clock_gettime()\n");
  }
  i8Now = ((uint64_t)ts.tv_sec)*1000+(ts.tv_nsec/1000000);
  if (i8Now >= i8To) { i8Prev=i8Now; return; }

  /*--- Waiting loop -----------------------------------------------*/
  while (i8Now < i8To) {

    /* Sleep for a moment */
    iRet = nanosleep(&tsSleep, NULL);
    if (iRet != 0) {error_exit(1,"Error happend while nanosleeping\n");}

    /* Get the current time */
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
      error_exit(1,"Error happend while clock_gettime()\n");
    }
    i8Now = ((uint64_t)ts.tv_sec)*1000+(ts.tv_nsec/1000000);
  }

  /*--- Finish waiting ---------------------------------------------*/
  i8Prev = i8To;
  return;
}
