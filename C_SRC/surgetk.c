/*####################################################################
#
# SURGETK - Surge Tank: Absorb a Burst of STDIN and Output It Smoothly
#
# USAGE   : surgetk [-s|-o|-d] [-v] size
#           surgetk -i
# Args    : size ........ Capacity (in bytes) of the ring buffer which
#                         absorbs the bytes that have arrived at the
#                         standard input but have not been sent to the
#                         standard output yet.
#                         The unit is byte defaultly. You can also
#                         specify the unit like '10MiB'. Available
#                         units are 'B', 'kB', 'KB', 'KiB', 'MB',
#                         'MiB', 'GB', 'GiB'. ("kB" means 1000 bytes;
#                         "KB" and "KiB" both mean 1024 bytes.)
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
#           -i .......... Investigation mode. Instead of doing the
#                         surge-tank operation, investigate the sizes
#                         of the various OS/libc buffers that a byte
#                         passes through around this command (see the
#                         manual for the details of each), print them
#                         to the standard output, and exit immediately.
#                         This overrides all the other options, and
#                         the "size" argument becomes unnecessary.
#                         The output format is one buffer per line,
#                         "name size(in bytes)" separated by a space,
#                         with the size column right-aligned.
#                         A buffer whose size this command failed to
#                         determine (e.g. one that is unsupported on
#                         the running OS) is simply omitted.
# Retuen  : Return 0 when finished successfully (the standard input
#           reached EOF and the buffer was completely flushed, or the
#           "-i" investigation finished).
#           Return 1 when this command was forced to disconnect by
#           the "-d" option.
#
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
 * standard level) is used there instead. Everywhere else (except
 * Linux, which needs _GNU_SOURCE for F_GETPIPE_SZ), we ask for
 * _XOPEN_SOURCE 700 rather than _POSIX_C_SOURCE 200809L alone: on
 * FreeBSD, _POSIX_C_SOURCE alone leaves __XSI_VISIBLE unset, hiding
 * XSI interfaces that some of these commands need.                  */
#if defined(__linux__)
  /* This definition is for F_GETPIPE_SZ, a Linux/glibc extension */
  #define _GNU_SOURCE
#elif defined(__sun) || defined(__SVR4)
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
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>
#include <locale.h>
#include <signal.h>
#ifdef __APPLE__
  #include <sys/types.h>  /* sys/sysctl.h needs this for u_int etc. */
  #include <sys/sysctl.h> /* for sysctlbyname("hw.memsize", ...) */
#endif

/*--- macro constants ----------------------------------------------*/
#define OPT_PARM_BUF  64
/* Overflow-handling modes ("-s"/"-o"/"-d") */
#define OVERFLOW_STOP      0
#define OVERFLOW_OVERWRITE 1
#define OVERFLOW_DIE       2

/*--- data type definitions ----------------------------------------*/
typedef struct _ringbuf_t {
  unsigned char*  pucElem;     /* malloc'ed byte array (i8Capacity bytes).
                                   1 element == 1 byte, so no wrapper
                                   struct (unlike delay.c's ringelem_t)
                                   is needed here.                     */
  int64_t         i8Capacity;  /* number of elements == "size" in bytes */
  int64_t         i8Head;      /* index of the oldest unsent byte       */
  int64_t         i8Count;     /* number of valid bytes stored now      */
  int             iEof;        /* 1: thread A reached EOF on stdin      */
  pthread_mutex_t mu;          /* protects everything above             */
  pthread_cond_t  coNotEmpty;  /* signaled on push/overwrite/EOF;
                                   thread B waits on this                */
  pthread_cond_t  coNotFull;   /* signaled on pop; thread A waits on
                                   this in "-s" (suspend) mode           */
} ringbuf_t;
typedef struct _bufinfo_t {
  const char* pszName;   /* the buffer's name (used in the "-i" output) */
  long        lValue;    /* its size in bytes; meaningful only if iValid */
  int         iValid;    /* 1: lValue was determined  0: could not be   */
} bufinfo_t;

