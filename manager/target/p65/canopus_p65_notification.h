/* P65 88-byte notification input layout, recovered from CA5FF3C..CA60084
 * and CA5FCAC's deep-copy path. Pointer words are explicitly 32-bit. No
 * Band11 callback context is assumed; all callback/extension words are null. */
#ifndef CANOPUS_P65_NOTIFICATION_H
#define CANOPUS_P65_NOTIFICATION_H
#include <stddef.h>
#include <stdint.h>

struct canopus_p65_notification_v1 {
    uint64_t message_id;
    uint32_t repeat_count;
    uint32_t title, source, body, auxiliary;
    uint32_t small_icon, large_icon, extension36, extension40;
    uint32_t timestamp;
    uint32_t reserved48, color;
    uint32_t action, action_context, extension64, extension68;
    uint32_t open_callback, destroy_callback;
    uint8_t start_reminder, flags81, flags82, reserved83;
    uint32_t callback_data;
};
_Static_assert(sizeof(struct canopus_p65_notification_v1) == 88,
               "P65 notification size");
_Static_assert(offsetof(struct canopus_p65_notification_v1, title) == 12,
               "P65 title offset");
_Static_assert(offsetof(struct canopus_p65_notification_v1, start_reminder) == 80,
               "P65 reminder offset");
_Static_assert(offsetof(struct canopus_p65_notification_v1, callback_data) == 84,
               "P65 callback-data offset");

static inline void canopus_p65_notification_init(
    struct canopus_p65_notification_v1 *message, uint64_t id,
    uint32_t title, uint32_t source, uint32_t body, uint32_t icon)
{
    volatile uint8_t *bytes = (volatile uint8_t *)message;
    uint32_t i;
    for (i = 0; i < sizeof(*message); i++) bytes[i] = 0;
    message->message_id = id;
    message->title = title;
    message->source = source;
    message->body = body;
    message->small_icon = icon;
    message->large_icon = icon;
    /* Start with notification-center delivery, not popup/page callbacks.
     * All callback-owned lifetimes stay null on this experimental path. */
}
#endif
