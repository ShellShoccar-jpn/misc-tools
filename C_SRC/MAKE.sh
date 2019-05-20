#!/bin/sh
 
######################################################################
#
# MAKE.SH - Compile All C-progs in the Directory I am in
#
# USAGE   : MAKE.sh [options]
# Options : -u ......... Put the compiled executable files onto the
#                        upper directory
#           -d dir ..... Set the directory for compiled execute files
#                        to "dir"
#                        Howerer, if you set it as a relative path,
#                        the base directory of the relative path is
#                        regarded as the directory which MAKE.sh is in.
#           -c compiler  Set the compiler command to "compiler"
# Ret     : $?=0 (when all of the options are valid)
#
# Written by Shell-Shoccar Japan (@shellshoccarjpn) on 2019-05-21
#
# This is a public-domain software (CC0). It means that all of the
# people can use this for any purposes with no restrictions at all.
# By the way, We are fed up with the side effects which are brought
# about by the major licenses.
#
######################################################################


######################################################################
# Initial Configuration
######################################################################

# === Initialize shell environment ===================================
set -u
umask 0022
export LC_ALL=C
type command >/dev/null 2>&1 && type getconf >/dev/null 2>&1 &&
export PATH="$(command -p getconf PATH)${PATH+:}${PATH-}"
export POSIXLY_CORRECT=1 # to make Linux comply with POSIX
export UNIX_STD=2003     # to make HP-UX comply with POSIX

