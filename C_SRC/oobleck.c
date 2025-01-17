/*####################################################################
#
# OOBLECK - Output Lines Only When the Next Line Does Not Arrive for a While
#
# USAGE   : oobleck [-d fd|file] [-p n] holdingtime [file]
#         : oobleck [-d fd|file] [-p n] controlfile [file]
# Args    : holdingrule . Rule to hold the data from the data source.
#                         You can specify it by the following two methods.
#                           a. holding-time
#                              * The time of holding the current line
#                                until passing through.
#                                + If the next line did not come, the
#                                  current line would be sent to the
#                                  stdout. On the other hand, if the next
#                                  line came while holding the current
#                                  line, it would be overwritten with
#                                  the next line.
#                                + The holding-time means the term between
#                                  the following two moments: A and B.
#                                  A is the moment when the last byte
#                                  (LF) of the current line is received,
#                                  and B is the moment when the first
#                                  byte of the next line arrives.
#                                + The unit of the holding-time is second
#                                  defaultly. You can also specify the
#                                  unit like '100ms'. Available units are
#                                  's', 'ms', 'us', 'ns.' The maximum
#                                   value is INT_MAX for all units.
#                                + You can also specify it with the units
#                                  "%."
#                                  - '100%' (hold indefinitely)
#                                  - '0%'   (output immediately without
#                                            holding)
#                              * In this method, the number of lines when
#                                the command passes through the data is
#                                one. Use the following method if you
#                                want two or more lines.
#                           b. number-of-lines and holding-time
#                              * This method specifies two parameters,
#                                The latter one is entirely the same as
#                                the above.
#                              * The former one, "number-of-lines," is
#                                the number of lines of data this command
#                                will hold. If you set it to "n," this
#                                command will always hold the latest n
#                                lines of the incoming data on memory and
#                                flush them all when the holding-time has
#                                elapsed.
#                              * The usage is "number@time."
#                                + "number" is the number-of-lines. You
#                                  can set only a natural number from 1
#                                  to 256.
#                                + "@" is the delimiter to seperate
#                                  parts. Any whitespace characters are
#                                  not allowed to be inserted before and
#                                  after the atmark.
#                                + "time" is the holding-time. The usage
#                                  is explained in the previous section.
#                              * For example, if you want to get the last
#                                three lines when the incoming text data
#                                lets up for 500ms, you can write
#                                "3@500ms" as the holdingrule argument.
#           controlfile . Filepath to specify the holding-time instead
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
#                           + Regular file:
#                             If you use a regular file as the control-
#                             file, you have to write a new parameter
#                             into it with the "O_CREAT" mode or ">",
#                             not the "O_APPEND" mode or ">>" because
#                             the command always checks the new para-
#                             meter at the head of the regular file
#                             periodically.
#                             The holding-time of cheking is 0.1 secs.
#                             If you want to apply the new parameter
#                             immediately, send me the SIGHUP after
#                             updating the file.
#                           + Character-special file / Named-pipe;
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
#           [Only some operating systems support the following option]
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
# How to compile : cc -O3 -o __CMDNAME__ __SRCNAME__ -pthread
#
# Written by Shell-Shoccar Japan (@shellshoccarjpn) on 2025-01-17
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
#include <poll.h>
#include <pthread.h>
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
#if defined(_POSIX_PRIORITY_SCHEDULING) && !defined(__OpenBSD__) && !defined(__APPLE__)
  #include <sched.h>
  #include <sys/resource.h>
#endif

/*--- macro constants ----------------------------------------------*/
#define RINGBUF_NUM_MAX 256
/* Default holding time and lines parameter if the controlfile is given */
#define DEFAULT_HOLDINGTIME  0
#define DEFAULT_HOLDINGLINES 1
/* Interval time of looking at the parameter on the control file */
#define FREAD_ITRVL_SEC  0
#define FREAD_ITRVL_USEC 100000
/* Buffer size for the control file */
#define CTRL_FILE_BUF 64
/* Unit size of "Elastic Line Buffer" */
#define ELBUF_SIZE 1024

