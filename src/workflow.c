#include "workflow.h"

static volatile workflow_step_t active_step = WORKFLOW_STOPPED;

void workflow_set(workflow_step_t step)
{
    active_step = step;
}

workflow_step_t workflow_get(void)
{
    return active_step;
}

const char *workflow_name(workflow_step_t step)
{
    static const char *const names[] = {
        "Stopped",
        "Fox identifier in CW",
        "1 second pause",
        "Warble",
        "5 second pause",
        "Fox identifier in CW",
        "1 second pause",
        "Sweep",
        "5 second pause",
        "Station ID twice",
        "Idle",
        "Stop identification",
        "Reboot identification",
        "PicoCW ready",
        "PicoCW key down",
    };
    return (unsigned)step < (sizeof(names) / sizeof(names[0])) ?
        names[step] : "Unknown";
}
