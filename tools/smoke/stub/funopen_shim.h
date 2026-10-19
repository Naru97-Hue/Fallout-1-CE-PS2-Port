// glibc has fopencookie instead of BSD funopen: map one onto the other.
#pragma once
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include <stdio.h>
#include <sys/types.h>
typedef long fpos_t_shim;
struct FunopenShim {
    void* cookie;
    int (*readfn)(void*, char*, int);
    fpos_t (*seekfn)(void*, fpos_t, int);
    int (*closefn)(void*);
};
FILE* funopen_shim(void* cookie, int (*readfn)(void*, char*, int), void*, long (*seekfn)(void*, long, int), int (*closefn)(void*));
#define funopen(c, r, w, s, cl) funopen_shim((void*)(c), r, w, s, cl)
#define fpos_t long
