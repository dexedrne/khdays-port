#include "../compat/decomp_compat.h"
#include "../compat/sections.h"
#include "../runtime/core/window.h"
#include <stdlib.h>
#include <string.h>

extern void khdays_data_init(int module);
extern unsigned char *khdays_window;
/* no overlay is loaded here: no function of one at any address (runtime/core/overlays.c) */
int khdays_overlay_function_at(unsigned address, int *module)
{
    (void)address;
    (void)module;
    return 0;
}
void khdays_overlay_started(int module)
{
    (void)module;
}
void *khdays_native_at(unsigned ref)
{
    (void)ref;
    return NULL;
}
const char *khdays_diag_function_at(const void *p)
{
    (void)p;
    return "";
}
void khdays_log(const char *format, ...)
{
    (void)format;
}
void khdays_log_close(void) {}
void khdays_hal_unimplemented(const char *reason)
{
    (void)reason;
    abort();
}

static const unsigned value = 42;
KHDAYS_DATA_RECORD struct khdays_data_init record = {4, &value, sizeof(value), -1};
static void target(void) {}
KHDAYS_FUNCTION_RECORD const struct {
    unsigned reference;
    void (*function)(void);
} function_record = {123, target};
int main(void)
{
    unsigned words[2] = {0};
    khdays_window = (unsigned char *)words;
    khdays_data_init(-1);
    return words[1] != value || khdays_function(123) != target;
}
