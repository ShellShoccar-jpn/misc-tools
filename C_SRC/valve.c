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
#                         * rate   : '[kMG]bps' (regards as 1chr= 8bit)
#                                    'cps' (regards as 1chr=10bit)
#                         * output : '0%'   (completely shut the value)
#                                    '100%' (completely open the value)
#                         The maximum value is INT_MAX for all units.
#           controlfile . Filepath to specify the periodic time instead
#                         of by argument. You can change the parameter
#                         even when this command is running by updating
#                         the content of the controlfile.
#                         * The parameter syntax you can specify in this
#                           file is completely the same as the argument,
#                           but if you give me an invalid parameter, this
#                           command will ignore it silently with no error.
#                         * The default is "0bps" unless any valid para-
#                           meter is given.
#                         * You can choose one of the following three types
#                           as the controlfile.
#                           - Regular file:
#                             If you use a regular file as the control-
#                             file, you have to write a new parameter
#                             into it with the "O_CREAT" mode or ">",
#                             not the "O_APPEND" mode or ">>" because
#                             the command always checks the new para-
#                             meter at the head of the regular file
#                             periodically.
#                             The periodic time of cheking is 0.1 secs.
#                             If you want to apply the new parameter
#                             immediately, send me the SIGHUP after
#                             updating the file.
#                           - Character-special file / Named-pipe;
#                             It is better for the performance. If you
#                             use these types of files, you can write
#                             a new parameter with both the above two
#                             modes. The new parameter will be applied
#                             immediately just after writing.
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
#                         An administrative privilege might be required to
#                         use this option.
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
# Written by Shell-Shoccar Japan (@shellshoccarjpn) on 2025-01-28
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
#include <poll.h>
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
/* Buffer size for the read_1line() */
#define LINE_BUF 1024
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

/*--- data type definitions ----------------------------------------*/
typedef struct timespec tmsp;
typedef struct _thrcom_t {
  pthread_t       tMainth_id;       /* main thread ID                         */
  pthread_mutex_t mu;               /* The mutex variable                     */
  pthread_cond_t  co;               /* The condition variable                 */
  int             iRequested__main; /* Req. received flag (only in mainth)    */
  int             iReceived;        /* Set 1 when the param. has been received*/
  int64_t         i8Param1;         /* int64 variable #1 to sent to the mainth*/
} thcominfo_t;
typedef struct _thrmain_t {
  pthread_t       tSubth_id;        /* sub thread ID                          */
  int             iMu_isready;      /* Set 1 when mu has been initialized     */
  int             iCo_isready;      /* Set 1 when co has been initialized     */
  FILE*           fpIn;             /* File handle for the current input file */
} thmaininfo_t;

/*--- prototype functions ------------------------------------------*/
void* param_updater(void* pvArgs);
void update_periodic_time_type_r(char* pszCtrlfile);
#ifndef NOTTY
  void update_periodic_time_type_c(char* pszCtrlfile);
#endif
int64_t parse_periodictime(char *pszArg);
int change_to_rtprocess(int iPrio);
void spend_my_spare_time(tmsp *ptsPrev);
int read_1line(FILE *fp, tmsp *ptsGet1stchar);
void do_nothing(int iSig, siginfo_t *siInfo, void *pct);
#ifdef __ANDROID__
  void term_this_thread(int iSig, siginfo_t *siInfo, void *pct);
#endif
void recv_param_application_req(int iSig, siginfo_t *siInfo, void *pct);
void mainth_destructor(void* pvMainth);
void subth_destructor(void *pvFd);

/*--- global variables ---------------------------------------------*/
char*    gpszCmdname;     /* The name of this command                        */
int64_t  gi8Peritime;     /* Periodic time in nanosecond (-1 means infinity)
                           * - It is global but for only the main-th. The
                           *   sub-th has to write the parameter into the
                           *   gstThCom.i8Param1 instead when the sub-th
                           *   gives the main-th the new parameter.          */
