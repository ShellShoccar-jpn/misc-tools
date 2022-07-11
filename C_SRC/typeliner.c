/*####################################################################
#
# TYPELINER - Make a Line of a Bunch of Key Types
#
# USAGE   : typeliner [options]
# Options : -1 ....... Get only one bunch and exit immediately.
#                      It is equivalent to the option "-n 1."
#           -d ....... Ignore [CTRL]+[D]. It means that the EOT (0x04)
#                      will be treated as an ordinal character.
#           -e ....... Enable echo. You can see the letters you typed.
#           -n num ... Get only <num> bunches and exit immediately.
#                      (num<0) means getting bunches infinitely.
#                      This option works only when STDIN is connected
#                      to a terminal.
#           -t str ... Replace the terminator after a bunch with <str>.
#                      Default is "\n."
# Retuen  : 0 only when finished successfully
#
# How to compile : cc -O3 -o __CMDNAME__ __SRCNAME__
#
# Written by Shell-Shoccar Japan (@shellshoccarjpn) on 2022-07-12
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
#define BLKSIZE 8192
#define TRMSIZE  128
/*--- headers ------------------------------------------------------*/
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
/*--- global variables ---------------------------------------------*/
char*            gpszCmdname;       /* The name of this command       */
struct termios   gstTerms1st = {0};
char             gszBuf[BLKSIZE+1];
struct sigaction gsaExit;           /* To resume terminal conf before exit */
int              giVerbose;         /* greater number, more verbosely */

