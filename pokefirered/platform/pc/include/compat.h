#ifndef FIRERED_PC_COMPAT_H
#define FIRERED_PC_COMPAT_H
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#define DBGPRINTF(...) fprintf(stderr, __VA_ARGS__)
#define SAFE_MOD(a, b) ((b) ? (a) % (b) : 0)
#endif
