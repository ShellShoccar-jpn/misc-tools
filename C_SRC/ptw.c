/*####################################################################
#
# PTW - Pseudo Terminal Wrapper
#
# USAGE   : ptw [-f] command [argument ...]
# Options : -f ... Forcibly wrap the command in a PTY even though the
#                  command is placed at the end of the pipeline.
#                  Originally, it isn't necessary to use a PTY because
#                  the command placed at the end has a TTY-connected
#                  STDOUT.
# Retuen  : The return value will be decided by the wrapped command
#           when PTY wrapping has succeed. However, return a non-zero
#           number by this wrapper when failed.
#
# How to compile : cc -O3 -o __CMDNAME__ __SRCNAME__
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
/*--- macro constants ----------------------------------------------*/
#define BUFSIZE 8192
/*#define RAWMODE_FOR_MASTER*//*set raw mode for master (probably unnecessary)*/
/*--- headers ------------------------------------------------------*/
#ifdef __linux__
  #define _XOPEN_SOURCE 600
#endif
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>
#if (defined(__unix__) || defined(unix)) && !defined(USG)
  #include <sys/param.h> /* get OS identification macro */
#endif
#if !defined(BSD) && !defined(__APPLE__) && !defined(__linux__)
  #include <stropts.h>   /* include file for ioctl() on POSIX-compliant OS */
#else
  #include <sys/ioctl.h> /* include file for ioctl() on *BSD and Linux */
#endif
#if !defined(TABDLY) && defined(OXTABS)
  #define TABDLY OXTABS /* for classiic BSD */
#endif
/*--- prototype functions ------------------------------------------*/
#ifdef RAWMODE_FOR_MASTER
  void restore_master_termios(void);
#endif
/*--- global variables ---------------------------------------------*/
char*    gpszCmdname;     /* The name of this command                        */
int      giVerbose;       /* speaks more verbosely by the greater number     */
int      giForciblepty;   /* set 1 or more to use PTY forcibly               */
int      giFd1m, giFd1s;  /* PTY file descriptors                            */
struct termios gstTermm;  /* stdin terimios for master                       */

/*=== Define the functions for printing usage and error ============*/
/*--- exit with usage ----------------------------------------------*/
void print_usage_and_exit(void) {
  fprintf(stderr,
    "USAGE   : %s [-f] command [argument ...]\n"
    "Options : -f ... Forcibly wrap the command in a PTY even though the\n"
    "                 command is placed at the end of the pipeline.\n"
    "                 Originally, it isn't necessary to use a PTY because\n"
    "                 the command placed at the end has a TTY-connected\n"
    "                 STDOUT.\n"
    "Retuen  : The return value will be decided by the wrapped command\n"
    "          when PTY wrapping has succeed. However, return a non-zero\n"
    "          number by this wrapper when failed.\n"
    "Version : 2020-03-19 12:18:14 JST\n"
    "          (POSIX C language with \"POSIX centric\" programming)\n"
    "\n"
    "Shell-Shoccar Japan (@shellshoccarjpn), No rights reserved.\n"
    "This is public domain software. (CC0)\n"
    "\n"
    "The latest version is distributed at the following page.\n"
    "https://github.com/ShellShoccar-jpn/misc-tools\n"
    ,gpszCmdname);
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
  if (giFd1m >= 0) {close(giFd1m);}
  if (giFd1s >= 0) {close(giFd1s);}
  exit(iErrno);
}


/*####################################################################
# Main
####################################################################*/

