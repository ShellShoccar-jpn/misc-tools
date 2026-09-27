/*####################################################################
#
# CALCLOCK - Time Format Converter Between YYYYMMDDhhmmss and UNIX time
#
# USAGE   : calclock [+[n]h] [-r] f1 [f2 [...]] file
#           calclock -d[r] string
# Args    : f1,f2,.. Column number(s) that hold the date/time data to
#                    be converted. At least one must be given.
#                    Each one can be:
#                      n ........ an absolute column number
#                      NF ....... the last column
#                      NF-n ..... n columns before the last one
#                      a/b ...... a range, where "a" and "b" are each
#                                 one of the three forms above (in
#                                 either order)
#                      0 ........ means "every column" (overrides all
#                                 other specifications on the command
#                                 line, wherever it appears)
#                    A column range is resolved against the actual
#                    number of columns (NF) of each line separately.
#                    If a range whose lower bound resolves below
#                    column 1 is given (e.g. "NF-5/NF-2" on a line
#                    with fewer than 6 columns), this command reports
#                    an error and exits. In contrast, if a *single*
#                    NF-relative column (e.g. "NF-2" alone) resolves
#                    below column 1, that line's conversion for that
#                    column is silently skipped instead.
#                    If a specified column number is greater than the
#                    line's actual NF, the line is extended (the
#                    columns in between are filled with an empty
#                    string) so that the converted value can be
#                    appended at that column, same as awk's behavior.
#           file .... Text file which contains some time field to
#                    convert. "-" or omitting it means STDIN.
#           string .. It will be explained in the -d option
# Options : -d ...... Direct Mode :
#                    It make this command regard the last argument
#                    (<string>) as a field formatted string instead
#                    of <file>
#           +<n>h ... Regards the top <n> lines as comment and Print
#                    without converting
#           -r ...... Converts from UNIX time to YYYYMMDDhhmmss instead
#                    of from YYYYMMDDhhmmss to YYYYMMDDhhmmss
# Environs: LINE_BUFFERED
#             =yes ........ Line-buffered mode
#             =forcible ... Line-buffered mode (same as "yes"; this
#                           command always supports switching the
#                           buffering mode via setvbuf(), so there is
#                           no "impossible" case to error out on)
#
# Retuen  : Return 0 only when finished successfully. A malformed
#           calendar-time or UNIX-time value does not cause an error;
#           instead, the literal string "xxxxxxxxxx" (or, only for an
#           out-of-range UNIX time being converted to calendar time,
#           "xxxxxxxxxxxxxx") is inserted as the converted value, and
#           processing continues.
#
# How to compile : cc -O3 -std=c99 -o __CMDNAME__ __SRCNAME__ -lm
#
# Designed originally by Nobuaki Tounaka
# Ported to C by Shell-Shoccar Japan (@shellshoccarjpn) on 2026-09-24
#
# This is a public-domain software (CC0). It means that all of the
# people can use this for any purposes with no restrictions at all.
# By the way, we are fed up with the side effects which are brought
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
  #define _XOPEN_SOURCE 700 /* for getline() */
#endif
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <unistd.h>
#include <time.h>
#include <math.h>

/*--- macro constants ----------------------------------------------*/
#define ARG_BUF  1024    /* max size of one command-line argument      */
#define SENTINEL_UNIXTIME    "xxxxxxxxxx"     /* invalid calendar-time */
#define SENTINEL_CALENDARTIME "xxxxxxxxxxxxxx" /* out-of-range unixtime*/

/*--- data type definitions ----------------------------------------*/
typedef struct _fieldref_t {
  int     iIsAbs; /* 1:absolute column number in i8Abs
                     0:NF-relative; the column is (NF - i8Ofs).
                       i8Ofs==0 means "NF" itself.               */
  int64_t i8Abs;
  int64_t i8Ofs;
} fieldref_t;
typedef struct _fieldinstr_t {
  int        iIsRange; /* 0:single column (stFrom only)
                          1:range (stFrom..stTo, either order)    */
  fieldref_t stFrom;
  fieldref_t stTo;
} fieldinstr_t;
typedef struct _record_t {
  char**  aszFld;      /* aszFld[1..i8Nf] are the fields (1-origin) */
  int64_t i8Nf;
  int64_t i8Cap;       /* allocated size of aszFld                  */
  char*   pszZero;     /* the current "$0"; see iZeroValid           */
  size_t  szZeroCap;
  int     iZeroValid;  /* 1: pszZero is up to date (either untouched
                           since the line was read, or the last value
                           directly assigned via set_field(rec,0,..)).
                           0: some $N (N>=1) was assigned since, so
                           pszZero must be rebuilt (by OFS-joining
                           aszFld[1..NF]) before it can be read again
                           -- this is what AWK itself does, and is
                           exactly the source of the "whitespace
                           gets normalized to a single space, but
                           only on a line where something was
                           actually converted" behavior.             */
} record_t;

/*--- prototype functions ------------------------------------------*/
int      parse_fieldref(const char* pszTok, fieldref_t* pstRef);
int      parse_fieldarg(char* pszArg, fieldinstr_t* pstInstr);
void     cache_zero(record_t* pstRec, const char* pszVal);
void     split_record(record_t* pstRec, const char* pszLine);
void     reset_record(record_t* pstRec);
void     free_record(record_t* pstRec);
char*    get_field(record_t* pstRec, int64_t i8N);
void     set_field(record_t* pstRec, int64_t i8N, const char* pszVal);
char*    rebuild_line(record_t* pstRec);
void     ensure_mark_cap(int64_t i8NeedCap);
void     convert_one_field(record_t* pstRec, int64_t i8Fld, int iReverse);
void     resolve_and_convert(record_t* pstRec, int iReverse);
int64_t  substr_i64(const char* pszS, int64_t i8Start, int64_t i8Len);
int64_t  days_in_month(int64_t i8Y, int64_t i8M);
int64_t  days_on_jan1st(int64_t i8Y);
char*    YYYYMMDDhhmmss2unixtime(const char* pszIn, char* pszOut, size_t szOutCap);
char*    unixtime2YYYYMMDDhhmmss_neg(double dUt, const char* pszDp, char* pszOut, size_t szOutCap);
char*    unixtime2YYYYMMDDhhmmss(const char* pszIn, char* pszOut, size_t szOutCap);
void     process_stream(FILE* fp, int iReverse);

