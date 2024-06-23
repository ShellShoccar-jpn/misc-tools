/*####################################################################
#
# HEREWEGO - Sleep Until a Nice Round Time and Tell the Time
#
# USAGE   : herewego [options] [+standby] interval[-premature]
# Args    : interval  * Interval time between the specified nice round
#                       time.
#                     * For instance, when you set "0.25" to the argument
#                       that begins from a number "0," "1," ... "9" or the
#                       decimal point ".," and start this command at the
#                       moment the clock shows 2009-08-07T06:05:04.321,
#                       this command will try to end sleeping at
#                       2009-08-07T06:05:04.500 because n.00, n.25,
#                       n.50, and n.75 (n means any time) are the nice
#                       round times for the argument.
#                     * The default unit is second. You can also add a
#                       unit word as in "s," "ms," "us," or "ns." So
#                       you can set any of the followings:
#                       "1.23," "1.23s," "1230ms," "1230000us" ...
#                       These are all same meanings.
#           premature * When you set this parameter just after the
#                       "interval" parameter with the minus "-"
#                       character without space, this command will try
#                       to end sleeping earlier.
#                     * For instance, when you set "0.25-0.05" to the
#                       argument and start this command at the same
#                       moment as the above time, this command will try
#                       to end sleeping at
#                       2009-08-07T06:05:04.450 because the time is
#                       0.05 second earlier than n.50, that is one of
#                       the nice round times.
#                     * The default unit is second. You can also add a
#                       unit word as in "s," "ms," "us," or "ns." So
#                       you can set any of the followings:
#                       "1-1.23," "1-1.23s," "1-1230ms" ...
#                       These are all same meanings.
#                     * NOTE that the timestamp sent just before exiting
#                       is NOT INTENTIONALLY CHANGED. That is because
#                       this parameter exists to cancel the time lag
#                       between the moment this command sends the
#                       timestamp string and the moment some following
#                       device receives it.
#           standby   * When you set this parameter to the argument that
#                       begins from the plus sign "+", this command will
#                       firstly sleep for the specified duration.
#                     * For instance, when you run this command with the
#                       following arguments at
#                       2001-01-01T00:00:00.249999999
#                         $ herewego +0.1 0.25
#                       this command will probably end sleeping around
#                       2001-01-01T00:00:00.500000000. That is because
#                       this command will firstly sleep for 0.1 second.
#                       Then the clock will advance to
#                       2001-01-01T00:00:00.349999999. Thus the next nice
#                       around time is 2001-01-01T00:00:00.500000000.
#                     * Even if you don't set the "+0.1" argument in the
#                       above case, this command can hardly ever end
#                       sleeping at 2001-01-01T00:00:00.250000000. The
#                       actual exit time will be a little later than the
#                       timestamp this command says. That is because
#                       there is only one nanosecond to finish the task.
#                       It is too short for most computers in the 2020s.
#                       The point is that this parameter is important to
#                       keep the actual exiting time predictable. So YOU
#                       SHOULD SET THIS PARAMETER with a realistic
#                       duration in almost all situations.
#                     * The default unit is second. You can also add a
#                       unit word as in "s," "ms," "us," or "ns." So
#                       you can set any of the followings:
#                       "+1.23," "+1.23s," "+1230ms," "+1230000us"...
#                       These are all same meanings.
# Options : -0,-3,-6,-9 Specify resolution unit of the timestamp. For
#                       instance, timestamp becomes "YYYYMMDDhhmmss.nnn"
#                       when "-3" option is set. 
#                       You have to set one of them.
#                         -0 ... second (default)
#                         -3 ... millisecond
#                         -6 ... microsecond
#                         -9 ... nanosecond
#           -c,-e,-I .. Specify the format for the timestamp that will
#                       be displayed just before exiting. You can choose
#                       one of them.
#                         -c ... "YYYYMMDDhhmmss[.n]" (default)
#                                Calendar-time (standard time) in your
#                                timezone (".n" is the digits under
#                                second. It will be attached when -3 or
#                                -6 or -9 option is specified)
#                         -e ... "n[.n]"
#                                The number of seconds since the UNIX
#                                epoch (".n" is the same as -c)
#                         -I ... "YYYY-MM-DDThh:mm:ss[,n]{+|-}hh:mm"
#                                The ISO 8601 format
#                                (",n" is the same as -c)
#           -u ........ Set the timestamp displayed just before exiting
#                       in in UTC when -c option is set
#                       (same as that of date command)
#           -p n ...... Process priority setting [0-3] (if possible)
#                        0: Normal process
#                        1: Weakest realtime process (default)
#                        2: Strongest realtime process for generic users
#                           (for only Linux, equivalent 1 for otheres)
#                        3: Strongest realtime process of this host
#                       Larger numbers maybe require a privileged user,
#                       but if failed, it will try the smaller numbers.
# Retuen  : Return 0 only when finished successfully
#
# How to compile : cc -O3 -o __CMDNAME__ __SRCNAME__ -lrt
#                  (if it doesn't work)
# How to compile : cc -O3 -o __CMDNAME__ __SRCNAME__
#
# Written Shell-Shoccar Japan (@shellshoccarjpn) on 2024-06-23
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
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#if defined(_POSIX_PRIORITY_SCHEDULING) && !defined(__OpenBSD__) && !defined(__APPLE__)
  #include <sched.h>
  #include <sys/resource.h>
#endif

/*--- macro constants ----------------------------------------------*/
/* Buffer size for a line-string */
#define LINE_BUF        100

