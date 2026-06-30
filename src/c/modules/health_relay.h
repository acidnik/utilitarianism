#pragma once

//
// modules/health_relay — C-side health sampling and AppMessage relay
//
// Provides a self-contained module interface that:
// - Reads step count and heart rate from HealthService.
// - Sends that data to the phone via AppMessage.
// - Retries on send failure without creating a flood of pending messages.
//
// Usage in main():
//   health_relay_init();   // after window is pushed
//   moddable_createMachine(NULL);
//   health_relay_deinit(); // before app exit
//

// Start the relay. Subscribes to health events and schedules an early
// startup snapshot send.
void health_relay_init(void);

// Stop the relay. Cancels any pending timers.
void health_relay_deinit(void);
