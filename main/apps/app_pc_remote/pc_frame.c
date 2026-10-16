#include "pc_frame.h"
#include <string.h>
void pc_frame_reset(pc_frame_rx_t *rx) { if (rx) memset(rx, 0, sizeof(*rx)); }
int pc_frame_feed(pc_frame_rx_t *rx, const uint8_t *f, size_t n)
{
    if (!rx || !f || n <= PC_FRAME_HEADER_SIZE) return -1;
    uint8_t version=f[0], kind=f[1], index=f[4], count=f[5];
    uint16_t id=((uint16_t)f[2]<<8)|f[3];
    uint16_t total=((uint16_t)f[6]<<8)|f[7];
    size_t chunk=n-PC_FRAME_HEADER_SIZE;
    if (version!=2 || kind==0 || count==0 || count>PC_FRAME_MAX_PARTS ||
        index>=count || total==0 || total>PC_FRAME_MAX_MESSAGE ||
        chunk==0 || chunk>PC_FRAME_MAX_CHUNK) { pc_frame_reset(rx); return -2; }
    if (index==0) {
        if (rx->active) { pc_frame_reset(rx); return -3; }
        rx->active=1; rx->expected=total; rx->message_id=id;
        rx->kind=kind; rx->part_count=count;
    }
    if (!rx->active || rx->message_id!=id || rx->kind!=kind ||
        rx->part_count!=count || rx->expected!=total || rx->next_index!=index ||
        rx->received+chunk>total) { pc_frame_reset(rx); return -4; }
    memcpy(rx->buffer+rx->received, f+PC_FRAME_HEADER_SIZE, chunk);
    rx->received+=(uint16_t)chunk;
    rx->next_index++;
    if (index+1<count) return 0;
    if (rx->received!=rx->expected) { pc_frame_reset(rx); return -5; }
    rx->active=0;
    return 1;
}