/*--- macro func1: Calculate additon for time(timespec) + nanosec(int64_t)
 * [in]  (struct timespec)ts : time for the augend and it will be
 *                             overwritten by the result
 *       (int64_t)i8         : nano-second time for the addend
 * [ret] none                : The result will be written into the "ts"
 *                             itself.                                 */
#define tsadd(ts,i8) ts.tv_nsec+=i8%1000000000;ts.tv_sec+=ts.tv_nsec/1000000000+i8/1000000000;ts.tv_nsec%=1000000000

/*--- macro func1: Calculate subtraction for time(timespec) - nanosec(int64_t)
 * [in]  (struct timespec)ts : time for the minuend and it will be
 *                             overwritten by the result
 *       (int64_t)i8         : nano-second time for the subtrahend
 * [ret] none                : The result will be written into the "ts"
 *                             itself.                                 */
#define tssub(ts,i8) ts.tv_nsec-=i8%1000000000;ts.tv_sec-=(ts.tv_nsec<0)+i8/1000000000;ts.tv_nsec+=(ts.tv_nsec<0)*1000000000

/*--- macro func2: Calculate modulo for time(timespec) % nanosec(int64_t)
 * [in]  (struct timespec)ts : time for the dividend
 *       (int64_t)i8         : nano-second time for the divisor
 * [ret] (int64_t)           : Division remainder by "ts % i8"
                               in nano-second                          */
#define tsmod(ts,i8) ((((ts.tv_sec%i8)*(1000000000%i8))%i8+(ts.tv_nsec)%i8)%i8)

/*--- data type definitions ----------------------------------------*/
typedef struct timespec tmsp;

/*--- prototype functions ------------------------------------------*/
int64_t parse_duration(char *pszArg);
int     change_to_rtprocess(int iPrio);

/*--- global variables ---------------------------------------------*/
char*   gpszCmdname; /* The name of this command                    */
int64_t gi8Intv = -1 ; /* "interval" parameter in the arguments     */
int64_t gi8Prem =  0 ; /* "premature" parameter in the arguments    */
int64_t gi8Mini =  0 ; /* "standby" parameter in the arguments      */
int giTimeResol =  0 ; /* 0:second(def) 3:millisec 6:microsec 9:nanosec */
int giFmtType   = 'c'; /* 'c':calendar-time (default)
                      'e':UNIX-epoch-time                           */
int giVerbose   =  0 ; /* speaks more verbosely by the greater number   */
int giPrio      =  1 ; /* -p option number (default 1)                  */

