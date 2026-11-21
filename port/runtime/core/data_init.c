/* Copies the game's native data initializers into DS memory when their
 * module is loaded (see KHDAYS_DATA_INIT in compat/decomp_compat.h). The
 * entries are grouped by the linker between these two markers, in section
 * order ($a < $m < $z); it may pad between them with zeros, which is skipped. */
#include "../../compat/decomp_compat.h"
#include "runtime.h"
#include "window.h"

#include <string.h>

#include "../../compat/sections.h"
#ifdef _MSC_VER
#pragma section(".khdi$a", read, write)
#pragma section(".khdi$z", read, write)
__declspec(allocate(".khdi$a")) static struct khdays_data_init first_marker = {0, 0, 0, 0};
__declspec(allocate(".khdi$z")) static struct khdays_data_init last_marker = {0, 0, 0, 0};
#define RECORD_BEGIN (&first_marker + 1)
#define RECORD_END (&last_marker)
#else
extern const struct khdays_data_init records_begin[] KHDAYS_SECTION_START(khdi);
extern const struct khdays_data_init records_end[] KHDAYS_SECTION_END(khdi);
#define RECORD_BEGIN records_begin
#define RECORD_END records_end
#endif

void khdays_overlay_started(int id);

void khdays_data_init(int module)
{
    if (module >= 0) {
        khdays_overlay_started(module); /* an overlay: FS_StartOverlay (prepare.py HOOKS) */
    }
    for (const struct khdays_data_init *entry = RECORD_BEGIN; entry < RECORD_END; ++entry) {
        if (entry->address != 0 && entry->module == module) {
            memcpy(KHDAYS_W(entry->address), entry->init, entry->size);
        }
    }
}