/*=== Initialization ===============================================*/
int main(int argc, char *argv[]) {

/*--- Variables ----------------------------------------------------*/
int    iRet;              /* exit status for me                     */
int    iStdinIsATTY;      /* 1 if stdin is a TTY                    */
struct termios stTerms;   /* PTY slave terimios                     */
struct winsize stWsizem;  /* stdin window size for master           */
pid_t    pidMS;           /* PID (master or slave)                  */
char   szTran[BUFSIZE];   /* for master-slave transceiver           */
int    i, j, k, l;        /* all-purpose int                        */
char*  psz;               /* all-purpose char*                      */
#ifdef __OpenBSD__
  struct sigaction saIgnr;  /* for ignoring SIGHUP during preparation */
  struct sigaction saOrig;  /* for backing up signal action           */
#endif
/*--- Initialize ---------------------------------------------------*/
gpszCmdname = argv[0];
for (i=0; *(gpszCmdname+i)!='\0'; i++) {
  if (*(gpszCmdname+i)=='/') {gpszCmdname=gpszCmdname+i+1;}
}
giFd1m=-1;
giFd1s=-1;
/*=== Parse arguments ==============================================*/
#if !defined(__linux__)
while ((i=getopt(argc, argv,  "fvh")) != -1) {
#else
/* To make Linux complieant POSIX, "+" is required at the head of
   optstring on getopt() for only Linux                            */
while ((i=getopt(argc, argv, "+fvh")) != -1) {
#endif
  switch (i) {
    case 'f': giForciblepty=1; break;
    case 'v': giVerbose++;     break;
    case 'h': print_usage_and_exit();
    default : print_usage_and_exit();
  }
}
argc -= optind-1;
argv += optind  ;
if (argc<2) {print_usage_and_exit();}
if (giVerbose>0) {warning("verbose mode (level %d)\n",giVerbose);}

/*=== Just exec() immediately if connected a tty ===================*/
if (isatty(STDOUT_FILENO) == 1) {
  if (giVerbose > 0) {warning("STDOUT is already connected to a TTY.\n", psz);}
  if (!giForciblepty) {
    if (giVerbose > 0) {warning("So, I'll do exec() without PTY.\n", psz);}
    execvp(argv[0],&argv[0]);
    error_exit(errno,"%s: %s\n", argv[0], strerror(errno));
  }
  if (giVerbose > 0) {
    warning("However, I'll wrap it in a PTY because of \"-f\" option.\n", psz);
  }
}

/*=== Save parameters "termios"/"winsize" of STDIN =================*/
iStdinIsATTY = isatty(STDIN_FILENO);
if (iStdinIsATTY == 1) {
  if (tcgetattr(STDIN_FILENO, &gstTermm)                   < 0) {
    error_exit(errno,"tcgetattr() on master (saving): %s\n", strerror(errno));
  }
  if (ioctl(STDIN_FILENO, TIOCGWINSZ, (char *) &stWsizem)  < 0) {
    error_exit(errno,"ioctl(TIOCGWINSZ): %s\n"             , strerror(errno));
  }
}

/*=== Make a PTY pair exectute the wrapping program ================*/
/*--- Make PTY master (parent) -------------------------------------*/
if ((giFd1m=posix_openpt(O_RDWR))<0) {
  error_exit(errno,"posix_openpt(): %s\n", strerror(errno));
}
if (grantpt (giFd1m)             <0) {
  error_exit(errno,"grantpt(): %s\n"     , strerror(errno));
}
if (unlockpt(giFd1m)             <0) {
  error_exit(errno,"unlockpt(): %s\n"    , strerror(errno));
}
/*--- Make PTY slave and exectute the wrapping program (child) -----*/
if ((pidMS=fork()) < 0) {error_exit(errno,"fork() #1: %s\n", strerror(errno));}
if (pidMS == 0) {
  if (setsid() <0) {error_exit(errno,"setsid(): %s\n",strerror(errno));}
                       /* to be independent of the parent's session */
  if ((psz=ptsname(giFd1m)) == NULL) {error_exit(255,"Failed to ptsname()\n");}
  if (giVerbose > 0) {warning("PTY slave is \"%s\"\n", psz);}
  if ((giFd1s=open(psz,O_RDWR)) < 0) {
    error_exit(errno,"open(%s): %s\n", psz, strerror(errno));
  }
  #if !defined(BSD) && !defined(__APPLE__) && !defined(__linux__)
    /* On traditional System V OSs whose TTYs are impremented by STREAMS,
       it is necessary to set up stream if not enabled by autopush facility */
    if ((i=ioctl(giFd1s,I_FIND,"ldterm")) < 0) {
      error_exit(  255,"ioctl(I_FIND,\"ldterm\") error\n"  );
    }
    if (i == 0) {
      if (ioctl(giFd1s,I_PUSH,"ptem"    ) < 0) {
        error_exit(255,"ioctl(I_PUSH,\"perm\") error\n"    );
      }
      if (ioctl(giFd1s,I_PUSH,"ldterm"  ) < 0) {
        error_exit(255,"ioctl(I_PUSH,\"ldterm\") error\n"  );
      }
      #if !defined(__hpux) && !defined(_HPUX_SOURCE)
        /* According to ldterm(7) on HP-UX man, "ttcompat" is unnecessary
           for HP-UX system and it isn't actually provided to the system  */
        if (ioctl(giFd1s,I_PUSH,"ttcompat") < 0) {
          error_exit(255,"ioctl(I_PUSH,\"ttcompat\") error\n");
        }
      #endif
    }
  #endif
  #if defined(BSD) || defined(__APPLE__)
    /* On BSD family (also macOS), it's necessary to use TIOCSCTTY to
       assign controlling terminal.                                   */
    if (ioctl(giFd1s,TIOCSCTTY,(char*)0) < 0) {
      error_exit(255,"ioctl(TIOCSCTTY) error\n");
    }
  #endif
  #ifndef __OpenBSD__
    close(giFd1m); giFd1m=-1;
  #else
    /* On OpenBSD (at least <=6.5), it seems that it has some strange
       behaviors on PTY.
         - SIGHUP will be sent when the parent closes PTY master.
       Thus I give the following codes only to OpenBSD.               */
    memset(&saIgnr, 0, sizeof(saIgnr));
    saIgnr.sa_handler   = SIG_IGN;
    if (sigaction(SIGHUP,&saIgnr,&saOrig) != 0) {
      error_exit(errno,"sigaction() #1: %s\n",strerror(errno));
    }
    close(giFd1m); giFd1m=-1;
    if (sigaction(SIGHUP,&saOrig,NULL) != 0) {
      error_exit(errno,"sigaction() #2c: %s\n",strerror(errno));
    }
  #endif
  /* Restore the saved STDIN parameters */
  if (iStdinIsATTY == 1) {
    if (tcsetattr(giFd1s, TCSANOW, &gstTermm)    < 0) {
      error_exit(errno,"tcsetattr() on slave #1: %s\n", strerror(errno));
    }
    if (ioctl(giFd1s, TIOCSWINSZ, &stWsizem)     < 0) {
      error_exit(errno,"ioctl(TIOCSWINSZ): %s\n"      , strerror(errno));
    }
  }
  /* Assign PTY slave instead of std{in,out,err} for the slave side process */
  if (dup2(giFd1s, STDOUT_FILENO) != STDOUT_FILENO) {
    error_exit(errno,"dup2(slv,stdout): %s\n"       , strerror(errno));
  }
  if (giFd1s!=STDOUT_FILENO) {close(giFd1s);}
  /* Turn off echo for the PTY slave */
  if (isatty(STDOUT_FILENO) == 1) {
    if (tcgetattr(STDOUT_FILENO, &stTerms) < 0) {
      error_exit(errno,"tcgetattr() on slave: %s\n"   , strerror(errno));
    }
    stTerms.c_lflag &= ~( ECHO  | ECHOE  | ECHOK | ECHONL );
    stTerms.c_oflag &= ~( ONLCR | TABDLY                  );
    if (tcsetattr(STDOUT_FILENO, TCSANOW, &stTerms)     < 0) {
      error_exit(errno,"tcsetattr() on slave #2: %s\n", strerror(errno));
    }
  }
  /* Finally, exec the program which will be wrapped */
  execvp(argv[0],&argv[0]);
  error_exit(errno,"%s: %s\n", argv[0], strerror(errno));
}

/*=== Turn into raw mode for master STDIN ==========================*/
#ifdef RAWMODE_FOR_MASTER
if (isatty(STDOUT_FILENO) == 1) {
  /*--- Set flags and parameters for raw mode ----------------------*/
  gstTermm.c_iflag &= ~( BRKINT | ICRNL  | INLCR  | IGNCR  |
                         INPCK  | ISTRIP | IXON   | PARMRK );
  gstTermm.c_iflag |=  ( IGNBRK                            );
  gstTermm.c_oflag &= ~( OPOST                             );
  gstTermm.c_lflag &= ~( ECHO   | ICANON | IEXTEN | ISIG   );
  gstTermm.c_cflag &= ~( PARENB | CSIZE                    );
  gstTermm.c_cflag |=  ( CS8                               );
  gstTermm.c_cc[VMIN ] = 1;
  gstTermm.c_cc[VTIME] = 0;
  /*--- Turn into raw mode gracefully ------------------------------*/
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &gstTermm) < 0) {
    error_exit(errno,"tcsetattr() on master: %s\n", strerror(errno));
  }
  /*--- Set flags and parameters for raw mode ----------------------*/
  atexit(restore_master_termios);
  /*--- Make sure that all the flags and parameters have been set --*/
  if (tcgetattr(STDIN_FILENO, &gstTermm) < 0) {
    error_exit(errno,"tcgetattr() on master (verifying): %s\n",strerror(errno));
  }
  if ((gstTermm.c_iflag & ( BRKINT | ICRNL  | INLCR  | IGNCR  |
                            INPCK  | ISTRIP | IXON   | PARMRK |
                            IGNBRK                            ))!= IGNBRK ||
      (gstTermm.c_oflag & ( OPOST                             ))          ||
      (gstTermm.c_lflag & ( ECHO   | ICANON | IEXTEN | ISIG   ))          ||
      (gstTermm.c_cflag & ( CSIZE  | PARENB | CS8             ))!= CS8    ||
      (gstTermm.c_cc[VMIN]  != 1                               )          ||
      (gstTermm.c_cc[VTIME] != 0                               )           )
  {
    error_exit(255,"tcgetattr() on master (verifying): verify error\n");
  }
}
#endif

