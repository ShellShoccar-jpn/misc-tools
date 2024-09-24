/*####################################################################
#
# VALVE - Adjust the Data Transfer Rate in the UNIX Pipeline
#
# USAGE   : valve [-c|-l] [-r|-s] [-p n] periodictime [file [...]]
#           valve [-c|-l] [-r|-s] [-p n] controlfile [file [...]]
# Args    : periodictime  Periodic time from start sending the current
#                         block (means a character or a line) to start
#                         sending the next block.
#                         The unit of the periodic time is second
#                         defaultly. You can also specify the unit
#                         like '100ms'. Available units are 's', 'ms',
#                         'us', 'ns'.
#                         You can also specify it by the units/words.
#                          - rate   : '[kMG]bps' (regards as 1chr= 8bit)
#                                     'cps' (regards as 1chr=10bit)
#                          - output : '0%'   (completely shut the value)
#                                     '100%' (completely open the value)
#                         The maximum value is INT_MAX for all units.
#           controlfile . Filepath to specify the periodic time instead
#                         of by argument. The word you can specify in
#                         this file is completely the same as the argu-
#                         ment.
#                         However, you can replace the time by over-
#                         writing the file. You can use a regular file,
#                         a fifo file, or a character special file. The
#                         latter two types are better because the UNIX
#                         compatible programs have to poll the regular
#                         file periodically to confirm whether its
#                         content has been updated. So, this command
#                         also accesses the controlfile every 0.1
#                         second if the controlfile is a regular file.
#                         If you want to notify me of the update im-
#                         mediately, send me the SIGHUP. Finally, you
#                         always have to overwrite the new parameter
#                         at the top of the file. Do not use the append
#                         mode because this command read the parameter
#                         from the top.
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
#                         instantaneous data-transfer rate to be exeeded.
#                         It maybe affect badly for devices which have
#                         little buffer. So, this mode makes this command
#                         keep strictly the maximum instantaneous data-
#                         transfer rate limit decided by periodictime.
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
# How to compile : cc -O3 -o __CMDNAME__ __SRCNAME__ -pthread -lrt
#                  (if it doesn't work)
# How to compile : cc -O3 -o __CMDNAME__ __SRCNAME__ -pthread
#
# Note    : [What's "#ifndef NOTTY" for?]
#             That is to avoid any unknown side effects by supporting
#             TTY devices on the control file. If you are in some
#             trouble by that, try to compile with #define NOTTY as
#             follows.
#               $ gcc -DNOTTY -o valve valve.c
#
# Written by Shell-Shoccar Japan (@shellshoccarjpn) on 2024-09-25
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
#include <pthread.h>
#include <signal.h>
#include <locale.h>
#if defined(_POSIX_PRIORITY_SCHEDULING) && !defined(__OpenBSD__) && !defined(__APPLE__)
  #include <sched.h>
  #include <sys/resource.h>
#endif

/*--- macro constants ----------------------------------------------*/
/* Interval time of looking at the parameter on the control file */
#define FREAD_ITRVL_SEC  0
#define FREAD_ITRVL_USEC 100000
/* Time to retry the applying the new parameter to the main thread */
#define RETRY_APPLY_SEC  0
#define RETRY_APPLY_NSEC 500000000
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

/*--- prototype functions ------------------------------------------*/
void* param_updater(void* pvArgs);
int64_t parse_periodictime(char *pszArg);
int change_to_rtprocess(int iPrio);
void spend_my_spare_time(struct timespec *ptsPrev);
int read_1line(FILE *fp, struct timespec *ptsGet1stchar);
void do_nothing(int iSig, siginfo_t *siInfo, void *pct);
void update_periodic_time_type_r(int iSig, siginfo_t *siInfo, void *pct);
#ifndef NOTTY
  void update_periodic_time_type_c(void);
#endif

