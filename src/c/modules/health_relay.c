#include "health_relay.h"
#include <pebble.h>
#include <message_keys.auto.h>

// In this SDK build, message_keys.auto.h is empty and the key IDs are emitted
// as uint32_t variables in message_keys.auto.c (HEALTH_STEPS=10000,
// HEART_RATE_BPM=10001, matching package.json messageKeys order).
extern uint32_t MESSAGE_KEY_HEALTH_STEPS;
extern uint32_t MESSAGE_KEY_HEART_RATE_BPM;

//
// modules/health_relay — C-side health sampling and AppMessage relay
//
// Pebble Health APIs are available to C, not directly to Alloy JS.
// This module samples health data and sends AppMessage payloads that PKJS
// relays back to watch JS.
//
// Design: fire-and-forget, no retry timer.
// The C-side and the Moddable JS Message module share one AppMessage outbox.
// A retry loop would compete with JS for the outbox (APP_MSG_BUSY) and starve
// the event loop. Instead, we only attempt a send on health events (which fire
// infrequently — ~10 min) and once at startup. If the outbox is busy, we skip
// silently and wait for the next health event.

static AppTimer *s_startup_timer = NULL;

// Single attempt to send health snapshot. No retry on failure.
static void send_health_snapshot(void) {
	int32_t steps = (int32_t)health_service_sum_today(HealthMetricStepCount);
	int32_t heart_rate = (int32_t)health_service_peek_current_value(HealthMetricHeartRateBPM);

	DictionaryIterator *iter = NULL;
	AppMessageResult result = app_message_outbox_begin(&iter);
	if (result != APP_MSG_OK)
		return;  // outbox busy or not open — skip silently, no retry

	dict_write_int32(iter, MESSAGE_KEY_HEALTH_STEPS, steps);
	dict_write_int32(iter, MESSAGE_KEY_HEART_RATE_BPM, heart_rate);

	app_message_outbox_send();
	// Don't check result — no retry. Next health event handles it.
}

static void health_event_handler(HealthEventType type, void *context) {
	(void)context;
	// SignificantUpdate fires on subscribe (initial data).
	// HeartRateUpdate / MovementUpdate fire when values change.
	if (type == HealthEventHeartRateUpdate ||
	    type == HealthEventMovementUpdate ||
	    type == HealthEventSignificantUpdate) {
		send_health_snapshot();
	}
}

static void startup_timer_handler(void *context) {
	(void)context;
	s_startup_timer = NULL;
	send_health_snapshot();
}

void health_relay_init(void) {
	health_service_events_subscribe(health_event_handler, NULL);
	// One delayed attempt for initial data.
	// The emulator may not fire health events after SignificantUpdate,
	// so this ensures at least one send attempt after the channel is open.
	s_startup_timer = app_timer_register(3000, startup_timer_handler, NULL);
}

void health_relay_deinit(void) {
	health_service_events_unsubscribe();
	if (s_startup_timer) {
		app_timer_cancel(s_startup_timer);
		s_startup_timer = NULL;
	}
}