/*=== Transceive data from/to the PTY ==============================*/
/*--- Transceive ---------------------------------------------------*/
iRet = 0;
while (1) {
  j = (int)read(giFd1m, szTran, BUFSIZE);
  if (j <0) {
    if (errno != EIO) {
      error_exit(errno,"read() on mono RX: %s\n", strerror(errno));
    }
    /* EIO suggests, in this case, that the child is already dead and
       it's no problem. However, I will print a message just in case
       if -v option is given.                                         */
    if (giVerbose > 0) {warning("read() on mono RX: EIO occured\n");}
    j = 0;
  }
  if (j==0) {
    #ifndef __OpenBSD__
      break;
    #else
      /* On OpenBSD (at least <=6.5), it seems that it has some strange
         behaviors on PTY.
           - read() for PTY seems to work in non-blocking mode forcibly.
           - waitpid() can return 0 even though its children are still alive.
         Thus I give the following codes only to OpenBSD.                     */
      if (waitpid(-1,&j,WNOHANG) >= 0) {
        iRet = (WIFEXITED(j)  ) ? WEXITSTATUS(j)  :
               (WIFSIGNALED(j)) ? WTERMSIG(i)+127 :
                                  254             ;
        continue;
      }
      if (errno!=ECHILD) {error_exit(errno,"waitpid(): %s\n", strerror(errno));}
      if (giVerbose > 0) {warning("waitpid(): ECHILD occured\n");}
      close(giFd1m); giFd1m=-1;
      return iRet;
    #endif
  }
  k = j;
  while (k > 0) {
    l = (int)write(STDOUT_FILENO, szTran+j-k, k);
    if (l < 0) {error_exit(errno,"write() on mono RX: %s\n",strerror(errno));}
    k -= l;
  }
}
/*--- Close the PTY ------------------------------------------------*/
close(giFd1m); giFd1m=-1;

/*=== Wait for the child to exit ===================================*/
if (wait(&i) < 0) {error_exit(errno,"wait(): %s\n", strerror(errno));}
iRet = (WIFEXITED(i)  ) ? WEXITSTATUS(i)  :
       (WIFSIGNALED(i)) ? WTERMSIG(i)+127 :
                          254             ;

/*=== Finish with exit status the child returned basically =========*/
return iRet;}



/*####################################################################
# Functions
####################################################################*/

#ifdef RAWMODE_FOR_MASTER
  /*=== (Exit trap) Restore the termios parameters for master ========*/
  void restore_master_termios(void) {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &gstTermm) < 0) {
      fprintf(stderr,"%s: tcsetattr() on atexit()\n",gpszCmdname);
    }
  }
#endif
