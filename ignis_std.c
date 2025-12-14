#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int32_t Cwrite(const char *buf, ...)
{
  va_list args;
  va_start(args, buf);
  int32_t result = vprintf(buf, args);
  va_end(args);
  return result;
}

int32_t Cstrcmp(const char *str1, const char *str2)
{
  return strcmp(str1, str2);
}
int32_t CwriteLn(const char *buf, ...)
{
  va_list args;
  va_start(args, buf);
  int32_t result = vprintf(buf, args);
  va_end(args);
  return result + Cwrite("\n");
}

int32_t vwriteLn(const char *fmt, va_list args)
{
  int32_t result = vprintf(fmt, args);
  return result + Cwrite("\n");
}

int32_t writeLn(const char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  int32_t result = vwriteLn(fmt, args);
  va_end(args);
  return result;
}

char *CreadLn()
{
  size_t size = 256;
  char *buffer = (char *)malloc(size);
  if (!buffer)
    return NULL;

  if (fgets(buffer, size, stdin) != NULL)
  {
    size_t len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n')
    {
      buffer[len - 1] = '\0';
    }
    return buffer;
  }

  free(buffer);
  return NULL;
}

int32_t CstringToInt(const char *str)
{
  return (int32_t)strtol(str, NULL, 10);
}

void __IGN_std_io_internal_print_int(int32_t val) { printf("%d\n", val); }

const char *string_concat(const char *str1, const char *str2)
{
  if (!str1)
    str1 = "";
  if (!str2)
    str2 = "";

  size_t len1 = strlen(str1);
  size_t len2 = strlen(str2);
  char *result = (char *)malloc(len1 + len2 + 1);

  if (!result)
    return NULL;

  strcpy(result, str1);
  strcpy(result + len1, str2);
  result[len1 + len2] = '\0';

  return result;
}