/*=== Define the functions for printing usage and error ============*/

/*--- exit with usage ----------------------------------------------*/
void print_usage_and_exit(void) {
  fprintf(stderr,
    "USAGE   : %s [options] [+standby] interval[-premature]\n"
    "Args    : interval  * Interval time between the specified nice round\n"
    "                      time.\n"
    "                    * For instance, if you set \"0.25\" to the argument\n"
    "                      that begins from a number \"0,\" \"1,\" ... \"9\" or the\n"
    "                      decimal point \".,\" and start this command at the\n"
    "                      moment the clock shows 2009-08-07T06:05:04.321,\n"
    "                      this command will try to end sleeping at\n"
    "                      2009-08-07T06:05:04.500 because n.00, n.25,\n"
    "                      n.50, and n.75 (n means any time) are the nice\n"
    "                      round times for the argument.\n"
    "                    * The default unit is second. You can also add a\n"
    "                      unit word as in \"s,\" \"ms,\" \"us,\" or \"ns.\" So\n"
    "                      you can set any of the followings:\n"
    "                      \"1.23,\" \"1.23s,\" \"1230ms,\" \"1230000us\" ...\n"
    "                      These are all same meanings.\n"
    "          premature * When you set this parameter just after the\n"
    "                      \"interval\" parameter with the minus \"-\"\n"
    "                      character without space, this command will try\n"
    "                      to end sleeping earlier.\n"
    "                    * For instance, if you set \"0.25-0.05\" to the\n"
    "                      first argument and start this command at the\n"
    "                      same moment as the above time, this command\n"
    "                      will try to end sleeping at\n"
    "                      2009-08-07T06:05:04.450 because the time is\n"
    "                      0.05 second earlier than n.50, that is one of\n"
    "                      the nice round times.\n"
    "                    * The default unit is second. You can also add a\n"
    "                      unit word as in \"s,\" \"ms,\" \"us,\" or \"ns.\" So\n"
    "                      you can set any of the followings:\n"
    "                      \"1-1.23,\" \"1-1.23s,\" \"1-1230ms\" ...\n"
    "                      These are all same meanings.\n"
    "                    * NOTE that the timestamp sent just before exiting\n"
    "                      is NOT INTENTIONALLY CHANGED. That is because\n"
    "                      this parameter exists to cancel the time lag\n"
    "                      between the moment this command sends the\n"
    "                      timestamp string and the moment some following\n"
    "                      device receives it.\n"
    "          standby   * When you set this parameter to the argument that\n"
    "                      begins from the plus sign \"+\", this command will\n"
    "                      firstly sleep for the specified duration.\n"
    "                    * For instance, when you run this command with the\n"
    "                      following arguments at\n"
    "                      2001-01-01T00:00:00.249999999\n"
    "                        $ %s +0.1 0.25\n"
    "                      this command will probably end sleeping around\n"
    "                      2001-01-01T00:00:00.500000000. That is because\n"
    "                      this command will firstly sleep for 0.1 second.\n"
    "                      Then the clock will advance to\n"
    "                      2001-01-01T00:00:00.349999999. Thus the next nice\n"
    "                      around time is 2001-01-01T00:00:00.500000000.\n"
    "                    * Even if you don't set the \"+0.1\" argument in the\n"
    "                      above case, this command can hardly ever end\n"
    "                      sleeping at 2001-01-01T00:00:00.250000000. The\n"
    "                      actual exit time will be a little later than the\n"
    "                      timestamp this command says. That is because\n"
    "                      there is only one nanosecond to finish the task.\n"
    "                      It is too short for most computers in the 2020s.\n"
    "                      The point is that this parameter is important to\n"
    "                      keep the actual exiting time predictable. So YOU\n"
    "                      SHOULD SET THIS PARAMETER with a realistic\n"
    "                      duration in almost all situations.\n"
    "Options : -0,-3,-6,-9 Specify resolution unit of the timestamp. For\n"
    "                      instance, timestamp becomes \"YYYYMMDDhhmmss.nnn\"\n"
    "                      when \"-3\" option is set. \n"
    "                      You have to set one of them.\n"
    "                        -0 ... second (default)\n"
    "                        -3 ... millisecond\n"
    "                        -6 ... microsecond\n"
    "                        -9 ... nanosecond\n"
    "          -c,-e,-I .. Specify the format for the timestamp that will\n"
    "                      be displayed just before exiting. You can choose\n"
    "                      one of them.\n"
    "                        -c ... \"YYYYMMDDhhmmss[.n]\" (default)\n"
    "                               Calendar-time (standard time) in your\n"
    "                               timezone (\".n\" is the digits under\n"
    "                               second. It will be attached when -3 or\n"
    "                               -6 or -9 option is specified)\n"
    "                        -e ... \"n[.n]\"\n"
    "                               The number of seconds since the UNIX\n"
    "                               epoch (\".n\" is the same as -c)\n"
    "                        -I ... \"YYYY-MM-DDThh:mm:ss[,n]{+|-}hh:mm\"\n"
    "                               The ISO 8601 format\n"
    "                               (\",n\" is the same as -c)\n"
    "          -u ........ Set the timestamp displayed just before exiting\n"
    "                      in in UTC when -c option is set\n"
    "                      (same as that of date command)\n"
#if defined(_POSIX_PRIORITY_SCHEDULING) && !defined(__OpenBSD__) && !defined(__APPLE__)
    "          -p n ...... Process priority setting [0-3] (if possible)\n"
    "                       0: Normal process\n"
    "                       1: Weakest realtime process (default)\n"
    "                       2: Strongest realtime process for generic users\n"
    "                          (for only Linux, equivalent 1 for otheres)\n"
    "                       3: Strongest realtime process of this host\n"
    "                      Larger numbers maybe require a privileged user,\n"
    "                      but if failed, it will try the smaller numbers.\n"
#endif
    "Retuen  : Return 0 only when finished successfully\n"
    "\n"
    "Version : 2024-06-23 13:28:01 JST\n"
    "          (POSIX C language)\n"
    "\n"
    "Shell-Shoccar Japan (@shellshoccarjpn), No rights reserved.\n"
    "This is public domain software. (CC0)\n"
    "\n"
    "The latest version is distributed at the following page.\n"
    "https://github.com/ShellShoccar-jpn/tokideli\n"
    ,gpszCmdname ,gpszCmdname);
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
int        i;               /* all-purpose int                    */
int64_t    i8;              /* all-purpose int64_t                */
char*      psz;             /* all-purpose char*                  */
tmsp       tsT0;            /* Time this command booted ~ exiting */
tmsp       tsRep;           /* Time to report at exiting          */
char*      pszArg;          /* String to parsr an argument        */
struct tm* ptm;             /* a pointer of "tm" structure        */
char       szTs[LINE_BUF];  /* timestamp to be reported           */
char       szTim[72];       /* timestamp (year - sec)             */
char       szDec[21];       /* timestamp (under sec)              */
char       szTmz[ 7];       /* timestamp (timezone)               */

/*--- Initialize ---------------------------------------------------*/
if (clock_gettime(CLOCK_REALTIME,&tsT0) != 0) {
  error_exit(errno,"clock_gettime() at initialize: %s\n",strerror(errno));
}
gpszCmdname = argv[0];
for (i=0; *(gpszCmdname+i)!='\0'; i++) {
  if (*(gpszCmdname+i)=='/') {gpszCmdname=gpszCmdname+i+1;}
}
if (setenv("POSIXLY_CORRECT","1",1) < 0) {
  error_exit(errno,"setenv() at initialization: \n", strerror(errno));
}
setlocale(LC_CTYPE, "");

/*=== Parse arguments ==============================================*/

/*--- Parse options which start by "-" -----------------------------*/
while ((i=getopt(argc, argv, "0369ceIp:uvh")) != -1) {
  switch (i) {
    case '0': giTimeResol =  0 ;                 break;
    case '3': giTimeResol =  3 ;                 break;
    case '6': giTimeResol =  6 ;                 break;
    case '9': giTimeResol =  9 ;                 break;
    case 'c': 
    case 'e': 
    case 'I': giFmtType   =  i ;                 break;
    #if defined(_POSIX_PRIORITY_SCHEDULING) && !defined(__OpenBSD__) && !defined(__APPLE__)
      case 'p': if (sscanf(optarg,"%d",&giPrio) != 1) {print_usage_and_exit();}
                                               break;
    #endif
    case 'u': (void)setenv("TZ", "UTC0", 1);     break;
    case 'v': giVerbose++;                   break;
    case 'h': print_usage_and_exit();
    default : print_usage_and_exit();
  }
}
argc -= optind-1;
argv += optind  ;
if (giVerbose>0) {warning("verbose mode (level %d)\n",giVerbose);}

/*--- Parse arguments after the options -----------------------------*/
i = 0;
while ((pszArg = argv[i]) != NULL && i < argc) {
  if (pszArg[0] == '+') {
    gi8Mini = parse_duration(++pszArg);
    if (gi8Mini<0) {
      error_exit(1,
        "%s: \"standby\" parameter is out of range or invalid.\n",--pszArg);
    }
    i++;
    continue;
  }
  psz = strchr(pszArg, '-');
  if (psz != NULL) {
    gi8Prem = parse_duration(++psz);
    if (gi8Prem<0) {
      error_exit(1,
        "%s: \"premature\" parameter is out of range or invalid\n",pszArg);
    }
  *(--psz) = 0;
  }
  gi8Intv = parse_duration(pszArg);
  if (gi8Intv<0) {
    error_exit(1,
      "%s: \"interval\" parameter is out of range or invalid\n",pszArg);
  }
  i++;
}

/*--- Validate the parameters --------------------------------------*/
if (gi8Intv < 0                      ) {print_usage_and_exit();}
if ((gi8Prem>0) && (gi8Intv<=gi8Prem)) {
  error_exit(1,
    "\"premature\" parameter must be smaller than \"interval\" parameter.\n"
  );
}

/*=== Try to make me a realtime process ============================*/
if (change_to_rtprocess(giPrio)==-1) {print_usage_and_exit();}

/*=== Calculate the time this command should exit ==================*/
tsadd(tsT0,gi8Mini);
if (gi8Intv>0) {
  i8=tsmod(tsT0,gi8Intv);
  if (i8!=0) {tssub(tsT0,i8);tsadd(tsT0,gi8Intv);}
  memcpy(&tsRep,&tsT0,sizeof(tmsp));
  tssub(tsT0,gi8Prem);
} else         {
  memcpy(&tsRep,&tsT0,sizeof(tmsp));
}

/*=== Make the timestamp ===========================================*/

/*--- Make ---------------------------------------------------------*/
switch (giFmtType) {
  case 'c':
            switch (giTimeResol) {
              case 0 : if (tsRep.tv_nsec>=500000000L) {tsRep.tv_sec++ ;
                                                       tsRep.tv_nsec=0;}
                       szDec[0]=0;
                       break;
              case 3 : if (tsRep.tv_nsec>=999500000L) {tsRep.tv_sec++ ;
                                                       tsRep.tv_nsec=0;}
                       snprintf(szDec,21,".%03ld",tsRep.tv_nsec/1000000);
                       break;
              case 6 : if (tsRep.tv_nsec>=999999500L) {tsRep.tv_sec++ ;
                                                       tsRep.tv_nsec=0;}
                       snprintf(szDec,21,".%06ld",tsRep.tv_nsec/   1000);
                       break;
              default: snprintf(szDec,21,".%09ld",tsRep.tv_nsec        );
                       break;
            }
            ptm = localtime(&tsRep.tv_sec);
            if (ptm==NULL) {error_exit(255,"localtime(): returned NULL\n");}
            snprintf(szTs , LINE_BUF, "%04d%02d%02d%02d%02d%02d%s",
              ptm->tm_year+1900, ptm->tm_mon+1, ptm->tm_mday,
              ptm->tm_hour     , ptm->tm_min  , ptm->tm_sec , szDec);
            break;
  case 'e':
            switch (giTimeResol) {
              case 0 : if (tsRep.tv_nsec>=500000000L) {tsRep.tv_sec++ ;
                                                       tsRep.tv_nsec=0;}
                       szDec[0]=0;
                       break;
              case 3 : if (tsRep.tv_nsec>=999500000L) {tsRep.tv_sec++ ;
                                                       tsRep.tv_nsec=0;}
                       snprintf(szDec,21,".%03ld",tsRep.tv_nsec/1000000);
                       break;
              case 6 : if (tsRep.tv_nsec>=999999500L) {tsRep.tv_sec++ ;
                                                       tsRep.tv_nsec=0;}
                       snprintf(szDec,21,".%06ld",tsRep.tv_nsec/   1000);
                       break;
              default: snprintf(szDec,21,".%09ld",tsRep.tv_nsec        );
                       break;
            }
            ptm = localtime(&tsRep.tv_sec);
            if (ptm==NULL) {error_exit(255,"localtime(): returned NULL\n");}
            strftime(szTim,       20, "%s"  , ptm         );
            snprintf(szTs , LINE_BUF, "%s%s", szTim, szDec);
            break;
  case 'I':
            switch (giTimeResol) {
              case 0 : if (tsRep.tv_nsec>=500000000L) {tsRep.tv_sec++ ;
                                                       tsRep.tv_nsec=0;}
                       szDec[0]=0;
                       break;
              case 3 : if (tsRep.tv_nsec>=999500000L) {tsRep.tv_sec++ ;
                                                       tsRep.tv_nsec=0;}
                       snprintf(szDec,21,",%03ld",tsRep.tv_nsec/1000000);
                       break;
              case 6 : if (tsRep.tv_nsec>=999999500L) {tsRep.tv_sec++ ;
                                                       tsRep.tv_nsec=0;}
                       snprintf(szDec,21,",%06ld",tsRep.tv_nsec/   1000);
                       break;
              default: snprintf(szDec,21,",%09ld",tsRep.tv_nsec        );
                       break;
            }
            ptm = localtime(&tsRep.tv_sec);
            if (ptm==NULL) {error_exit(255,"localtime(): returned NULL\n");}
            snprintf(szTim, 72, "%04d-%02d-%02dT%02d:%02d:%02d",
              ptm->tm_year+1900, ptm->tm_mon+1, ptm->tm_mday,
              ptm->tm_hour     , ptm->tm_min  , ptm->tm_sec  );
            strftime(szTmz, 6, "%z", ptm);
            szTmz[6]=0; szTmz[5]=szTmz[4]; szTmz[4]=szTmz[3]; szTmz[3]=':';
            snprintf(szTs , LINE_BUF, "%s%s%s", szTim, szDec, szTmz);
            break;
  default : error_exit(255,"Unknown \"giFmtType\"\n");
}

/*=== Sleep until the time to exit =================================*/
switch (clock_nanosleep(CLOCK_REALTIME,TIMER_ABSTIME,&tsT0,NULL)) {
  case 0    : break;
  case EINTR: error_exit(1,"Exit because some signal interrupted my sleep.\n");
              break;
  default   : error_exit(1,"clock_nanosleep() failed\n");
              break;
}

/*=== Print the timestamp ==========================================*/
puts(szTs);

/*=== Finish normally ==============================================*/
return 0;}



/*####################################################################
# Functions
####################################################################*/

/*=== Parse the duration string ======================================
 * [in]  pszArg: String to be parsed (numner[+unit])
 * [ret] >= 0  : Interval value (in nanosecound)
 *       <=-1  : Means infinity (completely shut the valve)
 *       <=-2  : It is not a value                                  */
int64_t parse_duration(char *pszArg) {

  /*--- Variables --------------------------------------------------*/
  char   szUnit[LINE_BUF];
  double dNum;

  /*--- Check the lengths of the argument --------------------------*/
  if (strlen(pszArg)>32) {return -2;}

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
