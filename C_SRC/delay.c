/*####################################################################
#
# DELAY - Delay Each Byte from STDIN for a Certain Time Before Output
#
# USAGE   : delay [-s|-o|-d] [-v] [size@]time
#           delay [-s|-o|-d] [-v] controlfile
# Args    : time ........ Delay time from when a byte arrives at the
#                         standard input to when it is sent to the
#                         standard output.
#                         The unit of the delay time is second
#                         defaultly. You can also specify the unit
#                         like '100ms'. Available units are 's', 'ms',
#                         'us', 'ns', 'm', 'h', 'd'. "0" means no delay.
#           size ........ Size of the ring buffer which holds the
#                         bytes that have arrived but have not been
#                         sent yet. The unit is byte defaultly. You
#                         can also specify the unit like '10MiB'.
#                         Available units are 'B', 'kB', 'KB', 'KiB',
#                         'MB', 'MiB', 'GB', 'GiB'. ("kB" means 1000
#                         bytes; "KB" and "KiB" both mean 1024 bytes.)
#                         You have to add "@" just after this
#                         parameter to distinguish it from the time
#                         parameter. The default is "1MiB" if omitted.
#           controlfile . Filepath to specify the time/size instead of
#                         by argument. You can change the parameters
#                         even when this command is running by
#                         updating the content of the controlfile.
#                         * The syntax you can write in this file is
#                           "{time|size@time|size@}." Either part may
#                           be omitted to leave it unchanged, but if
#                           you give me an invalid parameter, this
#                           command will ignore it silently with no
#                           error. If "size@time" is given, both parts
#                           must be valid, or the whole line is ignored.
#                         * The default is time=0, size=1MiB unless
#                           any valid parameter is given.
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
#                           - Character-special file / Named-pipe:
#                             It is better for the performance. If you
#                             use these types of files, you can write
#                             a new parameter with both the above two
#                             modes. The new parameter will be applied
#                             immediately just after writing.
# Options : -s .......... (Default) Suspend mode. When the ring buffer
#                         is full and a new byte arrives, block the
#                         reading of the standard input until a slot
#                         becomes free.
#           -o .......... Overwrite mode. When the ring buffer is
#                         full, discard the oldest unsent byte and
#                         store the new one instead.
#           -d .......... Disconnect mode. When the ring buffer is
#                         full, terminate this command immediately
#                         without sending any of the buffered bytes.
#                         (-s, -o and -d are exclusive to each other.
#                          If you set two or more, the last one will
#                          be valid.)
#           -v .......... Verbose mode. Reports overflow events and
#                         internal behavior to the standard error.
#                         Giving this option two or more times raises
#                         the verbosity level.
# Retuen  : Return 0 when finished successfully (the standard input
#           reached EOF and the buffer was completely flushed).
#           Return 1 when this command was forced to disconnect by
#           the "-d" option.
#
# How to compile : cc -O3 -std=c99 -o __CMDNAME__ __SRCNAME__ -pthread -lrt
#                  (if it doesn't work)
# How to compile : cc -O3 -std=c99 -o __CMDNAME__ __SRCNAME__ -pthread
#
# Written by Shell-Shoccar Japan (@shellshoccarjpn) on 2026-09-25
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
/* Solaris 11.3's <sys/feature_tests.h> only recognizes the exact
 * values _XOPEN_SOURCE==600 / _POSIX_C_SOURCE==200112L for its UNIX 03
 * detection and has no notion of POSIX.1-2008/SUSv4 at all; requesting
 * 700/200809L there trips its strict conformance-level check and
 * aborts the build, so __EXTENSIONS__ (which sidesteps that check
 * entirely and exposes every POSIX/XSI/BSD interface regardless of C
 * standard level) is used there instead. Everywhere else, we ask for
 * _XOPEN_SOURCE 700 rather than _POSIX_C_SOURCE 200809L alone: on
 * FreeBSD, _POSIX_C_SOURCE alone leaves __XSI_VISIBLE unset, hiding
 * XSI interfaces (e.g. SA_SIGINFO/sa_sigaction, S_IFMT/S_IFREG) that
 * some of these commands need; _XOPEN_SOURCE 700 enables both.       */
#if defined(__sun) || defined(__SVR4)
  #define __EXTENSIONS__
#else
  #define _XOPEN_SOURCE 700 /* for setenv() */
#endif
#ifdef __APPLE__
  /* On macOS, _XOPEN_SOURCE alone hides BSD legacy types (u_int,
   * u_char, u_short, ...) that <sys/sysctl.h> depends on internally;
   * _DARWIN_C_SOURCE restores them.                                 */
  #define _DARWIN_C_SOURCE
#endif
#include <limits.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <pthread.h>
#include <poll.h>
#include <signal.h>
#include <locale.h>
#ifdef __APPLE__
  #include <sys/types.h>  /* sys/sysctl.h needs this for u_int etc. */
  #include <sys/sysctl.h> /* for sysctlbyname("hw.memsize", ...) */
#endif

/*--- macro constants ----------------------------------------------*/
/* Interval time of looking at the parameter on the control file */
#define FREAD_ITRVL_SEC  0
#define FREAD_ITRVL_USEC 100000
/* Buffer size for the control file / option parameter */
#define CTRL_FILE_BUF 64
#define OPT_PARM_BUF  64
/* Default delay time and buffer size if the controlfile is given */
#define DEFAULT_DELAYTIME  0
#define DEFAULT_BUFFERSIZE 1048576LL /* 1MiB */
/* Overflow-handling modes ("-s"/"-o"/"-d") */
#define OVERFLOW_STOP      0
#define OVERFLOW_OVERWRITE 1
#define OVERFLOW_DIE       2
#if !defined(CLOCK_MONOTONIC) || defined(__APPLE__)
  /* HP-UX has no CLOCK_MONOTONIC at all. macOS/Darwin does define it,
   * but its pthread implementation has no pthread_condattr_setclock()
   * (see create_ring_buf() below), so pthread_cond_timedwait() there
   * always compares the deadline against a realtime-based clock
   * internally; CLOCK_FOR_ME is kept as CLOCK_REALTIME on both so
   * that tsNow/tsTo stay consistent with whatever clock is actually
   * used to evaluate the cond-var timeout.                          */
  #define CLOCK_FOR_ME CLOCK_REALTIME
#else
  #define CLOCK_FOR_ME CLOCK_MONOTONIC
#endif

/*--- macro functions (timespec arithmetic, "ts" is a variable) ----*/
#define tsadd(ts,i8) ts.tv_nsec+=i8%1000000000;ts.tv_sec+=ts.tv_nsec/1000000000+i8/1000000000;ts.tv_nsec%=1000000000

