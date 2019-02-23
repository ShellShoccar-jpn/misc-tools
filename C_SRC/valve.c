/*####################################################################
#
# VALVE - Adjust the Flow Rate of UNIX Pipe Stream
#
# USAGE   : valve [-l] millisecs [file ...]
# Args    : millisecs ... The number of milliseconds to start sending
#                         the next character since sending the current
#                         one (up to 2147483647, about 597 hours)
#           file ........ filepath to be send ("-" means STDIN)
# Options : -l .......... Changes the adjustment unit from character to
#                         line. If the option is set, the top character
#                         of the next line will be started sending at
#                         the milliseconds later since the top character
#                         of the current one.
# Retuen  : Return 0 only when finished successfully
#
# Written by Shell-Shoccar Japan (@shellshoccarjpn) on 2019-02-23
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
void wait_intervally(uint64_t nInterval_msec);

/*--- global variables ---------------------------------------------*/
char* pszMypath;

/*=== Define the functions for printing usage and error ============*/

/*--- exit with usage ----------------------------------------------*/
void print_usage_and_exit(void) {
  int  n;
  int  nPos = 0;
  for (n=0; *(pszMypath+n)!='\0'; n++) {
    if (*(pszMypath+n)=='/') {nPos=n+1;}
  }
  WRV("USAGE   : %s [-l] millisecs [file ...]\n",pszMypath+nPos               );
  WRN("Args    : millisecs ... The number of milliseconds to start sending\n" );
  WRN("                        the next character since sending the current\n");
  WRN("                        one (up to 2147483647, about 597 hours)\n"     );
  WRN("          file ........ filepath to be send (\"-\" means STDIN)\n"     );
  WRN("Options : -l .......... Changes the adjustment unit from character\n"  );
  WRN("                        to line. If the option is set, the top\n"      );
  WRN("                        character of the next line will be started\n"  );
  WRN("                        sending at the milliseconds later since the\n" );
  WRN("                        top character of the current one.\n"           );
  WRN("Retuen  : Return 0 only when finished successfully\n"                  );
  WRN("Version : 2019-02-23 14:55:11 JST\n"                                   );
  WRN("          (POSIX C language)\n"                                        );
  exit(1);
}

/*--- print warning message ----------------------------------------*/
void warning(const char* szFormat, ...) {
  va_list va      ;
  int     n       ;
  int     nPos = 0;
  for (n=0; *(pszMypath+n)!='\0'; n++) {
    if (*(pszMypath+n)=='/') {nPos=n+1;}
  }
  va_start(va, szFormat);
  WRV("%s: ",pszMypath+nPos);
  vfprintf(stderr,szFormat,va);
  va_end(va);
  return;
}

/*--- exit with error message --------------------------------------*/
void error_exit(int nErrno, const char* szFormat, ...) {
  va_list va      ;
  int     n       ;
  int     nPos = 0;
  for (n=0; *(pszMypath+n)!='\0'; n++) {
    if (*(pszMypath+n)=='/') {nPos=n+1;}
  }
  va_start(va, szFormat);
  WRV("%s: ",pszMypath+nPos);
  vfprintf(stderr,szFormat,va);
  va_end(va);
  exit(nErrno);
}


/*####################################################################
# Main
####################################################################*/

