/*####################################################################
#
# QVALVE - Quantitative Valve for the UNIX Pipeline
#
# USAGE   : qvalve [-c|-l] [-t] [-1] [-p n] quantity [file [...]]
#           qvalve [-c|-l] [-t] [-1] [-p n] controlfile [file [...]]
# Args    : quantity ...  * Quantity this command allows to pass through.
#                         * The quantity is the number of bytes (for the
#                           -c option) or lines (for the -l option).
#                         * You can specify it by the following format.
#                           + [+]number[prefix]
#                             "+":
#                               - If you attach the plus symbol "+,"
#                                 this command adds the quantity to the
#                                 current value of the internal counter,
#                                 which means how many bytes/lines should
#                                 be passed through.
#                               - Thus, if you set 10 when the 5 bytes/
#                                 lines still remain to be outputted, the
#                                 value in the counter will be set to 15.
#                               - If you set a quantity without this
#                                 symbol, the value in the counter will
#                                 be overwritten. Thus, the value will
#                                 be 10 in the above case.
#                               - However, this symbol has no meaning
#                                 when you directly specify the quantity
#                                 in the argument because you cannot
#                                 specify the quantity twice or more
#                                 with the argument.
#                             number:
#                               - Just a number to specify the quantity.
#                               - You can specify a number including
#                                 decimal places when you use the
#                                 following prefix words.
#                               - However, you can specify the quantity
#                                 more accurately only with integers
#                                 than containing decimals.
#                             prefix:
#                               - You can add one of the following
#                                 prefixes.
#                                   "k" ... means number*1000.
#                                   "M" ... means number*1000^2.
#                                   "G" ... means number*1000^3.
#                                   "T" ... means number*1000^4.
#                                   "P" ... means number*1000^5.
#                                   "E" ... means number*1000^6.
#                                   "ki" .. means number*1024.
#                                   "Mi" .. means number*1024^2.
#                                   "Gi" .. means number*1024^3.
#                                   "Ti" .. means number*1024^4.
#                                   "Pi" .. means number*1024^5.
#                                   "Ei" .. means number*1024^6.
#                           + If the quantity you specified exceeds this
#                             computer's SIZE_MAX, the value of the
#                             quantity will be set to it.
#                         * Or, you can use the following command.
#                           + "t" ... * Terminate this command.
#                                     * It is the same behavior as
#                                       the closing the controlfile.
#                                       (See the -t option)
#           controlfile . Filepath to specify the quantity instead of by
#                         argument. You can change the parameter even when
#                         this command is running by updating the content
#                         of the controlfile.
#                         * The parameter syntax you can specify in this
#                           file is completely the same as the quantity
#                           argument, but if you give me an invalid
#                           parameter, this command will ignore it
#                           silently with no error.
#                         * The default is "0" unless any valid
#                           parameter is given.
#                         * You can choose one of the following three
#                           types as the controlfile.
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
# Options : -c .......... * (Default) The unit of the quantity will be
#                           set to "character" (byte).
#                         * The -l option will be disabled by this option.
#           -l .......... * The unit of the quantity will be set to
#                           "line."
#                         * The -c option will be disabled by this option.
#           -t .......... * Terminate this command when the control file
#                           is closed. After the termination, the standard
#                           I/O pipeline will be destroyed, and the
#                           commands that connect before and after will
#                           eventually terminate, too.
#                         * This mode is useful for commands that block
#                           the next operation unless the pipeline is
#                           destroyed, like AWK. You can notice the
#                           destruction to them by closing the control-
#                           file.
#                         * Without this option, this command will stay
#                           and wait for re-opening when the controlfile
#                           is closed. However, you can get the same
#                           behavior by giving the "t" command to the
#                           controlfile instead. (See the quantity
#                           section)
#           -1 .......... * Output one character/line (LF) at first before
#                           outputting the incoming data.
#                         * This option might work as a starter of the
#                           system embedding this command.
#           -p n ........ * Process priority setting [0-3] (if possible)
#                            0: Normal process
#                            1: Weakest realtime process (default)
#                            2: Strongest realtime process for generic users
#                               (for only Linux, equivalent 1 for otheres)
#                            3: Strongest realtime process of this host
#                         * Larger numbers maybe require a privileged user,
#                           but if failed, it will try the smaller numbers.
#                         * An administrative privilege might be required to
#                           use this option.
# Retuen  : Return 0 only when finished successfully
#
# How to compile : cc -O3 -o __CMDNAME__ __SRCNAME__ -pthread
#
# Written by Shell-Shoccar Japan (@shellshoccarjpn) on 2025-03-13
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
#define FREAD_ITRVL_NSEC 100000000
/* Buffer size for the read_1line() */
#define LINE_BUF 1024
/* Buffer size for the control file */
#define CTRL_FILE_BUF 64

