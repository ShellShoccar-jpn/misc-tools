#! /bin/sh

LF=$(printf '\\\n_');LF=${LF%_}
sed 's/\(\\[0-7][0-7][0-7]\)/'"$LF"'\\\1'"$LF"'/g' |
grep -v '^$'                                       |
while read line; do
  echo "_$line" | grep '^_\\[0-7][0-7][0-7]$' >/dev/null 2>&1
  if [ $? -eq 0 ]; then
    printf "$line"
  else
    echo -n "_$line" | sed 's/^_//'
  fi;
done