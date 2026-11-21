/* Desktop-only bridge. It deliberately delegates profile interpretation to
 * the C engine so the Rust UI cannot drift from the DSP configuration ABI. */
#include "dtsx_live.h"
#include <string.h>

int dtsx_desktop_publish_profile(const char *label, const char *path)
{
    if (!label || !path) return -1;
    DtsxConfig cfg;
    if (dtsx_config_defaults(&cfg, 48000.0) != 0) return -2;
    if (dtsx_config_load(&cfg, path) != 0) return -3;
    DtsxLiveHandle *live = dtsx_live_open(label, 1);
    if (!live) return -4;
    DtsxLiveParams p;
    dtsx_live_params_from_config(&cfg, &p);
    const int rc = dtsx_live_publish(live, &p);
    dtsx_live_close(live);
    return rc;
}

int dtsx_desktop_live_available(const char *label)
{
    DtsxLiveHandle *live = dtsx_live_open(label, 0);
    if (!live) return 0;
    dtsx_live_close(live);
    return 1;
}
