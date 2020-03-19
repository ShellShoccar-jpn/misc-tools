/*####################################################################
#
# VALVE - Adjust the UNIX Pipe Streaming Speed
#
# USAGE   : valve [-c|-l] [-r|-s] [-p n] periodictime [file ...]
#           valve [-c|-l] [-r|-s] [-p n] controlfile [file ...]
# Args    : periodictime  Periodic time from start sending the current
#                         block (means a character or a line) to start
#                         sending the next block.
#                         The unit of the periodic time is millisecond
#                         defaultly. You can also specify the unit
#                         like '100ms'. Available units are 's', 'ms',
#                         'us', 'ns'.
#                         You can also specify it by the units/words.
#                          - speed  : 'bps' (regards as 1charater= 8bit)
#                                     'cps' (regards as 1charater=10bit)
#                          - output : '0%'   (completely shut the value)
#                                     '100%' (completely open the value)
#                         The maximum value is INT_MAX for all units.
#           controlfile . Filepath to specify the periodic time instead
#                         of by argument. The word you can specify in
#                         this file is completely the same as the argu-
#                         ment.
#                         However, you can re-specify the time by over-
#                         writing the file. This command will read the
#                         new periodic time in 0.1 second after that.
#                         If you want to make this command read it im-
#                         mediately, send SIGHUP. (On macOS and OpenBSD,
#                         SIGALRM is used for it)
#           file ........ Filepath to be send ("-" means STDIN)
# Options : -c .......... (Default) Changes the periodic unit to
#                         character. This option defines that the
#                         periodic time is the time from sending the
#                         current character to sending the next one.
#                         -l option will be disabled by this option.
#           -l .......... Changes the periodic unit to line. This
#                         option defines that the periodic time is the
#                         time from sending the top character of the
#                         current line to sending the top character of
#                         the next line.
#                         -c option will be disabled by this option.
#           [The following options are for professional]
#           -r .......... (Default) Recovery mode
#                         On low spec computers, nanosleep() often over-
#                         sleeps too much and that causes lower throughput
#                         than specified. This mode makes this command
#                         recover the lost time by cutting down on sleep
#                         time.
#                         -s option will be disabled by this option.
#           -s .......... Strict mode
#                         Recovering the lost time causes the maximum
#                         instantaneous speed to be exeeded. It maybe
#                         affect badly for devices which have little
#                         buffer. So, this mode makes this command keep
#                         strictly the maximum instantaneous speed limit
#                         decided by periodictime.
#                         -r option will be disabled by this option.
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
# Note    : [What's "#ifndef NOTTY" for?]
#             That is to avoid any unknown side effects by supporting
#             TTY devices on the control file. If you are in some
#             trouble by that, try to compile with #define NOTTY as
#             follows.
#               $ gcc -DNOTTY -o valve valve.c
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
#include <limits.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <fcntl.h>
#include <signal.h>
#include <locale.h>
#if defined(_POSIX_PRIORITY_SCHEDULING) && !defined(__OpenBSD__) && !defined(__APPLE__)
  #include <sched.h>
  #include <sys/resource.h>
#endif

/*--- macro constants ----------------------------------------------*/
/* Interval time for looking at the file which Preriodic time is written */
#define FREAD_ITRVL_SEC  0
#define FREAD_ITRVL_USEC 100000
#define FREAD_ITRVL_NSEC 100000000
/* Buffer size for the control file */
#define CTRL_FILE_BUF 64
/* If you set the following definition to 2 or more, recovery mode will be
 * probably more effective. If unnecessary, set 0 to disable this.         */
#define RECOVMAX_MULTIPLIER 2
#if !defined(CLOCK_MONOTONIC)
  #define CLOCK_FOR_ME CLOCK_REALTIME /* for HP-UX */
#elif defined(__sun) || defined(__SunOS)
  /* CLOCK_MONOTONIC on Solaris requires privillege */
  #define CLOCK_FOR_ME CLOCK_REALTIME
#else
  #define CLOCK_FOR_ME CLOCK_MONOTONIC
#endif
#if !defined(__APPLE__) && !defined(__OpenBSD__)
  #define SIG_FOR_ME SIGHUP