struct stat gstCtrlfile;  /* stat for the control file                       */
int      giRecovery;      /* 0:normal 1:Recovery mode                        */
int      giVerbose;       /* speaks more verbosely by the greater number     */
thcominfo_t gstThCom;     /* Variables for threads communication             */

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
    "                        * rate   : '[kMG]bps' (regards as 1chr= 8bit)\n"
    "                                   'cps' (regards as 1chr=10bit)\n"
    "                        * output : '0%%'   (completely shut the value)\n"
    "                                   '100%%' (completely open the value)\n"
    "                        The maximum value is INT_MAX for all units.\n"
    "          controlfile . Filepath to specify the periodic time instead\n"
    "                        of by argument. You can change the parameter\n"
    "                        even when this command is running by updating\n"
    "                        the content of the controlfile.\n"
    "                        * The parameter syntax you can specify in this\n"
    "                          file is completely the same as the argument,\n"
    "                          but if you give me an invalid parameter, this\n"
    "                          command will ignore it silently with no error.\n"
    "                        * The default is \"0bps\" unless any valid para-\n"
    "                          meter is given.\n"
    "                        * You can choose one of the following three types\n"
    "                          as the controlfile.\n"
#ifdef NOTTY
    "                          (!) This is the regular file only version.\n"
#endif
    "                          - Regular file:\n"
    "                            If you use a regular file as the control-\n"
    "                            file, you have to write a new parameter\n"
    "                            into it with the \"O_CREAT\" mode or \">\",\n"
    "                            not the \"O_APPEND\" mode or \">>\" because\n"
    "                            the command always checks the new para-\n"
    "                            meter at the head of the regular file\n"
    "                            periodically.\n"
    "                            The periodic time of cheking is 0.1 secs.\n"
    "                            If you want to apply the new parameter\n"
    "                            immediately, send me the SIGHUP after\n"
    "                            updating the file.\n"
    "                          - Character-special file / Named-pipe:\n"
    "                            It is better for the performance. If you\n"
    "                            use these types of files, you can write\n"
    "                            a new parameter with both the above two\n"
    "                            modes. The new parameter will be applied\n"
    "                            immediately just after writing.\n"
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
    "                        An administrative privilege might be required to\n"
    "                        use this option.\n"
#endif
    "Version : 2025-01-28 16:47:55 JST\n"
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
struct sigaction saHup;   /* for signal handler definition (action) */
int      iUnit;           /* 0:character 1:line 2-:undefined        */
int      iPrio;           /* -p option number (default 1)           */
int      iRet;            /* return code                            */
int      iRet_r1l;        /* return value by read_1line()           */
char    *pszPath;         /* filepath on arguments                  */
char    *pszFilename;     /* filepath (for message)                 */
int      iFileno;         /* file# of filepath                      */
int      iFileno_opened;  /* number of the files opened successfully*/
int      iFd;             /* file descriptor                        */
tmsp     ts1st;           /* the time when the 1st char was got
                             (only for line mode)                   */
