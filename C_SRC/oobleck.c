/*####################################################################
#
# OOBLECK - Pass Through the Lines Not Updated for a Specified Time
#
# USAGE   : dilatant [-d fd|file] holdingtime [file]
#         : dilatant [-d fd|file] controlfile [file]
# Args    : holdingtime . The time of holding the current line until
#                         passing through.
#                         * If the next line did not come, the current
#                           line would be sent to the stdout. On the
#                           other hand, if the next line came while holding
#                           the current line, it would be overwritten
#                           with the next line.
#                         * The holding time means the term between the
#                           following two moments: A and B. A is the moment
#                           when the last byte (LF) of the current line is
#                           received, and B is the moment when the first
#                           byte of the next line arrives.
#                         * The unit of the holding time is second
#                           defaultly. You can also specify the unit
#                           like '100ms'. Available units are 's', 'ms',
#                           'us', 'ns.' The maximum value is INT_MAX for
#                            all units.
#                         * You can also specify it with the units "%."
#                           - '100%' (hold indefinitely)
#                           - '0%'   (output immediately without holding)
#           controlfile . Filepath to specify the holding time instead
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
#                             The holding time of cheking is 0.1 secs.
#                             If you want to apply the new parameter
#                             immediately, send me the SIGHUP after
#                             updating the file.
#                           - Character-special file / Named-pipe;
#                             It is better for the performance. If you
#                             use these types of files, you can write
#                             a new parameter with both the above two
#                             modes. The new parameter will be applied
#                             immediately just after writing.
#                         * If you change the parameter in the control
#                           file while this command is holding a line,
#                           the held line will be discarded, or drained
#                           if you set the -d option.
#           file ........ Filepath to be sent ("-" means STDIN)
#                         The file MUST be a textfile.
# Options : -d fd|file    If you set this option, the lines that will be
#                         dropped will be sent to the specified file
#                         descriptor or file.
#                         * When you set an integer, this command regards
#                           it as a file descriptor number. If you want
#                           to specify the file in the current directory
#                           that has a numerical filename, you have to
#                           add "./" before the name, like "./3."
#                         * When you set another type of string, this
#                           command regards it as a filename.
#
# How to compile : cc -O3 -o __CMDNAME__ __SRCNAME__ -pthread
#
# Retuen  : Return 0 only when finished successfully
#
# Written by Shell-Shoccar Japan (@shellshoccarjpn) on 2024-10-06
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
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <pthread.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

/*--- macro constants ----------------------------------------------*/
/* Default holding time parameter if a controlfile are given */
#define DEFAULT_HOLDINGTIME 0
/* Interval time of looking at the parameter on the control file */
#define FREAD_ITRVL_SEC  0
#define FREAD_ITRVL_USEC 100000
/* Buffer size for the control file */
#define CTRL_FILE_BUF 64
/* Unit size of "Elastic Line Buffer" */
#define ELBUF_SIZE 1024

/*--- structures ---------------------------------------------------*/
typedef struct _ELBUF {
  char           szBuf[ELBUF_SIZE];
  size_t         sSize;
  struct _ELBUF* pelbNext;
} elbuf_t;                /* Elastic Line Buffer                    */
typedef struct _thr_t {
  pthread_t       tMainth_id;       /* main thread ID                         */
  pthread_mutex_t mu;               /* The mutex variable                     */
  pthread_cond_t  co;               /* The condition variable                 */
  int             iSubth_iscreated; /* Set 1 when the subthread is created    */
  int             iMu_isready;      /* Set 1 when mu has been initialized     */
  int             iCo_isready;      /* Set 1 when co has been initialized     */
  int             iRequested__main; /* Req. received flag (only in mainth)    */
  int             iReceived;        /* Set 1 when the param. has been received*/
} thr_t;

