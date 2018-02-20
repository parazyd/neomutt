/**
 * @file
 * Logging Dispatcher
 *
 * @authors
 * Copyright (C) 2018 Richard Russon <rich@flatcap.org>
 *
 * @copyright
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @page logging Logging Dispatcher
 *
 * Logging Dispatcher
 *
 * | Data                | Description
 * | :------------------ | :--------------------------------------------------
 * | #MuttLogger         | The log dispatcher
 *
 * | File                     | Description
 * | :----------------------- | :-----------------------------------------------
 * | log_disp_file()          | Save a log line to a file
 * | log_disp_queue()         | Save a log line to an internal queue
 * | log_disp_stderr()        | Save a log line to stderr
 * | log_file_close()         | Close the log file
 * | log_file_open()          | Start logging to a file
 * | log_file_set_filename()  | Set the filename for the log
 * | log_file_set_level()     | Set the logging level
 * | log_file_set_version()   | Set the program's version number
 * | log_queue_add()          | Add a LogLine to the queue
 * | log_queue_empty()        | Free the contents of the queue
 * | log_queue_flush()        | Replay the log queue
 * | log_queue_save()         | Save the contents of the queue to a temporary file
 * | log_queue_set_max_size() | Set a upper limit for the queue length
 */

#include "config.h"
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "logging.h"
#include "file.h"
#include "memory.h"
#include "message.h"
#include "queue.h"
#include "string2.h"

const char *LevelAbbr = "PEWM12345"; /**< Abbreviations of logging level names */

/**
 * MuttLogger - The log dispatcher
 *
 * This function pointer controls where log messages are redirected.
 */
log_dispatcher_t MuttLogger = log_disp_stderr;

FILE *LogFileFP = NULL;      /**< Log file handle */
char *LogFileName = NULL;    /**< Log file name */
int LogFileLevel = 0;        /**< Log file level */
char *LogFileVersion = NULL; /**< Program version */

/**
 * LogQueue - In-memory list of log lines
 */
struct LogList LogQueue = STAILQ_HEAD_INITIALIZER(LogQueue);
int LogQueueCount = 0; /**< Number of entries currently in the log queue */
int LogQueueMax = 0;   /**< Maximum number of entries in the log queue */

/**
 * timestamp - Create a YYYY-MM-DD HH:MM:SS timestamp
 * @param stamp Unix time
 *
 * If stamp is 0, then the current time will be used.
 *
 * @note This function returns a pointer to a static buffer.
 *       Do not free it.
 */
static const char *timestamp(time_t stamp)
{
  static char buf[23] = "";
  static time_t last = 0;

  if (stamp == 0)
    stamp = time(NULL);

  if (stamp != last)
  {
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&stamp));
    last = stamp;
  }

  return buf;
}

/**
 * log_file_set_filename - Set the filename for the log
 * @param file Name to use
 * @retval  0 Success, file opened
 * @retval -1 Error, see errno
 */
int log_file_set_filename(const char *file)
{
  /* also handles both being NULL */
  if (mutt_str_strcmp(LogFileName, file) == 0)
    return 0;

  mutt_str_replace(&LogFileName, file);

  /* not running yet... */
  if (!LogFileFP)
    return -1;

  log_file_open(true);
  return 0;
}

/**
 * log_file_set_level - Set the logging level
 * @param level Logging level
 * @retval  0 Success
 * @retval -1 Error, level is out of range
 *
 * The level can be between 0 and LL_DEBUG5.
 */
int log_file_set_level(int level)
{
  if ((level < 0) || (level > 5))
    return -1;

  if (level == LogFileLevel)
    return 0;

  LogFileLevel = level;

  if (level == 0)
    log_file_close(true);
  else if (LogFileFP)
    mutt_message(_("Logging at level %d"), LogFileLevel);
  else
    log_file_open(true);

  return 0;
}

/**
 * log_file_set_version - Set the program's version number
 * @param version Version number
 *
 * The string will be appended directly to 'NeoMutt', so it should begin with a
 * hyphen.
 */
void log_file_set_version(const char *version)
{
  mutt_str_replace(&LogFileVersion, version);
}

/**
 * log_file_close - Close the log file
 * @param verbose If true, then log the event
 */
void log_file_close(bool verbose)
{
  if (!LogFileFP)
    return;

  fprintf(LogFileFP, "[%s] Closing log.\n", timestamp(0));
  mutt_file_fclose(&LogFileFP);
  if (verbose)
    mutt_message(_("Closed log file: %s"), LogFileName);
}

