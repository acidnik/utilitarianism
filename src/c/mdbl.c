#include <pebble.h>
#include "modules/health_relay.h"

// Deferred health relay init: the Moddable JS machine needs a few seconds to
// start and open its AppMessage channel. health_relay_init() is scheduled 5s
// later so the JS-owned channel is ready before C tries to send.
static void delayed_health_init(void *ctx) {
  (void)ctx;
  health_relay_init();
}

int main(void) {
  Window *w = window_create();
  window_stack_push(w, true);

  // Start the Alloy runtime first; schedule health relay 5s later.
  app_timer_register(5000, delayed_health_init, NULL);

#ifdef PBL_DEBUG
  // Built with `pebble build --debug`: enable the xsbug JavaScript debugger.
  ModdableCreationRecord cr = {
    .recordSize = sizeof(cr),
    .flags = kModdableCreationFlagDebug,
  };
  moddable_createMachine(&cr);
#else
  moddable_createMachine(NULL);
#endif

  health_relay_deinit();
  window_destroy(w);
}
