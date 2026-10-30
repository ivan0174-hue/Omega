#ifndef OMEGA_STDLIB_H
#define OMEGA_STDLIB_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void* malloc(size_t size);
void* calloc(size_t n, size_t size);
void* realloc(void* p, size_t size);
void  free(void* p);
int   abs(int x);
int   atoi(const char* s);
void  exit(int code) __attribute__((noreturn));
void  qsort(void* base, size_t n, size_t size, int (*cmp)(const void*, const void*));
#ifdef __cplusplus
}
#endif
#endif