int      i;               /* all-purpose int                        */
thmaininfo_t stMainth;    /* Variables required in handler functions*/

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
if (argc < 2) {print_usage_and_exit();}
/*--- Prepare the thread operation ---------------------------------*/
memset(&gstThCom, 0, sizeof(gstThCom    ));
memset(&stMainth, 0, sizeof(thmaininfo_t));
pthread_cleanup_push(mainth_destructor, &stMainth);
/*--- Parse the periodic time --------------------------------------*/
gi8Peritime = parse_periodictime(argv[0]);
if (gi8Peritime <= -2) {
  /* Set the initial parameter, which is "0%" */
  gi8Peritime=-1; gstThCom.i8Param1=gi8Peritime;
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
  gstThCom.tMainth_id = pthread_self();
  i = pthread_mutex_init(&gstThCom.mu, NULL);
  if (i) {error_exit(i,"pthread_mutex_init() in main(): %s\n",strerror(i));}
  stMainth.iMu_isready = 1;
  i = pthread_cond_init(&gstThCom.co, NULL);
  if (i) {error_exit(i,"pthread_cond_init() in main(): %s\n" ,strerror(i));}
  stMainth.iCo_isready = 1;
  i = pthread_create(&stMainth.tSubth_id,NULL,&param_updater,(void*)argv[0]);
  if (i) {
    stMainth.tSubth_id = 0;
    error_exit(i,"pthread_create() in main(): %s\n",strerror(i));
  }
  /* Register a SIGHUP handler to apply the new gi8Peritime  */
  memset(&saHup, 0, sizeof(saHup));
  sigemptyset(&saHup.sa_mask);
  saHup.sa_sigaction = recv_param_application_req;
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
    stMainth.fpIn = stdin;
    if (feof(stdin)) {clearerr(stdin);} /* Reset EOF condition when stdin */
  } else                   {
    stMainth.fpIn = fdopen(iFd, "r");
  }
  iFileno_opened++;

  /*--- Reading and writing loop -----------------------------------*/
  if (iFileno_opened==1 && gi8Peritime==-1) {
    spend_my_spare_time(NULL);
     /* This following code clears the internal static variable "tsPrev,"
        which remembers the last time this function was called.          */
    spend_my_spare_time(&(tmsp){.tv_sec=0, .tv_nsec=0});
  }
  switch (iUnit) {
    case 0:
              while ((i=getc(stMainth.fpIn)) != EOF) {
                spend_my_spare_time(NULL);
                while (putchar(i)==EOF) {
                  error_exit(errno,"main() #C1: %s\n",strerror(errno));
                }
              }
              break;
    case 1:
              if (ts1st.tv_nsec == -1) {
                iRet_r1l = read_1line(stMainth.fpIn,&ts1st);
                spend_my_spare_time(&ts1st);
                if (iRet_r1l != 0) {break;}
              }
              while (1) {
                if ( iRet_r1l                                 != EOF) {
                  spend_my_spare_time(NULL);
                }
                if ((iRet_r1l=read_1line(stMainth.fpIn,NULL)) !=   0) {
                  break;
                }
              }
              break;
    default:
              error_exit(255,"main() #L1: Invalid unit type\n");
  }

  /*--- Close the input file ---------------------------------------*/
  if (stMainth.fpIn != stdin) {fclose(stMainth.fpIn); stMainth.fpIn=NULL;}

  /*--- End loop ---------------------------------------------------*/
  if (pszPath == NULL) {break;}
  iFileno++;
}

/*=== Finish normally ==============================================*/
pthread_cleanup_pop(1);
return(iRet);}



/*####################################################################
# Subthread (Parameter Updater)
####################################################################*/

/*=== Initialization ===============================================*/
void* param_updater(void* pvArgs) {

/*--- Variables ----------------------------------------------------*/
char*  pszCtrlfile;
#ifdef __ANDROID__
struct sigaction sa;    /* for signal handler definition (action)   */
#endif
int    i;               /* all-purpose int                          */

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

#ifdef __ANDROID__
/*=== Set the signal handler to terminate the subthread ============*/
memset(&sa, 0, sizeof(sa));
sigemptyset(&sa.sa_mask);
sigaddset(&sa.sa_mask, SIGTERM);
sa.sa_sigaction = term_this_thread;
sa.sa_flags     = SA_SIGINFO | SA_RESTART;
if (sigaction(SIGTERM,&sa,NULL) != 0) {
  error_exit(errno,"sigaction() in param_updater(): %s\n",strerror(errno));
}
#endif

/*=== The routine when the control file is a regular file ==========*/
if (gstCtrlfile.st_mode & S_IFREG) {update_periodic_time_type_r(pszCtrlfile);}

/*=== The routine when the control file is a character special file */
#ifndef NOTTY
else                               {update_periodic_time_type_c(pszCtrlfile);}
#endif

/*=== End of the subthread (does not come here) ====================*/
return NULL;}



/*####################################################################
# Subroutines of the Subthread
####################################################################*/