#else
  #define SIG_FOR_ME SIGALRM
#endif

/*--- prototype functions ------------------------------------------*/
int64_t parse_periodictime(char *pszArg);
int change_to_rtprocess(int iPrio);
void spend_my_spare_time(struct timespec *ptsPrev);
int read_1line(FILE *fp, struct timespec *ptsGet1stchar);
void update_periodic_time_type_r(int iSig, siginfo_t *siInfo, void *pct);
#ifndef NOTTY
  void update_periodic_time_type_c(int iSig, siginfo_t *siInfo, void *pct);
#endif

/*--- global variables ---------------------------------------------*/
char*    gpszCmdname;     /* The name of this command                        */
int64_t  gi8Peritime;     /* Periodic time in nanosecond (-1 means infinity) */
int      giFd_ctrlfile;   /* File descriptor of the control file             */
struct sigaction gsaIgnr; /* for ignoring signals during signal handlers     */
struct sigaction gsaAlrm; /* for signal trap definition (action)             */
int      giRecovery;      /* 0:normal 1:Recovery mode                        */
int      giVerbose;       /* speaks more verbosely by the greater number     */

/*=== Define the functions for printing usage and error ============*/

/*--- exit with usage ----------------------------------------------*/
void print_usage_and_exit(void) {
  fprintf(stderr,
#if defined(_POSIX_PRIORITY_SCHEDULING) && !defined(__OpenBSD__) && !defined(__APPLE__)
    "USAGE   : %s [-c|-l] [-r|-s] [-p n] periodictime [file ...]\n"
    "          %s [-c|-l] [-r|-s] [-p n] controlfile [file ...]\n"
#else
    "USAGE   : %s [-c|-l] [-r|-s] periodictime [file ...]\n"
    "          %s [-c|-l] [-r|-s] controlfile [file ...]\n"
#endif
    "Args    : periodictime  Periodic time from start sending the current\n"
    "                        block (means a character or a line) to start\n"
    "                        sending the next block.\n"
    "                        The unit of the periodic time is millisecond\n"
    "                        defaultly. You can also specify the unit\n"
    "                        like '100ms'. Available units are 's', 'ms',\n"
    "                        'us', 'ns'.\n"
    "                        You can also specify it by the units/words.\n"
    "                         - speed  : 'bps' (regards as 1charater= 8bit)\n"
    "                                    'cps' (regards as 1charater=10bit)\n"
    "                         - output : '0%%'   (completely shut the value)\n"
    "                                    '100%%' (completely open the value)\n"
    "                        The maximum value is INT_MAX for all units.\n"
#ifdef NOTTY
    "                        (Regular file only for it on the version)\n"
#endif
    "          controlfile . Filepath to specify the periodic time instead\n"
    "                        of by argument. The word you can specify in\n"
    "                        this file is completely the same as the argu-\n"
    "                        ment.\n"
    "                        However, you can re-specify the time by over-\n"
    "                        writing the file. This command will read the\n"
    "                        new periodic time in 0.1 second after that.\n"
    "                        If you want to make this command read it im-\n"
    "                        mediately, send SIGHUP. (On macOS and OpenBSD,\n"
    "                        SIGALRM is used for it)\n"
    "          file ........ Filepath to be send (\"-\" means STDIN)\n"
    "Options : -c .......... (Default) Changes the periodic unit to\n"
    "                        character. This option defines that the\n"
    "                        periodic time is the time from sending the\n"
    "                        current character to sending the next one.\n"
    "                        -l option will be disabled by this option.\n"
    "          -l .......... Changes the periodic unit to line. This\n"
    "                        option defines that the periodic time is the\n"
    "                        time from sending the top character of the\n"
    "                        current line to sending the top character of\n"
    "                        the next line.\n"
    "                        -c option will be disabled by this option.\n"
    "          [The following options are for professional]\n"
    "          -r .......... (Default) Recovery mode \n"
    "                        On low spec computers, nanosleep() often over-\n"
    "                        sleeps too much and that causes lower throughput\n"
    "                        than specified. This mode makes this command\n"
    "                        recover the lost time by cutting down on sleep\n"
    "                        time.\n"
    "                        -s option will be disabled by this option.\n"
    "          -s .......... Strict mode\n"
    "                        Recovering the lost time causes the maximum\n"
    "                        instantaneous speed to be exeeded. It maybe\n"
    "                        affect badly for devices which have little\n"
    "                        buffer. So, this mode makes this command keep\n"
    "                        strictly the maximum instantaneous speed limit\n"
    "                        decided by periodictime.\n"
    "                        -r option will be disabled by this option.\n"
#if defined(_POSIX_PRIORITY_SCHEDULING) && !defined(__OpenBSD__) && !defined(__APPLE__)
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
    ,gpszCmdname,gpszCmdname);
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
int      iUnit;           /* 0:character 1:line 2-:undefined       */
int      iPrio;           /* -p option number (default 1)          */
int      iRet;            /* return code                           */
int      iRet_r1l;        /* return value by read_1line()          */
char    *pszPath;         /* filepath on arguments                 */
char    *pszFilename;     /* filepath (for message)                */
int      iFileno;         /* file# of filepath                     */
int      iFd;             /* file descriptor                       */
FILE    *fp;              /* file handle                           */
struct timespec ts1st;    /* the time when the 1st char was got
                             (only for line mode)                  */
int      i;               /* all-purpose int                       */
#if !defined(__APPLE__) && !defined(__OpenBSD__)
  struct itimerspec itInt;  /* for signal trap definition (interval) */
  struct sigevent   seInf;  /* for interval event definition         */
  timer_t           trId;   /* signal timer ID                       */
#else
  struct itimerval  itInt;  /* for signal trap definition (interval) */
#endif
struct stat stCtrlfile;   /* stat for the control file             */

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
iUnit     =0;
iPrio     =1;
giVerbose =0;
giRecovery=1;
/*--- Parse options which start by "-" -----------------------------*/
while ((i=getopt(argc, argv, "clp:rsvh")) != -1) {
  switch (i) {
    case 'c': iUnit = 0;      break;
    case 'l': iUnit = 1;      break;
#if defined(_POSIX_PRIORITY_SCHEDULING) && !defined(__OpenBSD__) && !defined(__APPLE__)
    case 'p': if (sscanf(optarg,"%d",&iPrio) != 1) {print_usage_and_exit();}
              break;
#endif
    case 'r': giRecovery = 1; break;
    case 's': giRecovery = 0; break;
    case 'v': giVerbose++;    break;
    case 'h': print_usage_and_exit();
    default : print_usage_and_exit();
  }
}
argc -= optind-1;
argv += optind  ;
if (giVerbose>0) {warning("verbose mode (level %d)\n",giVerbose);}
#if RECOVMAX_MULTIPLIER > 0
  if (giVerbose>0) {warning("RECOVMAX_MULTIPLIER is %d\n",RECOVMAX_MULTIPLIER);}
#endif

/*--- Parse the periodic time ----------------------------------------*/
if (argc < 2         ) {print_usage_and_exit();}
gi8Peritime = parse_periodictime(argv[0]);
if (gi8Peritime <= -2) {
  /* Make sure that the ontrol file has an acceptable type */
  if ((i=stat(argv[0],&stCtrlfile)) < 0) {
    error_exit(errno,"%s: %s\n",argv[0],strerror(errno));  
  }
  switch (stCtrlfile.st_mode & S_IFMT) {
    case S_IFREG : break;
#ifndef NOTTY
    case S_IFCHR : break;
    case S_IFIFO : break;
#endif
    default      : error_exit(255,"%s: Unsupported file type\n",argv[0]);
  }

  /* set the first parameter, which is "0%" */
  gi8Peritime = -1;

  /* (a) for a regular file */
  if ((stCtrlfile.st_mode & S_IFREG) == S_IFREG) {
    /* Open the file */
    if ((giFd_ctrlfile=open(argv[0],O_RDONLY)) < 0){
      error_exit(errno,"%s: %s\n",argv[0],strerror(errno));
    }

    /* Register the signal trap */
    memset(&gsaIgnr, 0, sizeof(gsaIgnr));
    gsaIgnr.sa_handler   = SIG_IGN;
    gsaIgnr.sa_flags     = SA_NODEFER;
    memset(&gsaAlrm, 0, sizeof(gsaAlrm));
    gsaAlrm.sa_sigaction = update_periodic_time_type_r;
    gsaAlrm.sa_flags     = SA_SIGINFO;
    if (sigaction(SIG_FOR_ME,&gsaAlrm,NULL) != 0) {
      error_exit(errno,"sigaction() in main() #a: %s\n",strerror(errno));
    }
#ifndef NOTTY
  } else {
  /* (b) for a character special file or a named pipe */
    /* Open the file */
    if ((giFd_ctrlfile=open(argv[0],O_RDONLY | O_NONBLOCK )) < 0){
      error_exit(errno,"%s: %s\n",argv[0],strerror(errno));
    }

    /* Register the signal trap */
    memset(&gsaIgnr, 0, sizeof(gsaIgnr));
    gsaIgnr.sa_handler   = SIG_IGN;
    gsaIgnr.sa_flags     = SA_NODEFER;
    memset(&gsaAlrm, 0, sizeof(gsaAlrm));
    gsaAlrm.sa_sigaction = update_periodic_time_type_c;
    gsaAlrm.sa_flags     = SA_SIGINFO;
    if (sigaction(SIG_FOR_ME,&gsaAlrm,NULL) != 0) {
      error_exit(errno,"sigaction() in main() #b: %s\n",strerror(errno));
    }
#endif
  }

  /* Start sending signal pulses to the signal trap */
  #if !defined(__APPLE__) && !defined(__OpenBSD__)
    memset(&seInf, 0, sizeof(seInf));
    seInf.sigev_value.sival_int  = 0;
    seInf.sigev_notify           = SIGEV_SIGNAL;
    seInf.sigev_signo            = SIG_FOR_ME;
    if (timer_create(CLOCK_FOR_ME, &seInf, &trId)) {
      error_exit(errno,"timer_create(): %s\n" ,strerror(errno));
    }
    memset(&itInt, 0, sizeof(itInt));
    itInt.it_value.tv_sec     = FREAD_ITRVL_SEC;
    itInt.it_value.tv_nsec    = FREAD_ITRVL_NSEC;
    itInt.it_interval.tv_sec  = FREAD_ITRVL_SEC;
    itInt.it_interval.tv_nsec = FREAD_ITRVL_NSEC;
    if (timer_settime(trId,0,&itInt,NULL)) {
      error_exit(errno,"timer_settime(): %s\n",strerror(errno));
    }
  #else
    memset(&itInt, 0, sizeof(itInt));
    itInt.it_interval.tv_sec  = FREAD_ITRVL_SEC;
    itInt.it_interval.tv_usec = FREAD_ITRVL_USEC;
    itInt.it_value.tv_sec     = FREAD_ITRVL_SEC;
    itInt.it_value.tv_usec    = FREAD_ITRVL_USEC;
    if (setitimer(ITIMER_REAL,&itInt,NULL)) {
      error_exit(errno,"setitimer(): %s\n"    ,strerror(errno));
    }
  #endif
}
argc--;
argv++;

/*=== Switch buffer mode ===========================================*/
switch (iUnit) {
  case 0:
            if (setvbuf(stdout,NULL,_IONBF,0)!=0) {
              error_exit(255,"Failed to switch to unbuffered mode\n");
            }
            break;
  case 1:
            if (setvbuf(stdout,NULL,_IOLBF,0)!=0) {
              error_exit(255,"Failed to switch to line-buffered mode\n");
            }
            break;
  default:
            error_exit(255,"main() #1: Invalid unit type\n");
            break;
}

/*=== Try to make me a realtime process ============================*/
if (change_to_rtprocess(iPrio)==-1) {print_usage_and_exit();}

/*=== Each file loop ===============================================*/
iRet         =  0;
iFileno      =  0;
iFd          = -1;
iRet_r1l     =  0;
ts1st.tv_nsec= -1; /* -1 means that a valid time is not in yet */
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
  switch (iUnit) {
    case 0:
              while ((i=getc(fp)) != EOF) {
                spend_my_spare_time(NULL);
                while (putchar(i)==EOF) {
                  if (errno == EINTR) {continue;}
                  error_exit(errno,"main() #C1: %s\n",strerror(errno));
                }
              }
              break;
    case 1:
              if (ts1st.tv_nsec == -1) {
                iRet_r1l = read_1line(fp,&ts1st);
                spend_my_spare_time(&ts1st);
                if (iRet_r1l != 0) {break;}
              }
              while (1) {
                if ( iRet_r1l                      != EOF) {
                  spend_my_spare_time(NULL);
                }
                if ((iRet_r1l=read_1line(fp,NULL)) !=   0) {
                  break;
                }
              }
              break;
    default:
              error_exit(255,"main() #L1: Invalid unit type\n");
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

/*=== Parse the periodic time ========================================
 * [ret] >= 0  : Interval value (in nanosecound)
 *       <=-1  : Means infinity (completely shut the valve)
 *       <=-2  : It is not a value                                  */
int64_t parse_periodictime(char *pszArg) {

  /*--- Variables --------------------------------------------------*/
  char  szVal[CTRL_FILE_BUF]; /* string buffer for the value part in pszArg */
  char *pszUnit             ;
  int   iLen, iVlen, iVal   ;
  int   iVlen_max           ;

  /*--- Get the lengths for the argument ---------------------------*/
  if ((iLen=strlen(pszArg))>=CTRL_FILE_BUF            ) {return -2;}
  iVlen_max=sprintf(szVal,"%d",INT_MAX);

  /*--- Try to interpret the argument as "<value>"+"unit" ----------*/
  for (iVlen=0; iVlen<iLen; iVlen++) {
    if (pszArg[iVlen]<'0' || pszArg[iVlen]>'9'){break;}
    szVal[iVlen] = pszArg[iVlen];
  }
  szVal[iVlen] = '\0';
  if (iVlen==0 || iVlen>iVlen_max                     ) {return -2;}
  if (sscanf(szVal,"%d",&iVal) != 1                   ) {return -2;}
  if ((strlen(szVal)==iVlen_max) && (iVal<(INT_MAX/2))) {return -2;}
  pszUnit = pszArg + iVlen;

  /* as a second value */
  if (strcmp(pszUnit, "s"  )==0) {return (int64_t)iVal*1000000000;}

  /* as a millisecond value */
  if (strlen(pszUnit)==0 || strcmp(pszUnit, "ms")==0) {
                                  return (int64_t)iVal*1000000;   }

  /* as a microsecond value */
  if (strcmp(pszUnit, "us" )==0) {return (int64_t)iVal*1000;      }

  /* as a nanosecond value */
  if (strcmp(pszUnit, "ns" )==0) {return (int64_t)iVal;           }

  /* as a bps value (1charater=8bit) */
  if (strcmp(pszUnit, "bps")==0) {
    return (iVal!=0) ? ( 80000000000LL/(int64_t)iVal+5)/10 : -1;
  }

  /* as a cps value (1charater=10bit) */
  if (strcmp(pszUnit, "cps")==0) {
    return (iVal!=0) ? (100000000000LL/(int64_t)iVal+5)/10 : -1;
  }

  /* as a % value (only "0%" or "100%") */
  if (strcmp(pszUnit, "%")==0)   {
    switch (iVal) {
      case   0: return -1;
      case 100: return  0;
      default : return -2;
    }
  }

  /*--- Otherwise, it is not a value -------------------------------*/
  return -2;
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

/*=== Read and write only one line ===================================
 * [in] fp            : Filehandle for read
 *      ptsGet1stchar : If this is not null, the time when the top
 *                      character has been gotten will be set.
 * [ret] 0   : Finished reading/writing due to '\n'
 *       1   : Finished reading/writing due to '\n', which is the last
 *             char of the file
 *       EOF : Finished reading/writing due to EOF              */
int read_1line(FILE *fp, struct timespec *ptsGet1stchar) {

  /*--- Variables --------------------------------------------------*/
  static int iHold = 0; /* set 1 if next character is currently held */
  static int iNextchar; /* variable for the next character           */
  int        iChar;

  /*--- Reading and writing a line ---------------------------------*/
  if (iHold) {iChar=iNextchar; iHold=0;} else {iChar=getc(fp);}
  if (ptsGet1stchar != NULL) {
    if (clock_gettime(CLOCK_FOR_ME,ptsGet1stchar) != 0) {
      error_exit(errno,"clock_gettime() in read_1line(): %s\n",strerror(errno));
    }
  }
  while (1) {
    switch (iChar) {
      case EOF:
                  return(EOF);
      case '\n':
                  while (putchar('\n' )==EOF) {
                    if (errno == EINTR) {continue;}
                    error_exit(errno,"putchar() #R1L-1: %s\n",strerror(errno));
                  }
                  iNextchar = getc(fp);
                  if (iNextchar!=EOF) {iHold=1;return 0;}
                  else                {        return 1;}
      default:
                  while (putchar(iChar)==EOF) {
                    if (errno == EINTR) {continue;}
                    error_exit(errno,"putchar() #R1L-2: %s\n",strerror(errno));
                  }
    }
    if (iHold) {iChar=iNextchar; iHold=0;} else {iChar=getc(fp);}
  }
}

/*=== Sleep until the next interval period ===========================
 * [in] gi8Peritime : Periodic time (-1 means infinity)
        ptsPrev     : If not null, set it to tsPrev and exit immediately */
void spend_my_spare_time(struct timespec *ptsPrev) {

  /*--- Variables --------------------------------------------------*/
  static struct timespec tsPrev = {0,0}; /* the time when this func
                                            called last time        */
  static struct timespec tsRecovmax  = {0,0} ;
  struct timespec        tsNow               ;
  struct timespec        tsTo                ;
  struct timespec        tsDiff              ;

  static int64_t         i8LastPeritime  = -1;

  uint64_t               ui8                 ;

  /*--- Set tsPrev and exit if ptsPrev has a time ------------------*/
  if (ptsPrev) {
    tsPrev.tv_sec  = ptsPrev->tv_sec ;
    tsPrev.tv_nsec = ptsPrev->tv_nsec;
    i8LastPeritime = gi8Peritime;
    return;
  }

top:
  /*--- Reset tsPrev if gi8Peritime was changed --------------------*/
  if (gi8Peritime != i8LastPeritime) {
    tsPrev.tv_sec  = 0;
    tsPrev.tv_nsec = 0;
    i8LastPeritime = gi8Peritime;
  }

  /*--- If "gi8Peritime" is neg., sleep until a signal comes -----*/
  if (gi8Peritime<0) {
    tsDiff.tv_sec  = 86400;
    tsDiff.tv_nsec =     0;
    while (1) {
      if (nanosleep(&tsDiff,NULL) != 0) {
        if (errno != EINTR) {
          error_exit(errno,"nanosleep() #1: %s\n",strerror(errno));
        }
        if (clock_gettime(CLOCK_FOR_ME,&tsPrev) != 0) {
          error_exit(errno,"clock_gettime() #1: %s\n",strerror(errno));
        }
        goto top; /* Go to "top" in case of a signal trap */
      }
    }
  }

  /*--- Calculate "tsTo", the time until which I have to wait ------*/
  ui8 = (uint64_t)tsPrev.tv_nsec + gi8Peritime;
  tsTo.tv_sec  = tsPrev.tv_sec + (time_t)(ui8/1000000000);
  tsTo.tv_nsec = (long)(ui8%1000000000);

  /*--- If the "tsTo" has been already past, return immediately without sleep */
  if (clock_gettime(CLOCK_FOR_ME,&tsNow) != 0) {
    error_exit(errno,"clock_gettime() #2: %s\n",strerror(errno));
  }
  if ((tsTo.tv_nsec - tsNow.tv_nsec) < 0) {
    tsDiff.tv_sec  = tsTo.tv_sec  - tsNow.tv_sec  -          1;
    tsDiff.tv_nsec = tsTo.tv_nsec - tsNow.tv_nsec + 1000000000;
  } else {
    tsDiff.tv_sec  = tsTo.tv_sec  - tsNow.tv_sec ;
    tsDiff.tv_nsec = tsTo.tv_nsec - tsNow.tv_nsec;
  }
  if (tsDiff.tv_sec < 0) {
    if (giVerbose>2) {warning("overslept\n");}
    if (
         (tsDiff.tv_sec >=tsRecovmax.tv_sec )
          &&
         (tsDiff.tv_nsec> tsRecovmax.tv_nsec)
       )
    {
      /* Set the next periodic time if the delay is short or
       * the current file is a regular one                   */
      tsPrev.tv_sec  = tsTo.tv_sec ;
      tsPrev.tv_nsec = tsTo.tv_nsec;
    } else {
      /* Otherwise, reset tsPrev by the current time */
      if (giVerbose>1) {warning("give up recovery this time\n");}
      tsPrev.tv_sec  = tsNow.tv_sec ;
      tsPrev.tv_nsec = tsNow.tv_nsec;
    }
    return;
  }

  /*--- Sleep until the next interval period -----------------------*/
  if (nanosleep(&tsDiff,NULL) != 0) {
    if (errno == EINTR) {goto top;} /* Go to "top" in case of a signal trap */
    error_exit(errno,"nanosleep() #2: %s\n",strerror(errno));
  }

  /*--- Update the amount of threshold time for recovery -----------*/
  if (giRecovery) {
    /* investigate the current time again */
    if (clock_gettime(CLOCK_FOR_ME,&tsNow) != 0) {
      error_exit(errno,"clock_gettime() #3: %s\n",strerror(errno));
    }
    /* calculate the oversleeping time */
    if ((tsTo.tv_nsec - tsNow.tv_nsec) < 0) {
      tsDiff.tv_sec  = tsTo.tv_sec  - tsNow.tv_sec  -          1;
      tsDiff.tv_nsec = tsTo.tv_nsec - tsNow.tv_nsec + 1000000000;
    } else {
      tsDiff.tv_sec  = tsTo.tv_sec  - tsNow.tv_sec ;
      tsDiff.tv_nsec = tsTo.tv_nsec - tsNow.tv_nsec;
    }
    /* update tsRecovmax */
    if (
         (tsDiff.tv_sec< tsRecovmax.tv_sec)
          ||
         (
           (tsDiff.tv_sec ==tsRecovmax.tv_sec )
            &&
           (tsDiff.tv_nsec <tsRecovmax.tv_nsec)
         )
       )
    {
      tsRecovmax.tv_sec  = tsDiff.tv_sec ;
      tsRecovmax.tv_nsec = tsDiff.tv_nsec;
#if RECOVMAX_MULTIPLIER >= 2
      /* multiply */
      ui8 = (uint64_t)tsRecovmax.tv_nsec * RECOVMAX_MULTIPLIER;
      tsRecovmax.tv_nsec = (long)(ui8%1000000000);
      tsRecovmax.tv_sec  = tsRecovmax.tv_sec * RECOVMAX_MULTIPLIER
                         + (time_t)(ui8/1000000000)               ;
#endif
      if (giVerbose>0) {
        warning("tsRecovmax updated (%ld,%ld)\n",
                tsRecovmax.tv_sec,tsRecovmax.tv_nsec);
      }
    }
  }

  /*--- Finish this function ---------------------------------------*/
  tsPrev.tv_sec  = tsTo.tv_sec ;
  tsPrev.tv_nsec = tsTo.tv_nsec;
  return;
}

/*=== SIGNALTRAP : Try to update "gi8Peritime" for a regular file ====
 * [in] gi8Peritime   : (must be defined as a global variable)
 *      giFd_ctrlfile : File descriptor for the file which the periodic
 *                      time is written                             */
void update_periodic_time_type_r(int iSig, siginfo_t *siInfo, void *pct) {

  /*--- Variables --------------------------------------------------*/
  char    szBuf[CTRL_FILE_BUF];
  int     iLen                ;
  int     i                   ;
  int64_t i8                  ;

  /*--- Ignore multi calling ---------------------------------------*/
  if (iSig > 0) {
    if (sigaction(SIG_FOR_ME,&gsaIgnr,NULL) != 0) {
      error_exit(errno,"sigaction() in the trap #R1: %s\n",strerror(errno));
    }
  }

  while (1) {

    /*--- Try to read the time -------------------------------------*/
    if (lseek(giFd_ctrlfile,0,SEEK_SET) < 0                 ) {break;}
    if ((iLen=read(giFd_ctrlfile,szBuf,CTRL_FILE_BUF-1)) < 1) {break;}
    for (i=0;i<iLen;i++) {if(szBuf[i]=='\n'){break;}}
    szBuf[i]='\0';
    i8 = parse_periodictime(szBuf);
    if (i8 <= -2                                            ) {break;}

    /*--- Update the periodic time ---------------------------------*/
    gi8Peritime = i8;

  break;}

  /*--- Restore the signal action ----------------------------------*/
  if (iSig > 0) {
    if (sigaction(SIG_FOR_ME,&gsaAlrm,NULL) != 0) {
      error_exit(errno,"sigaction() in the trap #R2: %s\n",strerror(errno));
    }
  }
}

#ifndef NOTTY
/*=== SIGNALTRAP : Try to update "gi8Peritime" for a char-sp/FIFO file
 * [in] gi8Peritime   : (must be defined as a global variable)
 *      giFd_ctrlfile : File descriptor for the file which the periodic
 *                      time is written                                   */
void update_periodic_time_type_c(int iSig, siginfo_t *siInfo, void *pct) {

  /*--- Variables --------------------------------------------------*/
  static char szCmdbuf[CTRL_FILE_BUF] = {0};
  static char iCmdbuflen              =  0 ;
  char        szBuf[CTRL_FILE_BUF];
  int         iLen     ;
  int         i        ;
  int         iEntkeyed;  /* 1 means enter key has pressed */
  int         iOverflow;
  int         iDoBufClr;
  int64_t     i8       ;

  /*--- Ignore multi calling ---------------------------------------*/
  if (iSig > 0) {
    if (sigaction(SIG_FOR_ME,&gsaIgnr,NULL) != 0) {
      error_exit(errno,"sigaction() in the trap #C1: %s\n",strerror(errno));
    }
  }

  while (1) {

    /*--- Read out the source string of the periodic time ----------*/
    iDoBufClr=0;
    iOverflow=0;
    while ((iLen=read(giFd_ctrlfile,szBuf,CTRL_FILE_BUF-1))==CTRL_FILE_BUF-1) {
      /*Read away the buffer and quit if the string in the buffer is too large*/
      iOverflow=1;
    }
    if (iOverflow) {iDoBufClr=1; break;}
    if (iLen < 0 ) {iDoBufClr=1; break;} /* some error */
    iEntkeyed = 0;
    for (i=0;i<iLen;i++) {if(szBuf[i]=='\n'){iEntkeyed=1;break;}}
    szBuf[i]='\0';
    strncat(szCmdbuf, szBuf, CTRL_FILE_BUF-iCmdbuflen-1);
    iCmdbuflen = strlen(szCmdbuf);
    if (iCmdbuflen >= CTRL_FILE_BUF-1) {
      /*Throw away the buffer and quit if the command string is too large*/
      iDoBufClr=1;;
      break;
    }
    if (iEntkeyed == 0) {break;}

    /*--- Try to read the time ---------------------------------------*/
    i8 = parse_periodictime(szCmdbuf);
    if (i8 <= -2) {iDoBufClr=1; break;} /*Invalid periodic time*/

    /*--- Update the periodic time -----------------------------------*/
    gi8Peritime = i8  ;
    iDoBufClr=1;

  break;}
  if (iDoBufClr) {szCmdbuf[0]='\0'; iCmdbuflen=0;}

  /*--- Restore the signal action ----------------------------------*/
  if (iSig > 0) {
    if (sigaction(SIG_FOR_ME,&gsaAlrm,NULL) != 0) {
      error_exit(errno,"sigaction() in the trap #C2: %s\n",strerror(errno));
    }
  }
}
#endif
