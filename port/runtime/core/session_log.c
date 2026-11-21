/* The session log (session_log.h). */
#include "session_log.h"
#include "../../host/app/instance.h"

#include <stdarg.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include "../../host/app/sync.h"
#ifndef _WIN32
#include <sys/stat.h>
#endif

static FILE *file;
static SRWLOCK lock = SRWLOCK_INIT;
static LARGE_INTEGER started, frequency;
static volatile LONG frame;
static u32 flushed_frame;
static u32 marks;
static volatile LONG wanted;   /* the settings ask for it */
static int opened_for_setting; /* this log was opened because they did */

static void open_now(void)
{
    char path[160];
    struct tm now;
    time_t stamp = time(NULL);
#ifdef _WIN32
    if (localtime_s(&now, &stamp)) return;
#else
    if (!localtime_r(&stamp, &now)) return;
#endif
    /* saves/logs -- a further player's copy on the PC: saves/player<N>/logs */
    const char *folder = khdays_host_user_folder();
#ifdef _WIN32
    CreateDirectoryA("saves", NULL);
    CreateDirectoryA(folder, NULL);
    snprintf(path, sizeof(path), "%s/logs", folder);
    CreateDirectoryA(path, NULL);
#else
    mkdir("saves", 0700);
    mkdir(folder, 0700);
    snprintf(path, sizeof(path), "%s/logs", folder);
    mkdir(path, 0700);
#endif
    char filename[48], timestamp[32];
    if (!strftime(filename, sizeof(filename), "session-%Y%m%d-%H%M%S.log", &now) ||
        !strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &now)) return;
    snprintf(path, sizeof(path), "%s/logs/%s", folder, filename);
    file = fopen(path, "w");
    if (file == NULL) {
        fprintf(stderr, "log: cannot write %s\n", path);
        return;
    }
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&started);
    fprintf(stderr, "log: this session's log is %s (F12 marks it)\n", path);
    fprintf(file, "khdays-native session log, %s\n", timestamp);
    fflush(file);
}

void khdays_log_open(void)
{
    static int registered;
    const char *setting = getenv("KHDAYS_LOG");
    int on = InterlockedCompareExchange(&wanted, 0, 0) != 0;
#if defined(_DEBUG)
    on = on || setting == NULL || strcmp(setting, "0") != 0;
#else
    on = on || (setting != NULL && strcmp(setting, "1") == 0);
#endif
    if (!registered) {
        registered = 1;
        atexit(khdays_log_close);
    }
    AcquireSRWLockExclusive(&lock);
    if (file == NULL && on) {
        open_now();
        opened_for_setting = 0;
    }
    ReleaseSRWLockExclusive(&lock);
}

void khdays_log_want(int on)
{
    InterlockedExchange(&wanted, on != 0);
    AcquireSRWLockExclusive(&lock);
    if (on && file == NULL) {
        open_now();
        opened_for_setting = 1;
    } else if (!on && file != NULL && opened_for_setting) {
        fprintf(file, "log: turned off in the settings\n");
        fclose(file);
        file = NULL;
        opened_for_setting = 0;
        fprintf(stderr, "log: turned off in the settings\n");
    }
    ReleaseSRWLockExclusive(&lock);
}

int khdays_log_on(void)
{
    return file != NULL;
}

void khdays_log_frame(u32 value)
{
    InterlockedExchange(&frame, (LONG)value);
    if (file != NULL && value - flushed_frame >= 60) {
        flushed_frame = value;
        AcquireSRWLockExclusive(&lock);
        if (file != NULL) {
            fflush(file);
        }
        ReleaseSRWLockExclusive(&lock);
    }
}

void khdays_log(const char *format, ...)
{
    LARGE_INTEGER now;
    va_list args;
    if (file == NULL) {
        return;
    }
    QueryPerformanceCounter(&now);
    AcquireSRWLockExclusive(&lock);
    if (file != NULL) {
        fprintf(file, "%9.3f f%-6ld ", (double)(now.QuadPart - started.QuadPart) / (double)frequency.QuadPart,
                (long)frame);
        va_start(args, format);
        vfprintf(file, format, args);
        va_end(args);
        fputc('\n', file);
    }
    ReleaseSRWLockExclusive(&lock);
}

void khdays_log_mark(void)
{
    if (file == NULL) {
        return;
    }
    ++marks;
    khdays_log("mark %u (F12)", marks);
    AcquireSRWLockExclusive(&lock);
    fflush(file);
    ReleaseSRWLockExclusive(&lock);
    fprintf(stderr, "log: mark %u\n", marks);
}

void khdays_log_close(void)
{
    /* a crash may come inside a line, the lock held: then only flushed */
    const int locked = TryAcquireSRWLockExclusive(&lock);
    if (file != NULL) {
        fflush(file);
        if (locked) {
            fclose(file);
            file = NULL;
        }
    }
    if (locked) {
        ReleaseSRWLockExclusive(&lock);
    }
}
