#include <stdlib.h>
#include <stdint.h>
#include "wayland-util.h"


static const struct wl_interface *types[] = {
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
};

static const struct wl_message wl_eom_requests[] = {
	{ "set_attribute", "uu", types + 0 },
	{ "get_output_info", "u", types + 0 },
};

static const struct wl_message wl_eom_events[] = {
	{ "output_count", "u", types + 0 },
	{ "output_info", "uuuuuuuu", types + 0 },
	{ "output_type", "uuu", types + 0 },
	{ "output_mode", "uu", types + 0 },
	{ "output_attribute", "uuuu", types + 0 },
};

WL_EXPORT const struct wl_interface wl_eom_interface = {
	"wl_eom", 1,
	2, wl_eom_requests,
	5, wl_eom_events,
};