/*--- prototype functions ------------------------------------------*/
void* param_updater(void* pvArgs);
void update_holding_time_type_r(char* pszCtrlfile);
void update_holding_time_type_c(char* pszCtrlfile);
int64_t parse_holdingtime(char *pszArg);
int read_1line_into_elbuf(FILE *fp, elbuf_t* elb);
void flush_elbuf(elbuf_t* elb, FILE* fp);
void free_elbuf(elbuf_t* elb);
void do_nothing(int iSig, siginfo_t *siInfo, void *pct);
void recv_param_application_req(int iSig, siginfo_t *siInfo, void *pct);
void destroy_thread_objects(void);

/*--- global variables ---------------------------------------------*/
char*    gpszCmdname;     /* The name of this command               */
int      giVerbose;       /* speaks more verbosely by the greater number     */
struct stat gstCtrlfile;  /* stat for the control file                       */
int64_t  gi8Holdtime;     /* Holding time in nanosecond (-1 means infinity)  */
elbuf_t  gelb = {0};      /* Elastic Line Buffer                             */
thr_t    gstTh;           /* Variables for threads communication             */

/*=== Define the functions for printing usage and error ============*/

/*--- exit with usage ----------------------------------------------*/
void print_usage_and_exit(void) {
  fprintf(stderr,
    "USAGE   : %s [-d fd|file] holdingtime [file]\n"
    "        : %s [-d fd|file] controlfile [file]\n"
    "Args    : holdingtime . The time of holding the current line until\n"
    "                        passing through.\n"
    "                        * If the next line did not come, the current\n"
    "                          line would be sent to the stdout. On the\n"
    "                          other hand, if the next line came while holding\n"
    "                          the current line, it would be overwritten\n"
    "                          with the next line.\n"
    "                        * The holding time means the term between the\n"
    "                          following two moments: A and B. A is the moment\n"
    "                          when the last byte (LF) of the current line is\n"
    "                          received, and B is the moment when the first\n"
    "                          byte of the next line arrives.\n"
    "                        * The unit of the holding time is second\n"
    "                          defaultly. You can also specify the unit\n"
    "                          like '100ms'. Available units are 's', 'ms',\n"
    "                          'us', 'ns.' The maximum value is INT_MAX for\n"
    "                           all units.\n"
    "                        * You can also specify it with the units \"%%.\"\n"
    "                          - '100%%' (hold indefinitely)\n"
    "                          - '0%%'   (output immediately without holding)\n"
    "          controlfile . Filepath to specify the holding time instead\n"
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
    "                          - Regular file:\n"
    "                            If you use a regular file as the control-\n"
    "                            file, you have to write a new parameter\n"
    "                            into it with the \"O_CREAT\" mode or \">\",\n"
    "                            not the \"O_APPEND\" mode or \">>\" because\n"
    "                            the command always checks the new para-\n"
    "                            meter at the head of the regular file\n"
    "                            periodically.\n"
    "                            The holding time of cheking is 0.1 secs.\n"
    "                            If you want to apply the new parameter\n"
    "                            immediately, send me the SIGHUP after\n"
    "                            updating the file.\n"
    "                          - Character-special file / Named-pipe:\n"
    "                            It is better for the performance. If you\n"
    "                            use these types of files, you can write\n"
    "                            a new parameter with both the above two\n"
    "                            modes. The new parameter will be applied\n"
    "                            immediately just after writing.\n"
    "                        * If you change the parameter in the control\n"
    "                          file while this command is holding a line,\n"
    "                          the held line will be discarded, or drained\n"
    "                          if you set the -d option.\n"
    "          file ........ Filepath to be sent (\"-\" means STDIN)\n"
    "                        The file MUST be a textfile.\n"
    "Options : -d fd|file .. If you set this option, the lines that will be\n"
    "                        dropped will be sent to the specified file\n"
    "                        descriptor or file.\n"
    "                        * When you set an integer, this command regards\n"
    "                          it as a file descriptor number. If you want\n"
    "                          to specify the file in the current directory\n"
    "                          that has a numerical filename, you have to\n"
    "                          add \"./\" before the name, like \"./3.\"\n"
    "                        * When you set another type of string, this\n"
    "                          command regards it as a filename.\n"
    "Version : 2024-10-06 03:15:16 JST\n"
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
  free_elbuf(&gelb); /* Special code for this command */
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
pthread_t tSub;           /* subthread ID                           */
char*    pszDrainname;    /* Drain stream name (for the -d option)  */
int      iDrainFd;        /* Drain filedesc. (for the -d option)    */
struct stat stCtrlfile;   /* stat for the control file              */
#if !defined(__APPLE__) && !defined(__OpenBSD__)
  struct itimerspec itInt;  /* for signal trap definition (interval)*/
  struct sigevent   seInf;  /* for interval event definition        */
  timer_t           trId;   /* signal timer ID                      */
#else
  struct itimerval  itInt;  /* for signal trap definition (interval)*/
#endif
char     szDummy[2];      /* Dummy string for sscanf()              */
char    *pszFilename;     /* filepath (for message)                 */
int      iFd;             /* file descriptor                        */
FILE    *fp;              /* file handle                            */
FILE    *fpDrain;         /* file handle for the drain              */
struct timespec tsHoldtime; /* The Holding time                     */
fd_set   fdsRead;         /* for pselect()                          */
int      iRet;            /* return code                            */
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
giVerbose    =    0;
iDrainFd     =   -1;
pszDrainname = NULL;
/*--- Parse options which start with "-" ---------------------------*/
while ((i=getopt(argc, argv, "d:hv")) != -1) {
  switch (i) {
    case 'd': if (sscanf(optarg,"%d%1s",&iDrainFd,szDummy) != 1) {iDrainFd=-1;}
              if (iDrainFd>=0) {pszDrainname=NULL;} else {pszDrainname=optarg;}
              break;
    case 'v': giVerbose++;    break;
    case 'h': print_usage_and_exit();
    default : print_usage_and_exit();
  }
}
if (giVerbose>0) {warning("verbose mode (level %d)\n",giVerbose);}
argc -= optind;
argv += optind;
/*--- Parse the holdingtime argument -------------------------------*/
if (argc < 1) {print_usage_and_exit();}
gi8Holdtime = parse_holdingtime(argv[0]);
if (gi8Holdtime <= -2) {
  /* Set the initial parameter, which means "immediately" */
  gi8Holdtime = 0;
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
  memset(&gstTh, 0, sizeof(gstTh));
  atexit(destroy_thread_objects);
  gstTh.tMainth_id = pthread_self();
  i = pthread_mutex_init(&gstTh.mu, NULL);
  if (i) {error_exit(i,"pthread_mutex_init() in main(): %s\n",strerror(i));}
  gstTh.iMu_isready = 1;
  i = pthread_cond_init(&gstTh.co, NULL);
  if (i) {error_exit(i,"pthread_cond_init() in main(): %s\n" ,strerror(i));}
  gstTh.iCo_isready = 1;
  i = pthread_create(&tSub,NULL,&param_updater,(void*)argv[0]);
  if (i) {error_exit(i,"pthread_create() in main(): %s\n"    ,strerror(i));}
  gstTh.iSubth_iscreated = 1;
  /* Register a SIGHUP handler to apply the new gi8Holdtime  */
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
} else {
  gstTh.iSubth_iscreated = 0;
}
argc--;
argv++;
if (argc > 1) {
  warning("Too many files specified. See the below help message.\n");
  print_usage_and_exit();
}

/*=== Main routine =================================================*/

/*--- Switch to the line-buffered mode -----------------------------*/
if (setvbuf(stdout,NULL,_IOLBF,0)!=0) {
  error_exit(255,"Failed to switch to line-buffered mode\n");
}
/*--- Open the input files -----------------------------------------*/
if (argv[0] == NULL || strcmp(argv[0], "-") == 0) {
  pszFilename = "stdin"                ;
  iFd         = STDIN_FILENO           ;
} else                                            {
  pszFilename = argv[0]                ;
  while ((iFd=open(argv[0], O_RDONLY)) < 0) {
    if (errno == EINTR) {continue;}
    error_exit(errno,"%s: %s\n",pszFilename,strerror(errno));
  }
}
if (iFd == STDIN_FILENO) {
  fp = stdin;
  if (feof(stdin)) {clearerr(stdin);} /* Reset EOF condition when stdin */
} else                   {
  fp = fdopen(iFd, "r");
}
/*--- Open the drain file if specified -----------------------------*/
if (pszDrainname != NULL) {
  while ((iDrainFd=open(pszDrainname,O_WRONLY|O_CREAT,0644))<0) {
    if (errno == EINTR) {continue;}
    error_exit(errno, "%s: %s\n", pszDrainname   ,
                                  strerror(errno) );
  };                       fpDrain = fdopen(iDrainFd, "w");  }
else if (iDrainFd != -1 ) {fpDrain = fdopen(iDrainFd, "w");  }
else                      {fpDrain = NULL;                   }
if (fpDrain) {
  if (setvbuf(fpDrain,NULL,_IOLBF,0)!=0) {
    error_exit(255,"Failed to switch to line-buffered mode (drain)\n");
  }
}

/*--- Play the oobleck (infinite loop) -----------------------------*/
do {
  /* 1) Read a line from stdin and store it in the EL-buffer */
  i = read_1line_into_elbuf(fp,&gelb);
  /* 2-a) If the stdin is EOF, flush the buffer */
  if      (i ==  0) {flush_elbuf(&gelb,stdout); break;                }
  /* 2-b) If some error happens on the stdin, exit */
  else if (i == -1) {error_exit(1,"%s: Reading error\n", pszFilename);}
  /* 2-c) If the stdin is not EOF yet, move on */
  /* 3) If the new parameter has arrived from the subthread, notify
        the acknowledgment to it.                                   */
  if (gstTh.iRequested__main) {
    if ((i=pthread_mutex_lock(&gstTh.mu)) != 0) {
      error_exit(i,"pthread_mutex_lock() in main(): %s\n", strerror(i));
    }
    gstTh.iReceived = 1;
    if ((i=pthread_cond_signal(&gstTh.co)) != 0) {
      error_exit(i,"pthread_cond_signal() in main(): %s\n", strerror(i));
    }
    if ((i=pthread_mutex_unlock(&gstTh.mu)) != 0) {
      error_exit(i,"pthread_mutex_unlock() in main(): %s\n", strerror(i));
    }
    if (giVerbose>0) {warning("gi8Holdtime=%ld\n",gi8Holdtime);}
    gstTh.iRequested__main = 0;
  }
  /* 4) (If the stdin is not EOF yet,) wait for the next line coming */
  FD_ZERO(     &fdsRead);
  FD_SET( iFd, &fdsRead);
  if (gi8Holdtime == -1) {
    i = pselect(iFd+1, &fdsRead, NULL, NULL, NULL, NULL);
  } else                 {
    tsHoldtime.tv_sec  = gi8Holdtime / 1000000000;
    tsHoldtime.tv_nsec = gi8Holdtime % 1000000000;
    i = pselect(iFd+1, &fdsRead, NULL, NULL, &tsHoldtime, NULL);
  }
  /* 4-a) If the next line has come in time, discard the current line */
  if      (i >  0                 ) {if(fpDrain){flush_elbuf(&gelb,fpDrain);};}
  /* 4-b) If the next line has not come in time, write the line to the stdout */
  else if (i == 0                 ) {flush_elbuf(&gelb,stdout);               }
  /* 4-c) If signal interruption happend, discard the current line, too */
  else if ((i==-1)&&(errno==EINTR)) {if(fpDrain){flush_elbuf(&gelb,fpDrain);};}
  /* 4-d) If another error happend, exit */
  else                   {error_exit(errno,"pselect(): %s\n",strerror(errno));}
} while (1);

/*=== Finish normally ==============================================*/
if (gstTh.iSubth_iscreated) {
  pthread_cancel(tSub); gstTh.iSubth_iscreated=0; pthread_join(tSub, NULL);
}
iRet=0;
return iRet;}



/*####################################################################
# Subthread (Parameter Updater)
####################################################################*/

/*=== Initialization ===============================================*/
void* param_updater(void* pvArgs) {

/*--- Variables ----------------------------------------------------*/
char*  pszCtrlfile;
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

/*=== The routine when the control file is a regular file ==========*/
if (gstCtrlfile.st_mode & S_IFREG) {update_holding_time_type_r(pszCtrlfile);}

/*=== The routine when the control file is a character special file */
else                               {update_holding_time_type_c(pszCtrlfile);}

/*=== End of the subthread (does not come here) ====================*/
return NULL;}



/*####################################################################
# Subroutines of the Subthread
####################################################################*/

/*=== Try to update "gi8Holdtime" for a regular file =================
 * [in]  pszCtrlfile   : Filename of the control file which the holding
 *                       time is written
 *       gi8Holdtime   : (must be defined as a global variable)
 *       gstTh.tMainth_id
 *                     : The main thread ID
 *       gstTh.mu      : Mutex object to lock
 *       gstTh.co      : Condition variable to send a signal to the sub-th
 * [out] gi8Holdtime   : Overwritten with the new parameter
 *       gstTh.iReceived
 *                     : Set to 0 after confirming that the main thread
 *                       receivedi the request                      */
void update_holding_time_type_r(char* pszCtrlfile) {

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
    i8 = parse_holdingtime(szBuf);
    if (i8          <= -2                                  ) {goto pause;}
    if (gi8Holdtime == i8                                  ) {goto pause;}
    /* 2) Update the holding time */
    gi8Holdtime = i8;
    if (pthread_kill(gstTh.tMainth_id, SIGHUP) != 0) {
      error_exit(errno,"pthread_kill() in type_r(): %s\n",strerror(errno));
    }
    if ((i=pthread_mutex_lock(&gstTh.mu)) != 0) {
      error_exit(i,"pthread_mutex_lock() in type_r(): %s\n"  , strerror(i));
    }
    while (! gstTh.iReceived) {
      if ((i=pthread_cond_wait(&gstTh.co, &gstTh.mu)) != 0) {
        error_exit(i,"pthread_cond_wait() in type_r(): %s\n" , strerror(i));
      }
    }
    gstTh.iReceived = 0;
    if ((i=pthread_mutex_unlock(&gstTh.mu)) != 0) {
      error_exit(i,"pthread_mutex_unlock() in type_r(): %s\n", strerror(i));
    }
    /* 3) Wait for the next timing */
pause:
    pause();
  }
}

