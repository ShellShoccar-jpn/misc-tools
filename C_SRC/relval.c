/*####################################################################
#
# RELVAL - Limit the Flow Rate of the UNIX Pipeline Like a Relief Valve
#
# USAGE   : relval [-c|-e|-I|-z] [-ku] [-d fd|file] ratelimit   [file [...]]
#           relval [-c|-e|-I|-z] [-ku] [-d fd|file] controlfile [file [...]]
# Args    : file ........ Filepath to be sent ("-" means STDIN)
#                         The file MUST be a textfile and MUST have
#                         a timestamp at the first field to make the
#                         timing of flow. The first space character
#                         <0x20> of every line will be regarded as
#                         the field delimiter.
#                         And, the string from the top of the line to
#                         the charater will be cut before outgoing to
#                         the stdout.
#           ratelimit ... Dataflow limit. You can specify it by the
#                         following two methods.
#                           1. interval time
#                              * One line will be allowed to pass through
#                                in the time you specified.
#                              * The usage is "time[unit]."
#                                - "time" is the numerical part. You
#                                  can use an integer or a decimal.
#                                - "unit" is the part of the unit of time.
#                                  You can choose one of "s," "ms," "us,"
#                                  "ns," "m," "h," or "d." The default
#                                  is "s."
#                              * If you set "1.24ms," this command
#                                allows up to one line of the source
#                                textdata to pass through every 1.24
#                                milliseconds.
#                           2. number per time
#                              * Text data of a specified number of lines
#                                are allowed to pass through in a specified
#                                time.
#                              * The usage is "number/time."
#                                - "number" is the part to specify the
#                                  numner of lines. You can set only a
#                                  natural number from 1 to 65535.
#                                - "/" is the delimiter to seperate
#                                  parts. You must insert any whitespace
#                                  characters before and after this slash
#                                  letter.
#                                - "time" is the part that specifies
#                                  the period. The usage is the same as
#                                  the interval time we explained above.
#                              * If you set "10/1.5," this command
#                                allows up to 10 lines to pass through
#                                every 1.5 seconds.
#           controlfile . Filepath to specify the ratelimit instead of
#                         by argument. You can change the parameter even
#                         when this command is running by updating the
#                         content of the controlfile.
#                         * The parameter syntax you can specify in this
#                           file is completely the same as the argument,
#                           but if you give me an invalid parameter, this
#                           command will ignore it silently with no error.
#                         * The default is "1/86400d" (virtually no line
#                           will pass) unless any valid parameter is given.
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
#                           - Character-special file / Named-pipe:
#                             It is better for the performance. If you
#                             use these types of files, you can write
#                             a new parameter with both the above two
#                             modes. The new parameter will be applied
#                             immediately just after writing.
# Options : -c,-e,-I,-z . Specify the format for timestamp. You can
#                         choose one of them.
#                           -c ... "YYYYMMDDhhmmss[.n]" (default)
#                                  Calendar time (standard time) in your
#                                  timezone (".n" is the digits under
#                                  second. You can specify up to nano
#                                  second.)
#                           -e ... "n[.n]"
#                                  The number of seconds since the UNIX
#                                  epoch (".n" is the same as -x)
#                           -I ... "YYYY-MM-DDThh:mm:ss[,n][{{+|-}hh:mm|Z}]"
#                                  Ext. ISO 8601 formatted time in your
#                                  timezone (".n" is the same as -x)
#                           -z ... "n[.n]"
#                                  The number of seconds since this
#                                  command has startrd (".n" is the
#                                  same as -x)
#           -u .......... Set the date in UTC when -c option is set
#                         (same as that of date command)
#           -k .......... Keep the timestamp when outputting each line.\n
#           -d fd|file .. If you set this option, the lines that will be
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
# Return  : Return 0 only when finished successfully
#
# How to compile : cc -O3 -std=c99 -o __CMDNAME__ __SRCNAME__ -pthread -lrt
#                  (if it doesn't work)
# How to compile : cc -O3 -std=c99 -o __CMDNAME__ __SRCNAME__ -pthread
#
# Written by Shell-Shoccar Japan (@shellshoccarjpn) on 2026-09-24
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
 * 700 there trips its strict conformance-level check and aborts the
 * build, so __EXTENSIONS__ (which sidesteps that check entirely and
 * exposes every POSIX/XSI/BSD interface regardless of C standard
 * level) is used there instead. Everywhere else, _XOPEN_SOURCE 700 is
 * used directly, for strptime().                                    */
#if defined(__sun) || defined(__SVR4)
  #define __EXTENSIONS__
#else
  #define _XOPEN_SOURCE 700
#endif
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

/*--- macro constants ----------------------------------------------*/
/* Interval time of looking at the parameter on the control file */
#define FREAD_ITRVL_SEC  0
#define FREAD_ITRVL_USEC 100000
/* Buffer size for the control file */
#define CTRL_FILE_BUF 64
#define BILLION 1000000000
#define RINGBUF_NUM_MAX 65535
/* Default ratelimit parameters if the controlfile is given
 * ("1/86400d" means one line is allowed to pass through per day,
 *  which is virtually the same as "no line will pass through.")   */
#define DEFAULT_MAXLINES 1
#define DEFAULT_DURATION ((int64_t)INT_MAX * (int64_t)BILLION)

/*--- data type definitions ----------------------------------------*/
typedef struct timespec tmsp;
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
} thmaininfo_t;

/*--- prototype functions ------------------------------------------*/
void*   param_updater(void* pvArgs);
void    update_ratelimit_type_r(char* pszCtrlfile);
void    update_ratelimit_type_c(char* pszCtrlfile);
void    do_nothing(int iSig, siginfo_t *siInfo, void *pct);
#ifdef __ANDROID__
void    term_this_thread(int iSig, siginfo_t *siInfo, void *pct);
#endif
void    recv_param_application_req(int iSig, siginfo_t *siInfo, void *pct);
void    mainth_destructor(void* pvMainth);
void    subth_destructor(void *pvFd);
void    apply_new_ratelimit(tmsp** pptsRingBuf, int* piLastitemCode,
          int* piMaxlines_prev);
int     erase_stale_items_in_the_ring_buffer(
          int iBufsize, tmsp* ptsBuf, int iLast, tmsp tsRef);
int     parse_calendartime(char* pszTime, tmsp *ptsTime);
int     parse_unixtime(char* pszTime, tmsp *ptsTime);
int     parse_iso8601time(char* pszTime, tmsp *ptsTime);
int     parse_ratelimit(char* pszRule, int64_t* pi8Duration, int* piMaxlines);
int     read_1st_field_as_a_timestamp(FILE *fp, char *pszTime);
int     read_and_drain_a_line(FILE *fp, FILE *fpDrain);
int     read_and_write_a_line(FILE *fp);
int     skip_over_a_line(FILE *fp);
int64_t parse_duration(char* pszDuration);
tmsp*   generate_ring_buf(int iSize);
void    release_ring_buf(tmsp* pts);

/*--- global variables ---------------------------------------------*/
int     giVerbose;       /* speaks more verbosely by the greater number   */
char*   gpszCmdname;     /* The name of this command                      */
int     giTZoffs;        /* Offset in second in the local timezone
                          * (used only in "-I" mode)                      */
int     giMaxlines;      /* the max number of lines allowed
                          * - It is global but for only the main-th. The
                          *   sub-th has to write the parameter into the
                          *   gstThCom.iParam1 instead when the sub-th
                          *   gives the main-th the new parameter.        */
int64_t gi8Duration;     /* the time to reset giMaxlines (in nanosecond)
                          * - It is global but for only the main-th. The
                          *   sub-th has to write the parameter into the
                          *   gstThCom.i8Param1 instead when the sub-th
                          *   gives the main-th the new parameter.        */
struct stat gstCtrlfile; /* stat for the control file                     */
thcominfo_t gstThCom;    /* Variables for threads communication           */


/*=== Define the functions for printing usage and error ============*/

