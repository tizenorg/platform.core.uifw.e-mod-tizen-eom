#include <stdlib.h>
#include <stdint.h>
#include "wayland-util.h"

extern const struct wl_interface wl_output_interface;

static const struct wl_interface *types[] = {
	&wl_output_interface,
	NULL,
	&wl_output_interface,
	NULL,
	NULL,
	NULL,
	&wl_output_interface,
	NULL,
	NULL,
	&wl_output_interface,
	NULL,
	NULL,
	NULL,
};

static const struct wl_message wl_eom_requests[] = {
	{ "set_attribute", "ou", types + 0 },
};

static const struct wl_message wl_eom_events[] = {
	{ "output_type", "ouuu", types + 2 },
	{ "output_mode", "ouu", types + 6 },
	{ "output_attribute", "ouuu", types + 9 },
};

WL_EXPORT const struct wl_interface wl_eom_interface = {
	"wl_eom", 1,
	1, wl_eom_requests,
	3, wl_eom_events,
};