/*=== Try to update the parameter for a regular file =================
 * [in]  pszCtrlfile      : Filename of the control file which the periodic
 *                          time is written
 *       gstThCom.tMainth_id
 *                        : The main thread ID
 *       gstThCom.mu      : Mutex object to lock
 *       gstThCom.co      : Condition variable to send a signal to the sub-th
 * [out] gstThCom.i8Param1: Overwritten with the new parameter
 *       gstThCom.iReceived
 *                        : Set to 0 after confirming that the main thread
 *                          receivedi the request                      */
void update_periodic_time_type_r(char* pszCtrlfile) {

  /*--- Variables --------------------------------------------------*/
  struct sigaction saAlrm; /* for signal handler definition (action)   */
  sigset_t         ssMask; /* unblocking signal list                   */
  struct itimerval itInt ; /* for signal handler definition (interval) */
  int              iFd_ctrlfile        ; /* file desc. of the ctrlfile */
  char             szBuf[CTRL_FILE_BUF]; /* parameter string buffer    */
  int              iLen                ; /* length of the parameter str*/
  int64_t          i8                  ;
  int              i                   ;

  /*--- Set the signal-triggered timer -----------------------------*/
  /* 0) Unblock the SIGALRM */
  if (sigemptyset(&ssMask) != 0) {
    error_exit(errno,"sigemptyset() in type_r(): %s\n",strerror(errno));
  }
  if (sigaddset(&ssMask,SIGALRM) != 0) {
    error_exit(errno,"sigaddset() in type_r(): %s\n",strerror(errno));
  }
  if ((i=pthread_sigmask(SIG_UNBLOCK,&ssMask,NULL)) != 0) {
    error_exit(i,"pthread_sigmask() in type_r(): %s\n",strerror(i));
  }
  /* 1) Register the signal handler */
  memset(&saAlrm, 0, sizeof(saAlrm));
  sigemptyset(&saAlrm.sa_mask);
  sigaddset(&saAlrm.sa_mask, SIGALRM);
  saAlrm.sa_sigaction = do_nothing;
  saAlrm.sa_flags     = SA_SIGINFO | SA_RESTART;
  if (sigaction(SIGALRM,&saAlrm,NULL) != 0) {
    error_exit(errno,"sigaction() in type_r(): %s\n",strerror(errno));
  }
  /* 2) Register a signal pulse and start it */
  memset(&itInt, 0, sizeof(itInt));
  itInt.it_interval.tv_sec  = FREAD_ITRVL_SEC;
  itInt.it_interval.tv_usec = FREAD_ITRVL_USEC;
  itInt.it_value.tv_sec     = FREAD_ITRVL_SEC;
  itInt.it_value.tv_usec    = FREAD_ITRVL_USEC;
  if (setitimer(ITIMER_REAL,&itInt,NULL)) {
    error_exit(errno,"setitimer(): %s\n"    ,strerror(errno));
  }

  /*--- Open the file ----------------------------------------------*/
  iFd_ctrlfile = -1;
  pthread_cleanup_push(subth_destructor, &iFd_ctrlfile);
  if ((iFd_ctrlfile=open(pszCtrlfile,O_RDONLY)) < 0){
    error_exit(errno,"%s: %s\n",pszCtrlfile,strerror(errno));
  }

  /*--- Get the new parameter on the file perodically (infinite loop) */
  while (1) {
    /* 1) Try to read the time */
    if (lseek(iFd_ctrlfile,0,SEEK_SET) < 0                 ) {goto pause;}
    if ((iLen=read(iFd_ctrlfile,szBuf,CTRL_FILE_BUF-1)) < 1) {goto pause;}
    for (i=0;i<iLen;i++) {if(szBuf[i]=='\n'){break;}}
    szBuf[i]='\0';
    i8 = parse_periodictime(szBuf);
    if (i8             <= -2                               ) {goto pause;}
    if (gstThCom.i8Param1 == i8                            ) {goto pause;}
    /* 2) Update the periodic time */
    gstThCom.i8Param1 = i8;
    if (pthread_kill(gstThCom.tMainth_id, SIGHUP) != 0) {
      error_exit(errno,"pthread_kill() in type_r(): %s\n",strerror(errno));
    }
    if ((i=pthread_mutex_lock(&gstThCom.mu))      != 0) {
      error_exit(i,"pthread_mutex_lock() in type_r(): %s\n"  , strerror(i));
    }
    while (! gstThCom.iReceived) {
      if ((i=pthread_cond_wait(&gstThCom.co, &gstThCom.mu)) != 0) {
        error_exit(i,"pthread_cond_wait() in type_r(): %s\n" , strerror(i));
      }
    }
    gstThCom.iReceived = 0;
    if ((i=pthread_mutex_unlock(&gstThCom.mu)) != 0) {
      error_exit(i,"pthread_mutex_unlock() in type_r(): %s\n", strerror(i));
    }
    /* 3) Wait for the next timing */
pause:
    pause();
  }

  /*--- End of the function (does not come here) -------------------*/
  pthread_cleanup_pop(0);
}

