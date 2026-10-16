#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define PC_FRAME_HEADER_SIZE 8
#define PC_FRAME_MAX_MESSAGE 1024
#define PC_FRAME_MAX_CHUNK 120
#define PC_FRAME_MAX_PARTS 16
typedef struct {
    uint8_t buffer[PC_FRAME_MAX_MESSAGE];
    uint16_t received;
    uint16_t expected;
    uint16_t message_id;
    uint8_t kind;
    uint8_t next_index;
    uint8_t part_count;
    uint8_t active;
} pc_frame_rx_t;
void pc_frame_reset(pc_frame_rx_t *rx);
/* 0: waiting; 1: complete; negative: invalid (receiver reset) */
int pc_frame_feed(pc_frame_rx_t *rx, const uint8_t *frame, size_t size);
#ifdef __cplusplus
}
#endif
