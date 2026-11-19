#ifndef CONTROL_PROTOCOL_H
#define CONTROL_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CONTROL_PACKET_SIZE 14U
#define CONTROL_MAX_PACKETS 10000U

typedef enum {
    CONTROL_START = 1, CONTROL_STOP, CONTROL_LEFT, CONTROL_RIGHT, CONTROL_TEST
} ControlCommand;

typedef struct {
    uint32_t session;
    uint32_t sequence;
    ControlCommand command;
    bool ack;
} ControlPacket;

/* One transmitter per session. Sequence numbers are 1..10000, without wrap. */
typedef struct {
    uint32_t session, highest, unique, duplicates, out_of_order;
    uint8_t seen[(CONTROL_MAX_PACKETS + 7U) / 8U];
} ControlTracker;

typedef enum { CONTROL_NEW, CONTROL_DUPLICATE, CONTROL_WRONG_SESSION,
               CONTROL_INVALID } ControlTrackResult;

bool Control_Encode(const ControlPacket *packet, uint8_t data[CONTROL_PACKET_SIZE]);
bool Control_Decode(const uint8_t *data, size_t length, ControlPacket *packet);
const char *Control_CommandName(ControlCommand command);
void ControlTracker_Reset(ControlTracker *tracker, uint32_t session);
ControlTrackResult ControlTracker_Add(ControlTracker *tracker, const ControlPacket *packet);
bool ControlTracker_Report(const ControlTracker *tracker, uint32_t total,
                           uint32_t *received, uint32_t *missing);

#endif
