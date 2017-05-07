/*####################################################################
#
# GETFTIMES - Get Timestamps of Each File
#
# USAGE   : getftimes [options] <file> [file ...]
# Options : -l ... Prints the timestamps in ISO8601 format
#           -- ... Finishes parsing arguments as options
# Output  : * Print the following 4 fields by each file
#             <atime> <mtime> <ctime> <filename>
#           * The format of each time is either
#             <YYYYMMDDhhmmss> or <YYYY-MM-DDThh:mm:ss+hhmm>.
#           * The latter format is set by -l option.
# Retuen  : Return 0 only when timestamps of all files
#           were able to be gotten.
#
# Written by Shell-Shoccar Japan (@shellshoccarjpn) on 2017-05-08
#
# This is a public-domain software (CC0). It means that all of the
# people can use this for any purposes with no restrictions at all.
# By the way, We are fed up with the side effects which are brought
# about by the major licenses.
#
####################################################################*/


/*####################################################################
# Initial Configuration
####################################################################*/

/*=== Initial Setting ==============================================*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

char* pszMypath;

/*=== Define the functions for printing usage ======================*/
void print_usage_and_exit(void) {
  int  i;
  int  iPos = 0;
  for (i=0; *(pszMypath+i)!='\0'; i++) {
    if (*(pszMypath+i)=='/') {iPos=i+1;}
  }
  fprintf(stderr, "Usage   : %s [options] <file> [file ...]\n",pszMypath+iPos);
  fprintf(stderr, "Options : -l ... Prints the timestamps in ISO8601 format\n");
  fprintf(stderr, "          -- ... Finishes parsing arguments as options \n");
  fprintf(stderr, "Output  : * Print the following 4 fields by each file\n");
  fprintf(stderr, "            <atime> <mtime> <ctime> <filename>\n");
  fprintf(stderr, "          * The format of each time is either \n");
  fprintf(stderr, "            <YYYYMMDDhhmmss> or\n");
  fprintf(stderr, "            <YYYY-MM-DDThh:mm:ss+hhmm>.\n");
  fprintf(stderr, "          * The latter format is set by -l option.\n");
  fprintf(stderr, "Retuen  : Return 0 only when timestamps of all files\n");
  fprintf(stderr, "          were able to be gotten. \n");
  fprintf(stderr, "Version : 2017-05-08 02:15:32 JST\n");
  fprintf(stderr, "          (POSIX C language)\n");
  exit(1);
}


/*####################################################################
# Main
####################################################################*/

main(int argc, char *argv[]){

  /*=== Initial Setting ============================================*/
  struct stat stFileinfo;
  struct tm*  pstTm;
  char        szAtim[25], szMtim[25], szCtim[25], szFmt[25], szDummy[25];
  int         bLongfmt;    /* Long option switch */
  int         i;           /* It means the argument position */
  int         iNerror = 0; /* The number of error to get timestamps */

  pszMypath = argv[0];

  /*=== Parse options ==============================================*/
  /*--- print the usage if no argument has given -------------------*/
  if (argc <= 1) { print_usage_and_exit(); }
  /*--- initialize option parameters -------------------------------*/
  bLongfmt = 0;
  /*--- get them ---------------------------------------------------*/
  for (i=1; i<argc; i++) {
    if        (strcmp (argv[i], "--"  )==0) {
      i++;
      break;
    } else if (strcmp (argv[i], "-l"  )==0) {
      bLongfmt = 1;
    } else if (strncmp(argv[i], "-" ,1)==0) {
      fprintf(stderr, "!(%s)(%d)\n",argv[i],strncmp(argv[i], "-" ,1));
      print_usage_and_exit();
    } else {
      break;
    }
  }

  /*=== Swtich to the long format mode if -l option has been set ===*/
  if (bLongfmt) {
    strcpy(szFmt  ,"%Y-%m-%dT%H:%M:%S%z");
    strcpy(szDummy,"%-24s %-24s %-24s " );
  } else {
    strcpy(szFmt  ,"%Y%m%d%H%M%S"      );
    strcpy(szDummy,"%-14s %-14s %-14s ");
  }

  /*=== Main loop ==================================================*/
  for (   ; i<argc; i++) {
    if (stat(argv[i],&stFileinfo)==0) {
      pstTm = localtime(&stFileinfo.st_atime); /* Not use st_*tim.tv_sec for */
      strftime(szAtim, 25, szFmt, pstTm);      /* compatibility with earlier */
      pstTm = localtime(&stFileinfo.st_mtime); /* versions */
      strftime(szMtim, 25, szFmt, pstTm);
      pstTm = localtime(&stFileinfo.st_ctime);
      strftime(szCtim, 25, szFmt, pstTm);
      printf("%s %s %s ",szAtim,szMtim,szCtim);
    } else {
      iNerror++;
       printf(szDummy, "-", "-", "-");
    }
    printf("%s\n",argv[i]);
  }

  /*=== Finish =====================================================*/
  exit((iNerror==0)?0:1);
}