/*--- exit with usage ----------------------------------------------*/
void print_usage_and_exit(void) {
  fprintf(stderr,
    "USAGE   : %s [-c|-e|-I|-z] [-ku] [-d fd|file] ratelimit   [file [...]]\n"
    "          %s [-c|-e|-I|-z] [-ku] [-d fd|file] controlfile [file [...]]\n"
    "Args    : file ........ Filepath to be sent (\"-\" means STDIN)\n"
    "                        The file MUST be a textfile and MUST have\n"
    "                        a timestamp at the first field to make the\n"
    "                        timing of flow. The first space character\n"
    "                        <0x20> of every line will be regarded as\n"
    "                        the field delimiter.\n"
    "                        And, the string from the top of the line to\n"
    "                        the charater will be cut before outgoing to\n"
    "                        the stdout.\n"
    "          ratelimit ... Dataflow limit. You can specify it by the\n"
    "                        following two methods.\n"
    "                          1. interval time\n"
    "                             * One line will be allowed to pass through\n"
    "                               in the time you specified.\n"
    "                             * The usage is \"time[unit].\"\n"
    "                               - \"time\" is the numerical part. You\n"
    "                                 can use an integer or a decimal.\n"
    "                               - \"unit\" is the part of the unit of\n"
    "                                 time. You can choose one of \"s,\"\n"
    "                                 \"ms,\" \"us,\" \"ns,\" \"m,\" \"h,\" or\n"
    "                                 \"d.\" The default is \"s.\"\n"
    "                             * If you set \"1.24ms,\" this command\n"
    "                               allows up to one line of the source\n"
    "                               textdata to pass through every 1.24\n"
    "                               milliseconds.\n"
    "                          2. number per time\n"
    "                             * Text data of a specified number of lines\n"
    "                               are allowed to pass through in a\n"
    "                               specified time.\n"
    "                             * The usage is \"number/time.\"\n"
    "                               - \"number\" is the part to specify the\n"
    "                                 numner of lines. You can set only a\n"
    "                                 natural number from 1 to 65535.\n"
    "                               - \"/\" is the delimiter to seperate\n"
    "                                 parts. You must insert any whitespace\n"
    "                                 characters before and after this slash\n"
    "                                 letter.\n"
    "                               - \"time\" is the part that specifies\n"
    "                                 the period. The usage is the same as\n"
    "                                 the interval time we explained above.\n"
    "                             * If you set \"10/1.5,\" this command\n"
    "                               allows up to 10 lines to pass through\n"
    "                               every 1.5 seconds.\n"
    "          controlfile . Filepath to specify the ratelimit instead of\n"
    "                        by argument. You can change the parameter even\n"
    "                        when this command is running by updating the\n"
    "                        content of the controlfile.\n"
    "                        * The parameter syntax you can specify in this\n"
    "                          file is completely the same as the argument,\n"
    "                          but if you give me an invalid parameter, this\n"
    "                          command will ignore it silently with no error.\n"
    "                        * The default is \"1/86400d\" (virtually no line\n"
    "                          will pass) unless any valid parameter is given.\n"
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
    "Options : -c,-e,-I,-z . Specify the format for timestamp. You can\n"
    "                        choose one of them.\n"
    "                          -c ... \"YYYYMMDDhhmmss[.n]\" (default)\n"
    "                                 Calendar time (standard time) in your\n"
    "                                 timezone (\".n\" is the digits under\n"
    "                                 second. You can specify up to nano\n"
    "                                 second.)\n"
    "                          -e ... \"n[.n]\"\n"
    "                                 The number of seconds since the UNIX\n"
    "                                 epoch (\".n\" is the same as -x)\n"
    "                          -I ... \"YYYY-MM-DDThh:mm:ss[,n][{{+|-}hh:mm|Z}]\"\n"
    "                                 Ext. ISO 8601 formatted time in your\n"
    "                                 timezone (\".n\" is the same as -x)\n"
    "                          -z ... \"n[.n]\"\n"
    "                                 The number of seconds since this\n"
    "                                 command has startrd (\".n\" is the\n"
    "                                 same as -x)\n"
    "          -u .......... Set the date in UTC when -c option is set\n"
    "                        (same as that of date command)\n"
    "          -k .......... Keep the timestamp when outputting each line.\n"
    "          -d fd|file .. If you set this option, the lines that will be\n"
    "                        dropped will be sent to the specified file\n"
    "                        descriptor or file.\n"
    "                        * When you set an integer, this command regards\n"
    "                          it as a file descriptor number. If you want\n"
    "                          to specify the file in the current directory\n"
    "                          that has a numerical filename, you have to\n"
    "                          add \"./\" before the name, like \"./3.\"\n"
    "                        * When you set another type of string, this\n"
    "                          command regards it as a filename.\n"
    "\n"
    "Version : 2026-09-24 01:09:03 JST\n"
    "          (POSIX C language)\n"
    "\n"
    "USP-NCNT prj. / Shell-Shoccar Japan (@shellshoccarjpn),\n"
    "No rights reserved. This is public domain software. (CC0)\n"
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
  vfprintf(stderr, szFormat, va);
  va_end(va);
  return;
}

/*--- exit with error message --------------------------------------*/
void error_exit(int iErrno, const char* szFormat, ...) {
  va_list va;
  va_start(va, szFormat);
  fprintf(stderr,"%s: ",gpszCmdname);
  vfprintf(stderr, szFormat, va);
  va_end(va);
  exit(iErrno);
}



/*####################################################################
# Main
####################################################################*/

/*=== Initialization ===============================================*/
int main(int argc, char *argv[]) {

/*--- Variables ----------------------------------------------------*/
sigset_t ssMask;         /* blocking signal list for the main thread */
struct sigaction saHup;  /* for signal handler definition (action)   */
int     i;              /* all-purpose int                          */
int     iDrainFd;       /* File Descriptor for Drain                */
int     iFd;            /* file descriptor                          */
int     iFileno;        /* file# of filepath                        */
int     iKeepTs;        /* -k option (>0:Keep timestamps, ==0:Drop) */
int     iLastitemCode;  /* last memorized item in the buffer        */
int     iMaxlines_prev; /* giMaxlines memorized to detect the change */
int     iMode;          /* 0:"-c" 1:"-e" 2:"-z" 3:"-I"              */
int     iNumVacantBuf;  /* Number of vacant items in the buffer     */
int     iRet;           /* return code                              */
char    szTime[43];     /* Buffer for the 1st field of lines        */
char    szDummy[2];     /* A dummy str (used in sscanf())           */
char*   pszDrainname;   /* Filename for Drain                       */
char*   pszFilename;    /* filepath (for message)                   */
char*   pszPath;        /* filepath on arguments                    */
FILE*   fp;             /* file handle                              */
FILE*   fpDrain;        /* file handle for the drain                */
tmsp    tsTime;         /* Parsed time for the 1st field            */
tmsp    tsRef;          /* Ref-time to erase the stale items        */
tmsp*   ptsRingBuf;     /* To memorize timestamp of passed lines    */
thmaininfo_t stMainth;  /* Variables required in handler functions  */

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
iMode        =  0;      /* 0:"-c"(default) 1:"-e" 2:"-z" 3:"-I" */
giVerbose    =  0;
gi8Duration  = BILLION; /* 1 second */
iDrainFd     = -1;
giMaxlines   =  1;
iKeepTs      =  0;
pszDrainname = NULL;

/*--- Parse options which start with "-" ---------------------------*/
while((i=getopt(argc, argv, "cezIukd:hv")) != -1) {
  switch(i){
    case 'c': iMode=0;                       break;
    case 'e': iMode=1;                       break;
    case 'z': iMode=2;                       break;
    case 'I': iMode=3;                       break;
    case 'u': (void)setenv("TZ", "UTC0", 1); break;
    case 'k': iKeepTs=1;                     break;
    case 'd': if (sscanf(optarg,"%d%1s",&iDrainFd,szDummy) != 1) {iDrainFd=-1;}
              if (iDrainFd>=0) {pszDrainname=NULL;} else {pszDrainname=optarg;}
              break;
    case 'v': giVerbose++;                   break;
    case 'h': print_usage_and_exit();
    default : print_usage_and_exit();
  }
}
if (giVerbose>0) {warning("verbose mode (level %d)\n",giVerbose);}
argc -= optind;
argv += optind;

/*--- Calculate the timezone offset if the -I option is enabled ----*/
if (iMode==3) {
  /* "giTZoffs" means "localtime - UTCtime" */
  giTZoffs = (int)difftime(mktime(localtime((time_t[]){0})),
                           mktime(   gmtime((time_t[]){0})) );
}

/*--- Parse the ratelimit argument ---------------------------------*/
if (argc < 1){print_usage_and_exit();}
/*--- Prepare the thread operation ---------------------------------*/
memset(&gstThCom, 0, sizeof(gstThCom    ));
memset(&stMainth, 0, sizeof(thmaininfo_t));
pthread_cleanup_push(mainth_destructor, &stMainth);
/*--- Try to interpret it as a ratelimit; if it fails, regard it ----
      as a controlfile and start the subthread                    -*/
if (parse_ratelimit(argv[0], &gi8Duration, &giMaxlines) != 0) {
  /* Set the initial parameter, which is virtually "no line passes" */
  giMaxlines  = DEFAULT_MAXLINES; gstThCom.iParam1  = giMaxlines ;
  gi8Duration = DEFAULT_DURATION; gstThCom.i8Param1 = gi8Duration;
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

/*=== Pre-Operation of the each file loop ==========================*/

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

/*--- generate a ring buffer ----------------------------------------*/
if ((ptsRingBuf=generate_ring_buf(giMaxlines)) == NULL) {
  error_exit(errno, "%s: %s\n", pszDrainname, strerror(errno));
}
iMaxlines_prev = giMaxlines;

/*=== Each file loop ===============================================*/
iRet          =  0;
iFileno       =  0;
iFd           = -1;
iLastitemCode =  0;
while (argc > 0 || iFileno == 0) {
  /*--- Read the filepath ------------------------------------------*/
  if (argc == 0) {pszPath="-";          }
  else           {pszPath=argv[iFileno];}
  argc--;

  /*--- Open one of the input files --------------------------------*/
  if (strcmp(pszPath, "-") == 0) {
    pszFilename = "stdin"                ;
    iFd         = STDIN_FILENO           ;
  } else                         {
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
  iFileno++;

  /*--- relief valve -----------------------------------------------*/
  switch(iMode){
    case 0: /* "-c" Calendar time mode */
            while(1){
              apply_new_ratelimit(&ptsRingBuf, &iLastitemCode, &iMaxlines_prev);
              switch(read_1st_field_as_a_timestamp(fp,szTime)){
                 case  1: /* read successfully */
                          if (! parse_calendartime(szTime, &tsTime)) {
                            warning("%s: %s: Invalid timestamp, "
                                    "skip this line\n", pszFilename, szTime);
                            iRet = 1;
                            switch (skip_over_a_line(fp)) {
                              case  1: /* expected LF */
                                       break;
                              case -1: /* expected EOF */
                                       goto CLOSE_THISFILE;
                              case -2: /* file access error */
                                       warning("%s: File access error, "
                                               "skip it\n", pszFilename);
                                       goto CLOSE_THISFILE;
                                       break;
                              default: /* bug of system error */
                                       error_exit(1,"Unexpected error at %d\n",
                                                  __LINE__);
                                       break;
                            }
                            break;
                          }
                          tsRef.tv_nsec = tsTime.tv_nsec
                                          - (long)gi8Duration%BILLION;
                          if (tsRef.tv_nsec < 0) {
                            tsRef.tv_nsec += BILLION;
                            tsRef.tv_sec   = tsTime.tv_sec - 1
                                             - (time_t)gi8Duration/BILLION;
                          } else {
                            tsRef.tv_sec = tsTime.tv_sec
                                           - (time_t)gi8Duration/BILLION;
                          }

                          iNumVacantBuf = erase_stale_items_in_the_ring_buffer(
                                            iMaxlines_prev, ptsRingBuf,
                                            iLastitemCode, tsRef);
                          if (iNumVacantBuf < 0) {
                            error_exit(1,"Unexpected error at %d\n", __LINE__);
                          }
                          if (iNumVacantBuf == 0) {
                            if (fpDrain == NULL) {
                              switch (skip_over_a_line(fp)) {
                                case  1: /* expected LF */
                                         break;
                                case -1: /* expected EOF */
                                         goto CLOSE_THISFILE;
                                case -2: /* file access error */
                                         warning("%s: File access error, "
                                                 "skip it\n", pszFilename);
                                         goto CLOSE_THISFILE;
                                         break;
                                default: /* bug of system error */
                                         error_exit(1,
                                                    "Unexpected error at %d\n",
                                                    __LINE__);
                                         break;
                              }
                              break;
                            } else {
                              if (iKeepTs) {
                                if (fputs(szTime, fpDrain) == EOF) {
                                  error_exit(1, "Access error at the drain\n");
                                }
                              }
                              switch (read_and_drain_a_line(fp,fpDrain)) {
                                case  1: /* expected LF */
                                         break;
                                case -1: /* expected EOF */
                                         goto CLOSE_THISFILE;
                                case -2: /* file access error */
                                         warning("%s: File access error, "
                                                 "skip it\n", pszFilename);
                                         iRet = 1;
                                         goto CLOSE_THISFILE;
                                         break;
                                default: /* bug of system error */
                                         error_exit(1,
                                                    "Unexpected error at %d\n",
                                                    __LINE__);
                                         break;
                              }
                              break;
                            }
                            break;
                          }
                          iLastitemCode = (iLastitemCode+1) % iMaxlines_prev;
                          ptsRingBuf[iLastitemCode].tv_sec  = tsTime.tv_sec;
                          ptsRingBuf[iLastitemCode].tv_nsec = tsTime.tv_nsec;
                          if (iKeepTs) {
                            if (fputs(szTime, stdout) == EOF) {
                              error_exit(1, "Access error at the stdout\n");
                            }
                          }
                          switch (read_and_write_a_line(fp)) {
                            case  1: /* expected LF */
                                     break;
                            case -1: /* expected EOF */
                                     goto CLOSE_THISFILE;
                            case -2: /* file access error */
                                     warning("%s: File access error, "
                                             "skip it\n", pszFilename);
                                     iRet = 1;
                                     goto CLOSE_THISFILE;
                                     break;
                            default: /* bug of system error */
                                     error_exit(1,
                                                "Unexpected error at %d\n",
                                                __LINE__);
                                     break;
                          }
                          break;
                 case  0: /* unexpected LF */
                          warning("%s: %s: Invalid timestamp field found, "
                                  "skip this line.\n", pszFilename, szTime);
                          iRet = 1;
                          break;
                 case -2: /* unexpected EOF */
                          warning("%s: Came to EOF suddenly\n", pszFilename);
                          iRet = 1;
                 case -1: /*   expected EOF */
                          goto CLOSE_THISFILE;
                          break;
                 case -3: /* file access error */
                          warning("%s: File access error, skip it\n",
                                  pszFilename);
                          iRet = 1;
                          goto CLOSE_THISFILE;
                 default: /* bug or system error */
                          error_exit(1,"Unexpected error at %d\n", __LINE__);
              }
            }
            break;
    case 1: /* "-e" Unix time mode */
    case 2: /* "-z" Zero time mode */
            while(1){
              apply_new_ratelimit(&ptsRingBuf, &iLastitemCode, &iMaxlines_prev);
              switch(read_1st_field_as_a_timestamp(fp,szTime)){
                 case  1: /* read successfully */
                          if (! parse_unixtime(szTime, &tsTime)) {
                            warning("%s: %s: Invalid timestamp, "
                                    "skip this line\n", pszFilename, szTime);
                            iRet = 1;
                            switch (skip_over_a_line(fp)) {
                              case  1: /* expected LF */
                                       break;
                              case -1: /* expected EOF */
                                       goto CLOSE_THISFILE;
                              case -2: /* file access error */
                                       warning("%s: File access error, "
                                               "skip it\n", pszFilename);
                                       goto CLOSE_THISFILE;
                                       break;
                              default: /* bug of system error */
                                       error_exit(1,"Unexpected error at %d\n",
                                                  __LINE__);
                                       break;
                            }
                            break;
                          }
                          tsRef.tv_nsec = tsTime.tv_nsec
                                          - (long)gi8Duration%BILLION;
                          if (tsRef.tv_nsec < 0) {
                            tsRef.tv_nsec += BILLION;
                            tsRef.tv_sec   = tsTime.tv_sec - 1
                                             - (time_t)gi8Duration/BILLION;
                          } else {
                            tsRef.tv_sec = tsTime.tv_sec
                                           - (time_t)gi8Duration/BILLION;
                          }

                          iNumVacantBuf = erase_stale_items_in_the_ring_buffer(
                                            iMaxlines_prev, ptsRingBuf,
                                            iLastitemCode, tsRef);
                          if (iNumVacantBuf < 0) {
                            error_exit(1,"Unexpected error at %d\n", __LINE__);
                          }
                          if (iNumVacantBuf == 0) {
                            if (fpDrain == NULL) {
                              switch (skip_over_a_line(fp)) {
                                case  1: /* expected LF */
                                         break;
                                case -1: /* expected EOF */
                                         goto CLOSE_THISFILE;
                                case -2: /* file access error */
                                         warning("%s: File access error, "
                                                 "skip it\n", pszFilename);
                                         goto CLOSE_THISFILE;
                                         break;
                                default: /* bug of system error */
                                         error_exit(1,
                                                    "Unexpected error at %d\n",
                                                    __LINE__);
                                         break;
                              }
                              break;
                            } else {
                              if (iKeepTs) {
                                if (fputs(szTime, fpDrain) == EOF) {
                                  error_exit(1, "Access error at the drain\n");
                                }
                              }
                              switch (read_and_drain_a_line(fp,fpDrain)) {
                                case  1: /* expected LF */
                                         break;
                                case -1: /* expected EOF */
                                         goto CLOSE_THISFILE;
                                case -2: /* file access error */
                                         warning("%s: File access error, "
                                                 "skip it\n", pszFilename);
                                         iRet = 1;
                                         goto CLOSE_THISFILE;
                                         break;
                                default: /* bug of system error */
                                         error_exit(1,
                                                    "Unexpected error at %d\n",
                                                    __LINE__);
                                         break;
                              }
                              break;
                            }
                            break;
                          }
                          iLastitemCode = (iLastitemCode+1) % iMaxlines_prev;
                          ptsRingBuf[iLastitemCode].tv_sec  = tsTime.tv_sec;
                          ptsRingBuf[iLastitemCode].tv_nsec = tsTime.tv_nsec;
                          if (iKeepTs) {
                            if (fputs(szTime, stdout) == EOF) {
                              error_exit(1, "Access error at the stdout\n");
                            }
                          }
                          switch (read_and_write_a_line(fp)) {
                            case  1: /* expected LF */
                                     break;
                            case -1: /* expected EOF */
                                     goto CLOSE_THISFILE;
                            case -2: /* file access error */
                                     warning("%s: File access error, "
                                             "skip it\n", pszFilename);
                                     iRet = 1;
                                     goto CLOSE_THISFILE;
                                     break;
                            default: /* bug of system error */
                                     error_exit(1,
                                                "Unexpected error at %d\n",
                                                __LINE__);
                                     break;
                          }
                          break;
                 case  0: /* unexpected LF */
                          warning("%s: %s: Invalid timestamp field found, "
                                  "skip this line.\n", pszFilename, szTime);
                          iRet = 1;
                          break;
                 case -2: /* unexpected EOF */
                          warning("%s: Came to EOF suddenly\n", pszFilename);
                          iRet = 1;
                 case -1: /*   expected EOF */
                          goto CLOSE_THISFILE;
                          break;
                 case -3: /* file access error */
                          warning("%s: File access error, skip it\n",
                                  pszFilename);
                          iRet = 1;
                          goto CLOSE_THISFILE;
                 default: /* bug or system error */
                          error_exit(1,"Unexpected error at %d\n", __LINE__);
              }
            }
            break;
    case 3: /* "-I" Extended ISO 8601 time mode */
            while(1){
              apply_new_ratelimit(&ptsRingBuf, &iLastitemCode, &iMaxlines_prev);
              switch(read_1st_field_as_a_timestamp(fp,szTime)){
                 case  1: /* read successfully */
                          if (! parse_iso8601time(szTime, &tsTime)) {
                            warning("%s: %s: Invalid ISO8601-time, "
                                    "skip this line\n", pszFilename, szTime);
                            iRet = 1;
                            switch (skip_over_a_line(fp)) {
                              case  1: /* expected LF */
                                       break;
                              case -1: /* expected EOF */
                                       goto CLOSE_THISFILE;
                              case -2: /* file access error */
                                       warning("%s: File access error, "
                                               "skip it\n", pszFilename);
                                       goto CLOSE_THISFILE;
                                       break;
                              default: /* bug of system error */
                                       error_exit(1,"Unexpected error at %d\n",
                                                  __LINE__);
                                       break;
                            }
                            break;
                          }
                          tsRef.tv_nsec = tsTime.tv_nsec
                                          - (long)gi8Duration%BILLION;
                          if (tsRef.tv_nsec < 0) {
                            tsRef.tv_nsec += BILLION;
                            tsRef.tv_sec   = tsTime.tv_sec - 1
                                             - (time_t)gi8Duration/BILLION;
                          } else {
                            tsRef.tv_sec = tsTime.tv_sec
                                           - (time_t)gi8Duration/BILLION;
                          }

                          iNumVacantBuf = erase_stale_items_in_the_ring_buffer(
                                            iMaxlines_prev, ptsRingBuf,
                                            iLastitemCode, tsRef);
                          if (iNumVacantBuf < 0) {
                            error_exit(1,"Unexpected error at %d\n", __LINE__);
                          }
                          if (iNumVacantBuf == 0) {
                            if (fpDrain == NULL) {
                              switch (skip_over_a_line(fp)) {
                                case  1: /* expected LF */
                                         break;
                                case -1: /* expected EOF */
                                         goto CLOSE_THISFILE;
                                case -2: /* file access error */
                                         warning("%s: File access error, "
                                                 "skip it\n", pszFilename);
                                         goto CLOSE_THISFILE;
                                         break;
                                default: /* bug of system error */
                                         error_exit(1,
                                                    "Unexpected error at %d\n",
                                                    __LINE__);
                                         break;
                              }
                              break;
                            } else {
                              if (iKeepTs) {
                                if (fputs(szTime, fpDrain) == EOF) {
                                  error_exit(1, "Access error at the drain\n");
                                }
                              }
                              switch (read_and_drain_a_line(fp,fpDrain)) {
                                case  1: /* expected LF */
                                         break;
                                case -1: /* expected EOF */
                                         goto CLOSE_THISFILE;
                                case -2: /* file access error */
                                         warning("%s: File access error, "
                                                 "skip it\n", pszFilename);
                                         iRet = 1;
                                         goto CLOSE_THISFILE;
                                         break;
                                default: /* bug of system error */
                                         error_exit(1,
                                                    "Unexpected error at %d\n",
                                                    __LINE__);
                                         break;
                              }
                              break;
                            }
                            break;
                          }
                          iLastitemCode = (iLastitemCode+1) % iMaxlines_prev;
                          ptsRingBuf[iLastitemCode].tv_sec  = tsTime.tv_sec;
                          ptsRingBuf[iLastitemCode].tv_nsec = tsTime.tv_nsec;
                          if (iKeepTs) {
                            if (fputs(szTime, stdout) == EOF) {
                              error_exit(1, "Access error at the stdout\n");
                            }
                          }
                          switch (read_and_write_a_line(fp)) {
                            case  1: /* expected LF */
                                     break;
                            case -1: /* expected EOF */
                                     goto CLOSE_THISFILE;
                            case -2: /* file access error */
                                     warning("%s: File access error, "
                                             "skip it\n", pszFilename);
                                     iRet = 1;
                                     goto CLOSE_THISFILE;
                                     break;
                            default: /* bug of system error */
                                     error_exit(1,
                                                "Unexpected error at %d\n",
                                                __LINE__);
                                     break;
                          }
                          break;
                 case  0: /* unexpected LF */
                          warning("%s: %s: Invalid timestamp field found, "
                                  "skip this line.\n", pszFilename, szTime);
                          iRet = 1;
                          break;
                 case -2: /* unexpected EOF */
                          warning("%s: Came to EOF suddenly\n", pszFilename);
                          iRet = 1;
                 case -1: /*   expected EOF */
                          goto CLOSE_THISFILE;
                          break;
                 case -3: /* file access error */
                          warning("%s: File access error, skip it\n",
                                  pszFilename);
                          iRet = 1;
                          goto CLOSE_THISFILE;
                 default: /* bug or system error */
                          error_exit(1,"Unexpected error at %d\n", __LINE__);
              }
            }
            break;
    default:
            error_exit(1,"Undefined mode\n");
  }

CLOSE_THISFILE:
  /*--- Close the input file ---------------------------------------*/
  if (fp != stdin) {fclose(fp);}

  /*--- End loop ---------------------------------------------------*/
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
if (gstCtrlfile.st_mode & S_IFREG) {update_ratelimit_type_r(pszCtrlfile);}

/*=== The routine when the control file is a character special file */
else                               {update_ratelimit_type_c(pszCtrlfile);}

/*=== End of the subthread (does not come here) ====================*/
return NULL;}



/*####################################################################
# Subroutines of the Subthread
####################################################################*/

/*=== Try to update the parameter for a regular file =================
 * [in]  pszCtrlfile      : Filename of the control file which the ratelimit
 *                          is written
 *       gstThCom.tMainth_id
 *                        : The main thread ID
 *       gstThCom.mu      : Mutex object to lock
 *       gstThCom.co      : Condition variable to send a signal to the sub-th
 * [out] gstThCom.i8Param1: The new duration part (int64_t)
 *       gstThCom.iParam1 : The new maxlines part (int)
 *       gstThCom.iReceived
 *                        : Set to 0 after confirming that the main thread
 *                          receivedi the request                      */
void update_ratelimit_type_r(char* pszCtrlfile) {

  /*--- Variables --------------------------------------------------*/
  struct sigaction saAlrm; /* for signal handler definition (action)   */
  sigset_t         ssMask; /* unblocking signal list                   */
  struct itimerval itInt ; /* for signal handler definition (interval) */
  int              iFd_ctrlfile        ; /* file desc. of the ctrlfile */
  char             szBuf[CTRL_FILE_BUF]; /* parameter string buffer    */
  int              iLen                ; /* length of the parameter str*/
  int64_t          i8                  ;
  int              iNum                ;
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
    /* 1) Try to read the ratelimit */
    if (lseek(iFd_ctrlfile,0,SEEK_SET) < 0                 ) {goto pause;}
    if ((iLen=read(iFd_ctrlfile,szBuf,CTRL_FILE_BUF-1)) < 1) {goto pause;}
    for (i=0;i<iLen;i++) {if(szBuf[i]=='\n'){break;}}
    szBuf[i]='\0';
    if (parse_ratelimit(szBuf, &i8, &iNum)            != 0) {goto pause;}
    if ((gstThCom.i8Param1==i8) && (gstThCom.iParam1==iNum)) {goto pause;}
    /* 2) Update the ratelimit */
    gstThCom.i8Param1 = i8 ;
    gstThCom.iParam1  = iNum;
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

/*=== Try to update the parameter for a char-sp/FIFO file ============
 * [in]  pszCtrlfile      : Filename of the control file which the ratelimit
 *                          is written
 *       gstThCom.tMainth_id
 *                        : The main thread ID
 *       gstThCom.mu      : Mutex object to lock
 *       gstThCom.co      : Condition variable to send a signal to the sub-th
 * [out] gstThCom.i8Param1: The new duration part (int64_t)
 *       gstThCom.iParam1 : The new maxlines part (int)
 *       gstThCom.iReceived
 *                        : Set to 0 after confirming that the main thread
 *                          receivedi the request                      */
void update_ratelimit_type_c(char* pszCtrlfile) {

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
  int     iNum                     ;
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
    if (parse_ratelimit(szCmdbuf, &i8, &iNum)             != 0) {
      szCmdbuf[0]='\0'; continue; /* Invalid ratelimit string */
    }
    if ((gstThCom.i8Param1==i8) && (gstThCom.iParam1==iNum)) {
      szCmdbuf[0]='\0'; continue; /* Parameters do not change */
    }
    gstThCom.i8Param1 = i8 ;
    gstThCom.iParam1  = iNum;
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


/*=== Parse a local calendar time ====================================
 * [in]  pszTime : calendar-time string in the localtime
 *                 (/[0-9]{11,20}(\.[0-9]{1,9})?/)
 *       ptsTime : To be set the parsed time ("timespec" structure)
 * [ret] > 0 : success
 *       ==0 : error (failure to parse)                             */
int parse_calendartime(char* pszTime, tmsp *ptsTime) {

  /*--- Variables --------------------------------------------------*/
  char szDate[21], szNsec[10], szDate2[26];
  int  i, j, k;            /* +-- 0:(reading integer part)          */
  char c;                  /* +-- 1:finish reading without_decimals */
  int  iStatus = 0; /* <--------- 2:to_be_started reading decimals  */
  struct tm tmDate;

  /*--- Separate pszTime into date and nanoseconds -----------------*/
  for (i=0; i<20; i++) {
    c = pszTime[i];
    if      (('0'<=c) && (c<='9')     ) {szDate[i]=c;                       }
    else if (c=='.'                   ) {szDate[i]=0; iStatus=2; i++; break;}
    else if (c==0 || c==' ' || c=='\t') {szDate[i]=0; iStatus=1;      break;}
    else                                {if (giVerbose>0) {
                                           warning("%c: Unexpected chr. in "
                                                   "the integer part\n",c);
                                         }
                                         return 0;
                                        }
  }
  if ((iStatus==0) && (i==20)) {
    switch (pszTime[20]) {
      case '.': szDate[20]=0; iStatus=2; i++; break;
      case  0 : szDate[20]=0; iStatus=1;      break;
      default : warning("The integer part of the timestamp is too big "
                        "as a calendar-time\n",c);
                return 0;
    }
  }
  switch (iStatus) {
    case 1 : strcpy(szNsec,"000000000");
             break;
    case 2 : j=i+9;
             k=0;
             for (; i<j; i++) {
               c = pszTime[i];
               if      (('0'<=c) && (c<='9')     ) {szNsec[k]=c; k++;}
               else if (c==0 || c==' ' || c=='\t') {break;           }
               else                                {
                 if (giVerbose>0) {
                   warning("%c: Unexpected chr. in the decimal part\n",c);
                 }
                 return 0;
               }
             }
             for (; k<9; k++) {szNsec[k]='0';}
             szNsec[9]=0;
             break;
    default: warning("Unexpected error in parse_calendartime(), "
                     "maybe a bug?\n");
             return 0;
  }

  /*--- Pack the time-string into the timespec structure -----------*/
  i = strlen(szDate)-10;
  if (i<=0) {return 0;}
  k =0; for (j=0; j<i; j++) {szDate2[k]=szDate[j];k++;} /* Y */
  szDate2[k]='-'; k++;
  i+=2; for (   ; j<i; j++) {szDate2[k]=szDate[j];k++;} /* m */
  szDate2[k]='-'; k++;
  i+=2; for (   ; j<i; j++) {szDate2[k]=szDate[j];k++;} /* d */
  szDate2[k]='T'; k++;
  i+=2; for (   ; j<i; j++) {szDate2[k]=szDate[j];k++;} /* H */
  szDate2[k]=':'; k++;
  i+=2; for (   ; j<i; j++) {szDate2[k]=szDate[j];k++;} /* M */
  szDate2[k]=':'; k++;
  i+=2; for (   ; j<i; j++) {szDate2[k]=szDate[j];k++;} /* S */
  szDate2[k] = 0;
  memset(&tmDate, 0, sizeof(tmDate));
  if(! strptime(szDate2, "%Y-%m-%dT%H:%M:%S", &tmDate)) {return 0;}
  ptsTime->tv_sec = mktime(&tmDate);
  if (ptsTime->tv_sec == (time_t)-1) {
    /* mktime() is only specified to return (time_t)-1 on failure; it
     * is NOT specified to leave errno unchanged on success (and in
     * practice it can be left set by unrelated internal work, such
     * as loading timezone data), so errno must not be used here.   */
    if (giVerbose>1) {warning("%s: Invalid calendar-time string\n",pszTime);}
    return 0;
  }
  ptsTime->tv_nsec = atol(szNsec);

  return 1;
}


/*=== Parse a UNIX-time ==============================================
 * [in]  pszTime : UNIX-time string (/[0-9]{1,19}(\.[0-9]{1,9})?/)
 *       ptsTime : To be set the parsed time ("timespec" structure)
 * [ret] > 0 : success
 *       ==0 : error (failure to parse)                             */
int parse_unixtime(char* pszTime, tmsp *ptsTime) {

  /*--- Variables --------------------------------------------------*/
  char szSec[20], szNsec[10];
  int  i, j, k;            /* +-- 0:(reading integer part)          */
  char c;                  /* +-- 1:finish reading without_decimals */
  int  iStatus = 0; /* <--------- 2:to_be_started reading decimals  */

  /*--- Separate pszTime into seconds and nanoseconds --------------*/
  for (i=0; i<19; i++) {
    c = pszTime[i];
    if      (('0'<=c) && (c<='9')     ) {szSec[i]=c;                       }
    else if (c=='.'                   ) {szSec[i]=0; iStatus=2; i++; break;}
    else if (c==0 || c==' ' || c=='\t') {szSec[i]=0; iStatus=1;      break;}
    else                                {if (giVerbose>0) {
                                           warning("%c: Unexpected chr. in "
                                                   "the integer part\n",c);
                                         }
                                         return 0;
                                        }
  }
  if ((iStatus==0) && (i==19)) {
    switch (pszTime[19]) {
      case '.': szSec[19]=0; iStatus=2; i++; break;
      case  0 : szSec[19]=0; iStatus=1;      break;
      default : warning("The integer part of the timestamp is too big "
                        "as a UNIX-time\n");
                return 0;
    }
  }
  switch (iStatus) {
    case 1 : strcpy(szNsec,"000000000");
             break;
    case 2 : j=i+9;
             k=0;
             for (; i<j; i++) {
               c = pszTime[i];
               if      (('0'<=c) && (c<='9')     ) {szNsec[k]=c; k++;}
               else if (c==0 || c==' ' || c=='\t') {break;           }
               else                                {
                 if (giVerbose>0) {
                   warning("%c: Unexpected chr. in the decimal part\n",c);
                 }
                 return 0;
               }
             }
             for (; k<9; k++) {szNsec[k]='0';}
             szNsec[9]=0;
             break;
    default: warning("Unexpected error in parse_unixtime(), maybe a bug?\n");
             return 0;
  }

  /*--- Pack the time-string into the timespec structure -----------*/
  ptsTime->tv_sec = (time_t)atoll(szSec);
  if (ptsTime->tv_sec<0) {
    ptsTime->tv_sec = (sizeof(time_t)>=8) ? LLONG_MAX : LONG_MAX;
  }
  ptsTime->tv_nsec = atol(szNsec);

  return 1;
}


/*=== Parse an extended ISO 8601 time ================================
 * [in]  pszTime : Ext. ISO 8601 formatted time string
 *                 ("YYYY-MM-DDThh:mm:ss[,n][{{+|-}hh:mm|Z}]")
 *       ptsTime : To be set the parsed time ("timespec" structure)
 * [ret] > 0 : success
 *       ==0 : error (failure to parse)                             */
int parse_iso8601time(char* pszTime, tmsp *ptsTime) {

  /*--- Variables --------------------------------------------------*/
  char szDate[26], szNsec[10];
  int  iTZoffs;            /* +-- 0:now reading integer part or invalid string*/
  int  i, j, k;            /* +-- 1:to_be_started reading decimals  */
  char c;                  /* +-- 2:to_be_started reading timezone  */
  int  iStatus = 0; /* <--------- 3:finished reading                */
  struct tm tmDate;

  /*--- Read the string (integer part) -----------------------------*/
  iStatus=0;
  while (1) {
    i=0;
    if (pszTime[i]<'0' || '9'<pszTime[i]) {break;} else {i++;} /* Y     */
    while ('0'<=pszTime[i] && pszTime[i]<='9' && i<10 ) {i++;} /* Y{,9} */
    if (pszTime[i] != '-'               ) {break;} else {i++;} /* -     */
    if (pszTime[i]<'0' || '9'<pszTime[i]) {break;} else {i++;} /* M     */
    if (pszTime[i]<'0' || '9'<pszTime[i]) {break;} else {i++;} /* M     */
    if (pszTime[i] != '-'               ) {break;} else {i++;} /* -     */
    if (pszTime[i]<'0' || '9'<pszTime[i]) {break;} else {i++;} /* D     */
    if (pszTime[i]<'0' || '9'<pszTime[i]) {break;} else {i++;} /* D     */
    if (pszTime[i] != 'T'               ) {break;} else {i++;} /* T     */
    if (pszTime[i]<'0' || '9'<pszTime[i]) {break;} else {i++;} /* h     */
    if (pszTime[i]<'0' || '9'<pszTime[i]) {break;} else {i++;} /* h     */
    if (pszTime[i] != ':'               ) {break;} else {i++;} /* :     */
    if (pszTime[i]<'0' || '9'<pszTime[i]) {break;} else {i++;} /* m     */
    if (pszTime[i]<'0' || '9'<pszTime[i]) {break;} else {i++;} /* m     */
    if (pszTime[i] != ':'               ) {break;} else {i++;} /* :     */
    if (pszTime[i]<'0' || '9'<pszTime[i]) {break;} else {i++;} /* s     */
    if (pszTime[i]<'0' || '9'<pszTime[i]) {break;} else {i++;} /* s     */
    switch (pszTime[i]) {
      case ',' :
      case '.' : iStatus = 1; break;
      case 'Z' :
      case '+' :
      case '-' : iStatus = 2; break;
      case 0   :
      case ' ' :
      case '\t': iStatus = 3; break;
      default  : break;
    }
    break;
  }
  if (iStatus==0) {
    if (giVerbose>0) {warning("%s: Invalid ISO 8601 string\n",pszTime);}
    return 0;
  }
  memcpy(szDate, pszTime, i); szDate[i]=0;

  /*--- Read the string (decimal part) -----------------------------*/
  switch (iStatus) {
    case 1 : i++;
             j=i+9;
             k=0;
             for (; i<j; i++) {
               c = pszTime[i];
               if      ('0'<=c  && c<='9'          ) {szNsec[k]=c; k++;}
               else if (c=='+'  || c=='-' || c=='Z') {iStatus=2; break;}
               else if (c=='\t' || c==' ' || c==0  ) {iStatus=3; break;}
               else                                  {
                 if (giVerbose>0) {
                   warning("%s: Invalid ISO 8601 string (decimal part)\n",
                           pszTime                                        );
                 }
                 return 0;
               }
             }
             if (i==j) {
               c = pszTime[i];
               if (c=='+'  || c=='-'        ) {iStatus=2; break;}
               if (c=='\t' || c==' ' || c==0) {iStatus=3; break;}
             }
             for (; k<9; k++) {szNsec[k]='0';}
             szNsec[9]=0;
             break;
    case 2 :
    case 3 : strcpy(szNsec,"000000000");
             break;
  }

  /*--- Read the string (timezone part) ----------------------------*/
  if (iStatus==2) {
    k=0;
    while (k==0) {
      iTZoffs  = 0;
      if (pszTime[i]=='Z') {j=1; k=1; break;}                    /* Z    */
      j = (pszTime[i]=='+') ? 1 : -1; i++;                       /* [+-] */
      if (pszTime[i]<'0' || '9'<pszTime[i]) {break;}
      iTZoffs += (pszTime[i]-'0')*36000; i++;                    /* h    */
      if (pszTime[i]<'0' || '9'<pszTime[i]) {break;}
      iTZoffs += (pszTime[i]-'0')* 3600; i++;                    /* h    */
      if (pszTime[i]==':'                 ) {i++;  }             /* :    */
      if (pszTime[i]<'0' || '9'<pszTime[i]) {break;}
      iTZoffs += (pszTime[i]-'0')*  600; i++;                    /* m    */
      if (pszTime[i]<'0' || '9'<pszTime[i]) {break;}
      iTZoffs += (pszTime[i]-'0')*   60; i++;                    /* m    */
      if (pszTime[i]!=' ' && pszTime[i]!='\t' && pszTime[i]!=0) {break;}
      iTZoffs *= j;
      k=1;
    }
    if (k==0) {
      if (iStatus==0) {
        warning("%s: Invalid ISO 8601 string (timezone part)\n",
                pszTime                                         );
        return 0;
      }
    }
  } else          {
    j=0;
  }

  /*--- Pack the time-string into the timespec structure -----------*/
  memset(&tmDate, 0, sizeof(tmDate));
  if (! strptime(szDate, "%Y-%m-%dT%H:%M:%S", &tmDate)) {
    if (giVerbose>1) {
      warning("Unexpect error at strptime() #1 in parse_iso8601time()\n");
    }
    return 0;
  }
  ptsTime->tv_sec  = mktime(&tmDate);
  if (ptsTime->tv_sec == (time_t)-1) {
    /* see the comment on the same check in parse_calendartime()     */
    if (giVerbose>1) {warning("%s: Invalid ISO 8601 string\n", pszTime);}
    return 0;
  }
  ptsTime->tv_nsec = atol(szNsec);
  if (j!=0) {ptsTime->tv_sec = ptsTime->tv_sec + giTZoffs - iTZoffs;}

  return 1;
}


/*=== Read and write only one line having a timestamp ================
 * [in] fp      : Filehandle for read
 *      pszTime : Pointer for the string buffer to get the timestamp on
 *                the 1st field with a field separator
 *                (Size of the buffer you give MUST BE 43 BYTES or more!)
 * [ret] == 0 : Finished reading due to '\n'
 *       == 1 : Finished reading successfully, you may use the result in
 *              the buffer
 *       ==-1 : Finished reading because no more data in the "fp"
 *       ==-2 : Finished reading due to the end of file
 *       ==-3 : Finished reading due to a file reading error
 *       other: Finished reading due to a system error              */
int read_1st_field_as_a_timestamp(FILE *fp, char *pszTime) {

  /*--- Variables --------------------------------------------------*/
  int        iTslen = 0; /* length of the timestamp string          */
  int        iChar;

  /*--- Reading and writing a line ---------------------------------*/
  while (1) {
    iChar = getc(fp);
    switch (iChar) {
      case ' ' :
      case '\t':
                 pszTime[iTslen  ] = iChar;
                 pszTime[iTslen+1] = 0;
                 return 1;
      case EOF :
                 if         (feof(  fp)) {
                   if (iTslen==0) {
                     return -1;
                   } else         {
                     if (giVerbose>0) {
                       warning("EOF came while reading 1st field\n");
                     }
                     return -2;
                   }
                 } else  if (ferror(fp)) {
                   if (giVerbose>0) {
                     warning("error while reading 1st field\n");
                   }
                   return -3;
                 } else                  {
                   return -4;
                 }
      case '\n':
                 return 0;
      default  :
                 if (iTslen>41) {                                 continue;}
                 else           {pszTime[iTslen]=iChar; iTslen++; continue;}
    }
  }
}


/*=== Read and write only one line to the drain ======================
 * [in] fp      : Filehandle for read
 *      fpDrain : Filehandle for drain
 * [ret] == 1   : Finished reading/writing due to '\n', which is the last
 *                char of the file
 *       ==-1   : Finished reading due to the end of file
 *       ==-2   : Finished reading due to a file reading error
 *       ==-3   : Finished reading due to a system error            */
int read_and_drain_a_line(FILE *fp, FILE *fpDrain) {

  /*--- Variables --------------------------------------------------*/
  int        iChar;

  /*--- Reading and writing a line ---------------------------------*/
  while (1) {
    iChar = getc(fp);
    switch (iChar) {
      case EOF :
                 if (feof(  fp)) {return -1;}
                 if (ferror(fp)) {return -2;}
                 else            {return -3;}
      case '\n':
                 if (putc('\n', fpDrain)==EOF) {
                   error_exit(errno,"write error #1: %s\n",
                              strerror(errno));
                 }
                 return 1;
      default  :
                 if (putc(iChar, fpDrain)==EOF) {
                   error_exit(errno,"write error #2: %s\n",
                              strerror(errno));
                 }
                 break;
    }
  }
  return -3;
}


/*=== Read and write only one line ===================================
 * [in] fp    : Filehandle for read
 * [ret] == 1 : Finished reading/writing due to '\n', which is the last
 *              char of the file
 *       ==-1 : Finished reading due to the end of file
 *       ==-2 : Finished reading due to a file reading error
 *       ==-3 : Finished reading due to a system error              */
int read_and_write_a_line(FILE *fp) {

  /*--- Variables --------------------------------------------------*/
  int        iChar;

  /*--- Reading and writing a line ---------------------------------*/
  while (1) {
    iChar = getc(fp);
    switch (iChar) {
      case EOF :
                 if (feof(  fp)) {return -1;}
                 if (ferror(fp)) {return -2;}
                 else            {return -3;}
      case '\n':
                 if (putchar('\n' )==EOF) {
                   error_exit(errno,"stdout write error #1: %s\n",
                              strerror(errno));
                 }
                 return 1;
      default  :
                 if (putchar(iChar)==EOF) {
                   error_exit(errno,"stdout write error #2: %s\n",
                              strerror(errno));
                 }
                 break;
    }
  }
  return -3;
}


/*=== Read and throw away one line ===================================
 * [in] fp    : Filehandle for read
 * [ret] == 1 : Finished reading/writing due to '\n', which is the last
 *              char of the file
 *       ==-1 : Finished reading due to the end of file
 *       ==-2 : Finished reading due to a file reading error
 *       ==-3 : Finished reading due to a system error              */
int skip_over_a_line(FILE *fp) {

  /*--- Variables --------------------------------------------------*/
  int        iChar;

  /*--- Reading and writing a line ---------------------------------*/
  while (1) {
    iChar = getc(fp);
    switch (iChar) {
      case EOF :
                 if (feof(  fp)) {return -1;}
                 if (ferror(fp)) {return -2;}
                 else            {return -3;}
      case '\n':
                 return 1;
      default  :
                 break;
    }
  }
}


/*=== Parse a ratelimit string ========================================
 * [in] pszRule     : The string to be parsed as a "ratelimit"
 *      pi8Duration : The pointer to get the parsed duration part
 *      piMaxlines  : The pointer to get the parsed number-of-lines part
 * [ret] ==0        : Succeed in parsing the parameters
 *       !=0        : Invalid rule string
 * [note] the values of {pi8Duration,piMaxlines} will be overwritten
 *        whether the parsing succeeds or not.                      */
int parse_ratelimit(char* pszRule, int64_t* pi8Duration, int* piMaxlines) {

  /*--- Variables --------------------------------------------------*/
  char* psz;

  /*--- Parse --------------------------------------------------------*/
  if ((psz = strchr(pszRule,'/')) != NULL){
    if (sscanf(pszRule,"%d",piMaxlines) != 1) {return 1;}
    psz++;
  }else{
    *piMaxlines = 1;
    psz         = pszRule;
  }
  *pi8Duration = parse_duration(psz);
  if (*piMaxlines<1 || RINGBUF_NUM_MAX<*piMaxlines) {return 1;}
  if (*pi8Duration <= -2                          ) {return 1;}

  /*--- Finish successfully ------------------------------------------*/
  return 0;
}


/*=== Parse the duration =============================================
 * [in] pszDuration : The string to be parsed as a duration
 * [ret] >= 0  : Interval value (in nanosecound)
 *       <=-1  : Means infinity (completely shut the valve)
 *       <=-2  : It is not a value                                  */
int64_t parse_duration(char *pszDuration) {

  /*--- Variables --------------------------------------------------*/
  char   szUnit[CTRL_FILE_BUF];
  double dNum;

  /*--- Check the lengths of the argument --------------------------*/
  if (strlen(pszDuration)>=CTRL_FILE_BUF) {return -2;}

  /*--- Try to interpret the argument as "<value>"[+"unit"] --------*/
  switch (sscanf(pszDuration, "%lf%s", &dNum, szUnit)) {
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


/*=== Allocate memory for a ring buffer ==============================
 * [in]  iSize : The size of the ring buffer
 * [ret] Pointer for the ring buffer when it succeeded in memory
         allocation. If it failed, it returns NULL.                 */
tmsp* generate_ring_buf(int iSize) {
  
  /*--- Variables --------------------------------------------------*/
  tmsp* ptsRet;
  int   i;

  /*--- Allocate memory with malloc --------------------------------*/
  ptsRet = (tmsp*) malloc((sizeof(tmsp)) * iSize);
  if (ptsRet == NULL) { return NULL; }

  /*--- Initialize the ring buffer ---------------------------------*/
  for (i=0;i<iSize;i++) { ptsRet[i].tv_nsec=-1; }

  /*--- Return the pointer of the buffer ---------------------------*/
  return ptsRet;
}

/*=== Free memory of the ring buffer =================================
 * [in] pts : The pointer to be released                            */
void release_ring_buf(tmsp* pts) {
  if (pts==NULL) { return; }
  free(pts);
  return;
}


/*=== Erase items that are deemed stale ==============================
 * [in]  iBufsize : The size of the ring buffer
         ptsBuf   : The pointer of the ring buffer
         iLast    : The last written item number 
         tsRef    : The reference time to erase the stale items
 * [ret] the number of vacancies (Success)
         A negative number       (Failure)                          */
int erase_stale_items_in_the_ring_buffer(
          int iBufsize, tmsp* ptsBuf, int iLast, tmsp tsRef) {
  
  /*--- Variables --------------------------------------------------*/
  int i;
  int iNum;
  int iVacancies = 0;

  /*--- Validate the arguments -------------------------------------*/
  if (iBufsize <= 0              ) { return -1; }
  if (ptsBuf   == NULL           ) { return -1; }
  if (iLast<0  || iLast>=iBufsize) { return -1; }

  /*--- Erase items that are deemed stale --------------------------*/
  for (i=1;i<=iBufsize;i++) {
    iNum = (iLast+i) % iBufsize;
    if (ptsBuf[iNum].tv_nsec < 0) { iVacancies++; continue; }
    if (ptsBuf[iNum].tv_sec < tsRef.tv_sec) {
      ptsBuf[iNum].tv_nsec=-1; iVacancies++; continue;
    }
    if (ptsBuf[iNum].tv_sec  == tsRef.tv_sec &&
        ptsBuf[iNum].tv_nsec <= tsRef.tv_nsec  ) {
      ptsBuf[iNum].tv_nsec=-1; iVacancies++; continue;
    }
    break;
  }

  /*--- Return the number of vacant items --------------------------*/
  return iVacancies;
}


/*=== Apply the new ratelimit if the subthread has requested it ======
 * [in]  gstThCom.iRequested__main : 1 when the new parameter has come
 *       giMaxlines, gi8Duration  : The new parameters (already updated
 *                                  by the SIGHUP handler)
 * [in/out] pptsRingBuf     : Pointer of the ring buffer pointer
 *          piLastitemCode  : Pointer of the last memorized item code
 *          piMaxlines_prev : Pointer to detect the change of giMaxlines
 * [note] If giMaxlines has changed, the ring buffer will be recreated,
 *        which means all memorized items (passed lines' timestamps)
 *        will be discarded.                                        */
void apply_new_ratelimit(tmsp** pptsRingBuf, int* piLastitemCode,
                          int* piMaxlines_prev                    ) {

  /*--- Variables --------------------------------------------------*/
  int i;

  /*--- Return immediately unless the subthread has requested -----*/
  if (! gstThCom.iRequested__main) { return; }

  if (giVerbose>0) {
    warning("giMaxlines=%d, gi8Duration=%lld\n",
            giMaxlines, (long long)gi8Duration);
  }

  /*--- Recreate the ring buffer if the size has changed -----------*
   * NOTE: This MUST be done BEFORE acknowledging the subthread below.
   *       Otherwise, the subthread (unblocked by the acknowledgment)
   *       could overwrite "giMaxlines" with yet another value (by the
   *       SIGHUP handler) before this function finishes resizing the
   *       ring buffer for the current one, which would desynchronize
   *       "giMaxlines" and the actual size of the ring buffer.       */
  if (giMaxlines != *piMaxlines_prev) {
    if (giVerbose>0) {
      warning("RingBuffer will be recreated (size: %d -> %d)\n",
              *piMaxlines_prev, giMaxlines                      );
    }
    release_ring_buf(*pptsRingBuf);
    if ((*pptsRingBuf=generate_ring_buf(giMaxlines)) == NULL) {
      error_exit(errno, "apply_new_ratelimit(): %s\n", strerror(errno));
    }
    *piLastitemCode  = 0;
    *piMaxlines_prev = giMaxlines;
  }

  /*--- Notify the subthread that the main thread received it -----*/
  if ((i=pthread_mutex_lock(&gstThCom.mu)) != 0) {
    error_exit(i,"pthread_mutex_lock() in apply_new_ratelimit(): %s\n",
               strerror(i));
  }
  gstThCom.iReceived = 1;
  if ((i=pthread_cond_signal(&gstThCom.co)) != 0) {
    error_exit(i,"pthread_cond_signal() in apply_new_ratelimit(): %s\n",
               strerror(i));
  }
  if ((i=pthread_mutex_unlock(&gstThCom.mu)) != 0) {
    error_exit(i,"pthread_mutex_unlock() in apply_new_ratelimit(): %s\n",
               strerror(i));
  }
  gstThCom.iRequested__main = 0;

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
 * wake myself (the main thread) up even while sleeping.
 * [in]  gstThCom.i8Param1, gstThCom.iParam1
 *                          : The new parameters the sub-th gave
 * [out] gi8Duration, giMaxlines
 *                          : The new parameters the sub-th gave
 *       gstThCom.iRequested__main
 *                          : set to 1 to notify the main-th of the request */
void recv_param_application_req(int iSig, siginfo_t *siInfo, void *pct) {
  gi8Duration               = gstThCom.i8Param1;
  giMaxlines                = gstThCom.iParam1;
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