/*--- data type definitions ----------------------------------------*/
typedef struct timespec tmsp;
typedef struct _thrcom_t {
  pthread_t       tMainth_id;       /* main thread ID                         */
  pthread_mutex_t mu;               /* The mutex variable                     */
  pthread_cond_t  co;               /* The condition variable                 */
  size_t          sizQty;           /* The quantity counter                   */
  int             iTerm_req;        /* Request flag to terminate this command */
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
void update_periodic_time_type_c(char* pszCtrlfile);
int parse_quantity(char* pszArg, size_t* psiz);
int change_to_rtprocess(int iPrio);
int read_1line(FILE *fp);
void term_request(int iSig, siginfo_t *siInfo, void *pct);
void do_nothing(int iSig, siginfo_t *siInfo, void *pct);
#ifdef __ANDROID__
  void term_this_thread(int iSig, siginfo_t *siInfo, void *pct);
#endif
void mainth_destructor(void* pvMainth);
void subth_destructor(void *pvFd);

/*--- global variables ---------------------------------------------*/
char*    gpszCmdname;     /* The name of this command                        */
int      giOpt_t;         /* The "-t" option flag                            */
struct stat gstCtrlfile;  /* stat for the control file                       */
int      giRecovery;      /* 0:normal 1:Recovery mode                        */
int      giVerbose;       /* speaks more verbosely by the greater number     */
thcominfo_t gstThCom;     /* Variables for threads communication             */

/*=== Define the functions for printing usage and error ============*/

/*--- exit with usage ----------------------------------------------*/
void print_usage_and_exit(void) {
  fprintf(stderr,
#if defined(_POSIX_PRIORITY_SCHEDULING) && !defined(__OpenBSD__) && !defined(__APPLE__)
    "USAGE   : %s [-c|-l] [-t] [-p n] quantity [file [...]]\n"
    "          %s [-c|-l] [-t] [-p n] controlfile [file [...]]\n"
#else
    "USAGE   : %s [-c|-l] [-t] quantity [file [...]]\n"
    "          %s [-c|-l] [-t] controlfile [file [...]]\n"
#endif
    "Args    : quantity ...  * Quantity this command allows to pass through.\n"
    "                        * The quantity is the number of bytes (for the\n"
    "                          -c option) or lines (for the -l option).\n"
    "                        * You can specify it by the following format.\n"
    "                          + [+]number[prefix]\n"
    "                            \"+\":\n"
    "                              - If you attach the plus symbol \"+,\"\n"
    "                                this command adds the quantity to the\n"
    "                                current value of the internal counter,\n"
    "                                which means how many bytes/lines should\n"
    "                                be passed through.\n"
    "                              - Thus, if you set 10 when the 5 bytes/\n"
    "                                lines still remain to be outputted, the\n"
    "                                value in the counter will be set to 15.\n"
    "                              - If you set a quantity without this\n"
    "                                symbol, the value in the counter will\n"
    "                                be overwritten. Thus, the value will\n"
    "                                be 10 in the above case.\n"
    "                              - However, this symbol has no meaning\n"
    "                                when you directly specify the quantity\n"
    "                                in the argument because you cannot\n"
    "                                specify the quantity twice or more\n"
    "                                with the argument.\n"
    "                            number:\n"
    "                              - Just a number to specify the quantity.\n"
    "                              - You can specify a number including\n"
    "                                decimal places when you use the\n"
    "                                following prefix words.\n"
    "                              - However, you can specify the quantity\n"
    "                                more accurately only with integers\n"
    "                                than containing decimals.\n"
    "                            prefix:\n"
    "                              - You can add one of the following\n"
    "                                prefixes.\n"
    "                                  \"k\" ... means number*1000.\n"
    "                                  \"M\" ... means number*1000^2.\n"
    "                                  \"G\" ... means number*1000^3.\n"
    "                                  \"T\" ... means number*1000^4.\n"
    "                                  \"P\" ... means number*1000^5.\n"
    "                                  \"E\" ... means number*1000^6.\n"
    "                                  \"ki\" .. means number*1024.\n"
    "                                  \"Mi\" .. means number*1024^2.\n"
    "                                  \"Gi\" .. means number*1024^3.\n"
    "                                  \"Ti\" .. means number*1024^4.\n"
    "                                  \"Pi\" .. means number*1024^5.\n"
    "                                  \"Ei\" .. means number*1024^6.\n"
    "                          + If the quantity you specified exceeds this\n"
    "                            computer's SIZE_MAX, the value of the\n"
    "                            quantity will be set to it.\n"
    "                        * Or, you can use the following command.\n"
    "                          + \"t\" ... * Terminate this command.\n"
    "                                    * It is the same behavior as\n"
    "                                      the closing the controlfile.\n"
    "                                      (See the -t option)\n"
    "          controlfile . Filepath to specify the quantity instead of by\n"
    "                        argument. You can change the parameter even when\n"
    "                        this command is running by updating the content\n"
    "                        of the controlfile.\n"
    "                        * The parameter syntax you can specify in this\n"
    "                          file is completely the same as the quantity\n"
    "                          argument, but if you give me an invalid\n"
    "                          parameter, this command will ignore it\n"
    "                          silently with no error.\n"
    "                        * The default is \"0\" unless any valid\n"
    "                          parameter is given.\n"
    "                        * You can choose one of the following three\n"
    "                          types as the controlfile.\n"
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
    "                          - Character-special file / Named-pipe;\n"
    "                            It is better for the performance. If you\n"
    "                            use these types of files, you can write\n"
    "                            a new parameter with both the above two\n"
    "                            modes. The new parameter will be applied\n"
    "                            immediately just after writing.\n"
    "          file ........ Filepath to be send (\"-\" means STDIN)\n"
    "Options : -c .......... * (Default) The unit of the quantity will be\n"
    "                          set to \"character\" (byte).\n"
    "                        * The -l option will be disabled by this option.\n"
    "          -l .......... * The unit of the quantity will be set to\n"
    "                          \"line.\"\n"
    "                        * The -c option will be disabled by this option.\n"
    "          -t .......... * Terminate this command when the control file\n"
    "                          is closed. After the termination, the standard\n"
    "                          I/O pipeline will be destroyed, and the\n"
    "                          commands that connect before and after will\n"
    "                          eventually terminate, too.\n"
    "                        * This mode is useful for commands that block\n"
    "                          the next operation unless the pipeline is\n"
    "                          destroyed, like AWK. You can notice the\n"
    "                          destruction to them by closing the control-\n"
    "                          file.\n"
    "                        * Without this option, this command will stay\n"
    "                          and wait for re-opening when the controlfile\n"
    "                          is closed. However, you can get the same\n"
    "                          behavior by giving the \"t\" command to the\n"
    "                          controlfile instead. (See the quantity\n"
    "                          section)\n"
    "          -1 .......... * Output one character/line (LF) at first before\n"
    "                          outputting the incoming data.\n"
    "                        * This option might work as a starter of the\n"
    "                          system embedding this command.\n"
#if defined(_POSIX_PRIORITY_SCHEDULING) && !defined(__OpenBSD__) && !defined(__APPLE__)
    "          -p n ........ * Process priority setting [0-3] (if possible)\n"
    "                           0: Normal process\n"
    "                           1: Weakest realtime process (default)\n"
    "                           2: Strongest realtime process for generic users\n"
    "                              (for only Linux, equivalent 1 for otheres)\n"
    "                           3: Strongest realtime process of this host\n"
    "                        * Larger numbers maybe require a privileged user,\n"
    "                          but if failed, it will try the smaller numbers.\n"
    "                        * An administrative privilege might be required to\n"
    "                          use this option.\n"
#endif
    "Retuen  : Return 0 only when finished successfully\n"
    "Version : 2025-03-13 21:55:01 JST\n"
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
struct sigaction sa;      /* for signal handler definition (action) */
sigset_t ssMask;          /* blocking signal list for the main thread*/
int      iUnit;           /* 0:character 1:line 2-:undefined        */
int      iOpt_1;          /* -1 option flag (default 0)             */
int      iPrio;           /* -p option number (default 1)           */
int      iRet;            /* return code                            */
int      iRet_r1l;        /* return value by read_1line()           */
char    *pszPath;         /* filepath on arguments                  */
char    *pszFilename;     /* filepath (for message)                 */
int      iFileno;         /* file# of filepath                      */
int      iFd;             /* file descriptor                        */
size_t   siz;             /* all-purpose size_t                     */
int      i, j;            /* all-purpose int                        */
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
iOpt_1    =0;
iPrio     =1;
giOpt_t   =0;
giVerbose =0;
giRecovery=1;
/*--- Parse options which start by "-" -----------------------------*/
while ((i=getopt(argc, argv, "cl1tp:vh")) != -1) {
  switch (i) {
    case 'c': iUnit   = 0;    break;
    case 'l': iUnit   = 1;    break;
    case '1': iOpt_1  = 1;    break;
    case 't': giOpt_t = 1;    break;
#if defined(_POSIX_PRIORITY_SCHEDULING) && !defined(__OpenBSD__) && !defined(__APPLE__)
    case 'p': if (sscanf(optarg,"%d",&iPrio) != 1) {print_usage_and_exit();}
              break;
#endif
    case 'v': giVerbose++;    break;
    case 'h': print_usage_and_exit();
    default : print_usage_and_exit();
  }
}
argc -= optind-1;
argv += optind  ;
if (giVerbose>0) {warning("verbose mode (level %d)\n",giVerbose);}
if (argc < 2) {print_usage_and_exit();}
/*--- Prepare the thread operation ---------------------------------*/
memset(&gstThCom, 0, sizeof(gstThCom    ));
memset(&stMainth, 0, sizeof(thmaininfo_t));
pthread_cleanup_push(mainth_destructor, &stMainth);
/*--- Parse the periodic time --------------------------------------*/
i = parse_quantity(argv[0], &siz);
if (i <= 0) {
  /* Set the initial parameter, the Quantity is zero. */
  gstThCom.sizQty=0;
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
  /* Block the SIGHUP (A SIGHUP handler will be set only on the sub-th) */
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
  /* Start the subthread */
  memset(&sa, 0, sizeof(sa));
  sigemptyset(&sa.sa_mask);
  sigaddset(&sa.sa_mask, SIGTERM);
  sa.sa_sigaction = term_request;
  sa.sa_flags     = SA_SIGINFO;
  if (sigaction(SIGTERM,&sa,NULL) != 0) {
    error_exit(errno,"sigaction() in main(): %s\n",strerror(errno));
  }
} else {
  gstThCom.sizQty = siz;
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

/*=== Output the starter charater/line when -1 is enabled ==========*/
if (iOpt_1 && putchar('\n')==EOF) {
  error_exit(errno, "putchar() in main() #0: %s\n", strerror(errno));
}

/*=== Each file loop ===============================================*/
iRet          =  0;
iFileno       =  0;
iFd           = -1;
iRet_r1l      =  0;
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

  /*--- Reading and writing loop -----------------------------------*/
  switch (iUnit) {
    case 0:
              errno = 0;
              while ((i=getc(stMainth.fpIn)) != EOF) {
                if ((j=pthread_mutex_lock(&gstThCom.mu))                != 0) {
                  error_exit(j,
                             "pthread_mutex_lock() in main() #1: %s\n",
                             strerror(j)                               );
                }
                while (gstThCom.sizQty==0 && gstThCom.iTerm_req==0) {
                  if ((j=pthread_cond_wait(&gstThCom.co, &gstThCom.mu)) != 0) {
                    error_exit(j,
                               "pthread_cond_wait() in main() #1: %s\n",
                               strerror(j)                              );
                  }
                }
                gstThCom.sizQty--;
                if ((j=pthread_mutex_unlock(&gstThCom.mu))              != 0) {
                  error_exit(j,
                             "pthread_mutex_unlock() in main() #1: %s\n",
                             strerror(j)                                 );
                }
                if (gstThCom.iTerm_req) {mainth_destructor(&stMainth);
                                         return(iRet);                }
                while (putchar(i)==EOF) {
                  error_exit(errno,
                             "putchar() in main() #1: %s\n",
                             strerror(errno)                );
                }
              }
              if (ferror(stMainth.fpIn) && errno==EINTR) {
                mainth_destructor(&stMainth);
                return(iRet);
              }
              break;
    case 1:
              errno = 0;
              while ((i=getc(stMainth.fpIn)) != EOF) {
                if ((j=pthread_mutex_lock(&gstThCom.mu))                != 0) {
                  error_exit(j,
                             "pthread_mutex_lock() in main() #2: %s\n",
                             strerror(j)                               );
                }
                while (gstThCom.sizQty==0 && gstThCom.iTerm_req==0) {
                  if ((j=pthread_cond_wait(&gstThCom.co, &gstThCom.mu)) != 0) {
                    error_exit(j,
                               "pthread_cond_wait() in main() #2: %s\n",
                               strerror(j)                              );
                  }
                }
                gstThCom.sizQty--;
                if ((j=pthread_mutex_unlock(&gstThCom.mu))              != 0) {
                  error_exit(j,
                             "pthread_mutex_unlock() in main() #2: %s\n",
                             strerror(j)                                 );
                }
                if (gstThCom.iTerm_req) {mainth_destructor(&stMainth);
                                         return(iRet);                }
                while (putchar(i)==EOF) {
                  error_exit(errno,
                             "putchar() in main() #2: %s\n",
                             strerror(errno)                );
                }
                if (read_1line(stMainth.fpIn) != 0) {break;}
              }
              if (ferror(stMainth.fpIn) && errno==EINTR) {
                mainth_destructor(&stMainth);
                return(iRet);
              }
              break;
    default:
              error_exit(255,"main(): Invalid unit type\n");
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
struct sigaction sa;    /* for signal handler definition (action)   */
int    i;               /* all-purpose int                          */

/*=== Validate the control file ====================================*/
if (! pvArgs) {error_exit(255,"line #%d: Fatal error\n",__LINE__);}
pszCtrlfile = (char*)pvArgs;
/* Make sure that the control file has an acceptable type */
switch (gstCtrlfile.st_mode & S_IFMT) {
  case S_IFREG : break;
  case S_IFCHR : break;
  case S_IFIFO : break;
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
else                               {update_periodic_time_type_c(pszCtrlfile);}

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
 * [out] gstThCom.sizQty  : the new Quantity                        */
void update_periodic_time_type_r(char* pszCtrlfile) {

  /*--- Variables --------------------------------------------------*/
  struct sigaction sa;        /* for signal handler definition (action)   */
  struct itimerval itInt;     /* for signal handler definition (interval) */
  int              iFd_ctrlfile           ; /* file desc. of the ctrlfile */
  char             szBuf[2][CTRL_FILE_BUF]; /* parameter string buffers   */
  int              iLen                   ; /* length of the parameter str*/
  int              i, j, k                ;
  size_t           siz                    ;

  /*--- Set the signal-triggered handlers and timer ----------------*/
  /* 1) Register the signal handlers (SIGHUP and SIGALRM) */
  memset(&sa, 0, sizeof(sa));
  sigemptyset(&sa.sa_mask);
  sigaddset(&sa.sa_mask, SIGHUP );
  sigaddset(&sa.sa_mask, SIGALRM);
  sa.sa_sigaction = do_nothing;
  sa.sa_flags     = SA_SIGINFO | SA_RESTART;
  if (sigaction(SIGALRM,&sa,NULL) != 0) {
    error_exit(errno,"sigaction() in type_r(): %s\n",strerror(errno));
  }
  /* 2) Unblock the signals (SIGHUP and SIGALRM) */
  if ((i=pthread_sigmask(SIG_UNBLOCK,&sa.sa_mask,NULL)) != 0) {
    error_exit(i,"pthread_sigmask() in type_r(): %s\n",strerror(i));
  }
  /* 3) Register a signal pulse and start it */
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
  szBuf[0][0]='\0'; szBuf[1][0]='\0'; i=1;
  while (1) {
    /* 0) Switch the buffer */
    i=1-i;
    /* 1) Try to read the time */
    if (lseek(iFd_ctrlfile,0,SEEK_SET) < 0                    ) {goto pause;}
    if ((iLen=read(iFd_ctrlfile,szBuf[i],CTRL_FILE_BUF-1)) < 1) {goto pause;}
    for (j=0;j<iLen;j++) {if(szBuf[i][j]=='\n'){break;}}
    szBuf[i][j]='\0';
    if (strncmp(szBuf[0],szBuf[1],CTRL_FILE_BUF) == 0         ) {goto pause;}
    j = parse_quantity(szBuf[i], &siz);
    if (j< 0) {goto pause;}
    if (j==0) {break     ;}
    if ((k=pthread_mutex_lock(  &gstThCom.mu)) != 0) {
      error_exit(k,"pthread_mutex_lock() in type_r(): %s\n"  , strerror(k));
    }
    if (j==1) {
      gstThCom.sizQty = siz;
    } else {
      gstThCom.sizQty = (UINT_MAX-gstThCom.sizQty < siz) ? UINT_MAX
                                                         : gstThCom.sizQty+siz;
    }
    if ((k=pthread_cond_signal( &gstThCom.co)) != 0) {
      error_exit(k,"pthread_cond_signal() in type_r(): %s\n" , strerror(k));
    }
    if ((k=pthread_mutex_unlock(&gstThCom.mu)) != 0) {
      error_exit(k,"pthread_mutex_unlock() in type_r(): %s\n", strerror(k));
    }
    /* 3) Wait for the next timing */
pause:
    pause();
  }

  /*--- Request the main-th. to terminate this command -------------*/
  if (pthread_kill(gstThCom.tMainth_id, SIGTERM) != 0) {
    error_exit(errno,"pthread_kill() in type_r(): %s\n",strerror(errno));
  }

  /*--- End of the function (does not come here) -------------------*/
  pthread_cleanup_pop(0);
}

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
  char    cBuf0[3][CTRL_FILE_BUF]  ; /* 0th buffers (three bunches) */
  int     iBuf0DatSiz[3]           ; /* Data sizes of the three     */
  int     iBuf0Lst                 ; /* Which bunch was written last*/
  int     iBuf0ReadTimes           ; /* Num of times of Buf0 writing*/
  char    szBuf1[CTRL_FILE_BUF*2+1]; /* 1st buffer                  */
  char    szCmdbuf[CTRL_FILE_BUF]  ; /* Buffer for the new parameter*/
  struct pollfd fdsPoll[1]         ;
  char*   psz                      ;
  size_t  siz                      ;
  int     i, j                     ;

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
  iBuf0DatSiz[0]='\0'; iBuf0DatSiz[1]='\0'; iBuf0DatSiz[2]='\0';
  iBuf0Lst      =  2 ; iBuf0ReadTimes=  0 ;
  do {
    iBuf0Lst=(iBuf0Lst+1)%3;
    iBuf0DatSiz[iBuf0Lst]=read(iFd_ctrlfile,cBuf0[iBuf0Lst],CTRL_FILE_BUF);
    if (iBuf0DatSiz[iBuf0Lst]==0) {iBuf0Lst=(iBuf0Lst+2)%3; i=0; break;}
    iBuf0ReadTimes++;
  } while ((i=poll(fdsPoll,1,0)) > 0);
  if (i==0 && iBuf0ReadTimes==0) {
    /* Once the status changes to EOF, poll() considers the fd readable
       until it is re-opened. To avoid overload caused by that misdetection,
       this command does one of the following actions.
         - If the "-t" option is enabled, it will terminate itself.
         - If the "-t" option is disabled, it will sleep for 0.1 seconds
           every lap while the fd is EOF.                                 */
    if (giOpt_t>0) {
      if (giVerbose>0) {
        warning("%s: Controlfile closed. Terminate myself.\n", pszCtrlfile);
      }
      break;
    }
    if (giVerbose>0) {
      warning("%s: Controlfile closed! Please re-open it.\n", pszCtrlfile);
    }
    nanosleep(&(tmsp){.tv_sec=FREAD_ITRVL_SEC,.tv_nsec=FREAD_ITRVL_NSEC}, NULL);
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
    i = parse_quantity(szCmdbuf, &siz);
    if (i< 0) {szCmdbuf[0]='\0'; continue;} /* Invalid periodic time */
    if (i==0) {                  break   ;} /* Request to terminate  */
    if ((j=pthread_mutex_lock(  &gstThCom.mu)) != 0) {
      error_exit(j,"pthread_mutex_lock() in type_c(): %s\n"  , strerror(j));
    }
    if (i==1) {
      gstThCom.sizQty = siz;
    } else {
      gstThCom.sizQty = (UINT_MAX-gstThCom.sizQty < siz) ? UINT_MAX
                                                         : gstThCom.sizQty+siz;
    }
    if ((j=pthread_cond_signal( &gstThCom.co)) != 0) {
      error_exit(j,"pthread_cond_signal() in type_c(): %s\n" , strerror(j));
    }
    if ((j=pthread_mutex_unlock(&gstThCom.mu)) != 0) {
      error_exit(j,"pthread_mutex_unlock() in type_c(): %s\n", strerror(j));
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

  /*--- Request the main-th. to terminate this command -------------*/
  if (pthread_kill(gstThCom.tMainth_id, SIGTERM) != 0) {
    error_exit(errno,"pthread_kill() in type_c(): %s\n",strerror(errno));
  }

  /*--- End of the function (does not come here) -------------------*/
  pthread_cleanup_pop(0);
}



/*####################################################################
# Other Functions
####################################################################*/

/*=== Parse the quantity string ======================================
 * [in] pszArg : Quantity string to be parsed
 *      psiz   : Pointer to set the parsed value
 *               (*psiz will be updated only when ret==1)
 * [ret] == 2  : Succeed in parsing (the string meant an additional quantity)
 *       == 1  : Succeed in parsing (the string meant a quantity)
 *       == 0  : Succeed in parsing, but it meant the termination command
 *       <=-1  : It was neither a quantity nor a command            */
int parse_quantity(char* pszArg, size_t* psiz) {

  /*--- Variables --------------------------------------------------*/
  char     szUnit[CTRL_FILE_BUF];
  char     szTmp[CTRL_FILE_BUF];
  double   dNum;      /* Type1: variable to parse a string containing decimals*/
  size_t   sizNum;    /* Type2: variable to parse an integer string */
  int      iType;     /* Set to n when the type is n */
  int      iAddition; /* Set to 1 when the string starts with "+" */
  uint64_t ui8Scale;  /* concrete value of the prefix */
  int      i, j;
  size_t   siz;

  /*--- Check the lengths of the argument --------------------------*/
  if (strlen(pszArg)>=CTRL_FILE_BUF) {return -1;}

  /*--- Detect the "+" or "t" if it exists while skipping the
   *    white space chrs.                                     ------*/
  i = (int)strlen(pszArg);
  iAddition = 0;
  for (j=0; j<i; j++) {
    if (*pszArg==' ' || *pszArg=='\t') {             pszArg++; continue;}
    if (*pszArg=='+'                 ) {iAddition=1; pszArg++; break   ;}
    if (*pszArg=='T' || *pszArg=='t' ) {return 0;                       }
    break;
  }

  /*--- Try to interpret the argument as "<value>"[+"unit"] --------*/
  if ((i=(int)strlen(pszArg)) == 0) {return -1;}
  if      (strchr(pszArg,'.')) {iType=1;}
  else if (strchr(pszArg,'e')) {iType=1;}
  else                         {
    strncpy(szTmp, pszArg, CTRL_FILE_BUF);
    if (szTmp[i-1]=='i') {szTmp[i-1]='\0';}
    if ((i=(int)strlen(szTmp))==0) {return -1;}
    if (szTmp[i-1]=='E') {szTmp[i-1]='\0';}
    if (strchr(szTmp,'e')||strchr(szTmp,'E')) {iType=1;}
    else                                      {iType=2;}
  }
  if (iType==1) {
    switch (sscanf(pszArg, "%lf%s", &dNum, szUnit)) {
      case   2:                     break;
      case   1: strcpy(szUnit, ""); break;
      default : return -1;
    }
    if (dNum < 0                     ) {return -1;}
  } else {
    switch (sscanf(pszArg, "%zu%s", &sizNum, szUnit)) {
      case   2:                     break;
      case   1: strcpy(szUnit, ""); break;
      default : return -1;
    }
  }

  /*--- Try to parse the unit part ---------------------------------*/
  if      (strcmp(szUnit,""  )==0) {ui8Scale=1;}
  else if (strcmp(szUnit,"k" )==0) {ui8Scale=1000;}
  else if (strcmp(szUnit,"K" )==0) {ui8Scale=1024;} /* conventional */
  else if (strcmp(szUnit,"ki")==0) {ui8Scale=1024;}
  else if (strcmp(szUnit,"M" )==0) {ui8Scale=1000000;}
  else if (strcmp(szUnit,"Mi")==0) {ui8Scale=1048576;}
  else if (strcmp(szUnit,"G" )==0) {ui8Scale=1000000000;}
  else if (strcmp(szUnit,"Gi")==0) {ui8Scale=1073741824;}
  else if (strcmp(szUnit,"T" )==0) {ui8Scale=1000000000000;}
  else if (strcmp(szUnit,"Ti")==0) {ui8Scale=1099511627776;}
  else if (strcmp(szUnit,"P" )==0) {ui8Scale=1000000000000000;}
  else if (strcmp(szUnit,"Pi")==0) {ui8Scale=1125899906842624;}
  else if (strcmp(szUnit,"E" )==0) {ui8Scale=1000000000000000000;}
  else if (strcmp(szUnit,"Ei")==0) {ui8Scale=1152921504606846976;}
  else                             {return -1;}

  /*--- Multiply them and return the quantity ----------------------*/
  if (iType == 1) {
    dNum *= ui8Scale;
    if (dNum   > (double)SIZE_MAX ) {*psiz=SIZE_MAX       ; return 1+iAddition;}
    else                            {*psiz=(size_t)dNum   ; return 1+iAddition;}
  } else          {
    if (sizNum > SIZE_MAX/ui8Scale) {*psiz=SIZE_MAX       ; return 1+iAddition;}
    else                            {*psiz=sizNum*ui8Scale; return 1+iAddition;}
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

/*=== Read and write only one line ===================================
 * [in] fp            : Filehandle for read
 * [ret] 0   : Finished reading/writing due to '\n'
 *       1   : Finished reading/writing due to '\n', which is the last
 *             char of the file
 *       EOF : Finished reading/writing due to EOF              */
int read_1line(FILE *fp) {

  /*--- Variables --------------------------------------------------*/
  char       szBuf[LINE_BUF]; /* Buffer for reading 1-line */
  int        iChar;           /* Buffer for reading 1-char */
  int        iLen;            /* Actual size of string in the buffer */

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

/*=== SIGNALHANDLER : Set the termination flag =======================
 * This function is for the main-th. to stop getc() blocking (!SA_RESTART)
 * or pthread_cond_wait() blocking. And then, the main-th. will terminate
 * spontaneously by noticing the flag "gstThCom.iTerm_req=1."       */
void term_request(int iSig, siginfo_t *siInfo, void *pct) {
  int i;
  gstThCom.iTerm_req=1;
  if ((i=pthread_cond_signal(&gstThCom.co)) != 0) {
    error_exit(i,"pthread_cond_signal() in term_request(): %s\n" , strerror(i));
  }
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
