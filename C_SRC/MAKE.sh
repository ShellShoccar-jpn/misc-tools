#!/bin/sh
 
######################################################################
#
# MAKE.SH - Compile All C-progs in the Directory I am in
#
# USAGE  : MAKE.sh [-u]
# Options: -u ... Put the compiled executable files onto the upper
#                 directory
# RET    : $?=0 (when all of the options are valid)
#
# Written by Shell-Shoccar Japan (@shellshoccarjpn) on 2019-05-14
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
	Usage   : ${0##*/} [-u]
	Options : -u ... Put the compiled executable files onto the upper
	                 directory
	Version : 2019-05-14 03:45:05 JST
	USAGE
  exit 1
}
error_exit() {
  ${2+:} false && echo "${0##*/}: $2" 1>&2
  exit $1
}

# === Define the other parameters ====================================
COMPILERS='clang gcc xlc cc c99 tcc'
ACK=$(printf '\006')
NAK=$(printf '\025')


######################################################################
# Parse arguments
######################################################################

# === Initialize option parameters ===================================
upperdir=0

# === Parse arguments ================================================
[ $# -gt 1 ] && print_usage_and_exit
for arg in ${1+"$@"}; do
  case "$arg" in
    '-u') upperdir=1          ;;
       *) print_usage_and_exit;;
  esac
done


######################################################################
# Main
######################################################################

# === Get my directory path ==========================================
Homedir=$(d=${0%/*}/; [ "_$d" = "_$0/" ] && d='./'; cd "$d"; pwd)

# === Get the output directory path ==================================
case $upperdir in
  0) Dir_aout=. ;;
  *) Dir_aout=..;;
esac

# === Choose the compiler command ====================================
CC=''
for cc in $COMPILERS; do
  type $cc >/dev/null 2>&1 && { CC=$cc; break; }
done
case "$CC" in
  '') error_exit 1 'No compiler found'          ;;
   *) echo "I will use \"$CC\" as compiler" 1>&2;;
esac

# === Complie all c-progs ============================================
cd "$Homedir" || error_exit 1 "$Homedir: Can't move to the directory"
find . -name '[0-9A-Za-z]*.c' |
while IFS= read -r File_src; do
  # --- set filenames ------------------------------------------------
  file_aout=${File_src##*/}; file_aout=${file_aout%.c}
  File_src_e=$(printf '%s\n' "$File_src"                |
               sed "$(printf 's/[ \t\\\047"]/\\\\&/g')" )
  File_aout_e=$(printf '%s\n' "$Dir_aout/$file_aout"     |
                sed "$(printf 's/[ \t\\\047"]/\\\\&/g')" )
  # --- export variables ---------------------------------------------
  export CC
  export File_src_e
  export File_aout_e
  # --- generate one liner to compile the file -----------------------
  s=$(cat "$File_src"                                                    |
      sed -n '1,/^##*\*\/$/p'                                            |
      sed -n '/^# *How to compile *: */{s/^# *How to compile *: *//;p;}' |
      head -n 1                                                          |
      awk '{                                                             #
             line="";                                                    #
             for (i=1; i<=NF; i++) {                                     #
               if      ($i=="cc" && i==1 ) {s=ENVIRON["CC"         ];}   #
               else if ($i=="__CMDNAME__") {s=ENVIRON["File_aout_e"];}   #
               else if ($i=="__SRCNAME__") {s=ENVIRON["File_src_e" ];}   #
               else                        {s=$i;                    }   #
               line = line " " s;                                        #
             }                                                           #
             print substr(line,2);                                       #
           }'                                                            )
  # --- Compile it ---------------------------------------------------
  printf '%s\n' "$s" 1>&2
  eval $s
done


######################################################################
# Finish
######################################################################

exit 0