/*--- data type definitions ----------------------------------------*/
typedef struct timespec tmsp;
typedef struct _ELBUF {
  char            szBuf[ELBUF_SIZE];
  size_t          sSize;
  struct _ELBUF*  pelbNext;
} elbuf_t;                /* Elastic Line Buffer                    */
typedef struct _RINGBUF {
  elbuf_t*        pelbRing;    /* Pointer of the ring buffer */
  int             iSize;       /* Size (Number of ELBs)      */
  int             iLatestLine; /* Which Line Is the Last One */
} ringbuf_t;              /* Bunch of the ELBs (Ring buffer)        */
typedef struct _thrcom_t {
  pthread_t       tMainth_id;       /* main thread ID                         */
  pthread_mutex_t mu;               /* The mutex variable                     */
  pthread_cond_t  co;               /* The condition variable                 */
  int             iRequested__main; /* Req. received flag (only in mainth)    */
  int             iReceived;        /* Set 1 when the param. has been received*/
  int64_t         i8Param1;         /* int64 variable #1 to sent to the mainth*/
  int             iParam1;          /* int variable #1 to sent to the mainth  */
} thcominfo_t;
typedef struct _thrmain_t {
  pthread_t       tSubth_id;        /* sub thread ID                          */
  int             iMu_isready;      /* Set 1 when mu has been initialized     */
  int             iCo_isready;      /* Set 1 when co has been initialized     */
  FILE*           fpIn;             /* File handle for the current input file */
  FILE*           fpDrain;          /* File handle for the drain file         */
} thmaininfo_t;

/*--- prototype functions ------------------------------------------*/
void* param_updater(void* pvArgs);
void update_holding_time_type_r(char* pszCtrlfile);
void update_holding_time_type_c(char* pszCtrlfile);
int parse_holdingrule(char *pszRule, int64_t* pi8Hldtime, int* piNumlin);
int64_t parse_holdingtime(char *pszArg);
int read_1line_into_ringbuf(FILE *fp, ringbuf_t* pstRingbuf);
void flush_elbuf_chain(elbuf_t* pelbHead, FILE* fp);
void flush_ringbuf(ringbuf_t* pstRingbuf, FILE* fp);
void release_following_elbufs(elbuf_t* elb);
int  create_ring_buf(ringbuf_t* pstRingbuf);
void destroy_ring_buf(ringbuf_t* pstRingbuf);
int change_to_rtprocess(int iPrio);
void do_nothing(int iSig, siginfo_t *siInfo, void *pct);
#ifdef __ANDROID__
void term_this_thread(int iSig, siginfo_t *siInfo, void *pct);
#endif
void recv_param_application_req(int iSig, siginfo_t *siInfo, void *pct);
void mainth_destructor(void* pvMainth);
void subth_destructor(void *pvFd);

/*--- global variables ---------------------------------------------*/
char*    gpszCmdname;     /* The name of this command               */
int      giVerbose;       /* speaks more verbosely by the greater number     */
struct stat gstCtrlfile;  /* stat for the control file                       */
int      giHoldlines = 0; /* Number of lines to be kept in the buffer
                           * - It is global but for only the main-th. The
                           *   sub-th has to write the parameter into the
                           *   gstThCom.iParam1 instead when the sub-th
                           *   gives the main-th the new parameter.          */
int64_t  gi8Holdtime;     /* Holding time in nanosecond (-1 means infinity)
                           * - It is global but for only the main-th. The
                           *   sub-th has to write the parameter into the
                           *   gstThCom.i8Param1 instead when the sub-th
                           *   gives the main-th the new parameter.          */
ringbuf_t gstRingBuf = {0}; /* Ringed Buffer of Elastic Line Buffer          */
thcominfo_t gstThCom;      /* Variables for threads communication            */

/*=== Define the functions for printing usage and error ============*/