/*--- prototype functions ------------------------------------------*/
int64_t parse_buffersize(char* pszArg);
int     create_ring_buf(ringbuf_t* pstRing, int64_t i8Capacity);
void    destroy_ring_buf(ringbuf_t* pstRing);
void*   thread_reader(void* pvArgs);
void*   thread_writer(void* pvArgs);
void    unlock_mutex(void* pvMu);
void    investigate_and_exit(void);
#ifdef __ANDROID__
  void  term_this_thread(int iSig, siginfo_t *siInfo, void *pct);
#endif

/*--- global variables ---------------------------------------------*/
char*       gpszCmdname;     /* The name of this command                     */
int         giVerbose;       /* speaks more verbosely by the greater number  */
int         giOverflow;      /* OVERFLOW_STOP/OVERWRITE/DIE ("-s"/"-o"/"-d") */
int64_t     gi8Buffersize;   /* buffer size in bytes == gstRing.i8Capacity   */
ringbuf_t   gstRing = {0};   /* Ring buffer shared by threads A and B        */
pthread_t   gtThreadA_id;    /* the reader thread's ID                       */
pthread_t   gtThreadB_id;    /* the writer thread's ID                       */
int         giRingMu_isready;  /* Set 1 when gstRing.mu has been initialized */
int         giRingCoNE_isready;/* Set 1 when gstRing.coNotEmpty is ready     */
int         giRingCoNF_isready;/* Set 1 when gstRing.coNotFull is ready      */
int         giAborted;       /* 1: "-d" forced a disconnect (set only by
                                 thread A; main() reads it only after both
                                 threads have been joined, so the join's
                                 happens-before relation makes the plain
                                 "int" visible without extra synchronization) */

/*=== Define the functions for printing usage and error ============*/

