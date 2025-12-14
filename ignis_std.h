#ifndef IGNIS_STD_H
#define IGNIS_STD_H

#include <stdint.h>
#include <stdarg.h>

// Standard library functions
int32_t CwriteLn(const char *buf, ...);
int32_t Cwrite(const char *buf, ...);
const char *CreadLn();

// Varargs helper functions
int32_t writeLn(const char *fmt, ...);
int32_t vwriteLn(const char *fmt, va_list args);
int32_t Cstrcmp(const char *str1, const char *str2);
int32_t CstringToInt(const char *str);
// String concatenation
const char *string_concat(const char *str1, const char *str2);

#endif // IGNIS_STD_H