#ifndef NOTTY
/*=== Try to update the parameter for a char-sp/FIFO file ============
 * [in]  pszCtrlfile      : Filename of the control file which the periodic
 *                          time is written
 *       gstThCom.tMainth_id
 *                        : The main thread ID
 *       gstThCom.mu      : Mutex object to lock
 *       gstThCom.co      : Condition variable to send a signal to the sub-th
 * [out] gstThCom.i8Param1: The new parameter
 *       gstThCom.iReceived
 *                        : Set to 0 after confirming that the main thread
 *                          receivedi the request                      */
void update_periodic_time_type_c(char* pszCtrlfile) {

  /*--- Variables --------------------------------------------------*/
  int     iFd_ctrlfile             ; /* file desc. of the ctrlfile  */
  char    cBuf0[3][CTRL_FILE_BUF]  ; /* 0th buffers (two bunches)   */
  int     iBuf0DatSiz[3]           ; /* Data sizes of the two       */
  int     iBuf0Lst                 ; /* Which bunch was written last*/
  int     iBuf0ReadTimes           ; /* Num of times of Buf0 writing*/
  char    szBuf1[CTRL_FILE_BUF*2+1]; /* 1st buffer                  */
  char    szCmdbuf[CTRL_FILE_BUF]  ; /* Buffer for the new parameter*/
  struct pollfd fdsPoll[1]         ;
  char*   psz                      ;
  int64_t i8                       ;
  int     i, j, k                  ;

  /*--- Initialize the buffer for the parameter --------------------*/
  szCmdbuf[0] = '\0';

  /*--- Open the file ----------------------------------------------*/
  iFd_ctrlfile = -1;
  pthread_cleanup_push(subth_destructor, &iFd_ctrlfile);
  if ((iFd_ctrlfile=open(pszCtrlfile,O_RDONLY)) < 0) {
    error_exit(errno,"%s: %s\n",pszCtrlfile,strerror(errno));
  }
  fdsPoll[0].fd     = iFd_ctrlfile;
  fdsPoll[0].events = POLLIN      ;

  /*--- Begin of the infinite loop ---------------------------------*/
  while (1) {

  /*--- Read the ctrlfile and write the data into the Buf0          *
   *    until the unread data does not remain              ---------*/
  iBuf0DatSiz[0]=0; iBuf0DatSiz[1]=0; iBuf0DatSiz[2]=0;
  iBuf0Lst      =2; iBuf0ReadTimes=0;
  do {
    iBuf0Lst=(iBuf0Lst+1)%3;
    iBuf0DatSiz[iBuf0Lst]=read(iFd_ctrlfile,cBuf0[iBuf0Lst],CTRL_FILE_BUF);
    if (iBuf0DatSiz[iBuf0Lst]==0) {iBuf0Lst=(iBuf0Lst+2)%3; i=0; break;}
    iBuf0ReadTimes++;
  } while ((i=poll(fdsPoll,1,0)) > 0);
  if (i==0 && iBuf0ReadTimes==0) {
    /* Once the status changes to EOF, poll() considers the fd readable
       until it is re-opened. To avoid overload caused by that misdetection,
       this command sleeps for 0.1 seconds every lap while the fd is EOF.   */
    if (giVerbose>0) {
      warning("%s: Controlfile closed! Please re-open it.\n", pszCtrlfile);
    }
    nanosleep(&(tmsp){.tv_sec=0, .tv_nsec=100000000}, NULL);
    continue;
  }
  if (i < 0) {
    error_exit(errno,"poll() in type_c(): %s\n",strerror(errno));
  }
  if (iBuf0DatSiz[iBuf0Lst] < 0) {
    error_exit(errno,"read() in type_c(): %s\n",strerror(errno));
  }

  /*--- Normalized the data in the Buf0 and write it into Buf1      *
   *     1) Contatinate the two bunch of data in the Buf0           *
   *        and write the data into the Buf1                        *
   *     2) Replace all NULLs in the data on Buf1 with <0x20>       *
   *     3) Make the data on the Buf1 a null-terminated string -----*/
  psz       = szBuf1;
  iBuf0Lst  = (iBuf0Lst+2)%3;
  memcpy(psz, cBuf0[iBuf0Lst], (size_t)iBuf0DatSiz[iBuf0Lst]);
  psz      += iBuf0DatSiz[iBuf0Lst];
  iBuf0Lst  = (iBuf0Lst+1)%3;
  memcpy(psz, cBuf0[iBuf0Lst], (size_t)iBuf0DatSiz[iBuf0Lst]);
  psz      += iBuf0DatSiz[iBuf0Lst];
  i = iBuf0DatSiz[(iBuf0Lst+2)%3]+iBuf0DatSiz[iBuf0Lst];
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
    if (i8                <= -2) {
      szCmdbuf[0]='\0'; continue; /* Invalid periodic time */
    }
    if (gstThCom.i8Param1 == i8) {
      szCmdbuf[0]='\0'; continue; /* Parameter does not change */
    }
    gstThCom.i8Param1 = i8;
    if (pthread_kill(gstThCom.tMainth_id, SIGHUP)           != 0) {
      error_exit(errno,"pthread_kill() in type_c(): %s\n",strerror(errno));
    }
    if ((k=pthread_mutex_lock(&gstThCom.mu))                != 0) {
      error_exit(k,"pthread_mutex_lock() in type_c(): %s\n"  , strerror(k));
    }
    while (! gstThCom.iReceived) {
      if ((k=pthread_cond_wait(&gstThCom.co, &gstThCom.mu)) != 0) {
        error_exit(k,"pthread_cond_wait() in type_c(): %s\n" , strerror(k));
      }
    }
    gstThCom.iReceived = 0;
    if ((k=pthread_mutex_unlock(&gstThCom.mu))              != 0) {
      error_exit(k,"pthread_mutex_unlock() in type_c(): %s\n", strerror(k));
    }
    szCmdbuf[0]='\0'; continue;
  }

  /*--- ROUTINE B: For the string on the Buf1 is not terminated '\n'*/
  /*      - This kind of string means that it is a portion of the   *
   *        new parameter's string, which has come while the user   *
   *        is still typing it. So, this command tries to           *
   *        concatenate the partial strings instead of the          *
   *        notification.                                           */
  else {
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

  /*--- End of the function (does not come here) -------------------*/
  pthread_cleanup_pop(0);
}
#endif



/*####################################################################
# Other Functions
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
int read_1line(FILE *fp, tmsp *ptsGet1stchar) {

  /*--- Variables --------------------------------------------------*/
  char       szBuf[LINE_BUF]; /* Buffer for reading 1-line */
  int        iChar;           /* Buffer for reading 1-char */
  int        iLen;            /* Actual size of string in the buffer */

  /*--- Read/Write the 1st char. if the arriving time is required --*/
  if (ptsGet1stchar != NULL) {
    iChar=getc(fp);
    if (clock_gettime(CLOCK_FOR_ME,ptsGet1stchar) != 0) {
      error_exit(errno,"clock_gettime() in read_1line(): %s\n",strerror(errno));
    }
    switch (iChar) {
      case EOF :
                  return(EOF);
      case '\n':
                  while (putchar('\n' )==EOF) {
                    error_exit(errno,"putchar() #R1L-1: %s\n",strerror(errno));
                  }
                  iChar=getc(fp);
                  if (iChar==EOF) {return 1;}
                  if (ungetc(iChar,fp)==EOF) {
                    error_exit(errno,"ungetc() #R1L-1: %s\n",strerror(errno));
                  }
                  return 0;
      default  :
                  while (putchar(iChar)==EOF) {
                    error_exit(errno,"putchar() #R1L-2: %s\n",strerror(errno));
                  }
    }
  }

  /*--- Read/Write a string until the "\n." ------------------------*/
  while (fgets(szBuf,LINE_BUF,fp) != NULL) {
    if (fputs(szBuf,stdout) < 0) {
      error_exit(errno,"fputs() #R1L-1: %s\n",strerror(errno));
    }
    iLen = strnlen(szBuf, LINE_BUF);
    if (szBuf[iLen-1] == '\n') {
      iChar=getc(fp);
      if (iChar==EOF) {return 1;}
      if (ungetc(iChar,fp)==EOF) {
        error_exit(errno,"ungetc() #R1L-2: %s\n",strerror(errno));
      }
      return 0;
    }
    if (iLen < LINE_BUF-1) {
      iChar=getc(fp);
      if (iChar==EOF) {return EOF;}
      while (putchar(iChar)==EOF) {
        error_exit(errno,"putchar() #R1L-3: %s\n",strerror(errno));
      }
    }
  }
  return EOF;
}