/*--- exit with usage ----------------------------------------------*/
void print_usage_and_exit(void) {
  fprintf(stderr,
#if defined(_POSIX_PRIORITY_SCHEDULING) && !defined(__OpenBSD__) && !defined(__APPLE__)
    "USAGE   : %s [-d fd|file] [-p n] holdingtime [file]\n"
    "        : %s [-d fd|file] [-p n] controlfile [file]\n"
#else
    "USAGE   : %s [-d fd|file] holdingtime [file]\n"
    "        : %s [-d fd|file] controlfile [file]\n"
#endif
    "Args    : holdingrule . Rule to hold the data from the data source.\n"
    "                        You can specify it by the following two methods.\n"
    "                          a. holding-time\n"
    "                             * The time of holding the current line\n"
    "                               until passing through.\n"
    "                               + If the next line did not come, the\n"
    "                                 current line would be sent to the\n"
    "                                 stdout. On the other hand, if the next\n"
    "                                 line came while holding the current\n"
    "                                 line, it would be overwritten with\n"
    "                                 the next line.\n"
    "                               + The holding time means the term between\n"
    "                                 the following two moments: A and B.\n"
    "                                 A is the moment when the last byte\n"
    "                                 (LF) of the current line is received,\n"
    "                                 and B is the moment when the first\n"
    "                                 byte of the next line arrives.\n"
    "                               + The unit of the holding time is second\n"
    "                                 defaultly. You can also specify the\n"
    "                                 unit like '100ms'. Available units are\n"
    "                                 's', 'ms', 'us', 'ns.' The maximum\n"
    "                                  value is INT_MAX for all units.\n"
    "                               + You can also specify it with the units\n"
    "                                 \"%%.\"\n"
    "                                 - '100%%' (hold indefinitely)\n"
    "                                 - '0%%'   (output immediately without\n"
    "                                           holding)\n"
    "                             * In this method, the number of lines when\n"
    "                               the command passes through the data is\n"
    "                               one. Use the following method if you\n"
    "                               want two or more lines.\n"
    "                          b. number-of-lines and holding-time\n"
    "                             * This method specifies two parameters,\n"
    "                               The latter one is entirely the same as\n"
    "                               the above.\n"
    "                             * The former one, \"number-of-lines,\" is\n"
    "                               the number of lines of data this command\n"
    "                               will hold. If you set it to \"n,\" this\n"
    "                               command will always hold the latest n\n"
    "                               lines of the incoming data in memory and\n"
    "                               flush them all when the holding-time has\n"
    "                               elapsed.\n"
    "                             * The usage is \"number@time.\"\n"
    "                               + \"number\" is the number-of-lines. You\n"
    "                                 can set only a natural number from 1\n"
    "                                 to 256.\n"
    "                               + \"@\" is the delimiter to seperate\n"
    "                                 parts. Any whitespace characters are\n"
    "                                 not allowed to be inserted before and\n"
    "                                 after the atmark.\n"
    "                               + \"time\" is the holding-time. The usage\n"
    "                                 is explained in the previous section.\n"
    "                             * For example, if you want to get the last\n"
    "                               three lines when the incoming text data\n"
    "                               lets up for 500ms, you can write\n"
    "                               \"3@500ms\" as the holdingrule argument.\n"
    "          controlfile . Filepath to specify the holding-time instead\n"
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
    "                          + Regular file:\n"
    "                            If you use a regular file as the control-\n"
    "                            file, you have to write a new parameter\n"
    "                            into it with the \"O_CREAT\" mode or \">\",\n"
    "                            not the \"O_APPEND\" mode or \">>\" because\n"
    "                            the command always checks the new para-\n"
    "                            meter at the head of the regular file\n"
    "                            periodically.\n"
    "                            The holding-time of cheking is 0.1 secs.\n"
    "                            If you want to apply the new parameter\n"
    "                            immediately, send me the SIGHUP after\n"
    "                            updating the file.\n"
    "                          + Character-special file / Named-pipe:\n"
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
    "Version : 2025-01-17 13:45:36 JST\n"
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
char*    pszDrainname;    /* Drain stream name (for the -d option)  */
int      iDrainFd;        /* Drain filedesc. (for the -d option)    */
int      iPrio;           /* -p option number (default 1)           */
struct stat stCtrlfile;   /* stat for the control file              */
char     szDummy[2];      /* Dummy string for sscanf()              */
char    *pszFilename;     /* filepath (for message)                 */
int      iFd;             /* file descriptor                        */
tmsp     tsHoldtime;      /* The Holding time                       */
fd_set   fdsRead;         /* for pselect()                          */
int      iRet;            /* return code                            */
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
giVerbose    =    0;
iDrainFd     =   -1;
iPrio        =    1;
pszDrainname = NULL;
/*--- Parse options which start with "-" ---------------------------*/
while ((i=getopt(argc, argv, "d:p:hv")) != -1) {
  switch (i) {
    case 'd': if (sscanf(optarg,"%d%1s",&iDrainFd,szDummy) != 1) {iDrainFd=-1;}
              if (iDrainFd>=0) {pszDrainname=NULL;} else {pszDrainname=optarg;}
              break;
#if defined(_POSIX_PRIORITY_SCHEDULING) && !defined(__OpenBSD__) && !defined(__APPLE__)
    case 'p': if (sscanf(optarg,"%d",&iPrio) != 1) {print_usage_and_exit();}
              break;
#endif
    case 'v': giVerbose++;    break;
    case 'h': print_usage_and_exit();
    default : print_usage_and_exit();
  }
}
if (giVerbose>0) {warning("verbose mode (level %d)\n",giVerbose);}
argc -= optind;
argv += optind;
if (argc < 1) {print_usage_and_exit();}
/*--- Prepare the thread operation ---------------------------------*/
memset(&gstThCom, 0, sizeof(gstThCom    ));
memset(&stMainth, 0, sizeof(thmaininfo_t));
pthread_cleanup_push(mainth_destructor, &stMainth);
/*--- Parse the holdingtime argument -------------------------------*/
i = parse_holdingrule(argv[0], &gi8Holdtime, &giHoldlines);
if (i != 0) {
  /* Set the initial parameter, which means "immediately" */
  gi8Holdtime=DEFAULT_HOLDINGTIME ; gstThCom.i8Param1=gi8Holdtime;
  giHoldlines=DEFAULT_HOLDINGLINES; gstThCom.iParam1 =giHoldlines;
  gstRingBuf.iSize = giHoldlines; gstRingBuf.pelbRing = NULL;
  if (create_ring_buf(&gstRingBuf) > 0) {
    error_exit(errno,"create_ring_buf() in main() #1\n");
  }
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
  /* Register a SIGHUP handler to apply the new parameters */
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
  stMainth.fpIn = stdin;
  if (feof(stdin)) {clearerr(stdin);} /* Reset EOF condition when stdin */
} else                   {
  stMainth.fpIn = fdopen(iFd, "r");
}
/*--- Open the drain file if specified -----------------------------*/
if (pszDrainname != NULL) {
  while ((iDrainFd=open(pszDrainname,O_WRONLY|O_CREAT,0644))<0) {
    if (errno == EINTR) {continue;}
    error_exit(errno, "%s: %s\n", pszDrainname   ,
                                  strerror(errno) );
  };                       stMainth.fpDrain = fdopen(iDrainFd, "w");  }
else if (iDrainFd != -1 ) {stMainth.fpDrain = fdopen(iDrainFd, "w");  }
else                      {stMainth.fpDrain = NULL;                   }
if (stMainth.fpDrain) {
  if (setvbuf(stMainth.fpDrain,NULL,_IOLBF,0)!=0) {
    error_exit(255,"Failed to switch to line-buffered mode (drain)\n");
  }
}

/*--- Play the oobleck (infinite loop) -----------------------------*/
do {
  /* 1) Create/Recreate the ring buffer if required */
  if (gstRingBuf.iSize != giHoldlines) {
    if (giVerbose>0) {
      warning("RingBuffer will be recreated (size: %d -> %d)\n",
              gstRingBuf.iSize, giHoldlines                     );
    }
    if (gstRingBuf.iSize > 0) {
      if(stMainth.fpDrain){flush_ringbuf(&gstRingBuf,stMainth.fpDrain);}
      destroy_ring_buf(&gstRingBuf);
    }
    gstRingBuf.iSize = giHoldlines;
    if (create_ring_buf(&gstRingBuf) > 0) {
      error_exit(errno,"create_ring_buf() in main() #2\n");
    }
  }
  /* 2) Read a line from stdin and store it in the EL-buffer */
  i = read_1line_into_ringbuf(stMainth.fpIn,&gstRingBuf);
  /* 3-a) If the stdin is EOF, flush the buffer */
  if      (i ==  0) {flush_ringbuf(&gstRingBuf,stdout); break;        }
  /* 3-b) If some error happens on the stdin, exit */
  else if (i == -1) {error_exit(1,"%s: Reading error\n", pszFilename);}
  /* 3-c) If the stdin is not EOF yet, move on */
  /* 4) If the new parameter has arrived from the subthread, notify
        the acknowledgment to it.                                   */
  if (gstThCom.iRequested__main) {
    if ((i=pthread_mutex_lock(&gstThCom.mu)) != 0) {
      error_exit(i,"pthread_mutex_lock() in main(): %s\n", strerror(i));
    }
    gstThCom.iReceived = 1;
    if ((i=pthread_cond_signal(&gstThCom.co)) != 0) {
      error_exit(i,"pthread_cond_signal() in main(): %s\n", strerror(i));
    }
    if ((i=pthread_mutex_unlock(&gstThCom.mu)) != 0) {
      error_exit(i,"pthread_mutex_unlock() in main(): %s\n", strerror(i));
    }
    if (giVerbose>0) {warning("gi8Holdtime=%ld\n",gi8Holdtime);}
    gstThCom.iRequested__main = 0;
  }
  /* 5) (If the stdin is not EOF yet,) wait for the next line coming */
  FD_ZERO(     &fdsRead);
  FD_SET( iFd, &fdsRead);
  if (gi8Holdtime == -1) {
    i = pselect(iFd+1, &fdsRead, NULL, NULL, NULL, NULL);
  } else                 {
    tsHoldtime.tv_sec  = gi8Holdtime / 1000000000;
    tsHoldtime.tv_nsec = gi8Holdtime % 1000000000;
    i = pselect(iFd+1, &fdsRead, NULL, NULL, &tsHoldtime, NULL);
  }
  /* 6-a) If the next line has come in time, discard the current line */
  if      ( i>  0                 ) {
    /* If fpDrain is open and the incoming data still continues, flush
       the oldest line now. Otherwise, the oldest line, which should be
       output to the drain, will be lost by the next reading.           */
    if (stMainth.fpDrain && ((i=fgetc(stMainth.fpIn))!=EOF)) {
      ungetc(i, stMainth.fpIn);
      i = (gstRingBuf.iLatestLine+1) % gstRingBuf.iSize;
      flush_elbuf_chain(&gstRingBuf.pelbRing[i], stMainth.fpDrain);
    }
  }
  /* 6-b) If the next line has not come in time, write the line to the stdout */
  else if ( i== 0                 ) {flush_ringbuf(&gstRingBuf,stdout);}
  /* 6-c) If signal interruption happend, discard the current line, too */
  else if ((i==-1)&&(errno==EINTR)) {
    if(stMainth.fpDrain){flush_ringbuf(&gstRingBuf,stMainth.fpDrain);}
  }
  /* 6-d) If another error happend, exit */
  else                   {error_exit(errno,"pselect(): %s\n",strerror(errno));}
} while (1);

/*=== Finish normally ==============================================*/
pthread_cleanup_pop(1);
iRet=0;
return iRet;}



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
if (gstCtrlfile.st_mode & S_IFREG) {update_holding_time_type_r(pszCtrlfile);}

/*=== The routine when the control file is a character special file */
else                               {update_holding_time_type_c(pszCtrlfile);}

/*=== End of the subthread (does not come here) ====================*/
return NULL;}