/*--- data type definitions ----------------------------------------*/
typedef struct timespec tmsp;
typedef struct _ringelem_t {
  unsigned char ucData;      /* the received byte                       */
  tmsp          tsArrival;   /* the time it arrived (tv_nsec==-1 marks
                                 an unused slot; not used for validity
                                 judgement, kept only for hygiene)       */
} ringelem_t;
typedef struct _ringbuf_t {
  ringelem_t*     pElem;       /* malloc'ed array (i8Capacity elements) */
  int64_t         i8Capacity;  /* number of elements == "size" in bytes */
  int64_t         i8Head;      /* index of the oldest unsent element    */
  int64_t         i8Count;     /* number of valid elements stored now   */
  int             iEof;        /* 1: thread A reached EOF on stdin      */
  pthread_mutex_t mu;          /* protects everything above and
                                   gi8Delaytime while it is being read  */
  pthread_cond_t  coNotEmpty;  /* signaled on push/overwrite/EOF/param-
                                   apply; thread B waits on this        */
  pthread_cond_t  coNotFull;   /* signaled on pop/grow; thread A waits
                                   on this in "-s" (suspend) mode       */
} ringbuf_t;
typedef struct _thrcom_t {
  pthread_t       tMainth_id;       /* main thread ID                         */
  pthread_mutex_t mu;               /* The mutex variable                     */
  pthread_cond_t  co;               /* The condition variable                 */
  int             iRequested__main; /* Req. received flag (only in mainth)    */
  int             iReceived;        /* Set 1 when the param. has been received*/
  int64_t         i8Param1;         /* new delay time (ns); -1: not given now */
  int64_t         i8Param2;         /* new buffer size (bytes); -1: not given */
} thcominfo_t;
typedef struct _thrmain_t {
  pthread_t tThreadA_id;    /* the reader thread's ID                        */
  pthread_t tThreadB_id;    /* the writer thread's ID                        */
  pthread_t tThreadC_id;    /* the controlfile watcher's ID (0 if format 1)  */
  int       iMu_isready;      /* Set 1 when gstThCom.mu has been initialized */
  int       iCo_isready;      /* Set 1 when gstThCom.co has been initialized */
  int       iRingMu_isready;  /* Set 1 when gstRing.mu has been initialized  */
  int       iRingCoNE_isready;/* Set 1 when gstRing.coNotEmpty is ready      */
  int       iRingCoNF_isready;/* Set 1 when gstRing.coNotFull is ready       */
} thmaininfo_t;

/*--- prototype functions ------------------------------------------*/
int64_t parse_delaytime(char* pszArg);
int64_t parse_buffersize(char* pszArg);
int     parse_delayarg(char* pszArg, int64_t* pi8Time, int64_t* pi8Size);
int     parse_delayctrl(char* pszLine, int64_t* pi8Time, int* piTimeGiven,
                                        int64_t* pi8Size, int* piSizeGiven);
int     create_ring_buf(ringbuf_t* pstRing);
void    destroy_ring_buf(ringbuf_t* pstRing);
int     resize_ring_buf(ringbuf_t* pstRing, int64_t i8NewCap);
int64_t flush_due_elements(ringbuf_t* pstRing, int64_t i8Delaytime);
void*   thread_reader(void* pvArgs);
void*   thread_writer(void* pvArgs);
void*   param_updater(void* pvArgs);
void    update_delayparam_type_r(char* pszCtrlfile);
void    update_delayparam_type_c(char* pszCtrlfile);
void    do_nothing(int iSig, siginfo_t *siInfo, void *pct);
#ifdef __ANDROID__
  void  term_this_thread(int iSig, siginfo_t *siInfo, void *pct);
#endif
void    recv_param_application_req(int iSig, siginfo_t *siInfo, void *pct);
void    unlock_mutex(void* pvMu);
void    mainth_destructor(void* pvMainth);
void    subth_destructor(void* pvFd);

/*--- global variables ---------------------------------------------*/
char*       gpszCmdname;     /* The name of this command                     */
int         giVerbose;       /* speaks more verbosely by the greater number  */
int         giOverflow;      /* OVERFLOW_STOP/OVERWRITE/DIE ("-s"/"-o"/"-d") */
struct stat gstCtrlfile;     /* stat for the control file                    */
int64_t     gi8Delaytime;    /* delay time in nanosecond (main-th only; the
                                 sub-th writes the new value into
                                 gstThCom.i8Param1 instead)                  */
int64_t     gi8Buffersize;   /* buffer size in bytes == gstRing.i8Capacity
                                 (main-th only, same reasoning as above)     */
ringbuf_t   gstRing = {0};   /* Ring buffer shared by threads A/B/main       */
thcominfo_t gstThCom;        /* Variables for threads communication          */
thmaininfo_t gstMainth;      /* Thread ids + init flags for cleanup          */
volatile sig_atomic_t gbFinished; /* 1: EOF fully drained -> normal shutdown */
volatile sig_atomic_t gbAborted;  /* 1: "-d" die triggered -> abort shutdown */

/*=== Define the functions for printing usage and error ============*/

