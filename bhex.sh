#! /bin/sh

LF=$(printf '\\\n_');LF=${LF%_}
cat /dev/urandom                                                     |
od -A n -t x2                                                        |
sed 's/\([0-9a-f]\{4\}\)/'"$LF"'\\u\1/g'                             |
grep -v '^[[:space:]]*$'                                             |
head -n 1000000                                                      |
awk '                                                                \
BEGIN {                                                              \
  OFS="";                                                            \
  for(i=255;i>=0;i--) {                                              \
    s=sprintf("%c",i);                                               \
    bhex2chr[sprintf("%02x",i)]=s;                                   \
    bhex2chr[sprintf("%02X",i)]=s;                                   \
    bhex2int[sprintf("%02x",i)]=i;                                   \
    bhex2int[sprintf("%02X",i)]=i;                                   \
  }                                                                  \
  for(i=65535;i>=0;i--) {           # もし高速なら                   \
    whex2int[sprintf("%02x",i)]=i;  # bhex2intの代わりに             \
    whex2int[sprintf("%02X",i)]=i;  # こちらの連想配列を用いる       \
  }                                                                  \
}                                                                    \
{                                                                    \
  i=bhex2int[substr($0,3,2)]*256+bhex2int[substr($0,5,2)];           \
}
'