/*####################################################################
# Subroutines of the Subthread
####################################################################*/

/*=== Try to update the parameter for a regular file =================
 * [in]  pszCtrlfile      : Filename of the control file which the holding
 *                          time is written
 *       gstThCom.tMainth_id
 *                        : The main thread ID
 *       gstThCom.mu      : Mutex object to lock
 *       gstThCom.co      : Condition variable to send a signal to the sub-th
 * [out] gstThCom.iParam1 : The new parameter (int)
 *       gstThCom.i8Param1: The new parameter (int64_t)
 *       gstThCom.iReceived
 *                        : Set to 0 after confirming that the main thread
 *                          receivedi the request                      */
void update_holding_time_type_r(char* pszCtrlfile) {

  /*--- Variables --------------------------------------------------*/
  struct sigaction saAlrm; /* for signal handler definition (action)   */
  sigset_t         ssMask; /* unblocking signal list                   */
  struct itimerval itInt ; /* for signal handler definition (interval) */
  int              iFd_ctrlfile        ; /* file desc. of the ctrlfile */
  char             szBuf[CTRL_FILE_BUF]; /* parameter string buffer    */
  int              iLen                ; /* length of the parameter str*/
  int64_t          i8                  ;
  int              i,j                 ;

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
    i = parse_holdingrule(szBuf, &i8, &j);
    if (i != 0                                             ) {goto pause;}
    if ((gstThCom.i8Param1==i8) && (gstThCom.iParam1==j)   ) {goto pause;}
    /* 2) Update the holding time */
    gstThCom.i8Param1 = i8;
    gstThCom.iParam1  =  j;
    if (pthread_kill(gstThCom.tMainth_id, SIGHUP) != 0) {
      error_exit(errno,"pthread_kill() in type_r(): %s\n",strerror(errno));
    }
    if ((i=pthread_mutex_lock(&gstThCom.mu)) != 0) {
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

/*=== Try to update the parameter for a char-sp/FIFO file ============
 * [in]  pszCtrlfile      : Filename of the control file which the holding
 *                          time is written
 *       gstThCom.tMainth_id
 *                        : The main thread ID
 *       gstThCom.mu      : Mutex object to lock
 *       gstThCom.co      : Condition variable to send a signal to the sub-th
 * [out] gstThCom.iParam1 : The new parameter (int)
 *       gstThCom.i8Param1: The new parameter (int64_t)
 *       gstThCom.iReceived
 *                        : Set to 0 after confirming that the main thread
 *                          receivedi the request                      */
void update_holding_time_type_c(char* pszCtrlfile) {

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
    j = parse_holdingrule(szBuf1, &i8, &k);
    if (j != 0                                          ) {
      szCmdbuf[0]='\0'; continue; /* Invalid rule string */
    }
    if ((gstThCom.i8Param1==i8) && (gstThCom.iParam1==k)) {
      szCmdbuf[0]='\0'; continue; /* Parameters do not change */
    }
    gstThCom.i8Param1 = i8;
    gstThCom.iParam1  =  k;
    if (pthread_kill(gstThCom.tMainth_id, SIGHUP) != 0) {
      error_exit(errno,"pthread_kill() in type_c(): %s\n",strerror(errno));
    }
    if ((j=pthread_mutex_lock(&gstThCom.mu)) != 0) {
      error_exit(j,"pthread_mutex_lock() in type_c(): %s\n"  , strerror(j));
    }
    while (! gstThCom.iReceived) {
      if ((j=pthread_cond_wait(&gstThCom.co, &gstThCom.mu)) != 0) {
        error_exit(j,"pthread_cond_wait() in type_c(): %s\n" , strerror(j));
      }
    }
    gstThCom.iReceived = 0;
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

  /*--- End of the function (does not come here) -------------------*/
  pthread_cleanup_pop(0);
}



/*####################################################################
# Other Functions
####################################################################*/

/*=== Parse a ratelimit string =======================================
 * [in] pszRule    : The string to be parsed as a "holdingrule"
 *      pi8Hldtime : The pointer to get the parsed holdingtime part
 *      piNumlin   : The pointer to get the parsed number-of-lines part
 * [ret] ==0       : Succeed in parsing the parameters
 *       ==1       : Argument error (e.g. null pointer)
 *       ==2       : Invalid rule string
 * [note] the values of {pi8Hldtime,piNumlin} will be overwritten
 *        whether the parsing succeeds or not.                      */
int parse_holdingrule(char *pszRule, int64_t* pi8Hldtime, int* piNumlin) {

  /*--- Definitions ------------------------------------------------*/
  char* psz;

  /*--- Validate the arguments -------------------------------------*/
  if (! pszRule   ) {return 1;}
  if (! pi8Hldtime) {return 1;}
  if (! piNumlin  ) {return 1;}

  /*--- Parse ------------------------------------------------------*/
  if ((psz=strchr(pszRule,'@')) != NULL){
    if (sscanf(pszRule,"%d",piNumlin) != 1) {return 2;}
    psz++;
  }else{
    *piNumlin =       1;
    psz       = pszRule;
  }
  *pi8Hldtime = parse_holdingtime(psz);
  if (*piNumlin<1 || RINGBUF_NUM_MAX<*piNumlin) {return 2;}
  if (*pi8Hldtime <= -2                       ) {return 2;}

  /*--- Finish successfully ----------------------------------------*/
  return 0;
}


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

/*=== Read one line into the ELB ring buffer =========================
 * [in] fp         : Filehandle for read
 *      pstRingbuf : Pointer of the ELB ring buffer
 *                   This argument must have the correct iSize and
 *                   iLatestLine members.
 * [ret]  1 : Finished reading with '\n'
 *        0 : Finished reading due to EOF
 *       -1 : Finished reading due to an file error                 */
int read_1line_into_ringbuf(FILE *fp, ringbuf_t* pstRingbuf) {

  /*--- Variables --------------------------------------------------*/
  elbuf_t* pelbCurrent;
  elbuf_t* pelbNew;
  int      iNextLine;
  int      iRet;

  /*--- Validate the arguments -------------------------------------*/
  if (! fp        ) {error_exit(1,"read_1line_into_ringbuf(): fp is NULL\n");}
  if (! pstRingbuf) {error_exit(1,"read_1line_into_ringbuf(): RB is NULL\n");}
  if (! pstRingbuf->pelbRing) {
    error_exit(1,"read_1line_into_ringbuf(): pstRingbuf->pelbRing is NULL\n");}
  if (pstRingbuf->iLatestLine < 0) {
    error_exit(1,"read_1line_into_ringbuf(): pstRingbuf->iLatestLine is <0\n");}
  if (pstRingbuf->iSize <= pstRingbuf->iLatestLine) {
    error_exit(1,"read_1line_into_ringbuf(): pstRingbuf->iSize is larger\n");}

  /*--- Write a line string data into one of the EL-buffer chains --*/
  iRet        = -2;
  iNextLine   = (pstRingbuf->iLatestLine+1) % pstRingbuf->iSize;
  pelbCurrent = &pstRingbuf->pelbRing[iNextLine];
  while (fgets(pelbCurrent->szBuf, sizeof(pelbCurrent->szBuf), fp)) {
    pelbCurrent->sSize = strlen(pelbCurrent->szBuf);
    if (pelbCurrent->sSize < sizeof(pelbCurrent->szBuf)-1) {
      if (pelbCurrent->szBuf[pelbCurrent->sSize-1] == '\n') {iRet= 1; break;}
      if (feof(  fp)                                      ) {iRet= 0; break;}
      if (ferror(fp)                                      ) {iRet=-1; break;}
      error_exit(1,"read_1line_into_elbuf(): Unexpected error #1\n");
    }
    if (pelbCurrent->szBuf[pelbCurrent->sSize-1] == '\n') {
      if (pelbCurrent->pelbNext != NULL) {pelbCurrent->pelbNext->sSize=0;}
      iRet=1; break;
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

  /*--- Increment the "iLatestLine" only if any data has come ------*/
  if (iRet >= -1) { /* "iRet>=-1" means that this function has
                       already received one or more bytes.     */
    pstRingbuf->iLatestLine = iNextLine;
  } else {
    if      (feof(  fp)) {iRet= 0;}
    else if (ferror(fp)) {iRet=-1;}
    else                 {
      error_exit(1,"read_1line_into_elbuf(): Unexpected error #2\n");}
  }

  /*--- Truncate the EL-buffer chain if the line data is shorter ---*/
  release_following_elbufs(pelbCurrent);

  /*--- Return -----------------------------------------------------*/
  return iRet;
}

/*=== Flush an EL-buffer chain to a file =============================
 * [notice] After flishing, this command releases the memory for following
 *          chunks of every elb. (The top chunk will remain) And,
 *          sets the sSize of the first chunk to 0.
 * [in] pelbHead : Pointer of the head of the EL-buffer chain
 *      fp       : File handle to output                            */
void flush_elbuf_chain(elbuf_t* pelbHead, FILE* fp) {

  /*--- Variables --------------------------------------------------*/
  elbuf_t* pelbCurrent;

  /*--- Validate the arguments -------------------------------------*/
  if (! pelbHead) {error_exit(1,"flush_1elbuf_chain(): pelbHead is NULL\n");}
  if (!       fp) {error_exit(1,"flush_1elbuf_chain(): fp is NULL\n"      );}

  /*--- Flush the EL-buffer chain and initialize it ----------------*/
  pelbCurrent = pelbHead;
  do {
    if (pelbCurrent->sSize == 0) {break;}
    if (fputs(pelbCurrent->szBuf, fp) == EOF) {
      error_exit(errno,"Write error: %s\n",strerror(errno));
    }
    /* Flush the next buffer if all of the following conditions are satisfied.
     *   a. The size of the current buffer is full.
     *   b. The current buffer is not terminated with "\n."
     *   c. The next buffer exists.                                         */
    if (pelbCurrent->sSize < sizeof(pelbCurrent->szBuf)-1) {break;}
    if (pelbCurrent->szBuf[pelbCurrent->sSize-1] == '\n' ) {break;}
    pelbCurrent = pelbCurrent->pelbNext;
  } while (pelbCurrent);
  pelbHead->sSize = 0;
  release_following_elbufs(pelbHead);

  /*--- Finish -----------------------------------------------------*/
  return;
}

/*=== Flush the bunch of EL-buffers (ring buffer) to a file ==========
 * [notice] After flishing, this command releases the memory for following
 *          chunks of every elb chain. (The top chunk will remain) And,
 *          sets the sSize of the first chunk to 0.
 * [in] pstRingbuf : Pointer of the ring buffer
 *      fp         : File handle to output                          */
void flush_ringbuf(ringbuf_t* pstRingbuf, FILE* fp) {

  /*--- Variables --------------------------------------------------*/
  int      i,j,k;

  /*--- Validate the arguments -------------------------------------*/
  if (! pstRingbuf) {error_exit(1,"flush_ringbuf(): pstRingbuf is NULL\n");}
  if (!         fp) {error_exit(1,"flush_ringbuf(): fp is NULL\n"        );}

  /*--- Flush the buffered line data and initialize all ELB chains -*/
  i = pstRingbuf->iLatestLine + 1;
  j = i                       + pstRingbuf->iSize;
  for (k=i; k<j; k++) {
    flush_elbuf_chain(&pstRingbuf->pelbRing[k%(pstRingbuf->iSize)],fp);
  }

  /*--- Finish -----------------------------------------------------*/
  return;
}

/*=== Release the memory for the EL-buffers except the 1st chunk =====
 * [notice] This function releases ONLY CHUNKS FOLLOWING THE 1ST one.
 *          The 1st chunk of the EL-buffer, which is specified with the
 *          argument, will remain, but the content in that will be erased.
 * [in] pelb : Pointer of the elastic line buffer                   */
void release_following_elbufs(elbuf_t* pelb) {

  /*--- Variables --------------------------------------------------*/
  elbuf_t* pelbCurrent;
  elbuf_t* pelbNext   ;

  /*--- Log --------------------------------------------------------*/
  if (giVerbose>1) {warning("Enter release_following_elbufs()\n");}

  /*--- Validate the argument --------------------------------------*/
  if (! pelb) {error_exit(1,"release_following_elbufs(): pelb is NULL\n");}

  /*--- Release the memory -----------------------------------------*/
  pelbCurrent    = pelb->pelbNext;
  pelb->pelbNext = NULL;
  while (pelbCurrent != NULL) {
    pelbNext = pelbCurrent->pelbNext;
    free(pelbCurrent);
    pelbCurrent = pelbNext;
  }

  /*--- Return -----------------------------------------------------*/
  return;
}

/*=== Allocate memory for a ring buffer ==============================
 * [in]  pstRingbuf : The pointer of the ring buffer.
 *                    It MUST be specified the size of the buffer in
 *                    advance at pstRingbuf->iSize.
 *                    And also, pstRingbuf->pelbRing MUST be NULL.
 *                    Otherwise, this function refuses malloc,
 * [ret] 0 will be returned when success, otherwise >0.             */
int create_ring_buf(ringbuf_t* pstRingbuf) {

  /*--- Variables --------------------------------------------------*/
  elbuf_t* pelb;
  int      i   ;

  /*--- Log --------------------------------------------------------*/
  if (giVerbose>1) {warning("Enter create_ring_buf()\n");}

  /*--- Validate the argument --------------------------------------*/
  if (!pstRingbuf                 ) {return 1;}
  if (pstRingbuf->iSize < 0       ) {return 2;}
  if (pstRingbuf->pelbRing != NULL) {return 3;} /* Means already malloced! */

  /*--- Allocate memory with malloc --------------------------------*/
  pelb = (elbuf_t*) malloc((sizeof(elbuf_t)) * pstRingbuf->iSize);
  if (pelb == NULL) { return 4; }

  /*--- Initialize the ring buffer ---------------------------------*/
  for (i=0;i<pstRingbuf->iSize;i++) { pelb[i].sSize=0; pelb[i].pelbNext=NULL; }

  /*--- Set the pointer of the buffer and reset the latest line num */
  pstRingbuf->pelbRing    = pelb;
  pstRingbuf->iLatestLine = 0;

  /*--- Return successfully ----------------------------------------*/
  return 0;
}

/*=== Free memory of the ring buffer =================================
 * [in] pelbRing_buf : The pointer of the ring buffer to be released */
void destroy_ring_buf(ringbuf_t* pstRingbuf) {

  /*--- Variables --------------------------------------------------*/
  int      i   ;
  elbuf_t* pelb;

  /*--- Log --------------------------------------------------------*/
  if (giVerbose>1) {warning("Enter destroy_ring_buf()\n");}

  /*--- Validate the argument --------------------------------------*/
  if (pstRingbuf == NULL) { return; }

  /*--- Release only the following ELB chunks of every line --------*/
  pelb = pstRingbuf->pelbRing;
  for (i=0; i<pstRingbuf->iSize; i++) {release_following_elbufs(pelb); pelb++;}

  /*--- Release the ring (bunch of the 1st ELB chunks) -------------*/
  free(pstRingbuf->pelbRing);
  pstRingbuf->pelbRing = NULL;
  pstRingbuf->iSize    = 0   ;

  /*--- Return successfully ----------------------------------------*/
  return;
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

/*=== SIGNALHANDLER : Do nothing =====================================
 * This function does nothing, but it is helpful to break a thread
 * sleeping by using this as a signal handler.                      */
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
 * [out] gi8Holdtime         : The new parameter the sub-th gave
 * [out] gstThCom.iRequested : set to 1 to notify the main-th of the request */
void recv_param_application_req(int iSig, siginfo_t *siInfo, void *pct) {
  gi8Holdtime            = gstThCom.i8Param1;
  giHoldlines            = gstThCom.iParam1 ;
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

  /*--- Destroy the ring buffer ------------------------------------*/
  if (giVerbose>0) {warning("RingBuf is destroied\n");}
  destroy_ring_buf(&gstRingBuf);

  /*--- Close files ------------------------------------------------*/
  if (pstMainth->fpIn    != NULL) {
    if (giVerbose>0) {warning("Input file is closed\n");}
    fclose(pstMainth->fpIn   ); pstMainth->fpIn   = NULL;
  }
  if (pstMainth->fpDrain != NULL) {
    if (giVerbose>0) {warning("Drain is closed\n");}
    fclose(pstMainth->fpDrain); pstMainth->fpDrain= NULL;
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