/*--- exit with usage ----------------------------------------------*/
void print_usage_and_exit(void) {
  fprintf(stderr,
    "USAGE   : %s [-s|-o|-d] [-v] [size@]time\n"
    "          %s [-s|-o|-d] [-v] controlfile\n"
    "Args    : time ........ Delay time from when a byte arrives at the\n"
    "                        standard input to when it is sent to the\n"
    "                        standard output.\n"
    "                        The unit of the delay time is second\n"
    "                        defaultly. You can also specify the unit\n"
    "                        like '100ms'. Available units are 's', 'ms',\n"
    "                        'us', 'ns', 'm', 'h', 'd'. \"0\" means no delay.\n"
    "          size ........ Size of the ring buffer which holds the\n"
    "                        bytes that have arrived but have not been\n"
    "                        sent yet. The unit is byte defaultly. You\n"
    "                        can also specify the unit like '10MiB'.\n"
    "                        Available units are 'B', 'kB', 'KB', 'KiB',\n"
    "                        'MB', 'MiB', 'GB', 'GiB'. (\"kB\" means 1000\n"
    "                        bytes; \"KB\" and \"KiB\" both mean 1024 bytes.)\n"
    "                        You have to add \"@\" just after this\n"
    "                        parameter to distinguish it from the time\n"
    "                        parameter. The default is \"1MiB\" if omitted.\n"
    "          controlfile . Filepath to specify the time/size instead of\n"
    "                        by argument. You can change the parameters\n"
    "                        even when this command is running by\n"
    "                        updating the content of the controlfile.\n"
    "                        * The syntax you can write in this file is\n"
    "                          \"{time|size@time|size@}.\" Either part may\n"
    "                          be omitted to leave it unchanged, but if\n"
    "                          you give me an invalid parameter, this\n"
    "                          command will ignore it silently with no\n"
    "                          error. If \"size@time\" is given, both parts\n"
    "                          must be valid, or the whole line is ignored.\n"
    "                        * The default is time=0, size=1MiB unless\n"
    "                          any valid parameter is given.\n"
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
    "                          - Character-special file / Named-pipe:\n"
    "                            It is better for the performance. If you\n"
    "                            use these types of files, you can write\n"
    "                            a new parameter with both the above two\n"
    "                            modes. The new parameter will be applied\n"
    "                            immediately just after writing.\n"
    "Options : -s .......... (Default) Suspend mode. When the ring buffer\n"
    "                        is full and a new byte arrives, block the\n"
    "                        reading of the standard input until a slot\n"
    "                        becomes free.\n"
    "          -o .......... Overwrite mode. When the ring buffer is\n"
    "                        full, discard the oldest unsent byte and\n"
    "                        store the new one instead.\n"
    "          -d .......... Disconnect mode. When the ring buffer is\n"
    "                        full, terminate this command immediately\n"
    "                        without sending any of the buffered bytes.\n"
    "                        (-s, -o and -d are exclusive to each other.\n"
    "                         If you set two or more, the last one will\n"
    "                         be valid.)\n"
    "          -v .......... Verbose mode. Reports overflow events and\n"
    "                        internal behavior to the standard error.\n"
    "                        Giving this option two or more times raises\n"
    "                        the verbosity level.\n"
    "Retuen  : Return 0 when finished successfully (the standard input\n"
    "          reached EOF and the buffer was completely flushed).\n"
    "          Return 1 when this command was forced to disconnect by\n"
    "          the \"-d\" option.\n"
    "\n"
    "Version : 2026-09-25 18:52:30 JST\n"
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
int      iIsCtrlfile;     /* 0:format 1 (direct value) 1:format 2 (controlfile) */
int      i;               /* all-purpose int                        */

/*--- Initialize ---------------------------------------------------*/
gpszCmdname = argv[0];
for (i=0; *(gpszCmdname+i)!='\0'; i++) {
  if (*(gpszCmdname+i)=='/') {gpszCmdname=gpszCmdname+i+1;}
}
if (setenv("POSIXLY_CORRECT","1",1) < 0) {
  error_exit(errno,"setenv() at initialization: %s\n", strerror(errno));
}
setlocale(LC_CTYPE, "");

/*=== Parse arguments ==============================================*/

/*--- Set default parameters of the arguments ----------------------*/
giVerbose  = 0;
giOverflow = OVERFLOW_STOP;

/*--- Parse options which start with "-" ---------------------------*/
while ((i=getopt(argc, argv, "sodvh")) != -1) {
  switch (i) {
    case 's': giOverflow = OVERFLOW_STOP;      break;
    case 'o': giOverflow = OVERFLOW_OVERWRITE; break;
    case 'd': giOverflow = OVERFLOW_DIE;       break;
    case 'v': giVerbose++;                     break;
    case 'h': print_usage_and_exit();
    default : print_usage_and_exit();
  }
}
argc -= optind;
argv += optind;
if (giVerbose>0) {warning("verbose mode (level %d)\n",giVerbose);}
if (argc != 1) {print_usage_and_exit();}

/*--- Prepare the thread operation ---------------------------------*/
memset(&gstThCom,  0, sizeof(gstThCom ));
memset(&gstMainth, 0, sizeof(gstMainth));
gbFinished = 0;
gbAborted  = 0;
pthread_cleanup_push(mainth_destructor, &gstMainth);

/*--- Decide whether argv[0] is a direct value or a controlfile ----*/
iIsCtrlfile = parse_delayarg(argv[0], &gi8Delaytime, &gi8Buffersize);
if (iIsCtrlfile) {
  /* Set the initial parameters */
  gi8Delaytime      = DEFAULT_DELAYTIME;
  gi8Buffersize      = DEFAULT_BUFFERSIZE;
  gstThCom.i8Param1 = -1;
  gstThCom.i8Param2 = -1;
  if (stat(argv[0],&gstCtrlfile) < 0) {
    error_exit(errno,"%s: %s\n",argv[0],strerror(errno));
  }
}

/*--- Set up the SIGHUP machinery. This is ALWAYS required (even in  *
 *    format 1, with no controlfile at all) because threads A/B use  *
 *    it to notify the main thread that the whole program is done    *
 *    (EOF fully drained, or "-d" forced an abort); the main thread   *
 *    is never a worker itself here, so it always needs to be woken. */
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
if (sigemptyset(&ssMask) != 0) {
  error_exit(errno,"sigemptyset() #2 in main(): %s\n",strerror(errno));
}
if (sigaddset(&ssMask,SIGHUP) != 0) {
  error_exit(errno,"sigaddset() #2 in main(): %s\n",strerror(errno));
}
if ((i=pthread_sigmask(SIG_BLOCK,&ssMask,NULL)) != 0) {
  error_exit(i,"pthread_sigmask() #2 in main(): %s\n",strerror(i));
}
gstThCom.tMainth_id = pthread_self();
i = pthread_mutex_init(&gstThCom.mu, NULL);
if (i) {error_exit(i,"pthread_mutex_init() in main(): %s\n",strerror(i));}
gstMainth.iMu_isready = 1;
i = pthread_cond_init(&gstThCom.co, NULL);
if (i) {error_exit(i,"pthread_cond_init() in main(): %s\n" ,strerror(i));}
gstMainth.iCo_isready = 1;
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

/*--- Create the ring buffer ----------------------------------------*/
gstRing.i8Capacity = gi8Buffersize;
i = create_ring_buf(&gstRing);
if (i) {error_exit(i,"create_ring_buf() in main(): %s\n",strerror(i));}
gstMainth.iRingMu_isready   = 1;
gstMainth.iRingCoNE_isready = 1;
gstMainth.iRingCoNF_isready = 1;

/*=== Switch buffer mode and start the worker threads ==============*/
if (setvbuf(stdout,NULL,_IONBF,0)!=0) {
  error_exit(255,"Failed to switch to unbuffered mode\n");
}
if (feof(stdin)) {clearerr(stdin);} /* Reset EOF condition when stdin */

i = pthread_create(&gstMainth.tThreadB_id, NULL, thread_writer, NULL);
if (i) {gstMainth.tThreadB_id=0; error_exit(i,"pthread_create() thread_writer: %s\n",strerror(i));}
i = pthread_create(&gstMainth.tThreadA_id, NULL, thread_reader, NULL);
if (i) {gstMainth.tThreadA_id=0; error_exit(i,"pthread_create() thread_reader: %s\n",strerror(i));}
if (iIsCtrlfile) {
  i = pthread_create(&gstMainth.tThreadC_id, NULL, param_updater, (void*)argv[0]);
  if (i) {gstMainth.tThreadC_id=0; error_exit(i,"pthread_create() param_updater: %s\n",strerror(i));}
}

/*=== Dispatcher loop (main thread never touches the data itself) ==*/
while (1) {
  pause();
  if (gbFinished || gbAborted) {break;}
  if (gstThCom.iRequested__main) {
    int64_t i8NewTime = gstThCom.i8Param1;
    int64_t i8NewSize = gstThCom.i8Param2;

    if ((i=pthread_mutex_lock(&gstRing.mu)) != 0) {
      error_exit(i,"pthread_mutex_lock() in main() dispatcher: %s\n",strerror(i));
    }
    if (i8NewSize>=0 && i8NewSize!=gi8Buffersize) {
      int iErr = resize_ring_buf(&gstRing, i8NewSize);
      if (iErr != 0) {
        warning("resize_ring_buf(): %s\n",strerror(iErr));
      } else {
        gi8Buffersize = i8NewSize;
        if (giVerbose>0) {
          warning("Ring buffer resized to %lld bytes\n",(long long)gi8Buffersize);
        }
      }
    }
    if (i8NewTime>=0) {
      gi8Delaytime = i8NewTime;
      if (giVerbose>0) {warning("Delay time changed to %lld ns\n",(long long)gi8Delaytime);}
    }
    (void)flush_due_elements(&gstRing, gi8Delaytime);
    if ((i=pthread_cond_broadcast(&gstRing.coNotEmpty)) != 0) {
      error_exit(i,"pthread_cond_broadcast() #1 in main(): %s\n",strerror(i));
    }
    if ((i=pthread_cond_broadcast(&gstRing.coNotFull)) != 0) {
      error_exit(i,"pthread_cond_broadcast() #2 in main(): %s\n",strerror(i));
    }
    if ((i=pthread_mutex_unlock(&gstRing.mu)) != 0) {
      error_exit(i,"pthread_mutex_unlock() in main() dispatcher: %s\n",strerror(i));
    }

    if ((i=pthread_mutex_lock(&gstThCom.mu)) != 0) {
      error_exit(i,"pthread_mutex_lock() in main() ack: %s\n",strerror(i));
    }
    gstThCom.iReceived = 1;
    if ((i=pthread_cond_signal(&gstThCom.co)) != 0) {
      error_exit(i,"pthread_cond_signal() in main() ack: %s\n",strerror(i));
    }
    if ((i=pthread_mutex_unlock(&gstThCom.mu)) != 0) {
      error_exit(i,"pthread_mutex_unlock() in main() ack: %s\n",strerror(i));
    }
    gstThCom.iRequested__main = 0;
  }
}

/*=== Finish =========================================================*/
pthread_cleanup_pop(1);
return gbAborted ? 1 : 0;}



/*####################################################################
# Thread A (Reader)
####################################################################*/

/*=== Read bytes from stdin and push them into the ring buffer =====*/
void* thread_reader(void* pvArgs) {

  /*--- Variables --------------------------------------------------*/
  int     iChar;
  tmsp    tsNow;
  int64_t i8Idx;
  int     i;

  while (1) {
    iChar = getc(stdin);
    if (iChar == EOF) {
      if (ferror(stdin) && giVerbose>0) {
        warning("Error while reading the standard input\n");
      }
      if ((i=pthread_mutex_lock(&gstRing.mu)) != 0) {
        error_exit(i,"pthread_mutex_lock() in thread_reader() #1: %s\n",strerror(i));
      }
      gstRing.iEof = 1;
      if ((i=pthread_cond_broadcast(&gstRing.coNotEmpty)) != 0) {
        error_exit(i,"pthread_cond_broadcast() in thread_reader(): %s\n",strerror(i));
      }
      if ((i=pthread_mutex_unlock(&gstRing.mu)) != 0) {
        error_exit(i,"pthread_mutex_unlock() in thread_reader() #1: %s\n",strerror(i));
      }
      break;
    }
    if (clock_gettime(CLOCK_FOR_ME,&tsNow) != 0) {
      error_exit(errno,"clock_gettime() in thread_reader(): %s\n",strerror(errno));
    }

    if ((i=pthread_mutex_lock(&gstRing.mu)) != 0) {
      error_exit(i,"pthread_mutex_lock() in thread_reader() #2: %s\n",strerror(i));
    }

    if (gstRing.i8Count == gstRing.i8Capacity) {
      /*--- The ring buffer is full: act according to "-s"/"-o"/"-d" */
      if (giVerbose>0) {
        warning("Ring buffer is full! action=%s\n",
          giOverflow==OVERFLOW_STOP      ? "suspend reading (-s)"           :
          giOverflow==OVERFLOW_OVERWRITE ? "overwrite the oldest byte (-o)" :
                                            "disconnect (-d)"                 );
      }
      if (giOverflow == OVERFLOW_STOP) {
        pthread_cleanup_push(unlock_mutex, &gstRing.mu);
        while (gstRing.i8Count == gstRing.i8Capacity) {
          if ((i=pthread_cond_wait(&gstRing.coNotFull,&gstRing.mu)) != 0) {
            error_exit(i,"pthread_cond_wait() in thread_reader(): %s\n",strerror(i));
          }
        }
        pthread_cleanup_pop(0);
        /* a slot is free now: fall through to the normal push below */
      } else if (giOverflow == OVERFLOW_OVERWRITE) {
        i8Idx = gstRing.i8Head;
        gstRing.pElem[i8Idx].ucData    = (unsigned char)iChar;
        gstRing.pElem[i8Idx].tsArrival = tsNow;
        gstRing.i8Head = (i8Idx+1) % gstRing.i8Capacity;
        if ((i=pthread_cond_broadcast(&gstRing.coNotEmpty)) != 0) {
          error_exit(i,"pthread_cond_broadcast() in thread_reader() overwrite: %s\n",strerror(i));
        }
        if ((i=pthread_mutex_unlock(&gstRing.mu)) != 0) {
          error_exit(i,"pthread_mutex_unlock() in thread_reader() overwrite: %s\n",strerror(i));
        }
        continue;
      } else { /* OVERFLOW_DIE */
        if ((i=pthread_mutex_unlock(&gstRing.mu)) != 0) {
          error_exit(i,"pthread_mutex_unlock() in thread_reader() die: %s\n",strerror(i));
        }
        gbAborted = 1;
        if (pthread_kill(gstThCom.tMainth_id, SIGHUP) != 0) {
          error_exit(errno,"pthread_kill() in thread_reader(): %s\n",strerror(errno));
        }
        return NULL;
      }
    }

    /*--- Normal push ------------------------------------------------*/
    i8Idx = (gstRing.i8Head + gstRing.i8Count) % gstRing.i8Capacity;
    gstRing.pElem[i8Idx].ucData    = (unsigned char)iChar;
    gstRing.pElem[i8Idx].tsArrival = tsNow;
    gstRing.i8Count++;
    if ((i=pthread_cond_signal(&gstRing.coNotEmpty)) != 0) {
      error_exit(i,"pthread_cond_signal() in thread_reader(): %s\n",strerror(i));
    }
    if ((i=pthread_mutex_unlock(&gstRing.mu)) != 0) {
      error_exit(i,"pthread_mutex_unlock() in thread_reader() #2: %s\n",strerror(i));
    }
  }
  return NULL;
}



/*####################################################################
# Thread B (Writer)
####################################################################*/

/*=== Wait for each element's due time and write it to stdout ======*/
void* thread_writer(void* pvArgs) {

  /*--- Variables --------------------------------------------------*/
  int64_t i8Idx;
  tmsp    tsNow, tsTo;
  int     iRc;
  int     i;

  if ((i=pthread_mutex_lock(&gstRing.mu)) != 0) {
    error_exit(i,"pthread_mutex_lock() in thread_writer() #1: %s\n",strerror(i));
  }
  pthread_cleanup_push(unlock_mutex, &gstRing.mu);

  while (1) {
    while (gstRing.i8Count==0 && !gstRing.iEof) {
      if ((i=pthread_cond_wait(&gstRing.coNotEmpty,&gstRing.mu)) != 0) {
        error_exit(i,"pthread_cond_wait() in thread_writer() #1: %s\n",strerror(i));
      }
    }
    if (gstRing.i8Count==0 && gstRing.iEof) {
      break; /* fully drained and no more data will ever come: finished */
    }

    i8Idx = gstRing.i8Head;
    tsTo  = gstRing.pElem[i8Idx].tsArrival;
    tsadd(tsTo, gi8Delaytime);

    if (clock_gettime(CLOCK_FOR_ME,&tsNow) != 0) {
      error_exit(errno,"clock_gettime() in thread_writer(): %s\n",strerror(errno));
    }
    if (tsTo.tv_sec>tsNow.tv_sec ||
        (tsTo.tv_sec==tsNow.tv_sec && tsTo.tv_nsec>tsNow.tv_nsec)) {
      iRc = pthread_cond_timedwait(&gstRing.coNotEmpty,&gstRing.mu,&tsTo);
      if (iRc!=0 && iRc!=ETIMEDOUT) {
        error_exit(iRc,"pthread_cond_timedwait() in thread_writer(): %s\n",strerror(iRc));
      }
      if (iRc == 0) {
        continue; /* woken early: state may have changed, re-evaluate  */
      }
      /* ETIMEDOUT: the due time has come */
    }

    if (gstRing.i8Head!=i8Idx || gstRing.i8Count==0) {
      continue; /* stale (overwritten/flushed by someone else): re-evaluate */
    }

    while (putchar(gstRing.pElem[i8Idx].ucData) == EOF) {
      error_exit(errno,"putchar() in thread_writer(): %s\n",strerror(errno));
    }
    gstRing.i8Head = (i8Idx+1) % gstRing.i8Capacity;
    gstRing.i8Count--;
    if ((i=pthread_cond_signal(&gstRing.coNotFull)) != 0) {
      error_exit(i,"pthread_cond_signal() in thread_writer(): %s\n",strerror(i));
    }
  }

  pthread_cleanup_pop(1); /* unlocks gstRing.mu */

  gbFinished = 1;
  if (pthread_kill(gstThCom.tMainth_id, SIGHUP) != 0) {
    error_exit(errno,"pthread_kill() in thread_writer(): %s\n",strerror(errno));
  }
  return NULL;
}



/*####################################################################
# Thread C (Parameter Updater)
####################################################################*/

/*=== Initialization ===============================================*/
void* param_updater(void* pvArgs) {

/*--- Variables ----------------------------------------------------*/
char*  pszCtrlfile;
#ifdef __ANDROID__
struct sigaction sa;    /* for signal handler definition (action)   */
#endif

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
if (gstCtrlfile.st_mode & S_IFREG) {update_delayparam_type_r(pszCtrlfile);}

/*=== The routine when the control file is a character special file */
else                               {update_delayparam_type_c(pszCtrlfile);}

/*=== End of the subthread (does not come here) ====================*/
return NULL;}



/*####################################################################
# Subroutines of Thread C
####################################################################*/

/*=== Try to update the parameter for a regular file =================
 * [in]  pszCtrlfile      : Filename of the control file which the time/
 *                          size is written
 *       gstThCom.tMainth_id
 *                        : The main thread ID
 *       gstThCom.mu      : Mutex object to lock
 *       gstThCom.co      : Condition variable to send a signal to the sub-th
 * [out] gstThCom.i8Param1: The new delay time (ns), or -1 if not given
 *       gstThCom.i8Param2: The new buffer size (bytes), or -1 if not given
 *       gstThCom.iReceived
 *                        : Set to 0 after confirming that the main thread
 *                          receivedi the request                      */
void update_delayparam_type_r(char* pszCtrlfile) {

  /*--- Variables --------------------------------------------------*/
  struct sigaction saAlrm; /* for signal handler definition (action)   */
  sigset_t         ssMask; /* unblocking signal list                   */
  struct itimerval itInt ; /* for signal handler definition (interval) */
  int              iFd_ctrlfile        ; /* file desc. of the ctrlfile */
  char             szBuf[CTRL_FILE_BUF]; /* parameter string buffer    */
  int              iLen                ; /* length of the parameter str*/
  int64_t          i8Time, i8Size;
  int              iTimeGiven, iSizeGiven;
  int64_t          i8LastTime = DEFAULT_DELAYTIME;  /* last APPLIED values,
                                                        used to detect a
                                                        real change      */
  int64_t          i8LastSize = DEFAULT_BUFFERSIZE;
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
    /* 1) Try to read the parameter */
    if (lseek(iFd_ctrlfile,0,SEEK_SET) < 0                 ) {goto pause_here;}
    if ((iLen=read(iFd_ctrlfile,szBuf,CTRL_FILE_BUF-1)) < 1) {goto pause_here;}
    for (i=0;i<iLen;i++) {if(szBuf[i]=='\n'){break;}}
    szBuf[i]='\0';
    if (parse_delayctrl(szBuf,&i8Time,&iTimeGiven,&i8Size,&iSizeGiven) != 0) {
      goto pause_here;
    }
    if ((!iTimeGiven || i8Time==i8LastTime) &&
        (!iSizeGiven || i8Size==i8LastSize)   ) {goto pause_here;}
    /* 2) Update the parameter */
    gstThCom.i8Param1 = iTimeGiven ? i8Time : -1;
    gstThCom.i8Param2 = iSizeGiven ? i8Size : -1;
    if (iTimeGiven) {i8LastTime = i8Time;}
    if (iSizeGiven) {i8LastSize = i8Size;}
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
pause_here:
    pause();
  }

  /*--- End of the function (does not come here) -------------------*/
  pthread_cleanup_pop(0);
}