/*--- exit with usage ----------------------------------------------*/
void print_usage_and_exit(void) {
  fprintf(stderr,
    "USAGE   : %s [-s|-o|-d] [-v] size\n"
    "          %s -i\n"
    "Args    : size ........ Capacity (in bytes) of the ring buffer which\n"
    "                        absorbs the bytes that have arrived at the\n"
    "                        standard input but have not been sent to the\n"
    "                        standard output yet.\n"
    "                        The unit is byte defaultly. You can also\n"
    "                        specify the unit like '10MiB'. Available\n"
    "                        units are 'B', 'kB', 'KB', 'KiB', 'MB',\n"
    "                        'MiB', 'GB', 'GiB'. (\"kB\" means 1000 bytes;\n"
    "                        \"KB\" and \"KiB\" both mean 1024 bytes.)\n"
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
    "          -i .......... Investigation mode. Instead of doing the\n"
    "                        surge-tank operation, investigate the sizes\n"
    "                        of the various OS/libc buffers that a byte\n"
    "                        passes through around this command (see the\n"
    "                        manual for the details of each), print them\n"
    "                        to the standard output, and exit immediately.\n"
    "                        This overrides all the other options, and\n"
    "                        the \"size\" argument becomes unnecessary.\n"
    "                        The output format is one buffer per line,\n"
    "                        \"name size(in bytes)\" separated by a space,\n"
    "                        with the size column right-aligned.\n"
    "                        A buffer whose size this command failed to\n"
    "                        determine (e.g. one that is unsupported on\n"
    "                        the running OS) is simply omitted.\n"
    "Retuen  : Return 0 when finished successfully (the standard input\n"
    "          reached EOF and the buffer was completely flushed, or the\n"
    "          \"-i\" investigation finished).\n"
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
int      i;               /* all-purpose int                        */
int      iInvestigate;    /* -i option (0:normal 1:investigate&exit) */
#ifdef __ANDROID__
struct sigaction sa;      /* for signal handler definition (action) */
#endif

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
giVerbose      = 0;
giOverflow     = OVERFLOW_STOP;
iInvestigate   = 0;

/*--- Parse options which start with "-" ---------------------------*/
while ((i=getopt(argc, argv, "sodvih")) != -1) {
  switch (i) {
    case 's': giOverflow   = OVERFLOW_STOP;      break;
    case 'o': giOverflow   = OVERFLOW_OVERWRITE; break;
    case 'd': giOverflow   = OVERFLOW_DIE;       break;
    case 'v': giVerbose++;                       break;
    case 'i': iInvestigate = 1;                  break;
    case 'h': print_usage_and_exit();
    default : print_usage_and_exit();
  }
}
argc -= optind;
argv += optind;

/*--- "-i": investigate the buffer sizes and exit immediately ------*/
if (iInvestigate) {investigate_and_exit();}

if (giVerbose>0) {warning("verbose mode (level %d)\n",giVerbose);}
if (argc != 1) {print_usage_and_exit();}

/*--- Parse the size argument ---------------------------------------*/
gi8Buffersize = parse_buffersize(argv[0]);
if (gi8Buffersize <= 0) {
  error_exit(1,"%s: Invalid size\n",argv[0]);
}

/*--- Create the ring buffer ----------------------------------------*/
i = create_ring_buf(&gstRing, gi8Buffersize);
if (i) {error_exit(i,"create_ring_buf() in main(): %s\n",strerror(i));}
giRingMu_isready   = 1;
giRingCoNE_isready = 1;
giRingCoNF_isready = 1;

/*=== Switch buffer mode and start the worker threads ==============*/
if (setvbuf(stdout,NULL,_IONBF,0)!=0) {
  error_exit(255,"Failed to switch to unbuffered mode\n");
}
if (feof(stdin)) {clearerr(stdin);} /* Reset EOF condition when stdin */

#ifdef __ANDROID__
/*=== Set the signal handler to terminate a thread (Android has no  *
 *     pthread_cancel(), so thread_reader() asks thread_writer() to *
 *     exit via SIGTERM instead when it needs to abort it)          */
memset(&sa, 0, sizeof(sa));
sigemptyset(&sa.sa_mask);
sigaddset(&sa.sa_mask, SIGTERM);
sa.sa_sigaction = term_this_thread;
sa.sa_flags     = SA_SIGINFO | SA_RESTART;
if (sigaction(SIGTERM,&sa,NULL) != 0) {
  error_exit(errno,"sigaction() in main(): %s\n",strerror(errno));
}
#endif

i = pthread_create(&gtThreadB_id, NULL, thread_writer, NULL);
if (i) {error_exit(i,"pthread_create() thread_writer: %s\n",strerror(i));}
i = pthread_create(&gtThreadA_id, NULL, thread_reader, NULL);
if (i) {error_exit(i,"pthread_create() thread_reader: %s\n",strerror(i));}

/*=== Wait for both threads to finish (normally or via "-d" abort) =*/
i = pthread_join(gtThreadA_id, NULL);
if (i) {error_exit(i,"pthread_join() thread_reader: %s\n",strerror(i));}
i = pthread_join(gtThreadB_id, NULL);
if (i) {error_exit(i,"pthread_join() thread_writer: %s\n",strerror(i));}

/*=== Finish =========================================================*/
if (giRingMu_isready)   {pthread_mutex_destroy(&gstRing.mu        );}
if (giRingCoNE_isready) {pthread_cond_destroy( &gstRing.coNotEmpty);}
if (giRingCoNF_isready) {pthread_cond_destroy( &gstRing.coNotFull );}
destroy_ring_buf(&gstRing);

return giAborted ? 1 : 0;}



/*####################################################################
# Thread A (Reader)
####################################################################*/

/*=== Read bytes from stdin and push them into the ring buffer =====*/
void* thread_reader(void* pvArgs) {

  /*--- Variables --------------------------------------------------*/
  int     iChar;
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
        gstRing.pucElem[i8Idx] = (unsigned char)iChar;
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
        giAborted = 1;
        #ifndef __ANDROID__
        if (pthread_cancel(gtThreadB_id) != 0) {
          error_exit(errno,"pthread_cancel() in thread_reader(): %s\n",strerror(errno));
        }
        #else
        if (pthread_kill(gtThreadB_id, SIGTERM) != 0) {
          error_exit(errno,"pthread_kill() in thread_reader(): %s\n",strerror(errno));
        }
        #endif
        return NULL;
      }
    }

    /*--- Normal push ------------------------------------------------*/
    i8Idx = (gstRing.i8Head + gstRing.i8Count) % gstRing.i8Capacity;
    gstRing.pucElem[i8Idx] = (unsigned char)iChar;
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

