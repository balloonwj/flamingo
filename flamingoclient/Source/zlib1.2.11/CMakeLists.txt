cmake_minimum_required(VERSION 3.0)
project(zlib)
include_directories(.)
SET(ZLIB_SOURCE_FILE adler32.c
CMakeLists.txt
compress.c
crc32.c
crc32.h
deflate.c
deflate.h
gzclose.c
gzguts.h
gzlib.c
gzread.c
gzwrite.c
infback.c
inffast.c
inffast.h
inffixed.h
inflate.c
inflate.h
inftrees.c
inftrees.h
trees.c
trees.h
uncompr.c
zconf.h
zlib.h
zlibdemo.c
zutil.c
zutil.h)
add_library(zlib ${ZLIB_SOURCE_FILE})