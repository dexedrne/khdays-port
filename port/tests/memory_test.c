#include "../runtime/core/runtime.h"
#include "../runtime/core/io.h"
#include "../runtime/core/window.h"
#include "../sound/snd7_native.h"

#undef NDEBUG
#include <assert.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <unistd.h>

unsigned char *khdays_window;
static int ds_memory;
int khdays_game_heap_ds_memory(void) { return ds_memory; }
void khdays_snd7_native_set_ds_base(uintptr_t base) { assert(base == (uintptr_t)khdays_window); }

static void check_guard(unsigned address)
{
    pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
        struct rlimit limit = {0, 0};
        setrlimit(RLIMIT_CORE, &limit);
        *(volatile unsigned char *)KHDAYS_W(address) = 1;
        _exit(0);
    }
    int status;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFSIGNALED(status) && (WTERMSIG(status) == SIGSEGV || WTERMSIG(status) == SIGBUS));
}

int main(int argc, char **argv)
{
    ds_memory = argc == 2;
    assert(khdays_memory_map());
    khdays_window[0x02000001] = 83;
    assert(khdays_window[0x02400001] == 83);
    assert(khdays_window[0x02c00001] == (ds_memory ? 83 : 0));
    khdays_window[0x027f4321] = 62;
    assert(khdays_window[0x023f4321] == 62);
    khdays_window[0x027e0002] = 31;
    assert(khdays_window[0x023e0002] == 0);
    assert(khdays_window[0x08000000] == 255 && khdays_window[0x0a000000] == 255);
    check_guard(0);
    check_guard(0x04000000);
    check_guard(0x08000000);
    unsigned rest;
    unsigned address = KHDAYS_HEAPS_BASE + 17;
    assert(!khdays_heaps_touched(address, &rest));
    khdays_window[address] = 14;
    assert(khdays_heaps_touched(address, &rest) && rest > 0);
    assert(khdays_heaps_committed() > 0);
    khdays_heaps_reset();
    assert(!khdays_heaps_touched(address, &rest));
    assert(khdays_window[address] == 0);
    return 0;
}