/*--- global variables ---------------------------------------------*/
char*        gpszCmdname;      /* the name of this command                  */
int          giAllFields;      /* 1: a literal "0" was given somewhere      */
fieldinstr_t* gpstInstr;       /* the parsed field instructions             */
int64_t      gi8InstrNum;
int64_t      gi8MaxAbsField;   /* the largest absolute column number that
                                   appears anywhere in gpstInstr (used to
                                   size the per-record "mark" buffer)       */
unsigned char* gpucMark;       /* reusable "which columns to convert" buffer*/
int64_t      gi8MarkCap;
int64_t      giOpth;           /* number of header lines ("+[n]h")          */
int64_t      giCentury;        /* (current year/100)*100, for 2-digit years */
double       gdOffset;         /* local-time - UTC-time, in seconds         */
int64_t      gi8MaxCalcedYearP;/* days_on_jan1st() cache, forward direction */
int64_t      gi8MinCalcedYearN;/* days_on_jan1st() cache, backward direction*/
int64_t*     gpi8DaysCache;    /* [year - giCacheBase] -> days since epoch  */
int64_t      giCacheBase;
int64_t      giCacheCap;

/*=== Define the functions for printing usage and error ============*/

/*--- exit with usage ----------------------------------------------*/
void print_usage_and_exit(void) {
  fprintf(stderr,
    "USAGE   : %s [+[n]h] [-r] f1 [f2 [...]] file\n"
    "          %s -d[r] string\n"
    "Args    : f1,f2,.. Column number(s) that hold the date/time data to\n"
    "                   be converted. At least one must be given.\n"
    "                   Each one can be:\n"
    "                     n ........ an absolute column number\n"
    "                     NF ....... the last column\n"
    "                     NF-n ..... n columns before the last one\n"
    "                     a/b ...... a range, where \"a\" and \"b\" are each\n"
    "                                one of the three forms above (in\n"
    "                                either order)\n"
    "                     0 ........ means \"every column\" (overrides all\n"
    "                                other specifications on the command\n"
    "                                line, wherever it appears)\n"
    "                   A column range is resolved against the actual\n"
    "                   number of columns (NF) of each line separately.\n"
    "                   If a range whose lower bound resolves below\n"
    "                   column 1 is given (e.g. \"NF-5/NF-2\" on a line\n"
    "                   with fewer than 6 columns), this command reports\n"
    "                   an error and exits. In contrast, if a *single*\n"
    "                   NF-relative column (e.g. \"NF-2\" alone) resolves\n"
    "                   below column 1, that line's conversion for that\n"
    "                   column is silently skipped instead.\n"
    "                   If a specified column number is greater than the\n"
    "                   line's actual NF, the line is extended (the\n"
    "                   columns in between are filled with an empty\n"
    "                   string) so that the converted value can be\n"
    "                   appended at that column, same as awk's behavior.\n"
    "          file .... Text file which contains some time field to\n"
    "                   convert. \"-\" or omitting it means STDIN.\n"
    "          string .. It will be explained in the -d option\n"
    "Options : -d ...... Direct Mode :\n"
    "                   It make this command regard the last argument\n"
    "                   (<string>) as a field formatted string instead\n"
    "                   of <file>\n"
    "          +<n>h ... Regards the top <n> lines as comment and Print\n"
    "                   without converting\n"
    "          -r ...... Converts from UNIX time to YYYYMMDDhhmmss instead\n"
    "                   of from YYYYMMDDhhmmss to YYYYMMDDhhmmss\n"
    "Environs: LINE_BUFFERED\n"
    "            =yes ........ Line-buffered mode\n"
    "            =forcible ... Line-buffered mode (same as \"yes\"; this\n"
    "                          command always supports switching the\n"
    "                          buffering mode via setvbuf(), so there is\n"
    "                          no \"impossible\" case to error out on)\n"
    "Retuen  : Return 0 only when finished successfully. A malformed\n"
    "          calendar-time or UNIX-time value does not cause an error;\n"
    "          instead, the literal string \"xxxxxxxxxx\" (or, only for an\n"
    "          out-of-range UNIX time being converted to calendar time,\n"
    "          \"xxxxxxxxxxxxxx\") is inserted as the converted value, and\n"
    "          processing continues.\n"
    "\n"
    "Version : 2026-09-24 01:09:03 JST\n"
    "          (POSIX C language)\n"
    "\n"
    "Designed originally by Nobuaki Tounaka\n"
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
int      i;                 /* all-purpose int                      */
int      iOptPart;          /* 1: still in the leading-option part  */
int      iDirectmode;       /* 1: "-d" direct mode                  */
int      iReverse;          /* 1: "-r" reverse-conversion mode      */
char*    pszDirectstr;      /* the <string> for "-d"                */
char*    pszFile;           /* the <file> argument                  */
char*    pszArg;
fieldinstr_t stInstr;
int      iInstrCap;
FILE*    fp;

/*--- Initialize ---------------------------------------------------*/
gpszCmdname = argv[0];
for (i=0; *(gpszCmdname+i)!='\0'; i++) {
  if (*(gpszCmdname+i)=='/') {gpszCmdname=gpszCmdname+i+1;}
}
if (setenv("POSIXLY_CORRECT","1",1) < 0) {
  error_exit(errno,"setenv() at initialization: %s\n", strerror(errno));
}

/*=== Parse arguments ==============================================*/
if (argc <= 1) {print_usage_and_exit();}

iOptPart      = 1;
iDirectmode   = 0;
iReverse      = 0;
pszDirectstr  = NULL;
pszFile       = NULL;
giAllFields   = 0;
giOpth        = 0;
gpstInstr     = NULL;
gi8InstrNum   = 0;
iInstrCap     = 0;

for (i=1; i<argc; i++) {
  pszArg = argv[i];

  /*--- the leading-option part ("-d","-dr","-rd","-r","+[n]h") -----*/
  if (iOptPart) {
    if (strcmp(pszArg,"-d")==0) {
      iDirectmode = 1;
      continue;
    } else if (strcmp(pszArg,"-dr")==0 || strcmp(pszArg,"-rd")==0) {
      iDirectmode = 1;
      iReverse    = 1;
      continue;
    } else if (strcmp(pszArg,"-r")==0) {
      iReverse = 1;
      continue;
    } else if (pszArg[0]=='+') {
      char* p = pszArg+1;
      char* q = p;
      while (*q>='0' && *q<='9') {q++;}
      if (*q!='h' || *(q+1)!='\0') {print_usage_and_exit();}
      if (q==p) {giOpth=1;} else {giOpth=(int64_t)atoll(p);}
      continue;
    }
    iOptPart = 0;
  }

  /*--- direct mode: the very last argument is the <string> --------*/
  if (i==argc-1 && iDirectmode) {
    pszDirectstr = pszArg;
    break;
  }

  /*--- try to interpret this argument as a field instruction -------*/
  if (parse_fieldarg(pszArg, &stInstr)) {
    if (gi8InstrNum >= iInstrCap) {
      iInstrCap = iInstrCap ? iInstrCap*2 : 8;
      gpstInstr = (fieldinstr_t*)realloc(gpstInstr,
                                          (size_t)iInstrCap*sizeof(fieldinstr_t));
      if (gpstInstr==NULL) {error_exit(errno,"realloc(): %s\n",strerror(errno));}
    }
    gpstInstr[gi8InstrNum++] = stInstr;
    continue;
  }

  /*--- otherwise, this must be the filename (only if it's the last) */
  if (i==argc-1) {
    pszFile = pszArg;
    continue;
  }

  /*--- otherwise, it is an invalid argument -------------------------*/
  print_usage_and_exit();
}

/*=== Validate the arguments =========================================*/
if (iDirectmode) {
  if (pszDirectstr==NULL || pszDirectstr[0]=='\0') {print_usage_and_exit();}
  for (i=0; pszDirectstr[i]!='\0'; i++) {
    char c = pszDirectstr[i];
    if (!((c>='0'&&c<='9') || c=='.')) {print_usage_and_exit();}
  }
  if (pszDirectstr[0]=='.') {print_usage_and_exit();}
} else {
  if (gi8InstrNum==0) {print_usage_and_exit();}
}

/*=== Calculate the "century" constant and the local-UTC offset ====*/
{
  time_t  tNow;
  struct tm stTmLocal, stTmGm, *pstTmp;

  tNow = time(NULL);
  if (tNow==(time_t)-1) {error_exit(errno,"time(): %s\n",strerror(errno));}
  pstTmp = localtime(&tNow);
  if (pstTmp==NULL) {error_exit(errno,"localtime(): %s\n",strerror(errno));}
  stTmLocal = *pstTmp;
  giCentury = (int64_t)((stTmLocal.tm_year+1900)/100)*100;

  pstTmp = gmtime(&tNow);
  if (pstTmp==NULL) {error_exit(errno,"gmtime(): %s\n",strerror(errno));}
  stTmGm = *pstTmp;
  /* gmtime() always sets tm_isdst=0. If mktime() were called on
   * stTmGm as is, it would (mis)interpret UTC's wall-clock digits
   * under the STANDARD (non-DST) offset even when the local zone is
   * actually observing DST right now, introducing a 1-hour error.
   * Forcing the same tm_isdst as the local side cancels this out.  */
  stTmGm.tm_isdst = stTmLocal.tm_isdst;
  {
    time_t tLocalAsIfUtc = mktime(&stTmLocal);
    time_t tGmAsIfUtc    = mktime(&stTmGm);
    if (tLocalAsIfUtc==(time_t)-1 || tGmAsIfUtc==(time_t)-1) {
      error_exit(errno,"mktime(): %s\n",strerror(errno));
    }
    gdOffset = difftime(tLocalAsIfUtc, tGmAsIfUtc);
  }
}

/*--- precompute the max absolute column number for "mark" sizing --*/
gi8MaxAbsField = 0;
for (i=0; i<gi8InstrNum; i++) {
  if (gpstInstr[i].stFrom.iIsAbs && gpstInstr[i].stFrom.i8Abs>gi8MaxAbsField) {
    gi8MaxAbsField = gpstInstr[i].stFrom.i8Abs;
  }
  if (gpstInstr[i].iIsRange && gpstInstr[i].stTo.iIsAbs &&
      gpstInstr[i].stTo.i8Abs>gi8MaxAbsField                                ) {
    gi8MaxAbsField = gpstInstr[i].stTo.i8Abs;
  }
}

/*=== Switch to the line-buffered mode if required ===================*/
{
  char* pszEnv = getenv("LINE_BUFFERED");
  int   iLbm = 0; /* 0:normal 1:line-buffered */
  if (pszEnv!=NULL) {
    char szUp[32];
    for (i=0; pszEnv[i]!='\0' && i<31; i++) {
      char c = pszEnv[i];
      szUp[i] = (c>='a'&&c<='z') ? (char)(c-'a'+'A') : c;
    }
    szUp[i] = '\0';
    if (strcmp(szUp,"TRUE")==0 || strcmp(szUp,"YES")==0 ||
        strcmp(szUp,"Y")==0    || strcmp(szUp,"1")==0      ) {
      iLbm = 1;
    } else if (strncmp(szUp,"FORC",4)==0 || strcmp(szUp,"2")==0) {
      /* "forcible": treated exactly the same as "yes" (setvbuf()
       * always succeeds in switching the buffering mode before any
       * I/O has happened on the stream, so there is no "impossible"
       * case to fall back from, unlike the shell version's awk-based
       * implementation).                                            */
      iLbm = 1;
    }
  }
  if (iLbm) {
    if (setvbuf(stdout,NULL,_IOLBF,0)!=0) {
      error_exit(255,"Failed to switch to line-buffered mode\n");
    }
  }
}

/*=== Main routine ====================================================*/
if (iDirectmode) {
  /* Direct mode always operates on exactly one field (column 1),
   * regardless of any field instructions that happened to be parsed
   * from earlier arguments (calclock.sh discards them the same way,
   * by force-setting "fldnums=1" once the trailing <string> is
   * reached). Any earlier argument that failed to parse as either a
   * field instruction or (being non-last) a filename was already
   * rejected via print_usage_and_exit() in the loop above.          */
  record_t stRec = {0};
  reset_record(&stRec);
  set_field(&stRec, 1, pszDirectstr);
  convert_one_field(&stRec, 1, iReverse);
  if (fputs(rebuild_line(&stRec),stdout)==EOF || fputc('\n',stdout)==EOF) {
    error_exit(errno,"stdout: %s\n",strerror(errno));
  }
  free_record(&stRec);
  return 0;
}

if (pszFile==NULL || strcmp(pszFile,"-")==0) {
  fp = stdin;
} else {
  fp = fopen(pszFile,"r");
  if (fp==NULL) {error_exit(errno,"%s: %s\n",pszFile,strerror(errno));}
}
process_stream(fp, iReverse);
if (fp!=stdin) {fclose(fp);}

return 0;}



/*####################################################################
# Functions: Field-Selector Parsing
####################################################################*/

/*=== Classify one field-selector token ==============================
 * [in]  pszTok : a single token, e.g. "3", "NF", "NF-2"
 * [out] pstRef : filled in on success
 * [ret] 1:valid   0:invalid (does not match any of the 3 forms)   */
int parse_fieldref(const char* pszTok, fieldref_t* pstRef) {
  const char* p;
  if (pszTok[0]=='\0') {return 0;}

  /*--- "n" : an absolute column number (digits only) ---------------*/
  for (p=pszTok; *p!='\0'; p++) {
    if (*p<'0' || *p>'9') {break;}
  }
  if (*p=='\0') {
    pstRef->iIsAbs = 1;
    pstRef->i8Abs  = (int64_t)atoll(pszTok);
    return 1;
  }

  /*--- "NF" or "NF-n" -----------------------------------------------*/
  if (strcmp(pszTok,"NF")==0) {
    pstRef->iIsAbs = 0;
    pstRef->i8Ofs  = 0;
    return 1;
  }
  if (strncmp(pszTok,"NF-",3)==0) {
    p = pszTok+3;
    if (*p=='\0') {return 0;}
    for (; *p!='\0'; p++) {
      if (*p<'0' || *p>'9') {return 0;}
    }
    pstRef->iIsAbs = 0;
    pstRef->i8Ofs  = (int64_t)atoll(pszTok+3);
    return 1;
  }

  return 0;
}

/*=== Classify one command-line field argument ========================
 * [in]  pszArg : e.g. "3", "NF-2", "2/5", "3/NF", "NF-3/NF-1"
 * [out] pstInstr : filled in on success
 * [I/O] giAllFields : set to 1 (as a side effect, exactly matching
 *       calclock.sh's behavior) whenever a plain "0" is recognized as
 *       one side of the argument, REGARDLESS of whether the argument
 *       as a whole ends up being accepted as a valid field selector.
 * [ret] 1:valid field selector (single or range)   0:not a field
 *       selector at all (the caller should try it as a filename, or
 *       reject it as an invalid argument)                          */
int parse_fieldarg(char* pszArg, fieldinstr_t* pstInstr) {
  char*  pszSlash1;
  char*  pszSlash2;
  char   szArg1[ARG_BUF];
  char   szArg2[ARG_BUF];
  int    iHas2;
  fieldref_t stR1, stR2;
  int    iOk1, iOk2;
  int    iJ;

  pszSlash1 = strchr(pszArg,'/');
  pszSlash2 = strrchr(pszArg,'/');
  if (pszSlash1!=NULL && pszSlash1==pszSlash2 &&
      pszSlash1!=pszArg && *(pszSlash1+1)!='\0'                  ) {
    size_t szLen1 = (size_t)(pszSlash1-pszArg);
    if (szLen1>=sizeof(szArg1) || strlen(pszSlash1+1)>=sizeof(szArg2)) {
      return 0;
    }
    memcpy(szArg1,pszArg,szLen1); szArg1[szLen1]='\0';
    strcpy(szArg2,pszSlash1+1);
    iHas2 = 1;
  } else {
    if (strlen(pszArg)>=sizeof(szArg1)) {return 0;}
    strcpy(szArg1,pszArg);
    szArg2[0] = '\0';
    iHas2 = 0;
  }

  iJ   = 0;
  iOk1 = parse_fieldref(szArg1,&stR1);
  if (iOk1) {
    iJ++;
    if (stR1.iIsAbs && stR1.i8Abs==0) {giAllFields=1;}
  }
  iOk2 = 0;
  if (iHas2) {
    iOk2 = parse_fieldref(szArg2,&stR2);
    if (iOk2) {
      iJ++;
      if (stR2.iIsAbs && stR2.i8Abs==0) {giAllFields=1;}
    }
  }

  if (iJ==2) {
    pstInstr->iIsRange = 1;
    pstInstr->stFrom   = stR1;
    pstInstr->stTo     = stR2;
    return 1;
  }
  if (iJ==1 && !iHas2) {
    pstInstr->iIsRange = 0;
    pstInstr->stFrom   = stR1;
    return 1;
  }
  return 0;
}


/*####################################################################
# Functions: The "record" (AWK-like $0/$1../$NF) Model
####################################################################*/

/*=== Reset (empty) a record, freeing any fields it currently holds =*/
void reset_record(record_t* pstRec) {
  int64_t i;
  for (i=1; i<=pstRec->i8Nf; i++) {free(pstRec->aszFld[i]);}
  pstRec->i8Nf = 0;
  pstRec->iZeroValid = 0;
}

/*=== Store a copy of pszVal as the record's current, valid "$0" ====*/
void cache_zero(record_t* pstRec, const char* pszVal) {
  size_t szNeed = strlen(pszVal)+1;
  if (szNeed>pstRec->szZeroCap) {
    pstRec->szZeroCap = szNeed*2;
    pstRec->pszZero = (char*)realloc(pstRec->pszZero,pstRec->szZeroCap);
    if (pstRec->pszZero==NULL) {error_exit(errno,"realloc(): %s\n",strerror(errno));}
  }
  memcpy(pstRec->pszZero,pszVal,szNeed);
  pstRec->iZeroValid = 1;
}

/*=== Split a line into fields, AWK default-FS style =================
 * Leading/trailing blanks are ignored; fields are separated by runs
 * of one or more blanks (space or tab). Also caches pszLine itself as
 * the record's current "$0" (matching AWK: reading $0 right after a
 * fresh split -- whether from reading an input line, or from a direct
 * "$0 = ..." assignment -- yields the exact string that was split,
 * not a value rebuilt from the fields).                              */
void split_record(record_t* pstRec, const char* pszLine) {
  const char* p = pszLine;
  reset_record(pstRec);
  while (*p!='\0') {
    const char* pStart;
    size_t      szLen;
    while (*p==' ' || *p=='\t') {p++;}
    if (*p=='\0') {break;}
    pStart = p;
    while (*p!='\0' && *p!=' ' && *p!='\t') {p++;}
    szLen = (size_t)(p-pStart);
    pstRec->i8Nf++;
    if (pstRec->i8Nf>=pstRec->i8Cap) {
      pstRec->i8Cap = pstRec->i8Cap ? pstRec->i8Cap*2 : 16;
      pstRec->aszFld = (char**)realloc(pstRec->aszFld,
                                        (size_t)pstRec->i8Cap*sizeof(char*));
      if (pstRec->aszFld==NULL) {error_exit(errno,"realloc(): %s\n",strerror(errno));}
    }
    pstRec->aszFld[pstRec->i8Nf] = (char*)malloc(szLen+1);
    if (pstRec->aszFld[pstRec->i8Nf]==NULL) {
      error_exit(errno,"malloc(): %s\n",strerror(errno));
    }
    memcpy(pstRec->aszFld[pstRec->i8Nf],pStart,szLen);
    pstRec->aszFld[pstRec->i8Nf][szLen] = '\0';
  }
  cache_zero(pstRec,pszLine);
}

/*=== Get a field (AWK semantics: reading past NF returns "") =======*/
char* get_field(record_t* pstRec, int64_t i8N) {
  if (i8N==0) {return rebuild_line(pstRec);}
  if (i8N<1 || i8N>pstRec->i8Nf) {return "";}
  return pstRec->aszFld[i8N];
}

/*=== Set a field (AWK semantics: writing past NF extends the record,
 *     filling the gap with empty strings; writing field 0 replaces
 *     and re-splits the whole record; writing any field N>=1
 *     invalidates the cached "$0", same as real AWK)                 */
void set_field(record_t* pstRec, int64_t i8N, const char* pszVal) {
  int64_t i;

  if (i8N==0) {
    char* pszCopy = strdup(pszVal);
    if (pszCopy==NULL) {error_exit(errno,"strdup(): %s\n",strerror(errno));}
    split_record(pstRec,pszCopy); /* also re-caches "$0" == pszCopy */
    free(pszCopy);
    return;
  }

  if (i8N>=pstRec->i8Cap) {
    int64_t i8NewCap = pstRec->i8Cap ? pstRec->i8Cap*2 : 16;
    while (i8N>=i8NewCap) {i8NewCap *= 2;}
    pstRec->aszFld = (char**)realloc(pstRec->aszFld,
                                      (size_t)i8NewCap*sizeof(char*));
    if (pstRec->aszFld==NULL) {error_exit(errno,"realloc(): %s\n",strerror(errno));}
    pstRec->i8Cap = i8NewCap;
  }
  for (i=pstRec->i8Nf+1; i<i8N; i++) {
    pstRec->aszFld[i] = (char*)malloc(1);
    if (pstRec->aszFld[i]==NULL) {error_exit(errno,"malloc(): %s\n",strerror(errno));}
    pstRec->aszFld[i][0] = '\0';
  }
  if (i8N<=pstRec->i8Nf) {free(pstRec->aszFld[i8N]);}
  pstRec->aszFld[i8N] = strdup(pszVal);
  if (pstRec->aszFld[i8N]==NULL) {error_exit(errno,"strdup(): %s\n",strerror(errno));}
  if (i8N>pstRec->i8Nf) {pstRec->i8Nf = i8N;}
  pstRec->iZeroValid = 0; /* "$0" now stale; rebuild on next read */
}

/*=== Get (rebuilding if necessary) the record's current "$0" ========
 * If nothing has invalidated it (no $N, N>=1, has been assigned since
 * the line was read or "$0" was last set directly), this returns the
 * exact original/assigned string, spacing and all. Otherwise it is
 * rebuilt by joining $1..$NF with a single space (AWK's default OFS)
 * -- this is the one and only place where whitespace normalization
 * happens, exactly matching real AWK.                                */
char* rebuild_line(record_t* pstRec) {
  int64_t i;
  size_t  szNeed = 1;
  size_t  szPos  = 0;

  if (pstRec->iZeroValid) {return pstRec->pszZero;}

  for (i=1; i<=pstRec->i8Nf; i++) {szNeed += strlen(pstRec->aszFld[i])+1;}
  if (szNeed>pstRec->szZeroCap) {
    pstRec->szZeroCap = szNeed*2;
    pstRec->pszZero = (char*)realloc(pstRec->pszZero,pstRec->szZeroCap);
    if (pstRec->pszZero==NULL) {error_exit(errno,"realloc(): %s\n",strerror(errno));}
  }
  for (i=1; i<=pstRec->i8Nf; i++) {
    size_t szLen = strlen(pstRec->aszFld[i]);
    if (i>1) {pstRec->pszZero[szPos++] = ' ';}
    memcpy(pstRec->pszZero+szPos,pstRec->aszFld[i],szLen);
    szPos += szLen;
  }
  pstRec->pszZero[szPos] = '\0';
  pstRec->iZeroValid = 1;
  return pstRec->pszZero;
}

/*=== Free everything owned by a record ==============================*/
void free_record(record_t* pstRec) {
  int64_t i;
  for (i=1; i<=pstRec->i8Nf; i++) {free(pstRec->aszFld[i]);}
  free(pstRec->aszFld);   pstRec->aszFld  = NULL;
  free(pstRec->pszZero);  pstRec->pszZero = NULL;
  pstRec->i8Nf = 0; pstRec->i8Cap = 0; pstRec->szZeroCap = 0;
  pstRec->iZeroValid = 0;
}


/*####################################################################
# Functions: Field Marking and Conversion
####################################################################*/

/*=== Make sure the "mark" buffer can hold at least i8NeedCap bytes =*/
void ensure_mark_cap(int64_t i8NeedCap) {
  if (i8NeedCap<=gi8MarkCap) {return;}
  gpucMark = (unsigned char*)realloc(gpucMark,(size_t)i8NeedCap);
  if (gpucMark==NULL) {error_exit(errno,"realloc(): %s\n",strerror(errno));}
  gi8MarkCap = i8NeedCap;
}

/*=== Convert one field's text and store it back as "orig conv" ====*/
void convert_one_field(record_t* pstRec, int64_t i8Fld, int iReverse) {
  char*   pszOrig;
  char*   pszOrigCopy;
  char*   pszConv;
  size_t  szConvCap;
  char*   pszNewval;
  size_t  szNeed;

  pszOrig     = get_field(pstRec,i8Fld);
  pszOrigCopy = strdup(pszOrig);
  if (pszOrigCopy==NULL) {error_exit(errno,"strdup(): %s\n",strerror(errno));}

  /* The ".d" suffix is copied through verbatim and can be as long as
   * the whole input, so size the output buffer accordingly.         */
  szConvCap = strlen(pszOrigCopy)+48;
  pszConv   = (char*)malloc(szConvCap);
  if (pszConv==NULL) {error_exit(errno,"malloc(): %s\n",strerror(errno));}

  if (iReverse) {
    unixtime2YYYYMMDDhhmmss(pszOrigCopy,pszConv,szConvCap);
  } else {
    YYYYMMDDhhmmss2unixtime(pszOrigCopy,pszConv,szConvCap);
  }

  szNeed = strlen(pszOrigCopy)+1+strlen(pszConv)+1;
  pszNewval = (char*)malloc(szNeed);
  if (pszNewval==NULL) {error_exit(errno,"malloc(): %s\n",strerror(errno));}
  snprintf(pszNewval,szNeed,"%s %s",pszOrigCopy,pszConv);
  set_field(pstRec,i8Fld,pszNewval);
  free(pszNewval);
  free(pszConv);
  free(pszOrigCopy);
}

/*=== Resolve all field instructions against this record's NF, mark
 *    the target columns, and convert each of them ==================*/
void resolve_and_convert(record_t* pstRec, int iReverse) {
  int64_t i8Nf  = pstRec->i8Nf;
  int64_t i8Cap = ((i8Nf>gi8MaxAbsField) ? i8Nf : gi8MaxAbsField) + 1;
  int64_t i, f;

  ensure_mark_cap(i8Cap);
  memset(gpucMark,0,(size_t)i8Cap);

  if (giAllFields) {
    for (f=1; f<=i8Nf; f++) {gpucMark[f]=1;}
  } else {
    for (i=0; i<gi8InstrNum; i++) {
      fieldinstr_t* pstIn = &gpstInstr[i];
      if (!pstIn->iIsRange) {
        fieldref_t* r = &pstIn->stFrom;
        if (r->iIsAbs) {
          if (r->i8Abs>=0 && r->i8Abs<i8Cap) {gpucMark[r->i8Abs]=1;}
        } else {
          int64_t fld = i8Nf - r->i8Ofs;
          if (r->i8Ofs==0) {
            /* bare "NF": marked UNCONDITIONALLY, even if fld==0 (the
             * empty-line edge case where NF==0 and "NF" resolves to
             * field 0, i.e. "$0" itself) -- unlike "NF-n" (n>=1)
             * below, this is never silently skipped.                */
            if (fld>=0 && fld<i8Cap) {gpucMark[fld]=1;}
          } else if (fld>=1 && fld<i8Cap) {
            gpucMark[fld]=1;
          }
          /* "NF-n" (n>=1) resolving to fld<1: silently skipped,
           * matching the shell's single NF-relative-column behavior
           * (not an error).                                         */
        }
      } else {
        fieldref_t* a = &pstIn->stFrom;
        fieldref_t* b = &pstIn->stTo;
        if (a->iIsAbs && b->iIsAbs) {
          int64_t lo = (a->i8Abs<b->i8Abs) ? a->i8Abs : b->i8Abs;
          int64_t hi = (a->i8Abs<b->i8Abs) ? b->i8Abs : a->i8Abs;
          for (f=lo; f<=hi; f++) {if (f>=0 && f<i8Cap) {gpucMark[f]=1;}}
        } else if (!a->iIsAbs && !b->iIsAbs) {
          /* both NF-relative (incl. bare "NF", whose offset is 0):
           * matches the shell's upfront expansion into individual
           * "NF-i" single columns for i=min..max; each such column
           * that resolves below 1 is silently skipped (no error).  */
          int64_t lo = (a->i8Ofs<b->i8Ofs) ? a->i8Ofs : b->i8Ofs;
          int64_t hi = (a->i8Ofs<b->i8Ofs) ? b->i8Ofs : a->i8Ofs;
          int64_t o;
          for (o=lo; o<=hi; o++) {
            f = i8Nf - o;
            if (o==0) {
              /* the "NF" (bare) member of the expansion: unconditional,
               * same reasoning as the single-column case above.      */
              if (f>=0 && f<i8Cap) {gpucMark[f]=1;}
            } else if (f>=1 && f<i8Cap) {
              gpucMark[f]=1;
            }
          }
        } else {
          fieldref_t* pAbs = a->iIsAbs ? a : b;
          fieldref_t* pNfr = a->iIsAbs ? b : a;
          if (pNfr->i8Ofs==0) {
            /* "abs/NF" (a "capped range"): only if abs<=NF, mark
             * abs..NF. If abs>NF, silently mark nothing.            */
            if (pAbs->i8Abs<=i8Nf) {
              for (f=pAbs->i8Abs; f<=i8Nf; f++) {
                if (f>=1 && f<i8Cap) {gpucMark[f]=1;}
              }
            }
          } else {
            /* "abs/NF-n" (n>=1): resolve both ends against this
             * record's NF; if the resolved lower bound is below
             * column 1, this is a hard error (matches the shell's
             * mark_range() behavior exactly, unlike every other
             * case above which silently skips instead).            */
            int64_t i8Resolved = i8Nf - pNfr->i8Ofs;
            int64_t lo = (pAbs->i8Abs<i8Resolved) ? pAbs->i8Abs : i8Resolved;
            int64_t hi = (pAbs->i8Abs<i8Resolved) ? i8Resolved : pAbs->i8Abs;
            if (lo<1) {
              error_exit(1,"invalid field range: resolved field number "
                           "is less than 1 (NF=%lld)\n",(long long)i8Nf);
            }
            for (f=lo; f<=hi; f++) {if (f>=0 && f<i8Cap) {gpucMark[f]=1;}}
          }
        }
      }
    }
  }

  /* Column 0 (the AWK "$0" itself) must be converted first, since
   * doing so replaces and re-splits the whole record.               */
  if (i8Cap>0 && gpucMark[0]) {convert_one_field(pstRec,0,iReverse);}
  for (f=1; f<i8Cap; f++) {
    if (gpucMark[f]) {convert_one_field(pstRec,f,iReverse);}
  }
}


/*####################################################################
# Functions: Calendar Arithmetic (ported from calclock.sh's AWK code)
####################################################################*/

/*=== Extract an integer from an AWK-"substr(s,start,len)"-style slice
 * ("start" is 1-origin, matching AWK)                                */
int64_t substr_i64(const char* pszS, int64_t i8Start, int64_t i8Len) {
  char    szBuf[64];
  int64_t i8SLen = (int64_t)strlen(pszS);
  int64_t i8From = i8Start-1;
  int64_t i8N    = i8Len;
  if (i8From<0) {i8From=0;}
  if (i8From>=i8SLen) {return 0;}
  if (i8From+i8N>i8SLen) {i8N=i8SLen-i8From;}
  if (i8N<=0) {return 0;}
  if (i8N>=(int64_t)sizeof(szBuf)) {i8N=(int64_t)sizeof(szBuf)-1;}
  memcpy(szBuf,pszS+i8From,(size_t)i8N);
  szBuf[i8N] = '\0';
  return (int64_t)atoll(szBuf);
}

/*=== Number of days in month M of year Y (Gregorian, proleptic) ====*/
int64_t days_in_month(int64_t i8Y, int64_t i8M) {
  static const int64_t ai8Dim[13] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
  if (i8M==2) {
    if (i8Y%4!=0)   {return 28;}
    if (i8Y%100!=0) {return 29;}
    if (i8Y%400!=0) {return 28;}
    return 29;
  }
  if (i8M<1 || i8M>12) {return 0;}
  return ai8Dim[i8M];
}

/*=== days_on_Jan1st_from_epoch[Y] , growable/cached, ported from the
 *     shell's incremental algorithm (both forward and backward)     */
int64_t days_on_jan1st(int64_t i8Y) {
  if (gpi8DaysCache==NULL) {
    gpi8DaysCache = (int64_t*)malloc(sizeof(int64_t));
    if (gpi8DaysCache==NULL) {error_exit(errno,"malloc(): %s\n",strerror(errno));}
    gpi8DaysCache[0] = 0; /* days_on_Jan1st_from_epoch[1970] = 0 */
    giCacheBase = 1970;
    giCacheCap  = 1970;
  }
  if (i8Y>giCacheCap) {
    int64_t i8NewMax  = i8Y;
    int64_t i8NewSize = i8NewMax-giCacheBase+1;
    int64_t* pi8New = (int64_t*)realloc(gpi8DaysCache,(size_t)i8NewSize*sizeof(int64_t));
    if (pi8New==NULL) {error_exit(errno,"realloc(): %s\n",strerror(errno));}
    gpi8DaysCache = pi8New;
    {
      int64_t j, i8V = gpi8DaysCache[giCacheCap-giCacheBase];
      for (j=giCacheCap; j<i8NewMax; j++) {
        i8V += (j%4!=0) ? 365 : (j%100!=0) ? 366 : (j%400!=0) ? 365 : 366;
        gpi8DaysCache[j+1-giCacheBase] = i8V;
      }
    }
    giCacheCap = i8NewMax;
  }
  if (i8Y<giCacheBase) {
    int64_t i8NewMin  = i8Y;
    int64_t i8OldSize = giCacheCap-giCacheBase+1;
    int64_t i8NewSize = giCacheCap-i8NewMin+1;
    int64_t* pi8New = (int64_t*)malloc((size_t)i8NewSize*sizeof(int64_t));
    if (pi8New==NULL) {error_exit(errno,"malloc(): %s\n",strerror(errno));}
    memcpy(pi8New+(giCacheBase-i8NewMin),gpi8DaysCache,(size_t)i8OldSize*sizeof(int64_t));
    {
      int64_t j, i8V = pi8New[giCacheBase-i8NewMin];
      for (j=giCacheBase-1; j>=i8NewMin; j--) {
        i8V -= (j%4!=0) ? 365 : (j%100!=0) ? 366 : (j%400!=0) ? 365 : 366;
        pi8New[j-i8NewMin] = i8V;
      }
    }
    free(gpi8DaysCache);
    gpi8DaysCache = pi8New;
    giCacheBase = i8NewMin;
  }
  return gpi8DaysCache[i8Y-giCacheBase];
}

/*=== Calendar-time ("YYYYMMDDhhmmss[.d]") -> UNIX-time ("n[.d]") ====
 * [out] pszOut : buffer for the result; must be at least
 *                strlen(pszIn)+32 bytes (the ".d" suffix, if any, is
 *                copied through verbatim and can be as long as the
 *                whole input)
 * [ret] pszOut                                                      */
char* YYYYMMDDhhmmss2unixtime(const char* pszIn, char* pszOut, size_t szOutCap) {
  char        szDateOnly[ARG_BUF];
  char        szNum[160];
  const char* pszDp;
  const char* pDot;
  int64_t     l, Y, M, D, h, m, s;
  double      dResult;

  pDot = strchr(pszIn,'.');
  if (pDot==NULL) {
    strncpy(szDateOnly,pszIn,sizeof(szDateOnly)-1); szDateOnly[sizeof(szDateOnly)-1]='\0';
    pszDp = "";
  } else {
    size_t n = (size_t)(pDot-pszIn);
    if (n>=sizeof(szDateOnly)) {n=sizeof(szDateOnly)-1;}
    memcpy(szDateOnly,pszIn,n); szDateOnly[n]='\0';
    pszDp = pDot;
  }

  l = (int64_t)strlen(szDateOnly);
  if (l<5) {snprintf(pszOut,szOutCap,"%s",SENTINEL_UNIXTIME); return pszOut;}

  if (l<8) {
    Y = substr_i64(szDateOnly,1,l-4) + giCentury;
    M = substr_i64(szDateOnly,l-3,2);
    D = substr_i64(szDateOnly,l-1,2);
    h = 0; m = 0; s = 0;
  } else if (l<12) {
    Y = substr_i64(szDateOnly,1,l-4);
    M = substr_i64(szDateOnly,l-3,2);
    D = substr_i64(szDateOnly,l-1,2);
    h = 0; m = 0; s = 0;
  } else {
    Y = substr_i64(szDateOnly,1,l-10);
    M = substr_i64(szDateOnly,l-9,2);
    D = substr_i64(szDateOnly,l-7,2);
    h = substr_i64(szDateOnly,l-5,2);
    m = substr_i64(szDateOnly,l-3,2);
    s = substr_i64(szDateOnly,l-1,2);
  }

  if (s>60 || m>59 || h>23 || M>12 || M<1 || D<1) {
    snprintf(pszOut,szOutCap,"%s",SENTINEL_UNIXTIME); return pszOut;
  }
  if (D>days_in_month(Y,M)) {
    snprintf(pszOut,szOutCap,"%s",SENTINEL_UNIXTIME); return pszOut;
  }

  if (M<3) {M+=12; Y--;}
  dResult = (365.0*(double)Y + trunc((double)Y/4) - trunc((double)Y/100)
             + trunc((double)Y/400) + trunc(306.0*(double)(M+1)/10) - 428
             + (double)D - 719163)*86400.0
            + (double)h*3600.0 + (double)m*60.0 + (double)s - gdOffset;

  snprintf(szNum,sizeof(szNum),"%.0f",dResult);
  snprintf(pszOut,szOutCap,"%s%s",szNum,pszDp);
  return pszOut;
}

/*=== The "ut<0" half of unixtime2YYYYMMDDhhmmss (ported verbatim
 *     from calclock.sh's unixtime2YYYYMMDDhhmmss_neg())              */
char* unixtime2YYYYMMDDhhmmss_neg(double dUt, const char* pszDp, char* pszOut, size_t szOutCap) {
  char    szNum[160];
  int64_t i8Ut = (int64_t)dUt;
  int64_t s,m,h,t,Y,M,D,i8DaysFromEpoch;

  s = ((i8Ut%60)+60)%60;         t = (i8Ut-s)/60;
  m = ((t%60)+60)%60;            t = (t-m)/60;
  h = ((t%24)+24)%24;
  i8DaysFromEpoch = (t-h)/24;

  Y = (int64_t)trunc((double)i8DaysFromEpoch/365.2425) + 1970 - 1;
  for (;;Y++) {
    if (i8DaysFromEpoch < days_on_jan1st(Y+1)) {break;}
  }
  D = i8DaysFromEpoch - days_on_jan1st(Y) + 1;
  for (M=1; ; M++) {
    int64_t dim = days_in_month(Y,M);
    if (D>dim) {D-=dim;} else {break;}
  }

  snprintf(szNum,sizeof(szNum),"%04lld%02lld%02lld%02lld%02lld%02lld",
           (long long)Y,(long long)M,(long long)D,(long long)h,(long long)m,(long long)s);
  snprintf(pszOut,szOutCap,"%s%s",szNum,pszDp);
  return pszOut;
}

/*=== UNIX-time ("n[.d]") -> Calendar-time ("YYYYMMDDhhmmss[.d]") ====
 * [out] pszOut : buffer for the result; must be at least
 *                strlen(pszIn)+32 bytes (same reasoning as above)
 * [ret] pszOut                                                      */
char* unixtime2YYYYMMDDhhmmss(const char* pszIn, char* pszOut, size_t szOutCap) {
  char        szDp[ARG_BUF];
  char        szNum[160];
  double      dUt;
  int64_t     Y,M,D,h,m,s,t,i8DaysFromEpoch;
  const char* pDot = strchr(pszIn,'.');

  if (pDot==NULL) {
    dUt = atof(pszIn);
    szDp[0] = '\0';
  } else if (pDot!=pszIn) {
    char szInt[ARG_BUF];
    size_t n = (size_t)(pDot-pszIn);
    if (n>=sizeof(szInt)) {n=sizeof(szInt)-1;}
    memcpy(szInt,pszIn,n); szInt[n]='\0';
    dUt = atof(szInt);
    strncpy(szDp,pDot,sizeof(szDp)-1); szDp[sizeof(szDp)-1]='\0';
  } else { /* the string starts with "." : dp=whole string, ut=0 */
    dUt = 0;
    strncpy(szDp,pszIn,sizeof(szDp)-1); szDp[sizeof(szDp)-1]='\0';
  }

  dUt += gdOffset;
  if (dUt>1000000000000.0 || dUt< -1000000000000.0) {
    snprintf(pszOut,szOutCap,"%s",SENTINEL_CALENDARTIME); return pszOut;
  }
  if (dUt<0) {return unixtime2YYYYMMDDhhmmss_neg(dUt,szDp,pszOut,szOutCap);}

  {
    int64_t i8Ut = (int64_t)dUt;
    s = i8Ut % 60;  t = i8Ut/60;
    m =    t % 60;  t = t/60;
    h =    t % 24;
    i8DaysFromEpoch = t/24;
  }

  Y = (int64_t)trunc((double)i8DaysFromEpoch/365.2425) + 1970 + 1;
  (void)days_on_jan1st(Y); /* make sure the cache is extended that far */
  for (;;Y--) {
    if (i8DaysFromEpoch >= days_on_jan1st(Y)) {break;}
  }
  D = i8DaysFromEpoch - days_on_jan1st(Y) + 1;
  for (M=1; ; M++) {
    int64_t dim = days_in_month(Y,M);
    if (D>dim) {D-=dim;} else {break;}
  }

  snprintf(szNum,sizeof(szNum),"%04lld%02lld%02lld%02lld%02lld%02lld",
           (long long)Y,(long long)M,(long long)D,(long long)h,(long long)m,(long long)s);
  snprintf(pszOut,szOutCap,"%s%s",szNum,szDp);
  return pszOut;
}


/*####################################################################
# Functions: Main Stream Processing
####################################################################*/

/*=== Read "fp" line by line, apply the header-skip / field-marking /
 *     conversion logic, and write the result to stdout ==============*/
void process_stream(FILE* fp, int iReverse) {
  char*    pszLine = NULL;
  size_t   szLineCap = 0;
  ssize_t  ssLen;
  int64_t  i8LineNo = 0;
  record_t stRec = {0};
  char*    pszEnv = getenv("LINE_BUFFERED");
  int      iFlush = (pszEnv!=NULL && pszEnv[0]!='\0');

  while ((ssLen=getline(&pszLine,&szLineCap,fp)) >= 0) {
    i8LineNo++;
    if (ssLen>0 && pszLine[ssLen-1]=='\n') {pszLine[--ssLen]='\0';}

    if (i8LineNo<=giOpth) {
      if (fputs(pszLine,stdout)==EOF || fputc('\n',stdout)==EOF) {
        error_exit(errno,"stdout: %s\n",strerror(errno));
      }
      if (iFlush) {fflush(stdout);}
      continue;
    }

    split_record(&stRec,pszLine);
    resolve_and_convert(&stRec,iReverse);
    if (fputs(rebuild_line(&stRec),stdout)==EOF || fputc('\n',stdout)==EOF) {
      error_exit(errno,"stdout: %s\n",strerror(errno));
    }
    if (iFlush) {fflush(stdout);}
  }
  if (ferror(fp)) {error_exit(errno,"read error: %s\n",strerror(errno));}

  free_record(&stRec);
  free(pszLine);
}
