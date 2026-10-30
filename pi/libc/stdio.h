/* Minimal stdio for the Omega bare metal build (implemented in pi/libc.cpp on top of Circle) */
#ifndef OMEGA_STDIO_H
#define OMEGA_STDIO_H
#include <stddef.h>
#include <stdarg.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct OMEGA_FILE FILE;
int printf(const char* fmt, ...) __attribute__((format(printf,1,2)));
int sprintf(char* buf, const char* fmt, ...) __attribute__((format(printf,2,3)));
int snprintf(char* buf, size_t n, const char* fmt, ...) __attribute__((format(printf,3,4)));
int vsnprintf(char* buf, size_t n, const char* fmt, va_list ap);
int puts(const char* s);
int putchar(int c);
int fflush(FILE* f);
extern FILE* stdout;
extern FILE* stderr;
#define fprintf(f, ...) printf(__VA_ARGS__)
#ifdef __cplusplus
}
#endif
#endif