/**
 * log_file_open - Start logging to a file
 * @param verbose If true, then log the event
 * @retval  0 Success
 * @retval -1 Error, see errno
 *
 * Before opening a log file, call log_file_set_version(), log_file_set_level()
 * and log_file_set_filename().
 */
int log_file_open(bool verbose)
{
  if (!LogFileName)
    return -1;

  if (LogFileFP)
    log_file_close(false);

  LogFileFP = mutt_file_fopen(LogFileName, "a+");
  if (!LogFileFP)
    return -1;

  fprintf(LogFileFP, "[%s] NeoMutt%s debugging at level %d\n", timestamp(0),
          NONULL(LogFileVersion), LogFileLevel);
  if (verbose)
    mutt_message(_("Debugging at level %d to file '%s'"), LogFileLevel, LogFileName);
  return 0;
}

/**
 * log_disp_file - Save a log line to a file
 * @param stamp    Unix time (optional)
 * @param file     Source file (UNUSED)
 * @param line     Source line (UNUSED)
 * @param function Source function
 * @param level    Logging level, e.g. #LL_WARNING
 * @param ...      Format string and parameters, like printf()
 * @retval -1 Error
 * @retval  0 Success, filtered
 * @retval >0 Success, number of characters written
 *
 * This log dispatcher saves a line of text to a file.  The format is:
 * * `[TIMESTAMP]<LEVEL> FUNCTION() FORMATTED-MESSAGE`
 *
 * The caller must first set #LogFileName and #LogFileLevel, then call
 * log_file_open().  Any logging above #LogFileLevel will be ignored.
 *
 * If stamp is 0, then the current time will be used.
 */
int log_disp_file(time_t stamp, const char *file, int line,
                  const char *function, int level, ...)
{
  if (!LogFileFP || (level < LL_PERROR) || (level > LogFileLevel))
    return 0;

  int ret = 0;
  int err = errno;

  if (!function)
    function = "UNKNOWN";

  ret += fprintf(LogFileFP, "[%s]<%c> %s() ", timestamp(stamp),
                 LevelAbbr[level + 3], function);

  va_list ap;
  va_start(ap, level);
  const char *fmt = va_arg(ap, const char *);
  ret = vfprintf(LogFileFP, fmt, ap);
  va_end(ap);

  if (level == LL_PERROR)
  {
    fprintf(LogFileFP, ": %s\n", strerror(err));
  }
  else if (level <= 0)
  {
    fputs("\n", LogFileFP);
    ret++;
  }

  return ret;
}

/**
 * log_queue_add - Add a LogLine to the queue
 * @param ll LogLine to add
 * @retval num Number of entries in the queue
 *
 * If #LogQueueMax is non-zero, the queue will be limited to this many items.
 */
int log_queue_add(struct LogLine *ll)
{
  STAILQ_INSERT_TAIL(&LogQueue, ll, entries);

  if ((LogQueueMax > 0) && (LogQueueCount >= LogQueueMax))
  {
    STAILQ_REMOVE_HEAD(&LogQueue, entries);
  }
  else
  {
    LogQueueCount++;
  }
  return LogQueueCount;
}

/**
 * log_queue_set_max_size - Set a upper limit for the queue length
 * @param size New maximum queue length
 *
 * @note size of 0 means unlimited
 */
void log_queue_set_max_size(int size)
{
  if (size < 0)
    size = 0;
  LogQueueMax = size;
}

/**
 * log_queue_empty - Free the contents of the queue
 *
 * Free any log lines in the queue.
 */
void log_queue_empty(void)
{
  struct LogLine *ll = NULL;
  struct LogLine *tmp = NULL;

  STAILQ_FOREACH_SAFE(ll, &LogQueue, entries, tmp)
  {
    STAILQ_REMOVE(&LogQueue, ll, LogLine, entries);
    FREE(&ll->message);
    FREE(&ll);
  }

  LogQueueCount = 0;
}

/**
 * log_queue_flush - Replay the log queue
 * @param disp Log dispatcher
 *
 * Pass all of the log entries in the queue to the log dispatcher provided.
 * The queue will be emptied afterwards.
 */
void log_queue_flush(log_dispatcher_t disp)
{
  struct LogLine *ll = NULL;
  STAILQ_FOREACH(ll, &LogQueue, entries)
  {
    disp(ll->time, ll->file, ll->line, ll->function, ll->level, "%s", ll->message);
  }

  log_queue_empty();
}

