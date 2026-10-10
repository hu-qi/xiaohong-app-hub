/*
MIT License

Copyright (c) 2026 Shenzhen Open Source Co-Creation Technology Co., Ltd. (AtomGit)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "log.h"

static const char *level_strings[] = {"F", "E", "W", "I", "D"};

static int log_level = LOG_INFO;
void log_set_level(int level)
{
    log_level = level;
}

void log_log(int level, const char *file, int line, const char *fmt, ...)
{
  char tmpFmt[256] = {0};
  if (level <= log_level) {
      va_list vargs;
      va_start(vargs, fmt);
      vsnprintf_s(tmpFmt, sizeof(tmpFmt), sizeof(tmpFmt) - 1, fmt, vargs);
      va_end(vargs);
      printf("[%s]%s", level_strings[level], tmpFmt);
    }
}