/*=== Initialization ===============================================*/
int main(int argc, char *argv[]) {

/*--- Variables ----------------------------------------------------*/
int  nInterval;
int  nUnit;        /* 0:character 1:line 2-:undefined */
int  nRet;         /* return code                     */
char *pszPath;     /* filepath on arguments           */
char *pszFilename; /* filepath (for message)          */
int  nFileno;      /* file# of filepath               */
int  nFd;          /* file descriptor                 */
FILE *fp;          /* file handle                     */
char szBuf[256];   /* all-purpose char                */
int  n;            /* all-purpose int                 */

/*--- Initialize ---------------------------------------------------*/
pszMypath = argv[0];
setlocale(LC_CTYPE, "");

/*=== Parse arguments ==============================================*/
nUnit=0;

/*--- Parse options which start by "-" -----------------------------*/
while ((n=getopt(argc, argv, "l")) != -1) {
  switch (n) {
    case 'l':
              nUnit = 1;
              break;
    default:
              print_usage_and_exit();
  }
}
argc -= optind-1;
argv += optind  ;

/*--- Parse the interval -------------------------------------------*/
if (argc < 2                            ) {print_usage_and_exit();}
n=sprintf(szBuf,"%d",MAX_INTERVAL);
if (strlen(argv[0]) > n                 ) {print_usage_and_exit();}
if (sscanf(argv[0],"%d",&nInterval) != 1) {print_usage_and_exit();}
if ((strlen(argv[0])==n        )&&
    (nInterval<(MAX_INTERVAL/2))        ) {print_usage_and_exit();}
argc--;
argv++;

/*=== Switch buffer mode ===========================================*/
switch (nUnit) {
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
nRet     =  0;
nFileno  =  0;
nFd      = -1;
while ((pszPath = argv[nFileno]) != NULL || nFileno == 0) {

  /*--- Open the input file ----------------------------------------*/
  if (pszPath == NULL || strcmp(pszPath, "-") == 0) {
    pszFilename = "stdin"                ;
    nFd         = STDIN_FILENO           ;
  } else                                            {
    pszFilename = pszPath                ;
    nFd         = open(pszPath, O_RDONLY);
  }
  if (nFd < 0) {
    nRet = 1;
    warning("%s: File open error\n",pszFilename);
    nFileno++;
    continue;
  }
  if (nFd == STDIN_FILENO) {
    fp = stdin;
    if (feof(stdin)) {clearerr(stdin);} /* Reset EOF condition when stdin */
  } else                   {
    fp = fdopen(nFd, "r");
  }

  /*--- Reading and writing loop -----------------------------------*/
  switch (nUnit) {
    case 0:
              while ((n=getc(fp)) != EOF) {
                wait_intervally(nInterval);
                if (putchar(n)==EOF) {
                  error_exit(1,"Cannot write to STDOUT\n");
                }
              }
              break;
    case 1:
              while (1) {
                wait_intervally(nInterval);
                if (read_1line(fp)==EOF) {break;}
              }
              break;
    default:
              error_exit(1,"FATAL: Invalid unit type\n");
  }

  if (pszPath == NULL) {break;}
  nFileno++;
}

/*=== Finish normally ==============================================*/
return(nRet);}



/*####################################################################
# Functions
####################################################################*/

/*=== Read and write only one line ===================================
 * [ret] 0   : Finished reading and writing by reading a '\n'
 *       EOF : Finished reading and writing due to EOF              */
int read_1line(FILE *fp) {

  /*--- Variables --------------------------------------------------*/
  static int nHold = 0; /* set 1 if next character is currently held */
  static int nNextchar; /* variable for the next character           */
  int        nChar0, nChar;

  /*--- Reading and writing a line ---------------------------------*/
  while (1) {
    if (nHold) {nChar=nNextchar; nHold=0;} else {nChar=getc(fp);}
    switch (nChar) {
      case EOF:
                  return(EOF);
                  break;
      case '\n':
                  if (putchar('\n' )==EOF) {
                    error_exit(1,"Cannot write to STDOUT\n");
                  }
                  nNextchar = getc(fp);
                  if (nNextchar==EOF) {        return(EOF);}
                  else                {nHold=1;return(  0);}
                  break;
      default:
                  if (putchar(nChar)==EOF) {
                    error_exit(1,"Cannot write to STDOUT\n");
                  }
    }
  }
}

/*=== Wait until the next interval =================================*/
void wait_intervally(uint64_t nInterval_msec) {

  /*--- Variables --------------------------------------------------*/
  static uint64_t n8Prev = 0; /* the time when this func called last time */
  uint64_t        n8Now     ;
  uint64_t        nTo       ;

  int             nRet;

  struct timespec ts;
  const struct timespec tsSleep = {0, 100000}; /* 0.1msec */

  /*--- Calculate "nTo", the time until which I have to wait -------*/
  nTo = n8Prev + nInterval_msec;

  /*--- If the "nTo" has been already past, set the current time into 
   *    "n8Prev" and return immediately                             */
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
    error_exit(1,"Error happend while clock_gettime()\n");
  }
  n8Now = ((uint64_t)ts.tv_sec)*1000+(ts.tv_nsec/1000000);
  if (n8Now >= nTo) { n8Prev=n8Now; return; }

  /*--- Waiting loop -----------------------------------------------*/
  n8Now = ((uint64_t)ts.tv_sec)*1000+(ts.tv_nsec/1000000);
  while (n8Now < nTo) {

    /* Sleep for a moment */
    nRet = nanosleep(&tsSleep, NULL);
    if (nRet != 0) {error_exit(1,"Error happend while nanosleeping\n");}

    /* Get the current time */
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
      error_exit(1,"Error happend while clock_gettime()\n");
    }
    n8Now = ((uint64_t)ts.tv_sec)*1000+(ts.tv_nsec/1000000);
  }

  /*--- Finish waiting ---------------------------------------------*/
  n8Prev = nTo;
  return;
}