# === Define the functions for printing usage and error message ======
print_usage_and_exit () {
  cat <<-USAGE 1>&2
	Usage   : ${0##*/} [options]
	Options : -u ......... Put the compiled executable files onto the
	                       upper directory
	          -d dir ..... Set the directory for compiled execute files
	                       to "dir"
	                       Howerer, if you set it as a relative path,
	                       the base directory of the relative path is
	                       regarded as the directory which MAKE.sh is in.
	          -c compiler  Set the compiler command to "compiler"
	Version : 2019-05-21 03:50:55 JST
	USAGE
  exit 1
}
error_exit() {
  ${2+:} false && echo "${0##*/}: $2" 1>&2
  exit $1
}

# === Get my directory path ==========================================
Homedir=$(d=${0%/*}/; [ "_$d" = "_$0/" ] && d='./'; cd "$d"; pwd)

# === Define the other parameters ====================================
COMPILERS='clang gcc xlc cc c99 tcc'
ACK=$(printf '\006')
NAK=$(printf '\025')
EE=$(printf 's/[]\t -\044\046-\052\073\074\076\077\134\140\173-\176[]/\\\\&/g')



######################################################################
# Parse arguments
######################################################################

# === Get the options ================================================
# --- initialize option parameters -----------------------------------
optu=0
CMD_cc=''
Dir_bin=''
#
# --- get them -------------------------------------------------------
optmode=''
while [ $# -gt 0 ]; do
  case $# in 0) break;; esac
  case "$optmode" in
    '') case "$1" in
          -[cdu]*)      s=$(printf '%s\n' "${1#-}"                           |
                            awk '{c = "_"; d = "_"; u = "_"; err = 0;        #
                                  for (i=1;i<=length($0);i++) {              #
                                    s = substr($0,i,1);                      #
                                    if      (s == "c") {c  ="c";i++;break;}  #
                                    else if (s == "d") {d  ="d";i++;break;}  #
                                    else if (s == "u") {u  ="u";          }  #
                                    else               {err= 1 ;          }  #
                                  }                                          #
                                  arg=substr($0,i);                          #
                                  printf("%s%s%s%s %s",c,d,u,err,arg);     }')
                        optarg=${s#* }
                        case "${s%% *}" in *1*) print_usage_and_exit;; esac
                        case "${s%% *}" in *c*) optmode='c'         ;; esac
                        case "${s%% *}" in *d*) optmode='d'         ;; esac
                        case "${s%% *}" in *u*) optu=1;             ;; esac
                        case "$optarg" in
                          '') shift; continue;;
                           *) s=$optarg      ;;                        esac ;;
          --upperdir)   optu=1     ; shift; continue                        ;;
          --compiler=*) optmode='c'; s=${1#--compiler=}                     ;;
          --bindir=*)   optmode='d'; s=${1#--bindir=}                       ;;
          -*)           print_usage_and_exit                                ;;
        esac                                                                  ;;
    *)  s=$1                                                                  ;;
  esac
  case "$optmode" in
    c) case "$s" in
         */*) ([ -f "$s" ] && [ -x "$s" ]) || {
                error_exit 1 'Invalid compiler by -c,--compiler option'
              }                                                        ;;
           *) type "$s" >/dev/null 2>&1    || {
                error_exit 1 'Invalid compiler by -c,--compiler option'
              }                                                        ;;
       esac
       CMD_cc=$s
       optmode=''; shift; continue                                            ;;
    d) case "$s" in
         /*) ([ -d "$s"          ] && [ -w "$s"          ]) || {
               error_exit 1 'Invalid directory by -d,--bindir option'
             }                                                         ;;
          *) ([ -d "$Homedir/$s" ] && [ -w "$Homedir/$s" ]) || {
               error_exit 1 'Invalid directory by -d,--bindir option'
             }                                                         ;;
       esac
       Dir_bin=${s%/}
       optmode=''; shift; continue                                            ;;
  esac
break; done



######################################################################
# Main
######################################################################

# === Get the output directory path ==================================
case $optu in
  0) Dir_aout=. ;;
  *) Dir_aout=..;;
esac
case "$Dir_bin" in
  '') :                    ;;
   *) Dir_aout=${Dir_bin%/};;
esac

# === Choose the compiler command ====================================
case "$CMD_cc" in '')
  for cc in $COMPILERS; do
    type $cc >/dev/null 2>&1 && { CMD_cc=$cc; break; }
  done
;; esac
case "$CMD_cc" in
  '') s='No compiler found. Set an available compiler by -c,--compiler option'
      error_exit 1 "$s"                                                       ;;
   *) echo "I will use \"$CMD_cc\" as compiler" 1>&2                          ;;
esac

# === Complie all c-progs ============================================
cd "$Homedir" || error_exit 1 "$Homedir: Can't move to the directory"
find . -name '[0-9A-Za-z]*.c' |
sed 's/^\.\///'               |
while IFS= read -r File_src; do
  # --- set filenames ------------------------------------------------
  file_aout=${File_src%.c}
  File_src_e=$( printf '%s\n' "$File_src"            | sed "$EE")
  File_aout_e=$(printf '%s\n' "$Dir_aout/$file_aout" | sed "$EE")
  # --- export variables ---------------------------------------------
  export CMD_cc
  export File_src_e
  export File_aout_e
  # --- generate one liner to compile the file -----------------------
  s=$(cat "$File_src"                                                    |
      sed -n '1,/^##*\*\/$/p'                                            |
      sed -n '/^# *How to compile *: */{s/^# *How to compile *: *//;p;}' |
      awk '{                                                             #
             line="";                                                    #
             for (i=1; i<=NF; i++) {                                     #
               if      ($i=="cc" && i==1 ) {s=ENVIRON["CMD_cc"     ];}   #
               else if ($i=="__CMDNAME__") {s=ENVIRON["File_aout_e"];}   #
               else if ($i=="__SRCNAME__") {s=ENVIRON["File_src_e" ];}   #
               else                        {s=$i;                    }   #
               line = line " " s;                                        #
             }                                                           #
             print substr(line,2);                                       #
           }'                                                            )
  # --- Try to compile it --------------------------------------------
  printf '%s\n' "$s$ACK" |
  while IFS= read -r line; do
    if [ "${line%$ACK}" != "$line" ]; then
      line=${line%$ACK}
      last=1
    else
      last=0
    fi
    eval echo $line 1>&2
    eval $line
    [ $? -eq 0 ] && { echo '==> OK' 1>&2; break; }
    case $last in
      1) echo '==> FAILED!! Give up compiling the source file.' 1>&2;;
      0) echo '==> Failed! Retry compiling in another way.'     1>&2;;
    esac
  done
done


######################################################################
# Finish
######################################################################

exit 0
