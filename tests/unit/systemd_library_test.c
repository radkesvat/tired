#include <stdio.h>
#include <systemd/sd-bus.h>
#include <systemd/sd-journal.h>
int main(void)
{
    sd_bus *bus = NULL;
    int result = sd_bus_new(&bus);
    if (result < 0)
        return 1;
    result = sd_bus_set_description(bus, "tired-library-test");
    sd_bus_unref(bus);
    if (result < 0)
        return 1;
    /* Link the journal reader and compression dependencies without contacting a
     * manager or opening the host journal. /dev/null is never a directory. */
    sd_journal *journal = NULL;
    result = sd_journal_open_directory(&journal, "/dev/null", 0);
    sd_journal_close(journal);
    if (result >= 0)
        return 1;
    return 0;
}
