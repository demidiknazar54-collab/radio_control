#include "ControlProtocol.h"
#include <string.h>

static uint16_t crc16(const uint8_t *data, size_t length)
{
    /* CRC-16/CCITT-FALSE: polynomial 0x1021, initial 0xFFFF. */
    uint16_t crc = 0xFFFFU;
    for (size_t i = 0; i < length; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (uint16_t)((crc & 0x8000U) ? ((uint32_t)crc << 1) ^ 0x1021U : (uint32_t)crc << 1);
    }
    return crc;
}

static void put32(uint8_t *p, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (8U * i));
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool valid(const ControlPacket *p)
{
    return p != NULL && p->session != 0 && p->sequence >= 1 &&
           p->sequence <= CONTROL_MAX_PACKETS &&
           p->command >= CONTROL_START && p->command <= CONTROL_TEST;
}

bool Control_Encode(const ControlPacket *packet, uint8_t data[CONTROL_PACKET_SIZE])
{
    if (data == NULL || !valid(packet)) return false;
    data[0] = 'L'; data[1] = 'C'; data[2] = 1;
    data[3] = (uint8_t)packet->command | (packet->ack ? 0x80U : 0U);
    put32(data + 4, packet->session);
    put32(data + 8, packet->sequence);
    uint16_t crc = crc16(data, 12);
    data[12] = (uint8_t)crc; data[13] = (uint8_t)(crc >> 8);
    return true;
}

bool Control_Decode(const uint8_t *data, size_t length, ControlPacket *packet)
{
    if (data == NULL || packet == NULL || length != CONTROL_PACKET_SIZE ||
        data[0] != 'L' || data[1] != 'C' || data[2] != 1) return false;
    if (crc16(data, 12) != ((uint16_t)data[12] | ((uint16_t)data[13] << 8))) return false;
    ControlPacket p = {get32(data + 4), get32(data + 8),
                       (ControlCommand)(data[3] & 0x7FU), (data[3] & 0x80U) != 0};
    if (!valid(&p)) return false;
    *packet = p;
    return true;
}

const char *Control_CommandName(ControlCommand command)
{
    switch (command) {
        case CONTROL_START: return "START";
        case CONTROL_STOP: return "STOP";
        case CONTROL_LEFT: return "LEFT";
        case CONTROL_RIGHT: return "RIGHT";
        case CONTROL_TEST: return "TEST";
        default: return "INVALID";
    }
}

void ControlTracker_Reset(ControlTracker *tracker, uint32_t session)
{
    memset(tracker, 0, sizeof(*tracker));
    tracker->session = session;
}

ControlTrackResult ControlTracker_Add(ControlTracker *tracker, const ControlPacket *packet)
{
    if (!valid(packet) || packet->ack) return CONTROL_INVALID;
    if (packet->session != tracker->session) return CONTROL_WRONG_SESSION;
    uint32_t index = packet->sequence - 1U;
    uint8_t mask = (uint8_t)(1U << (index % 8U));
    if ((tracker->seen[index / 8U] & mask) != 0) {
        tracker->duplicates++;
        return CONTROL_DUPLICATE;
    }
    tracker->seen[index / 8U] |= mask;
    tracker->unique++;
    if (packet->sequence < tracker->highest) tracker->out_of_order++;
    if (packet->sequence > tracker->highest) tracker->highest = packet->sequence;
    return CONTROL_NEW;
}

bool ControlTracker_Report(const ControlTracker *tracker, uint32_t total,
                           uint32_t *received, uint32_t *missing)
{
    if (total == 0 || total > CONTROL_MAX_PACKETS || total < tracker->highest ||
        received == NULL || missing == NULL) return false;
    *received = tracker->unique;
    *missing = total - tracker->unique;
    return true;
}