/*=== Sleep until the next interval period ===========================
 * [in]  gi8Peritime    : Periodic time (-1 means infinity)
 *       ptsPrev        : If not null, set it to tsPrev and exit immediately
 *       gstThCom.iReceived_main
 *                      : 1 when the request has come
 *       gstThCom.mu    : Mutex object to lock
 *       gstThCom.co    : Condition variable to send a signal to the sub-th
 * [out] gstThCom.iReceived, gstThCom.iRequested__main
 *                      : Set to 1, 0 respectively after dealing with the
 *                     request */
void spend_my_spare_time(tmsp *ptsPrev) {

  /*--- Variables --------------------------------------------------*/
  static tmsp    tsPrev = {0,0};         /* the time when this func
                                            called last time        */
  static tmsp    tsRecovmax  = {0,0} ;
  tmsp           tsNow               ;
  tmsp           tsTo                ;
  tmsp           tsDiff              ;

  static int64_t i8LastPeritime  = -1;

  uint64_t       ui8                 ;
  int            i                   ;


  /*--- Set tsPrev and exit if ptsPrev has a time ------------------*/
  if (ptsPrev) {
    tsPrev.tv_sec  = ptsPrev->tv_sec ;
    tsPrev.tv_nsec = ptsPrev->tv_nsec;
    i8LastPeritime = gi8Peritime;
    return;
  }

top:
  /*--- Notify the subthread that the main thread received the
        request if the request has come                        -----*/
  if (gstThCom.iRequested__main) {
    if ((i=pthread_mutex_lock(&gstThCom.mu))   != 0) {
      error_exit(i,"pthread_mutex_lock() in spend_my_spare_time(): %s\n",
                                                                strerror(i));
    }
    gstThCom.iReceived = 1;
    if ((i=pthread_cond_signal(&gstThCom.co))  != 0) {
      error_exit(i,"pthread_cond_signal() in spend_my_spare_time(): %s\n",
                                                                strerror(i));
    }
    if ((i=pthread_mutex_unlock(&gstThCom.mu)) != 0) {
      error_exit(i,"pthread_mutex_unlock() in spend_my_spare_time(): %s\n",
                                                                strerror(i));
    }
    if (giVerbose>0) {warning("gi8Peritime=%ld\n",gi8Peritime);}
    gstThCom.iRequested__main = 0;
  }

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
        goto top; /* Go to "top" in case of a signal handler */
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
    if (errno == EINTR) {
      i8LastPeritime=gi8Peritime;
      goto top; /* Go to "top" in case of a signal handler */
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

/*=== SIGNALHANDLER : Do nothing =====================================
 * This function does nothing, but it is helpful to break a thread
 * sleeping by using me as a signal handler.                        */
void do_nothing(int iSig, siginfo_t *siInfo, void *pct) {return;}

#ifdef __ANDROID__
/*=== SIGNALHANDLER : Terminate this thread ==========================
 * This function just terminates itself.                            */
void term_this_thread(int iSig, siginfo_t *siInfo, void *pct) {pthread_exit(0);}
#endif

/*=== SIGNALHANDLER : Received the parameter application request =====
 * This function sets the flag of the new parameter application request.
 * This function should be called as a signal handler so that you can
 * wake myself (the main thread) up even while sleeping with the
 * nanosleep().
 * [in]  stTh.i8Param1       : The new parameter the sub-th gave
 * [out] gi8Peritime         : The new parameter the sub-th gave
 * [out] gstThCom.iRequested : set to 1 to notify the main-th of the request */
void recv_param_application_req(int iSig, siginfo_t *siInfo, void *pct) {
  gi8Peritime               = gstThCom.i8Param1;
  gstThCom.iRequested__main = 1;
  return;
}

/*=== EXITHANDLER : Release the main-th. resources =================*/
/* This function should be registered with pthread_cleanup_push().
   [in] pvMainth : The pointer of the structure of the main thread
                    local variables                                 */
void mainth_destructor(void* pvMainth) {

  /*--- Variables --------------------------------------------------*/
  thmaininfo_t* pstMainth;

  /*--- Initialize -------------------------------------------------*/
  if (giVerbose>1) {warning("Enter mainth_destructor()\n");}
  if (! pvMainth ) {return;}
  pstMainth = (thmaininfo_t*)pvMainth;

  /*--- Terminate the sub thread -----------------------------------*/
  if (pstMainth->tSubth_id) {
    #ifndef __ANDROID__
    pthread_cancel(pstMainth->tSubth_id);
    #else
    pthread_kill(pstMainth->tSubth_id, SIGTERM);
    #endif
    pthread_join(pstMainth->tSubth_id, NULL);
    pstMainth->tSubth_id = 0;
  }

  /*--- Destroy mutex variables ------------------------------------*/
  if (pstMainth->iMu_isready) {
    if (giVerbose>0) {warning("Mutex is destroied\n");}
    pthread_mutex_destroy(&gstThCom.mu);pstMainth->iMu_isready=0;
  }
  if (pstMainth->iCo_isready) {
    if (giVerbose>0) {warning("Conditional variable is destroied\n");}
    pthread_cond_destroy( &gstThCom.co);pstMainth->iCo_isready=0;
  }

  /*--- Close files ------------------------------------------------*/
  if (pstMainth->fpIn != NULL) {
    if (giVerbose>0) {warning("Input file is closed\n");}
    fclose(pstMainth->fpIn); pstMainth->fpIn = NULL;
  }

  /*--- Finish -----------------------------------------------------*/
  return;
}

/*=== CLEANUPHANDLER : Release the sub-th. resources ===============*/
/* This function should be registered with pthread_cleanup_push().
   [in] pvFd : The pointer of the file descriptor the sub-th. opened. */
void subth_destructor(void *pvFd) {
  int* piFd;
  if (giVerbose>1) {warning("Enter subth_destructor()\n");}
  if (pvFd == NULL) {return;}
  piFd = (int*)pvFd;
  if (*piFd >= 0) {
    if (giVerbose>0) {warning("Ctrlfile is closed\n");}
    close(*piFd); *piFd=-1;
  }
  return;
}