/*=== Define the functions for printing usage and error ============*/
/*--- exit with usage ----------------------------------------------*/
void print_usage_and_exit(void) {
  fprintf(stderr,
    "USAGE   : %s [options]\n"
    "Options : -1 ....... Get only one bunch and exit immediately.\n"
    "                     It is equivalent to the option \"-n 1.\"\n"
    "          -d ....... Ignore [CTRL]+[D]. It means that the EOT (0x04)\n"
    "                     will be treated as an ordinal character.\n"
    "          -e ....... Enable echo. You can see the letters you typed.\n"
    "          -n num ... Get only <num> bunches and exit immediately.\n"
    "                     (num<0) means getting bunches infinitely.\n"
    "                     This option works only when STDIN is connected\n"
    "                     to a terminal.\n"
    "          -t str ... Replace the terminator after a bunch with <str>.\n"
    "                     Default is \"\n.\"\n"
    "Retuen  : 0 only when finished successfully\n"
    "Version : 2022-07-12 01:35:48 JST\n"
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
/*--- exit trap (for normal exit) ----------------------------------*/
void exit_trap(void) {
  if (tcsetattr(STDIN_FILENO, TCSANOW, &gstTerms1st) < 0) {
    error_exit(errno,"tcsetattr()#%d: %s\n", __LINE__, strerror(errno));
  }
  if (giVerbose>1) {warning("The terminal attributes recovered.\n");}
}
/*--- exit trap (interrupted) --------------------------------------*/
void interrupted_trap(int iSig, siginfo_t *siInfo, void *pct) {
  if (giVerbose>1) {warning("Interrupted by signal %d\n", iSig);}
  exit(128+iSig); /* This func calls exit_trap() before exit */
}


/*####################################################################
# Main
####################################################################*/

/*=== Initialization ===============================================*/
int main(int argc, char *argv[]) {
/*--- Variables ----------------------------------------------------*/
int            iIgnCtrlD, iNumofbunches, iSize_trm, iEchomode;
char           szTrm[TRMSIZE];
int            iSize_r, iSize_w, iOffset, iRemain, i;
struct termios stTerms;
/*--- Initialize ---------------------------------------------------*/
gpszCmdname = argv[0];
for (i=0; *(gpszCmdname+i)!='\0'; i++) {
  if (*(gpszCmdname+i)=='/') {gpszCmdname=gpszCmdname+i+1;}
}

/*=== Parse arguments ==============================================*/
/*--- Set default parameters of the arguments ----------------------*/
iIgnCtrlD     =  0;
iEchomode     =  0;
iNumofbunches = -1;
strcpy(szTrm,"\n");
iSize_trm     =  strlen(szTrm);
/*--- Parse options which start by "-" -----------------------------*/
while ((i=getopt(argc, argv, "1den:t:vh")) != -1) {
  switch (i) {
    case '1': iNumofbunches = 1;                 break;
    case 'd': iIgnCtrlD     = 1;                 break;
    case 'e': iEchomode     = 1;                 break;
    case 'n': if(sscanf(optarg,"%d",&iNumofbunches)!=1){print_usage_and_exit();}
                                                 break;
    case 't': i = strlen(optarg);
              if (i>=TRMSIZE) {
                error_exit(1,"<str> of the -t option must be within %d.\n",
                           TRMSIZE-1                                       );
              }
              strcpy(szTrm,optarg); iSize_trm=i; break;
    case 'v': giVerbose++;                       break;
    default : print_usage_and_exit();
  }
}
argc -= optind-1;
argv += optind  ;
if (giVerbose>1) {warning("verbose mode (level %d)\n",giVerbose);}
if (giVerbose>0 && iIgnCtrlD) {warning("[CTRL]+[D] will be ignored.\n");}
if (argc>1) {print_usage_and_exit();}
if (iNumofbunches==0) {return 0;}

/*=== Do the same as the cat command if STDIN isn't connected a terminal */
if (isatty(STDIN_FILENO)==0) {
  if (giVerbose>0) {
    warning("STDIN is not connected to a terminal.\n"                 );
    warning("This command will work at the same as the cat command.\n");
  }
  while ((iRemain=(int)read(STDIN_FILENO,gszBuf,BLKSIZE))>0) {
    for (iOffset=0; iRemain>0; iRemain-=iSize_w) {
      if ((iSize_w=(int)write(STDOUT_FILENO,gszBuf+iOffset,iRemain))<0) {
        error_exit(errno,"write()#%d: %s\n", __LINE__, strerror(errno));
      }
      iOffset+=iSize_w;
    }
  }
  if (iRemain<0) {error_exit(errno,"read()#%d: %s\n",__LINE__,strerror(errno));}
  return 0;
}

/*=== Disable ICANON and also disable ECHO if "-e" is not set ======*/
if (tcgetattr(STDIN_FILENO, &gstTerms1st) < 0) {
  error_exit(errno,"tcgetattr()#%d: %s\n", __LINE__, strerror(errno));
}
if (atexit(exit_trap)!=0) {
  error_exit(255,"atexit()#%d: Cannot register\n", __LINE__);
}
memset(&gsaExit, 0, sizeof(gsaExit));
gsaExit.sa_sigaction = interrupted_trap;
gsaExit.sa_flags     = SA_SIGINFO;
sigemptyset(&gsaExit.sa_mask);
sigaddset(&gsaExit.sa_mask, SIGHUP ); sigaddset(&gsaExit.sa_mask, SIGINT );
sigaddset(&gsaExit.sa_mask, SIGQUIT); sigaddset(&gsaExit.sa_mask, SIGPIPE);
sigaddset(&gsaExit.sa_mask, SIGALRM); sigaddset(&gsaExit.sa_mask, SIGTERM);
if (sigaction(SIGHUP ,&gsaExit,NULL) != 0)
  error_exit(errno,"sigaction()#%d: %s\n", __LINE__, strerror(errno));
if (sigaction(SIGINT ,&gsaExit,NULL) != 0)
  error_exit(errno,"sigaction()#%d: %s\n", __LINE__, strerror(errno));
if (sigaction(SIGQUIT,&gsaExit,NULL) != 0)
  error_exit(errno,"sigaction()#%d: %s\n", __LINE__, strerror(errno));
if (sigaction(SIGPIPE,&gsaExit,NULL) != 0)
  error_exit(errno,"sigaction()#%d: %s\n", __LINE__, strerror(errno));
if (sigaction(SIGALRM,&gsaExit,NULL) != 0)
  error_exit(errno,"sigaction()#%d: %s\n", __LINE__, strerror(errno));
if (sigaction(SIGTERM,&gsaExit,NULL) != 0)
  error_exit(errno,"sigaction()#%d: %s\n", __LINE__, strerror(errno));
memcpy(&stTerms, &gstTerms1st, sizeof(struct termios));
stTerms.c_lflag &= ~ICANON;
if (iEchomode==0) {stTerms.c_lflag &= ~ECHO;}
if (tcsetattr(STDIN_FILENO, TCSANOW, &stTerms)     < 0) {
  error_exit(errno,"tcsetattr()#%d: %s\n", __LINE__, strerror(errno));
}

/*=== Main loop ====================================================*/
iNumofbunches--;
while ((iSize_r=(int)read(STDIN_FILENO,gszBuf,BLKSIZE+1))>0) {
  /*--- If EOT follows the data, make this turn last ---------------*/
  if ((!iIgnCtrlD) && (gszBuf[iSize_r-1]==0x04)) {iNumofbunches=0; iSize_r--;}
  /*--- Write the data into STDOUT ---------------------------------*/
  iRemain=iSize_r;
  for (iOffset=0; iRemain>0; iRemain-=iSize_w) {
    if ((iSize_w=(int)write(STDOUT_FILENO,gszBuf+iOffset,iRemain))<0) {
      error_exit(errno,"write()#%d: %s\n", __LINE__, strerror(errno));
    }
    iOffset+=iSize_w;
  }
  /*--- Insert the LF if the non-full and non-LF-terminated --------*/
  if (!( iSize_r==0 || (iSize_r==1 && gszBuf[0]=='\n') || iSize_r>BLKSIZE)) {
    iRemain=iSize_trm;
    for (iOffset=0; iRemain>0; iRemain-=iSize_w) {
      if ((iSize_w=(int)write(STDOUT_FILENO,szTrm+iOffset,iRemain))<0) {
        error_exit(errno,"write()#%d: %s\n", __LINE__, strerror(errno));
      }
      iOffset+=iSize_w;
    }
  }
  /*--- last turn if requested -------------------------------------*/
  if      (iNumofbunches<0) {                 continue;}
  else if (iNumofbunches>0) {iNumofbunches--; continue;}
  else                      {                 break   ;}
}
if (iSize_r<0) {error_exit(errno,"read()#%d: %s\n", __LINE__, strerror(errno));}

/*=== Finish normally ==============================================*/
return 0;}