/*=== Try to update "gi8Holdtime" for a char-sp/FIFO file
 * [in]  pszCtrlfile   : Filename of the control file which the holding
 *                       time is written
 *       gi8Holdtime   : (must be defined as a global variable)
 *       gstTh.tMainth_id
 *                     : The main thread ID
 *       gstTh.mu      : Mutex object to lock
 *       gstTh.co      : Condition variable to send a signal to the sub-th
 * [out] gi8Holdtime   : Overwritten with the new parameter
 *       gstTh.iReceived
 *                     : Set to 0 after confirming that the main thread
 *                       receivedi the request                      */
void update_holding_time_type_c(char* pszCtrlfile) {

  /*--- Variables --------------------------------------------------*/
  int     iFd_ctrlfile             ; /* file desc. of the ctrlfile  */
  char    cBuf0[2][CTRL_FILE_BUF]  ; /* 0th buffers (two bunches)   */
  int     iBuf0DatSiz[2]           ; /* Data sizes of the two       */
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
  if ((iFd_ctrlfile=open(pszCtrlfile,O_RDONLY)) < 0) {
    error_exit(errno,"%s: %s\n",pszCtrlfile,strerror(errno));
  }
  fdsPoll[0].fd     = iFd_ctrlfile;
  fdsPoll[0].events = POLLIN      ;

  /*--- Begin of the infinite loop ---------------------------------*/
  while (1) {

  /*--- Read the ctrlfile and write the data into the Buf0          *
   *    until the unread data does not remain              ---------*/
  iBuf0DatSiz[0]=0; iBuf0DatSiz[1]=0;
  iBuf0Lst      =1; iBuf0ReadTimes=0;
  do {
    iBuf0Lst=1-iBuf0Lst;
    iBuf0DatSiz[iBuf0Lst]=read(iFd_ctrlfile,cBuf0[iBuf0Lst],CTRL_FILE_BUF);
    iBuf0ReadTimes++;
  } while ((i=poll(fdsPoll,1,0)) > 0);
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
    i8 = parse_holdingtime(szCmdbuf);
    if (i8          <= -2) {
      szCmdbuf[0]='\0'; continue; /* Invalid holding time */
    }
    if (gi8Holdtime == i8) {
      szCmdbuf[0]='\0'; continue; /* Parameter does not change */
    }
    gi8Holdtime = i8;
    if (pthread_kill(gstTh.tMainth_id, SIGHUP) != 0) {
      error_exit(errno,"pthread_kill() in type_c(): %s\n",strerror(errno));
    }
    if ((k=pthread_mutex_lock(&gstTh.mu)) != 0) {
      error_exit(k,"pthread_mutex_lock() in type_c(): %s\n"  , strerror(k));
    }
    while (! gstTh.iReceived) {
      if ((k=pthread_cond_wait(&gstTh.co, &gstTh.mu)) != 0) {
        error_exit(k,"pthread_cond_wait() in type_c(): %s\n" , strerror(k));
      }
    }
    gstTh.iReceived = 0;
    if ((k=pthread_mutex_unlock(&gstTh.mu)) != 0) {
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
}



/*####################################################################
# Other Functions
####################################################################*/

/*=== Parse the holding time =========================================
 * [ret] >= 0  : Interval value (in nanosecound)
 *       <=-1  : Means infinity (completely shut the valve)
 *       <=-2  : It is not a value                                  */
int64_t parse_holdingtime(char *pszArg) {

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

  /* as a % value (only "0%" or "100%") */
  if (strcmp(szUnit, "%")==0)   {
    if      (dNum==100) {return -1;}
    else if (dNum==  0) {return  0;}
    else                {return -2;}
  }

  /*--- Otherwise, it is not a value -------------------------------*/
  return -2;
}

/*=== Read one line into the elastic line buffer =====================
 * [in] fp  : Filehandle for read
 *      elb : Pointer of the elastic line buffer
 * [ret]  1 : Finished reading with '\n'
 *        0 : Finished reading due to EOF
 *       -1 : Finished reading due to an file error                 */
int read_1line_into_elbuf(FILE *fp, elbuf_t* elb) {
  elbuf_t* pelbCurrent;
  elbuf_t* pelbNew;
  
  if (! fp ) {error_exit(1,"read_1line_into_elbuf(): fp is NULL\n" );}
  if (! elb) {error_exit(1,"read_1line_into_elbuf(): elb is NULL\n");}
  
  pelbCurrent=elb;
  while (fgets(pelbCurrent->szBuf, sizeof(pelbCurrent->szBuf), fp)) {
    pelbCurrent->sSize = strlen(pelbCurrent->szBuf);
    if (pelbCurrent->sSize < sizeof(pelbCurrent->szBuf)-1) {
      if (pelbCurrent->szBuf[pelbCurrent->sSize-1] == '\n') {return 1;}
      if (feof(  fp)) {return  0;}
      if (ferror(fp)) {return -1;}
      error_exit(1,"read_1line_into_elbuf(): Unexpected error #1\n");
    }
    if (pelbCurrent->szBuf[pelbCurrent->sSize-1] == '\n') {
      if (pelbCurrent->pelbNext != NULL) {pelbCurrent->pelbNext->sSize=0;}
      return 1;
    }
    if (pelbCurrent->pelbNext == NULL) {
      if ((pelbNew=(elbuf_t*)malloc(sizeof(elbuf_t))) == NULL) {
        error_exit(1,"Memory is not enough.\n");
      }
      pelbNew->pelbNext     = NULL;
      pelbCurrent->pelbNext = pelbNew;
    }
    pelbCurrent = pelbCurrent->pelbNext;
  }
  if (feof(  fp)) {return  0;}
  if (ferror(fp)) {return -1;}
  error_exit(1,"read_1line_into_elbuf(): Unexpected error #2\n");
  return -1;
}

/*=== Flush the elastic line buffer to the stdout ====================
 * [in] elb : Pointer of the elastic line buffer                    */
void flush_elbuf(elbuf_t* elb, FILE* fp) {
  elbuf_t* pelbCurrent;

  if (! elb) {error_exit(1,"flush_elbuf(): elb is NULL\n");}
  if (!  fp) {error_exit(1,"flush_elbuf(): fp is NULL\n" );}

  pelbCurrent = elb;
  do {
    if (pelbCurrent->sSize == 0) {break;}
    if (fputs(pelbCurrent->szBuf, fp) == EOF) {
      error_exit(errno,"Write error: %s\n",strerror(errno));
    }
    /* Flush the next buffer if all of the following conditions are satisfied.
     *   a. The size of the current buffer is full.
     *   b. The current buffer is not terminated with "\n."
     *   c. The next buffer exists.                                           */
    if (pelbCurrent->sSize < sizeof(pelbCurrent->szBuf)-1) {break;}
    if (pelbCurrent->szBuf[pelbCurrent->sSize-1] == '\n' ) {break;}
    pelbCurrent = pelbCurrent->pelbNext;
  } while (pelbCurrent);
  elb->sSize = 0;
}

/*=== Release the memory for the elastic line buffer  ================
 * [in] elb : Pointer of the elastic line buffer                    */
void free_elbuf(elbuf_t* elb) {
  elbuf_t* pelbCurrent;
  elbuf_t* pelbNext   ;

  if (! elb) {error_exit(1,"free_elbuf(): elb is NULL\n");}

  if (elb->pelbNext == NULL) {return;} /* Does not release the 1st chain */

  pelbCurrent = elb->pelbNext;
  while (pelbCurrent != NULL) {
    pelbNext = pelbCurrent->pelbNext;
    free(pelbCurrent);
    pelbCurrent = pelbNext;
  }
}

/*=== SIGNALHANDLER : Do nothing =====================================
 * This function does nothing, but it is helpful to break a thread
 * sleeping by using me as a signal handler.                        */
void do_nothing(int iSig, siginfo_t *siInfo, void *pct) {return;}

/*=== SIGNALHANDLER : Received the parameter application request =====
 * This function sets the flag of the new parameter application request.
 * This function should be called as a signal handler so that you can
 * wake myself (the main thread) up even while sleeping with the
 * nanosleep().
 * [out] gstTh.iRequested : set to 1 to notify the main-th of the request */
void recv_param_application_req(int iSig, siginfo_t *siInfo, void *pct) {
  gstTh.iRequested__main = 1;
  return;
}

/*=== EXITHANDLER : Destroy thread objects =========================*/
void destroy_thread_objects(void) {
  if (gstTh.iMu_isready) {pthread_mutex_destroy(&gstTh.mu);gstTh.iMu_isready=0;}
  if (gstTh.iCo_isready) {pthread_cond_destroy( &gstTh.co);gstTh.iCo_isready=0;}
  return;
}