/**
 * log_queue_save - Save the contents of the queue to a temporary file
 * @param fp Open file handle
 * @retval num Number of lines written to the file
 *
 * The queue is written to a temporary file.  The format is:
 * * `[HH:MM:SS]<LEVEL> FORMATTED-MESSAGE`
 *
 * @note The caller should free the returned string and delete the file.
 */
int log_queue_save(FILE *fp)
{
  if (!fp)
    return 0;

  char buf[32];
  int count = 0;
  struct LogLine *ll = NULL;
  STAILQ_FOREACH(ll, &LogQueue, entries)
  {
    strftime(buf, sizeof(buf), "%H:%M:%S", localtime(&ll->time));
    fprintf(fp, "[%s]<%c> %s", buf, LevelAbbr[ll->level + 3], ll->message);
    if (ll->level <= 0)
      fputs("\n", fp);
    count++;
  }

  return count;
}

/**
 * log_disp_queue - Save a log line to an internal queue
 * @param stamp    Unix time
 * @param file     Source file
 * @param line     Source line
 * @param function Source function
 * @param level    Logging level, e.g. #LL_WARNING
 * @param ...      Format string and parameters, like printf()
 * @retval >0 Success, number of characters written
 *
 * This log dispatcher saves a line of text to a queue.
 * The format string and parameters are expanded and the other parameters are
 * stored as they are.
 *
 * @sa log_queue_set_max_size(), log_queue_flush(), log_queue_empty()
 *
 * @warning Log lines are limited to #LONG_STRING bytes.
 */
int log_disp_queue(time_t stamp, const char *file, int line,
                   const char *function, int level, ...)
{
  char buf[LONG_STRING] = "";
  int err = errno;

  va_list ap;
  va_start(ap, level);
  const char *fmt = va_arg(ap, const char *);
  int ret = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  if (level == LL_PERROR)
  {
    ret += snprintf(buf + ret, sizeof(buf) - ret, ": %s", strerror(err));
    level = LL_ERROR;
  }

  struct LogLine *ll = mutt_mem_calloc(1, sizeof(*ll));
  ll->time = stamp ? stamp : time(NULL);
  ll->file = file;
  ll->line = line;
  ll->function = function;
  ll->level = level;
  ll->message = mutt_str_strdup(buf);

  log_queue_add(ll);

  return ret;
}

/**
 * log_disp_stderr - Save a log line to stderr
 * @param stamp    Unix time (optional)
 * @param file     Source file (UNUSED)
 * @param line     Source line (UNUSED)
 * @param function Source function
 * @param level    Logging level, e.g. #LL_WARNING
 * @param ...      Format string and parameters, like printf()
 * @retval -1 Error
 * @retval  0 Success, filtered
 * @retval >0 Success, number of characters written
 *
 * This log dispatcher saves a line of text to stderr.  The format is:
 * * `[TIMESTAMP]<LEVEL> FUNCTION() FORMATTED-MESSAGE`
 *
 * @note The output will be coloured using ANSI escape sequences,
 *       unless the output is redirected.
 */
int log_disp_stderr(time_t stamp, const char *file, int line,
                    const char *function, int level, ...)
{
  if ((level < LL_PERROR) || (level > LogFileLevel))
    return 0;

  char buf[LONG_STRING];

  va_list ap;
  va_start(ap, level);
  const char *fmt = va_arg(ap, const char *);
  int ret = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  log_disp_file(stamp, file, line, function, level, "%s", buf);

  int err = errno;
  int colour = 0;
  bool tty = (isatty(fileno(stderr)) == 1);

  if (tty)
  {
    switch (level)
    {
      case LL_PERROR:
      case LL_ERROR:
        colour = 31;
        break;
      case LL_WARNING:
        colour = 33;
        break;
      case LL_MESSAGE:
        // colour = 36;
        break;
      case LL_DEBUG1:
      case LL_DEBUG2:
      case LL_DEBUG3:
      case LL_DEBUG4:
      case LL_DEBUG5:
        break;
    }
  }

  if (colour > 0)
    ret += fprintf(stderr, "\033[1;%dm", colour);

  fputs(buf, stderr);

  if (level == LL_PERROR)
    ret += fprintf(stderr, ": %s", strerror(err));

  if (colour > 0)
    ret += fprintf(stderr, "\033[0m");

  if (level < 1)
    ret += fprintf(stderr, "\n");

  return ret;
}