/*--- global variables ---------------------------------------------*/
char*    gpszCmdname;     /* The name of this command                        */
pthread_t gtMain;         /* main thread ID                                  */
int64_t  gi8Peritime;     /* Periodic time in nanosecond (-1 means infinity) */
int      giPtApplied;     /* gi8Peritime is 0:"not applied yet" 1:"applied"  */
struct stat gstCtrlfile;  /* stat for the control file                       */
int      giFd_ctrlfile;   /* File descriptor of the control file             */
struct sigaction gsaAlrm; /* for signal trap definition (action)             */
int      giRecovery;      /* 0:normal 1:Recovery mode                        */
int      giVerbose;       /* speaks more verbosely by the greater number     */

/*=== Define the functions for printing usage and error ============*/

/*--- exit with usage ----------------------------------------------*/
void print_usage_and_exit(void) {
  fprintf(stderr,
#if defined(_POSIX_PRIORITY_SCHEDULING) && !defined(__OpenBSD__) && !defined(__APPLE__)
    "USAGE   : %s [-c|-l] [-r|-s] [-p n] periodictime [file [...]]\n"
    "          %s [-c|-l] [-r|-s] [-p n] controlfile [file [...]]\n"
#else
    "USAGE   : %s [-c|-l] [-r|-s] periodictime [file [...]]\n"
    "          %s [-c|-l] [-r|-s] controlfile [file [...]]\n"
#endif
    "Args    : periodictime  Periodic time from start sending the current\n"
    "                        block (means a character or a line) to start\n"
    "                        sending the next block.\n"
    "                        The unit of the periodic time is second\n"
    "                        defaultly. You can also specify the unit\n"
    "                        like '100ms'. Available units are 's', 'ms',\n"
    "                        'us', 'ns'.\n"
    "                        You can also specify it by the units/words.\n"
    "                         - rate   : '[kMG]bps' (regards as 1chr= 8bit)\n"
    "                                    'cps' (regards as 1chr=10bit)\n"
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
    "                        However, you can replace the time by over-\n"
    "                        writing the file. You can use a regular file,\n"
    "                        a fifo file, or a character special file. The\n"
    "                        latter two types are better because the UNIX\n"
    "                        compatible programs have to poll the regular\n"
    "                        file periodically to confirm whether its\n"
    "                        content has been updated. So, this command\n"
    "                        also accesses the controlfile every 0.1\n"
    "                        second if the controlfile is a regular file.\n"
    "                        If you want to notify me of the update im-\n"
    "                        mediately, send me the SIGHUP. Finally, you\n"
    "                        always have to overwrite the new parameter\n"
    "                        at the top of the file. Do not use the append\n"
    "                        mode because this command read the parameter\n"
    "                        from the top.\n"
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
    "                        instantaneous data-transfer rate to be exeeded.\n"
    "                        It maybe affect badly for devices which have\n"
    "                        little buffer. So, this mode makes this command\n"
    "                        keep strictly the maximum instantaneous data-\n"
    "                        transfer rate limit decided by periodictime.\n"
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
    "Version : 2024-09-25 01:28:21 JST\n"
    "          (POSIX C language)\n"
    "\n"
    "Shell-Shoccar Japan (@shellshoccarjpn), No rights reserved.\n"
    "This is public domain software. (CC0)\n"
    "\n"
    "The latest version is distributed at the following page.\n"
    "https://github.com/ShellShoccar-jpn/tokideli\n"
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
sigset_t ssMask;          /* blocking signal list for the main thread*/
int      iUnit;           /* 0:character 1:line 2-:undefined        */
int      iPrio;           /* -p option number (default 1)           */
struct sigaction saHup;   /* for signal trap definition (action)    */
pthread_t tSub;           /* subthread ID                           */
int      iRet;            /* return code                            */
int      iRet_r1l;        /* return value by read_1line()           */
char    *pszPath;         /* filepath on arguments                  */
char    *pszFilename;     /* filepath (for message)                 */
int      iFileno;         /* file# of filepath                      */
int      iFileno_opened;  /* number of the files opened successfully*/
int      iFd;             /* file descriptor                        */
FILE    *fp;              /* file handle                            */
struct timespec ts1st;    /* the time when the 1st char was got
                             (only for line mode)                   */
int      i;               /* all-purpose int                        */

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
giPtApplied = 0;
gi8Peritime = parse_periodictime(argv[0]);
if (gi8Peritime <= -2) {
  /* Set the initial parameter, which is "0%" */
  gi8Peritime = -1;
  /* If the argument might be a control file, start the subthread */
  if (stat(argv[0],&gstCtrlfile) < 0) {
    error_exit(errno,"%s: %s\n",argv[0],strerror(errno));
  }
  /* Set sig-blocking only if the subthread will use SIGALRM */
  if (gstCtrlfile.st_mode & S_IFREG) {
    if (sigemptyset(&ssMask) != 0) {
      error_exit(errno,"sigemptyset() #1 in main(): %s\n",strerror(errno));
    }
    if (sigaddset(&ssMask,SIGALRM) != 0) {
      error_exit(errno,"sigaddset() #1 in main(): %s\n",strerror(errno));
    }
    if ((i=pthread_sigmask(SIG_BLOCK,&ssMask,NULL)) != 0) {
      error_exit(i,"pthread_sigmask() #1 in main(): %s\n",strerror(i));
    }
  }
  /* Block the SIGHUP temporarily before SIGHUP handler setting */
  if (sigemptyset(&ssMask) != 0) {
    error_exit(errno,"sigemptyset() #2 in main(): %s\n",strerror(errno));
  }
  if (sigaddset(&ssMask,SIGHUP) != 0) {
    error_exit(errno,"sigaddset() #2 in main(): %s\n",strerror(errno));
  }
  if ((i=pthread_sigmask(SIG_BLOCK,&ssMask,NULL)) != 0) {
    error_exit(i,"pthread_sigmask() #2 in main(): %s\n",strerror(i));
  }
  /* Start the subthread */
  gtMain = pthread_self();
  if ((i=pthread_create(&tSub,NULL,&param_updater,(void*)argv[0])) != 0) {
    error_exit(i,"pthread_create() in main(): %s\n",strerror(i));
  }
  /* Register a SIGHUP handler to apply the new gi8Peritime  */
  memset(&saHup, 0, sizeof(saHup));
  sigemptyset(&saHup.sa_mask);
  saHup.sa_sigaction = do_nothing;
  saHup.sa_flags     = SA_SIGINFO | SA_RESTART;
  if (sigaction(SIGHUP,&saHup,NULL) != 0) {
    error_exit(errno,"sigaction() in main(): %s\n",strerror(errno));
  }
  if (sigemptyset(&ssMask) != 0) {
    error_exit(errno,"sigemptyset() #3 in main(): %s\n",strerror(errno));
  }
  if (sigaddset(&ssMask,SIGHUP) != 0) {
    error_exit(errno,"sigaddset() #3 in main(): %s\n",strerror(errno));
  }
  if ((i=pthread_sigmask(SIG_UNBLOCK,&ssMask,NULL)) != 0) {
    error_exit(i,"pthread_sigmask() #3 in main(): %s\n",strerror(i));
  }
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
iRet          =  0;
iFileno       =  0;
iFd           = -1;
iRet_r1l      =  0;
ts1st.tv_nsec = -1; /* -1 means that a valid time is not in yet */
iFileno_opened=  0;
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
  iFileno_opened++;

  /*--- Reading and writing loop -----------------------------------*/
  if (iFileno_opened==1 && gi8Peritime==-1) {spend_my_spare_time(NULL);}
  switch (iUnit) {
    case 0:
              while ((i=getc(fp)) != EOF) {
                spend_my_spare_time(NULL);
                while (putchar(i)==EOF) {
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
# Subthread (Parameter Updater)
####################################################################*/

/*=== Initialization ===============================================*/
void* param_updater(void* pvArgs) {

/*--- Variables ----------------------------------------------------*/
char*  pszCtrlfile;
sigset_t ssMask;           /* unblocking signal list                */
int    i;                  /* all-purpose int                       */
struct itimerval  itInt;   /* for signal trap definition (interval) */

/*=== Validate the control file ====================================*/
if (! pvArgs) {error_exit(255,"line #%d: Fatal error\n",__LINE__);}
pszCtrlfile = (char*)pvArgs;
/* Make sure that the control file has an acceptable type */
switch (gstCtrlfile.st_mode & S_IFMT) {
  case S_IFREG : break;
#ifndef NOTTY
  case S_IFCHR : break;
  case S_IFIFO : break;
#endif
  default      : error_exit(255,"%s: Unsupported file type\n",pszCtrlfile);
}

/*=== The routine when the control file is a regular file ==========*/
if (gstCtrlfile.st_mode & S_IFREG) {

  /*--- Unblock the SIGALRM ----------------------------------------*/
  if (sigemptyset(&ssMask) != 0) {
    error_exit(errno,"sigemptyset() in param_updater(): %s\n",strerror(errno));
  }
  if (sigaddset(&ssMask,SIGALRM) != 0) {
    error_exit(errno,"sigaddset() in param_updater(): %s\n",strerror(errno));
  }
  if ((i=pthread_sigmask(SIG_UNBLOCK,&ssMask,NULL)) != 0) {
    error_exit(i,"pthread_sigmask() in param_updater(): %s\n",strerror(i));
  }

  /*--- Open the file ----------------------------------------------*/
  if ((giFd_ctrlfile=open(pszCtrlfile,O_RDONLY)) < 0){
    error_exit(errno,"%s: %s\n",pszCtrlfile,strerror(errno));
  }

  /*--- Register the signal handler --------------------------------*/
  memset(&gsaAlrm, 0, sizeof(gsaAlrm));
  sigemptyset(&gsaAlrm.sa_mask);
  sigaddset(&gsaAlrm.sa_mask, SIGALRM);
  gsaAlrm.sa_sigaction = update_periodic_time_type_r;
  gsaAlrm.sa_flags     = SA_SIGINFO | SA_RESTART;
  if (sigaction(SIGALRM,&gsaAlrm,NULL) != 0) {
    error_exit(errno,"sigaction() in param_updater(): %s\n",strerror(errno));
  }

  /*--- Register a signal pulse and start it -----------------------*/
  memset(&itInt, 0, sizeof(itInt));
  itInt.it_interval.tv_sec  = FREAD_ITRVL_SEC;
  itInt.it_interval.tv_usec = FREAD_ITRVL_USEC;
  itInt.it_value.tv_sec     = FREAD_ITRVL_SEC;
  itInt.it_value.tv_usec    = FREAD_ITRVL_USEC;
  if (setitimer(ITIMER_REAL,&itInt,NULL)) {
    error_exit(errno,"setitimer(): %s\n"    ,strerror(errno));
  }

  /*--- Sleep infinitely -------------------------------------------*/
  while(1) {pause();}

/*=== The routine when the control file is a character special file */
#ifndef NOTTY
} else {
  /*--- Open the file ----------------------------------------------*/
  if ((giFd_ctrlfile=open(pszCtrlfile,O_RDONLY)) < 0) {
    error_exit(errno,"%s: %s\n",pszCtrlfile,strerror(errno));
  }

  /*--- Read the file and update the parameter continuously --------*/
  update_periodic_time_type_c();
#endif
}

/*=== End of the subthread (does not come here) ====================*/
return NULL;}




/*####################################################################
# Functions
####################################################################*/

/*=== Parse the periodic time ========================================
 * [ret] >= 0  : Interval value (in nanosecound)
 *       <=-1  : Means infinity (completely shut the valve)
 *       <=-2  : It is not a value                                  */
int64_t parse_periodictime(char *pszArg) {

  /*--- Variables --------------------------------------------------*/
  char   szUnit[CTRL_FILE_BUF];
  double dNum;

  /*--- Check the lengths of the argument --------------------------*/
  if (strlen(pszArg)>=CTRL_FILE_BUF) {return -2;}

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

  /* as a bps value (1charater=8bit) */
  if (strcmp(szUnit, "bps")==0) {
    if (dNum >  8000000000LL) {return -2;}
    return (dNum!=0) ? (int64_t)( 8000000000LL/dNum) : -1;
  }
  if (strcmp(szUnit, "kbps")==0) {
    if (dNum >     8000000  ) {return -2;}
    return (dNum!=0) ? (int64_t)(    8000000  /dNum) : -1;
  }
  if (strcmp(szUnit, "Mbps")==0) {
    if (dNum >        8000  ) {return -2;}
    return (dNum!=0) ? (int64_t)(       8000  /dNum) : -1;
  }
  if (strcmp(szUnit, "Gbps")==0) {
    if (dNum >           8  ) {return -2;}
    return (dNum!=0) ? (int64_t)(          8  /dNum) : -1;
  }

  /* as a cps value (1charater=10bit) */
  if (strcmp(szUnit, "cps")==0) {
    if (dNum > 10000000000LL) {return -2;}
    return (dNum!=0) ? (int64_t)(10000000000LL/dNum) : -1;
  }

  /* as a % value (only "0%" or "100%") */
  if (strcmp(szUnit, "%")==0)   {
    if      (dNum==100) {return  0;}
    else if (dNum==  0) {return -1;}
    else                {return -2;}
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
                    error_exit(errno,"putchar() #R1L-1: %s\n",strerror(errno));
                  }
                  iNextchar = getc(fp);
                  if (iNextchar!=EOF) {iHold=1;return 0;}
                  else                {        return 1;}
      default:
                  while (putchar(iChar)==EOF) {
                    error_exit(errno,"putchar() #R1L-2: %s\n",strerror(errno));
                  }
    }
    if (iHold) {iChar=iNextchar; iHold=0;} else {iChar=getc(fp);}
  }
}

/*=== Sleep until the next interval period ===========================
 * [in]  gi8Peritime : Periodic time (-1 means infinity)
         ptsPrev     : If not null, set it to tsPrev and exit immediately
   [out] giPtApplied : This variable will be turned to 1 when the sleeping
                       time has been calculated with the gi8Peritime */
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
    giPtApplied    =     1;
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
  giPtApplied  = 1;

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
    if (errno == EINTR) {
      i8LastPeritime=gi8Peritime;
      goto top; /* Go to "top" in case of a signal trap */
    }
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

/*=== SIGNALTRAP : Do nothing ========================================
 * This function does nothing. However, it works effectively when you
 * want to interrupt the system calls and function using them and make
 * them stop with errno=EINTR.
 * For this command, it is useful to make the nanosleep() stop sleeping
 * and re-sleep for the new duration.                               */
void do_nothing(int iSig, siginfo_t *siInfo, void *pct) {
  if (giVerbose>0) {warning("gi8Peritime=%ld\n",gi8Peritime);}
  return;
}

/*=== SIGNALTRAP : Try to update "gi8Peritime" for a regular file ====
 * [in]  gi8Peritime   : (must be defined as a global variable)
 *       giFd_ctrlfile : File descriptor for the file which the periodic
 *                       time is written
 *       gtMain        : The main thread ID
 *       giPtApplied   : This variable will be turned to 1 when the sleeping
 *                       time has been calculated with the gi8Peritime
 * [out] giPtApplied   : This function will set the variable to 0 after
 *                       setting the new "gi8Peritime" value         */
void update_periodic_time_type_r(int iSig, siginfo_t *siInfo, void *pct) {

  /*--- Variables --------------------------------------------------*/
  char    szBuf[CTRL_FILE_BUF];
  int     iLen                ;
  int     i                   ;
  int64_t i8                  ;
  struct timespec tsRety      ;

  while (1) {

    /*--- Try to read the time -------------------------------------*/
    if (lseek(giFd_ctrlfile,0,SEEK_SET) < 0                 ) {break;}
    if ((iLen=read(giFd_ctrlfile,szBuf,CTRL_FILE_BUF-1)) < 1) {break;}
    for (i=0;i<iLen;i++) {if(szBuf[i]=='\n'){break;}}
    szBuf[i]='\0';
    i8 = parse_periodictime(szBuf);
    if (i8 <= -2                                            ) {break;}
    if ((gi8Peritime==i8) && (giPtApplied==1)               ) {break;}

    /*--- Update the periodic time ---------------------------------*/
    gi8Peritime = i8;
    giPtApplied =  0;
    do {
      if (pthread_kill(gtMain, SIGHUP) != 0) {
        error_exit(errno,"pthread_kill() in type_r(): %s\n",strerror(errno));
      }
      tsRety.tv_sec  = RETRY_APPLY_SEC;   /* Try sleeping for a while to */
      tsRety.tv_nsec = RETRY_APPLY_NSEC;  /* confirm the parameter has   */
      if (nanosleep(&tsRety,NULL) != 0) { /* been applied                */
        if (errno != EINTR) {
          error_exit(errno,"nanosleep() in type_r: %s\n",strerror(errno));
        }
      }
    } while (giPtApplied == 0);

  break;}

  /*--- Restore the signal action ----------------------------------*/
  if (iSig > 0) {
    if (sigaction(SIGALRM,&gsaAlrm,NULL) != 0) {
      error_exit(errno,"sigaction() in type_r(): %s\n",strerror(errno));
    }
  }
}

#ifndef NOTTY
/*=== Try to update "gi8Peritime" for a char-sp/FIFO file
 * [in]  gi8Peritime   : (must be defined as a global variable)
 *       giFd_ctrlfile : File descriptor for the file which the periodic
 *                       time is written
 *       gtMain        : The main thread ID
 *       giPtApplied   : This variable will be turned to 1 when the sleeping
 *                       time has been calculated with the gi8Peritime
 * [out] giPtApplied   : This function will set the variable to 0 after
 *                       setting the new "gi8Peritime" value         */
void update_periodic_time_type_c(void) {

  /*--- Variables --------------------------------------------------*/
  char    cBuf0[2][CTRL_FILE_BUF]  ; /* 0th buffers (two bunches)   */
  int     iBuf0DatSiz[2]           ; /* Data sizes of the two       */
  int     iBuf0Lst                 ; /* Which bunch was written last*/
  int     iBuf0ReadTimes           ; /* Num of times of Buf0 writing*/
  char    szBuf1[CTRL_FILE_BUF*2+1]; /* 1st buffer                  */
  char    szCmdbuf[CTRL_FILE_BUF]  ; /* Buffer for the new parameter*/
  struct timespec tsRety           ; /* Retry timer to apply        */
  char*   psz                      ;
  int64_t i8                       ;
  int     i, j                     ;


  /*--- Initialize -------------------------------------------------*/
  szCmdbuf[0]='\0';

  /*--- Begin of the infinite loop ---------------------------------*/
  while (1) {

  /*--- Read the ctrlfile and write the data into the Buf0          *
   *    until the unread data does not remain              ---------*/
  iBuf0DatSiz[0]=0; iBuf0DatSiz[1]=0;
  iBuf0Lst      =1; iBuf0ReadTimes=0;
  do {
    iBuf0Lst=1-iBuf0Lst;
    iBuf0DatSiz[iBuf0Lst]=read(giFd_ctrlfile,cBuf0[iBuf0Lst],CTRL_FILE_BUF);
    iBuf0ReadTimes++;
  } while (iBuf0DatSiz[iBuf0Lst]==CTRL_FILE_BUF);
  if (iBuf0DatSiz[iBuf0Lst] < 0) {
    error_exit(errno,"read() in type_c(): %s\n",strerror(errno));
  }

  /*--- Normalized the data in the Buf0 and write it into Buf1      *
   *     1) Contatinate the two bunch of data in the Buf0           *
   *        and write the data into the Buf1                        *
   *     2) Replace all NULLs in the data on Buf1 with <0x20>       *
   *     3) Make the data on the Buf1 a null-terminated string -----*/
  psz       = szBuf1;
  iBuf0Lst  = 1-iBuf0Lst;
  memcpy(psz, cBuf0[iBuf0Lst], (size_t)iBuf0DatSiz[iBuf0Lst]);
  psz      += iBuf0DatSiz[iBuf0Lst];
  iBuf0Lst  = 1-iBuf0Lst;
  memcpy(psz, cBuf0[iBuf0Lst], (size_t)iBuf0DatSiz[iBuf0Lst]);
  psz      += iBuf0DatSiz[iBuf0Lst];
  i = iBuf0DatSiz[0]+iBuf0DatSiz[1];
  for (j=0; j<i; j++) {if(szBuf1[j]=='\0'){szBuf1[j]=' ';}}
  szBuf1[i] = '\0';

  /*--- ROUTINE A: For the string on the Buf1 is terminated '\n' ---*/
  /*      - This kind of string means the user has finished typing  *
   *        the new parameter and has pressed the enter key. So,    *
   *        this command tries to notify the main thread of it.     */
  if (szBuf1[i-1]=='\n') {
    szBuf1[i-1]='\0';
    for (j=i-2; j>=0; j--) {if(szBuf1[j]=='\n'){break;}}
    j++;
    /* "j>0" means the Buf1 has 2 or more lines. So, this routine *
     * discards all but the last line,                            */
    if (j > 0) {
      if ((i-j-1) > (CTRL_FILE_BUF-1)) {
        szCmdbuf[0]='\0'; continue; /*String is too long */
      }
    } else {
      if (iBuf0ReadTimes>1 || ((i-j-1)+strlen(szCmdbuf)>(CTRL_FILE_BUF-1))) {
        szCmdbuf[0]='\0'; continue; /* String is too long */
      }
    }
    memcpy(szCmdbuf, szBuf1+j, i-j);
    i8 = parse_periodictime(szCmdbuf);
    if (i8 <= -2                             ) {
      szCmdbuf[0]='\0'; continue; /* Invalid periodic time */
    }
    if ((gi8Peritime==i8) && (giPtApplied==1)) {
      szCmdbuf[0]='\0'; continue; /* Already applied */
    }
    gi8Peritime = i8;
    giPtApplied =  0;
    do {
      if (pthread_kill(gtMain, SIGHUP) != 0) {
        error_exit(errno,"pthread_kill() in type_c(): %s\n",strerror(errno));
      }
      tsRety.tv_sec  = RETRY_APPLY_SEC;   /* Try sleeping for a while to */
      tsRety.tv_nsec = RETRY_APPLY_NSEC;  /* confirm the parameter has   */
      if (nanosleep(&tsRety,NULL) != 0) { /* been applied                */
        if (errno != EINTR) {
          error_exit(errno,"nanosleep() in type_c: %s\n",strerror(errno));
        }
      }
    } while (giPtApplied == 0);
    szCmdbuf[0]='\0'; continue;

  /*--- ROUTINE B: For the string on the Buf1 is not terminated '\n'*/
  /*      - This kind of string means that it is a portion of the   *
   *        new parameter's string, which has come while the user   *
   *        is still typing it. So, this command tries to           *
   *        concatenate the partial strings instead of the          *
   *        notification.                                           */
  } else {
    for (j=i-1; j>=0; j--) {if(szBuf1[j]=='\n'){break;}}
    j++;
    /* "j>0" means the Buf1 has 2 or more lines. So, this routine *
     * discards all but the last line,                            */
    if (j > 0) {
      if ((i-j) > (CTRL_FILE_BUF-1)) {
        szCmdbuf[0]='\0'; continue; /* String is too long */
      }
      memcpy(szCmdbuf, szBuf1+j, i-j+1);
    } else {
      if (iBuf0ReadTimes>1 || ((i-j-1)+strlen(szCmdbuf)>(CTRL_FILE_BUF-1))) {
        memset(szCmdbuf, ' ', CTRL_FILE_BUF-1); /*<-- This expresses that the */
        szCmdbuf[CTRL_FILE_BUF-1]='\0';         /*    new parameter string is */
        continue;                               /*    already too long.       */
      }
      memcpy(szCmdbuf+strlen(szCmdbuf), szBuf1+j, i-j+1);
    }
    continue;
  }

  /*--- End of the infinite loop -----------------------------------*/
  }
}
#endif