/*=== Write out every byte in the ring buffer as soon as available =*/
void* thread_writer(void* pvArgs) {

  /*--- Variables --------------------------------------------------*/
  int64_t i8Idx;
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
    while (putchar(gstRing.pucElem[i8Idx]) == EOF) {
      error_exit(errno,"putchar() in thread_writer(): %s\n",strerror(errno));
    }
    gstRing.i8Head = (i8Idx+1) % gstRing.i8Capacity;
    gstRing.i8Count--;
    if ((i=pthread_cond_signal(&gstRing.coNotFull)) != 0) {
      error_exit(i,"pthread_cond_signal() in thread_writer(): %s\n",strerror(i));
    }
  }

  pthread_cleanup_pop(1); /* unlocks gstRing.mu */

  return NULL;
}



/*####################################################################
# Functions
####################################################################*/

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

  /*--- Reject values >= 80% of the machine's total physical memory -*/
  /* Unlike delay.c, this ring buffer is a plain byte array (1 element
   * == 1 byte of real memory), so "dBytes" itself IS the actual
   * allocation size; no per-element overhead correction is needed.  */
#ifdef __APPLE__
  {
    uint64_t u8MemSize;
    size_t   szLen = sizeof(u8MemSize);
    /* macOS has no _SC_PHYS_PAGES; hw.memsize gives the same info
       (total physical RAM, in bytes) more directly.                */
    if (sysctlbyname("hw.memsize", &u8MemSize, &szLen, NULL, 0) == 0) {
      dMaxBytes = (double)u8MemSize * 0.80;
      if (dBytes >= dMaxBytes) {return -2;}
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
    if (dBytes >= dMaxBytes) {return -2;}
  }
#endif

  /*--- Floor to an integer number of bytes; 0 bytes is an error ----*/
  i8Bytes = (int64_t)dBytes;
  if (i8Bytes <= 0) {return -2;}

  return i8Bytes;
}

/*=== Allocate the ring buffer =========================================
 * [in]  i8Capacity : the number of bytes to allocate
 * [ret] ==0 : success   !=0 : failure (the value means "errno")     */
int create_ring_buf(ringbuf_t *pstRing, int64_t i8Capacity) {

  /*--- Variables --------------------------------------------------*/
  int i;

  pstRing->pucElem = (unsigned char*)malloc((size_t)i8Capacity);
  if (pstRing->pucElem == NULL) {return errno;}
  pstRing->i8Capacity = i8Capacity;
  pstRing->i8Head      = 0;
  pstRing->i8Count      = 0;
  pstRing->iEof         = 0;

  if ((i=pthread_mutex_init(&pstRing->mu, NULL))          != 0) {return i;}
  if ((i=pthread_cond_init(&pstRing->coNotEmpty, NULL))   != 0) {return i;}
  if ((i=pthread_cond_init(&pstRing->coNotFull, NULL))    != 0) {return i;}
  return 0;
}

/*=== Free the memory of the ring buffer (mutex/cond are destroyed  *
 *     separately by main(), which tracks their init status         *
 *     independently)                                                */
void destroy_ring_buf(ringbuf_t *pstRing) {
  if (pstRing->pucElem != NULL) {free(pstRing->pucElem); pstRing->pucElem=NULL;}
  return;
}

/*=== CLEANUPHANDLER : Unlock a mutex ================================
 * Generic helper for pthread_cleanup_push()/pthread_cleanup_pop().
 * [in] pvMu : the pthread_mutex_t* to unlock                        */
void unlock_mutex(void *pvMu) {
  pthread_mutex_unlock((pthread_mutex_t*)pvMu);
  return;
}

#ifdef __ANDROID__
/*=== SIGNALHANDLER : Terminate this thread ==========================
 * This function just terminates itself.                            */
void term_this_thread(int iSig, siginfo_t *siInfo, void *pct) {pthread_exit(0);}
#endif

/*=== Investigate the OS/libc buffer sizes and exit ("-i" option) ====
 * Creates a throwaway pipe of its own (not stdin/stdout, which may
 * not even be pipes) and queries its properties, matching exactly
 * what the manual's example commands (F_GETPIPE_SZ, st_blksize,
 * PIPE_BUF) do. A buffer this command fails to determine the size of
 * (typically because the running OS doesn't support that query) is
 * simply omitted from the output; that is not treated as an error.
 * The values are collected first and printed afterward as a table,
 * with the 2nd column (the size) right-aligned.                    */
