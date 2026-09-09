#include <pthread.h>
#include <stddef.h>
#include <unistd.h>

#include <dolphin/db.h>
#include <dolphin/os/OSError.h>
#include <sys/proc.h>
#include <sys/sysctl.h>

OSErrorHandler OSErrorTable[OS_ERROR_MAX];
static pthread_mutex_t error_handler_lock = PTHREAD_MUTEX_INITIALIZER;

OSErrorHandler OSSetErrorHandler(OSError error, OSErrorHandler handler)
{
    OSErrorHandler previous;
    if (error >= OS_ERROR_MAX) {
        return NULL;
    }
    pthread_mutex_lock(&error_handler_lock);
    previous = OSErrorTable[error];
    OSErrorTable[error] = handler;
    pthread_mutex_unlock(&error_handler_lock);
    return previous;
}

BOOL DBIsDebuggerPresent(void)
{
    int query[] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid() };
    struct kinfo_proc process = { 0 };
    size_t size = sizeof(process);
    if (sysctl(query, 4, &process, &size, NULL, 0) != 0) {
        return FALSE;
    }
    return (process.kp_proc.p_flag & P_TRACED) != 0;
}
