#!/bin/bash

c_files_in=`find cli daemon gtk libtransmission utils -name '*.[ch]'`
c_files_3rdparty="
libtransmission/ConvertUTF.c
libtransmission/ConvertUTF.h
libtransmission/jsonsl.c
libtransmission/jsonsl.h
libtransmission/wildmat.c"
c_files=`echo "${c_files_in} ${c_files_3rdparty}" | sort | uniq -u`
cxx_files=`find qt \( -name '*.cc' -o -name '*.h' \)`

uncrustify -c uncrustify.cfg --check -l C   ${c_files}   | grep FAIL
uncrustify -c uncrustify.cfg --check -l CPP ${cxx_files} | grep FAIL
