#! /bin/sh

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
{
  i=bhex2int[substr($0,3,2)]*256+bhex2int[substr($0,5,2)];           \
}
'