void investigate_and_exit(void) {

  /*--- Variables --------------------------------------------------*/
  int         aiPipe[2];
  struct stat stStat;
  long        lVal;
  int         iFd_pipemax;
  char        szBuf[32];
  ssize_t     iLen;
  bufinfo_t   astBuf[4];
  int         iNameWidth, iValueWidth, iWidth;
  int         i;
#if defined(F_GETPIPE_SZ)
  int         iPipesize;
#endif

  astBuf[0].pszName = "pipe_default_size"; astBuf[0].iValid = 0;
  astBuf[1].pszName = "pipe_max_size"    ; astBuf[1].iValid = 0;
  astBuf[2].pszName = "stdio_buf_size"   ; astBuf[2].iValid = 0;
  astBuf[3].pszName = "pipe_buf"         ; astBuf[3].iValid = 0;

  if (pipe(aiPipe) != 0) {
    error_exit(errno,"pipe() in investigate_and_exit(): %s\n",strerror(errno));
  }

  /*--- pipe_default_size: the capacity a freshly created pipe has  *
   *     by default (Linux/glibc extension; F_GETPIPE_SZ)            */
#if defined(F_GETPIPE_SZ)
  iPipesize = fcntl(aiPipe[1], F_GETPIPE_SZ);
  if (iPipesize >= 0) {astBuf[0].lValue = (long)iPipesize; astBuf[0].iValid = 1;}
#endif

  /*--- pipe_max_size: the ceiling a pipe's capacity can be grown to *
   *     (Linux only; read from procfs, skipped if unavailable)      */
  iFd_pipemax = open("/proc/sys/fs/pipe-max-size",O_RDONLY);
  if (iFd_pipemax >= 0) {
    iLen = read(iFd_pipemax,szBuf,sizeof(szBuf)-1);
    if (close(iFd_pipemax) != 0) {
      error_exit(errno,"close() in investigate_and_exit(): %s\n",strerror(errno));
    }
    if (iLen > 0) {
      szBuf[iLen] = '\0';
      astBuf[1].lValue = atol(szBuf); astBuf[1].iValid = 1;
    }
  }

  /*--- stdio_buf_size: the size stdio would pick as the buffer for *
   *     a pipe (based on the "st_blksize" field of fstat())         */
  if (fstat(aiPipe[0],&stStat) == 0) {
    astBuf[2].lValue = (long)stStat.st_blksize; astBuf[2].iValid = 1;
  }

  /*--- pipe_buf: PIPE_BUF, the size up to which a write() to a pipe *
   *     is guaranteed to be atomic (POSIX-standardized, portable)   */
  errno = 0;
  lVal = fpathconf(aiPipe[0],_PC_PIPE_BUF);
  if (lVal >= 0) {astBuf[3].lValue = lVal; astBuf[3].iValid = 1;}

  if (close(aiPipe[0]) != 0) {
    error_exit(errno,"close() in investigate_and_exit(): %s\n",strerror(errno));
  }
  if (close(aiPipe[1]) != 0) {
    error_exit(errno,"close() in investigate_and_exit(): %s\n",strerror(errno));
  }

  /*--- Print the collected values as a table, with the 2nd column  *
   *    (the size) right-aligned; the two column widths are decided *
   *    from the actually-valid entries only.                        */
  iNameWidth  = 0;
  iValueWidth = 0;
  for (i=0; i<4; i++) {
    if (! astBuf[i].iValid) {continue;}
    iWidth = (int)strlen(astBuf[i].pszName);
    if (iWidth > iNameWidth) {iNameWidth = iWidth;}
    iWidth = snprintf(szBuf,sizeof(szBuf),"%ld",astBuf[i].lValue);
    if (iWidth > iValueWidth) {iValueWidth = iWidth;}
  }
  for (i=0; i<4; i++) {
    if (! astBuf[i].iValid) {continue;}
    if (printf("%-*s %*ld\n",iNameWidth,astBuf[i].pszName,
                              iValueWidth,astBuf[i].lValue) < 0) {
      error_exit(errno,"printf() in investigate_and_exit(): %s\n",strerror(errno));
    }
  }

  exit(0);
}