/*=== Try to update the parameter for a char-sp/FIFO file ============
 * [in]  pszCtrlfile      : Filename of the control file which the time/
 *                          size is written
 *       gstThCom.tMainth_id
 *                        : The main thread ID
 *       gstThCom.mu      : Mutex object to lock
 *       gstThCom.co      : Condition variable to send a signal to the sub-th
 * [out] gstThCom.i8Param1: The new delay time (ns), or -1 if not given
 *       gstThCom.i8Param2: The new buffer size (bytes), or -1 if not given
 *       gstThCom.iReceived
 *                        : Set to 0 after confirming that the main thread
 *                          receivedi the request                      */
void update_delayparam_type_c(char* pszCtrlfile) {

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
  int64_t i8Time, i8Size;
  int     iTimeGiven, iSizeGiven;
  int64_t i8LastTime = DEFAULT_DELAYTIME;
  int64_t i8LastSize = DEFAULT_BUFFERSIZE;
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
    if (iBuf0DatSiz[iBuf0Lst]< 0) {
      error_exit(errno,"read() in type_c(): %s\n",strerror(errno));
    }
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
    if (parse_delayctrl(szCmdbuf,&i8Time,&iTimeGiven,&i8Size,&iSizeGiven) != 0) {
      szCmdbuf[0]='\0'; continue; /* Invalid parameter string */
    }
    if ((!iTimeGiven || i8Time==i8LastTime) &&
        (!iSizeGiven || i8Size==i8LastSize)   ) {
      szCmdbuf[0]='\0'; continue; /* Parameters do not change */
    }
    gstThCom.i8Param1 = iTimeGiven ? i8Time : -1;
    gstThCom.i8Param2 = iSizeGiven ? i8Size : -1;
    if (iTimeGiven) {i8LastTime = i8Time;}
    if (iSizeGiven) {i8LastSize = i8Size;}
    if (pthread_kill(gstThCom.tMainth_id, SIGHUP) != 0) {
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



/*####################################################################
# Functions
####################################################################*/

/*=== Parse the delay time ============================================
 * [ret] >= 0  : Delay time (in nanosecound)
 *       <=-1  : It is not a value                                  */
int64_t parse_delaytime(char *pszArg) {

  /*--- Variables --------------------------------------------------*/
  char   szUnit[OPT_PARM_BUF];
  double dNum;

  /*--- Check the lengths of the argument --------------------------*/
  if (strlen(pszArg) >= OPT_PARM_BUF) {return -2;}

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

  /* as a minute value */
  if (strcmp(szUnit, "m" )==0) {
    if (dNum > ((double)INT_MAX /         60)) {return -2;}
    return       (int64_t)(dNum *   60000000000LL);
  }

  /* as an hour value */
  if (strcmp(szUnit, "h" )==0) {
    if (dNum > ((double)INT_MAX /       3600)) {return -2;}
    return       (int64_t)(dNum * 3600000000000LL);
  }

  /* as a day value */
  if (strcmp(szUnit, "d" )==0) {
    if (dNum > ((double)INT_MAX /      86400)) {return -2;}
    return       (int64_t)(dNum * 86400000000000LL);
  }

  /*--- Otherwise, it is not a value -------------------------------*/
  return -2;
}

/*=== Parse the buffer size ===========================================
 * [ret] >  0  : Buffer size (in bytes)
 *       <=0   : It is not a value, or too large for this machine   */
int64_t parse_buffersize(char *pszArg) {

  /*--- Variables --------------------------------------------------*/
  char   szUnit[OPT_PARM_BUF];
  double dNum, dBytes, dMaxBytes;
#ifndef __APPLE__
  long   lPhysPages, lPageSize;
#endif
  int64_t i8Bytes;

  /*--- Check the lengths of the argument --------------------------*/
  if (strlen(pszArg) >= OPT_PARM_BUF) {return -2;}

  /*--- Try to interpret the argument as "<value>"[+"unit"] --------*/
  switch (sscanf(pszArg, "%lf%s", &dNum, szUnit)) {
    case   2:                      break;
    case   1: strcpy(szUnit, "B"); break;
    default : return -2;
  }
  if (dNum <= 0                    ) {return -2;} /* must be "greater than 0" */

  /*--- Convert to bytes (as a double, to avoid intermediate overflow) */
  if      (strcmp(szUnit, "B"  )==0) {dBytes = dNum;}
  else if (strcmp(szUnit, "kB" )==0) {dBytes = dNum * 1000.0;}
  else if (strcmp(szUnit, "KB" )==0) {dBytes = dNum * 1024.0;}
  else if (strcmp(szUnit, "KiB")==0) {dBytes = dNum * 1024.0;}
  else if (strcmp(szUnit, "MB" )==0) {dBytes = dNum * 1000000.0;}
  else if (strcmp(szUnit, "MiB")==0) {dBytes = dNum * 1048576.0;}
  else if (strcmp(szUnit, "GB" )==0) {dBytes = dNum * 1000000000.0;}
  else if (strcmp(szUnit, "GiB")==0) {dBytes = dNum * 1073741824.0;}
  else                               {return -2;}

  /*--- Reject values too large to represent safely -----------------*/
  if (dBytes > (double)INT64_MAX) {return -2;}

  /*--- Reject values whose actual ring-buffer allocation would be   *
   *     >= 80% of the machine's total physical memory. "dBytes" is   *
   *     the number of ELEMENTS (1 per requested byte), but each      *
   *     element actually costs sizeof(ringelem_t) bytes of real      *
   *     memory (it carries a timestamp alongside the data byte), so  *
   *     the real footprint must be compared, not "dBytes" itself.   */
#ifdef __APPLE__
  {
    uint64_t u8MemSize;
    size_t   szLen = sizeof(u8MemSize);
    /* macOS has no _SC_PHYS_PAGES; hw.memsize gives the same info
       (total physical RAM, in bytes) more directly.                */
    if (sysctlbyname("hw.memsize", &u8MemSize, &szLen, NULL, 0) == 0) {
      dMaxBytes = (double)u8MemSize * 0.80;
      if (dBytes * (double)sizeof(ringelem_t) >= dMaxBytes) {return -2;}
    }
  }
#else
  #ifdef _SC_PHYS_PAGES
    lPhysPages = sysconf(_SC_PHYS_PAGES);
  #else
    lPhysPages = -1; /* not available on this OS; the safety cap
                         below is then simply skipped              */
  #endif
  lPageSize  = sysconf(_SC_PAGE_SIZE);
  if (lPhysPages>0 && lPageSize>0) {
    dMaxBytes = (double)lPhysPages * (double)lPageSize * 0.80;
    if (dBytes * (double)sizeof(ringelem_t) >= dMaxBytes) {return -2;}
  }
#endif

  /*--- Floor to an integer number of bytes; 0 bytes is an error ----*/
  i8Bytes = (int64_t)dBytes;
  if (i8Bytes <= 0) {return -2;}

  return i8Bytes;
}

/*=== Parse the argv[0] for format 1 ("[size@]time") =================
 * [out] *pi8Time : the delay time  (ns),    valid only when ret==0
 *       *pi8Size : the buffer size (bytes), valid only when ret==0
 * [ret] ==0 : argv[0] was a valid direct value
 *       !=0 : argv[0] did not match the direct-value syntax and
 *             should be treated as a controlfile path instead       */
int parse_delayarg(char *pszArg, int64_t *pi8Time, int64_t *pi8Size) {

  /*--- Variables --------------------------------------------------*/
  char*   psz;
  char    szBuf[OPT_PARM_BUF];
  size_t  sizLen;
  int64_t i8Time, i8Size;

  if ((psz=strchr(pszArg,'@')) != NULL) {
    /* "size@time" */
    sizLen = (size_t)(psz-pszArg);
    if (sizLen >= OPT_PARM_BUF) {return 1;}
    memcpy(szBuf, pszArg, sizLen);
    szBuf[sizLen] = '\0';
    i8Size = parse_buffersize(szBuf);
    if (i8Size <= 0) {return 1;}
    psz++;
    i8Time = parse_delaytime(psz);
    if (i8Time < 0) {return 1;}
  } else {
    /* "time" only (size defaults to DEFAULT_BUFFERSIZE) */
    i8Size = DEFAULT_BUFFERSIZE;
    i8Time = parse_delaytime(pszArg);
    if (i8Time < 0) {return 1;}
  }
  *pi8Time = i8Time;
  *pi8Size = i8Size;
  return 0;
}

/*=== Parse one controlfile line for format 2 =========================
 * Accepted syntax: "{time|size@time|size@}." If "size@" is given, the
 * size part must be valid; if a time part follows the "@", it must
 * also be valid, or the whole line is rejected.
 * [out] *pi8Time & *piTimeGiven, *pi8Size & *piSizeGiven
 * [ret] ==0 : success (at least one of time/size is given and valid)
 *       !=0 : the whole line should be ignored                      */
int parse_delayctrl(char *pszLine, int64_t *pi8Time, int *piTimeGiven,
                                    int64_t *pi8Size, int *piSizeGiven) {

  /*--- Variables --------------------------------------------------*/
  char*  psz;
  char   szBuf[CTRL_FILE_BUF];
  size_t sizLen;

  *piTimeGiven = 0;
  *piSizeGiven = 0;

  if ((psz=strchr(pszLine,'@')) != NULL) {
    sizLen = (size_t)(psz-pszLine);
    if (sizLen >= CTRL_FILE_BUF) {return 1;}
    memcpy(szBuf, pszLine, sizLen);
    szBuf[sizLen] = '\0';
    *pi8Size = parse_buffersize(szBuf);
    if (*pi8Size <= 0) {return 1;}
    *piSizeGiven = 1;
    psz++;
    if (*psz == '\0') {return 0;} /* "size@" only: time is left unchanged */
    *pi8Time = parse_delaytime(psz);
    if (*pi8Time < 0) {return 1;}
    *piTimeGiven = 1;
    return 0;
  }

  /* no "@": time only */
  *pi8Time = parse_delaytime(pszLine);
  if (*pi8Time < 0) {return 1;}
  *piTimeGiven = 1;
  return 0;
}

/*=== Allocate and initialize the ring buffer =========================
 * [in]  pstRing->i8Capacity : the number of elements to allocate
 * [ret] ==0 : success   !=0 : failure (the value means "errno")     */
int create_ring_buf(ringbuf_t *pstRing) {

  /*--- Variables --------------------------------------------------*/
  int                 i;
  pthread_condattr_t  caAttr; /* to make the cond-vars use CLOCK_FOR_ME
                                  (pthread_cond_timedwait() would use
                                  CLOCK_REALTIME by default otherwise,
                                  which mismatches tsNow/tsTo below and
                                  fires the timeout instantly)         */

  pstRing->pElem = (ringelem_t*)malloc(sizeof(ringelem_t)*(size_t)pstRing->i8Capacity);
  if (pstRing->pElem == NULL) {return errno;}
  pstRing->i8Head  = 0;
  pstRing->i8Count = 0;
  pstRing->iEof    = 0;

  if ((i=pthread_mutex_init(&pstRing->mu, NULL))            != 0) {return i;}
  if ((i=pthread_condattr_init(&caAttr))                    != 0) {return i;}
  #ifndef __APPLE__
  if ((i=pthread_condattr_setclock(&caAttr, CLOCK_FOR_ME))  != 0) {return i;}
  #endif
  if ((i=pthread_cond_init(&pstRing->coNotEmpty, &caAttr))  != 0) {return i;}
  if ((i=pthread_cond_init(&pstRing->coNotFull, &caAttr))   != 0) {return i;}
  pthread_condattr_destroy(&caAttr);
  return 0;
}

/*=== Free the memory of the ring buffer (mutex/cond are destroyed  *
 *     separately, by mainth_destructor(), which tracks their init  *
 *     status independently)                                        */
void destroy_ring_buf(ringbuf_t *pstRing) {
  if (pstRing->pElem != NULL) {free(pstRing->pElem); pstRing->pElem=NULL;}
  return;
}

/*=== Resize (grow or shrink) the ring buffer =========================
 * PRECONDITION: the caller already holds pstRing->mu.
 * [in]  i8NewCap : the new capacity (in elements == bytes)
 * [ret] ==0 : success   !=0 : failure (the value means "errno")     */
int resize_ring_buf(ringbuf_t *pstRing, int64_t i8NewCap) {

  /*--- Variables --------------------------------------------------*/
  ringelem_t* pNew;
  int64_t     i8OldCap = pstRing->i8Capacity;
  int64_t     i8HeadChunk, i8TailChunk, i8Growth;
  int64_t     k;

  if (i8NewCap == i8OldCap) {return 0;}

  if (i8NewCap > i8OldCap) {
    /*--- Grow -------------------------------------------------------*/
    pNew = (ringelem_t*)realloc(pstRing->pElem, sizeof(ringelem_t)*(size_t)i8NewCap);
    if (pNew == NULL) {return errno;}
    pstRing->pElem = pNew;
    for (k=i8OldCap; k<i8NewCap; k++) {           /* 1) init the new region */
      pstRing->pElem[k].ucData        = 0;
      pstRing->pElem[k].tsArrival.tv_sec  = 0;
      pstRing->pElem[k].tsArrival.tv_nsec = -1;
    }
    if (pstRing->i8Head + pstRing->i8Count > i8OldCap) {
      /* 2) the data physically wraps around: restore contiguity      */
      i8HeadChunk = i8OldCap - pstRing->i8Head;         /* at [head,oldCap) */
      i8TailChunk = pstRing->i8Count - i8HeadChunk;     /* at [0,tailChunk) */
      i8Growth    = i8NewCap - i8OldCap;
      if (i8TailChunk<=i8HeadChunk && i8TailChunk<=i8Growth) {
        /* Option A: move the smaller (front) chunk right after the old end.
           Safe: destination [oldCap,oldCap+tailChunk) fits inside the newly
           grown region by construction, and never overlaps the source.    */
        memmove(&pstRing->pElem[i8OldCap], &pstRing->pElem[0],
                (size_t)(i8TailChunk*(int64_t)sizeof(ringelem_t)));
      } else {
        /* Option B: move the head-side chunk to the new physical end.
           Always safe for ANY growth amount > 0: the destination start
           (i8NewCap-i8HeadChunk) is always >= i8TailChunk here, so it never
           collides with the untouched tail chunk at [0,tailChunk).
           memmove() (not memcpy()) is required because source and
           destination CAN overlap when i8Growth < i8HeadChunk.            */
        memmove(&pstRing->pElem[i8NewCap-i8HeadChunk], &pstRing->pElem[pstRing->i8Head],
                (size_t)(i8HeadChunk*(int64_t)sizeof(ringelem_t)));
        pstRing->i8Head = i8NewCap - i8HeadChunk;
      }
    }
    pstRing->i8Capacity = i8NewCap;
  } else {
    /*--- Shrink -------------------------------------------------------*
     * Per spec: all buffered (unsent) data is discarded on shrink.      */
    pNew = (ringelem_t*)realloc(pstRing->pElem, sizeof(ringelem_t)*(size_t)i8NewCap);
    if (pNew == NULL) {return errno;}
    pstRing->pElem = pNew;
    for (k=0; k<i8NewCap; k++) {
      pstRing->pElem[k].ucData        = 0;
      pstRing->pElem[k].tsArrival.tv_sec  = 0;
      pstRing->pElem[k].tsArrival.tv_nsec = -1;
    }
    pstRing->i8Capacity = i8NewCap;
    pstRing->i8Head     = 0;
    pstRing->i8Count     = 0;
  }
  return 0;
}

/*=== Flush every element whose due time has already passed ==========
 * PRECONDITION: the caller already holds pstRing->mu.
 * [ret] the number of elements flushed                              */
int64_t flush_due_elements(ringbuf_t *pstRing, int64_t i8Delaytime) {

  /*--- Variables --------------------------------------------------*/
  int64_t i8Flushed = 0;
  tmsp    tsNow, tsDue;
  int64_t i8Idx;

  if (clock_gettime(CLOCK_FOR_ME,&tsNow) != 0) {
    error_exit(errno,"clock_gettime() in flush_due_elements(): %s\n",strerror(errno));
  }

  while (pstRing->i8Count > 0) {
    i8Idx = pstRing->i8Head;
    tsDue = pstRing->pElem[i8Idx].tsArrival;
    tsadd(tsDue, i8Delaytime);
    if (tsDue.tv_sec>tsNow.tv_sec ||
        (tsDue.tv_sec==tsNow.tv_sec && tsDue.tv_nsec>tsNow.tv_nsec)) {
      break; /* the ring buffer is time-ordered: nothing further is due */
    }
    while (putchar(pstRing->pElem[i8Idx].ucData) == EOF) {
      error_exit(errno,"putchar() in flush_due_elements(): %s\n",strerror(errno));
    }
    pstRing->i8Head = (i8Idx+1) % pstRing->i8Capacity;
    pstRing->i8Count--;
    i8Flushed++;
  }
  return i8Flushed;
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
 * This just sets the flag; the actual work (mutex lock, possibly
 * realloc()) is NOT async-signal-safe, so it is all done afterward, *
 * in main()'s dispatcher loop once pause() returns.                 *
 * [out] gstThCom.iRequested__main : set to 1 to notify the main-th  */
void recv_param_application_req(int iSig, siginfo_t *siInfo, void *pct) {
  gstThCom.iRequested__main = 1;
  return;
}

/*=== CLEANUPHANDLER : Unlock a mutex ================================
 * Generic helper for pthread_cleanup_push()/pthread_cleanup_pop().
 * [in] pvMu : the pthread_mutex_t* to unlock                        */
void unlock_mutex(void *pvMu) {
  pthread_mutex_unlock((pthread_mutex_t*)pvMu);
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

  /*--- Terminate the worker/watcher threads ------------------------*/
  if (pstMainth->tThreadA_id) {
    #ifndef __ANDROID__
    pthread_cancel(pstMainth->tThreadA_id);
    #else
    pthread_kill(pstMainth->tThreadA_id, SIGTERM);
    #endif
    pthread_join(pstMainth->tThreadA_id, NULL);
    pstMainth->tThreadA_id = 0;
  }
  if (pstMainth->tThreadB_id) {
    #ifndef __ANDROID__
    pthread_cancel(pstMainth->tThreadB_id);
    #else
    pthread_kill(pstMainth->tThreadB_id, SIGTERM);
    #endif
    pthread_join(pstMainth->tThreadB_id, NULL);
    pstMainth->tThreadB_id = 0;
  }
  if (pstMainth->tThreadC_id) {
    #ifndef __ANDROID__
    pthread_cancel(pstMainth->tThreadC_id);
    #else
    pthread_kill(pstMainth->tThreadC_id, SIGTERM);
    #endif
    pthread_join(pstMainth->tThreadC_id, NULL);
    pstMainth->tThreadC_id = 0;
  }

  /*--- Destroy mutex/cond variables --------------------------------*/
  if (pstMainth->iMu_isready) {
    if (giVerbose>0) {warning("Mutex (gstThCom) is destroied\n");}
    pthread_mutex_destroy(&gstThCom.mu);pstMainth->iMu_isready=0;
  }
  if (pstMainth->iCo_isready) {
    if (giVerbose>0) {warning("Conditional variable (gstThCom) is destroied\n");}
    pthread_cond_destroy( &gstThCom.co);pstMainth->iCo_isready=0;
  }
  if (pstMainth->iRingMu_isready) {
    if (giVerbose>0) {warning("Mutex (gstRing) is destroied\n");}
    pthread_mutex_destroy(&gstRing.mu);pstMainth->iRingMu_isready=0;
  }
  if (pstMainth->iRingCoNE_isready) {
    pthread_cond_destroy( &gstRing.coNotEmpty);pstMainth->iRingCoNE_isready=0;
  }
  if (pstMainth->iRingCoNF_isready) {
    pthread_cond_destroy( &gstRing.coNotFull);pstMainth->iRingCoNF_isready=0;
  }

  /*--- Free the ring buffer memory (never rely on the OS to do it) -*/
  destroy_ring_buf(&gstRing);

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
