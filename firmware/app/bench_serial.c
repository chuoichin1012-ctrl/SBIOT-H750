// Bench (Plan 07a): with the board's two RS485 ports wired to each other, one port acts as the other's
// counterpart, timed by the board itself. Driven from the UART7 console (app_main.c):
//   E<port> 0                     off
//   E<port> 1                     echo every frame back
//   E<port> 2 <gap_ms> <hex>      answer each frame with <hex>, in two halves <gap_ms> apart (0: one write);
//                                 with '|' in <hex>, in those parts instead (up to 4)
//   E<port> 3 <seconds>           stream bytes 0..255 continuously for <seconds>
//   E<port> 4 <hex>               send <hex> now (an XOFF 13, an XON 11)
//   F<port>                       the frames received since the last F: count, bytes, lengths and gaps
#include "bench_serial.h"
#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "blog.h"
#include "now_us.h"
#include "queue.h"
#include "serial.h"
#include "task.h"

#define TASK_WORDS 768
#define LOG_FRAMES 16

typedef struct {
    int mode, gap_ms;
    uint8_t reply[128];
    size_t reply_len;
    size_t cut[4];  // the '|' marks in the reply
    int ncut;
    uint32_t stream_s;
    // what came in (F)
    volatile uint32_t frames, bytes;
    uint16_t len[LOG_FRAMES];
    uint32_t at_ms[LOG_FRAMES];
} peer_t;

static peer_t s_peer[SERIAL_PORTS + 1];
static QueueHandle_t s_work;  // port numbers with something to do
static StaticQueue_t s_work_mem;
static uint8_t s_work_store[8 * sizeof(int)];
static StaticTask_t s_tcb;
static StackType_t s_stack[TASK_WORDS];
static uint8_t s_echo[SERIAL_PORTS + 1][SERIAL_FRAME_MAX];
static volatile size_t s_echo_len[SERIAL_PORTS + 1];

static int hexv(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : (c | 0x20) >= 'a' && (c | 0x20) <= 'f' ? (c | 0x20) - 'a' + 10 : -1;
}

static size_t parse_hex(const char *s, uint8_t *out, size_t max, size_t *cut, int *ncut)
{
    size_t n = 0;
    while (*s && n < max) {
        if (*s == ' ' || *s == '|') {
            if (*s == '|' && cut && *ncut < 4)
                cut[(*ncut)++] = n;
            s++;
            continue;
        }
        if (hexv(s[0]) < 0 || hexv(s[1]) < 0)
            break;
        out[n++] = (uint8_t)(hexv(s[0]) << 4 | hexv(s[1]));
        s += 2;
    }
    return n;
}

// From serial_rx[n]: log the frame, and hand the answer to the bench task (serial_rx must not block).
static void on_frame(int port, const uint8_t *data, size_t len, void *ctx)
{
    (void)ctx;
    peer_t *p = &s_peer[port];
    uint32_t i = p->frames % LOG_FRAMES;
    p->len[i] = (uint16_t)len;
    p->at_ms[i] = (uint32_t)(now_us() / 1000u);
    p->frames++;
    p->bytes += (uint32_t)len;
    if (p->mode == 1 && !s_echo_len[port]) {
        memcpy(s_echo[port], data, len);
        s_echo_len[port] = len;
    }
    if (p->mode == 1 || p->mode == 2)
        xQueueSend(s_work, &port, 0);
}

static void task(void *arg)
{
    (void)arg;
    for (;;) {
        int port;
        xQueueReceive(s_work, &port, portMAX_DELAY);
        peer_t *p = &s_peer[port];
        if (p->mode == 1 && s_echo_len[port]) {
            serial_write(port, s_echo[port], s_echo_len[port], 2000);
            s_echo_len[port] = 0;
        } else if (p->mode == 2 && p->reply_len) {
            if (!p->gap_ms) {
                serial_write(port, p->reply, p->reply_len, 1000);
            } else {
                size_t at[6] = {0}, n = 1;
                if (p->ncut)
                    for (int i = 0; i < p->ncut; i++)
                        at[n++] = p->cut[i];
                else
                    at[n++] = p->reply_len / 2;
                at[n] = p->reply_len;
                for (size_t i = 0; i < n; i++) {
                    if (i)
                        vTaskDelay(pdMS_TO_TICKS(p->gap_ms));
                    serial_write(port, p->reply + at[i], at[i + 1] - at[i], 1000);  // returns when the last bit has left
                }
            }
        } else if (p->mode == 3) {
            static uint8_t chunk[256];
            for (int i = 0; i < 256; i++)
                chunk[i] = (uint8_t)i;
            uint64_t end = now_us() + (uint64_t)p->stream_s * 1000000u;
            uint32_t sent = 0;
            while (now_us() < end && serial_write(port, chunk, sizeof chunk, 1000) == 0)
                sent += sizeof chunk;
            p->mode = 0;
            blog("app: E%d streamed %lu bytes\r\n", port, (unsigned long)sent);
        }
    }
}

void bench_serial_init(void)
{
    s_work = xQueueCreateStatic(8, sizeof(int), s_work_store, &s_work_mem);
    xTaskCreateStatic(task, "bench_ser", TASK_WORDS, NULL, 8, s_stack, &s_tcb);
    for (int port = 1; port <= SERIAL_PORTS; port++)
        serial_subscribe(port, on_frame, NULL);
}

void bench_serial_cmd(char cmd, const char *line)
{
    int port = line[0] - '0';
    if (port < 1 || port > SERIAL_PORTS)
        return;
    peer_t *p = &s_peer[port];
    if (cmd == 'F') {
        uint32_t n = p->frames;
        char out[200];
        int q = snprintf(out, sizeof out, "app: F%d frames %lu bytes %lu:", port, (unsigned long)n, (unsigned long)p->bytes);
        uint32_t first = n > LOG_FRAMES ? n - LOG_FRAMES : 0;
        for (uint32_t k = first; k < n && q < (int)sizeof out - 16; k++) {
            uint32_t i = k % LOG_FRAMES;
            long gap = k > first ? (long)(p->at_ms[i] - p->at_ms[(k - 1) % LOG_FRAMES]) : 0;
            q += snprintf(out + q, sizeof out - q, " %u+%ld", p->len[i], gap);
        }
        blog("%s\r\n", out);
        p->frames = p->bytes = 0;
        return;
    }
    int mode = 0, a = 0;
    const char *rest = line + 1;
    while (*rest == ' ')
        rest++;
    sscanf(rest, "%d", &mode);
    while (*rest && *rest != ' ')
        rest++;
    if (mode == 2 || mode == 3) {
        while (*rest == ' ')
            rest++;
        sscanf(rest, "%d", &a);
        while (*rest && *rest != ' ')
            rest++;
    }
    if (mode == 4) {
        uint8_t b[64];
        size_t n = parse_hex(rest, b, sizeof b, NULL, NULL);
        int r = serial_write(port, b, n, 1000);
        blog("app: E%d sent %u bytes: %d\r\n", port, (unsigned)n, r);
        return;
    }
    p->mode = mode;
    p->gap_ms = mode == 2 ? a : 0;
    p->stream_s = mode == 3 ? (uint32_t)a : 0;
    p->ncut = 0;
    p->reply_len = mode == 2 ? parse_hex(rest, p->reply, sizeof p->reply, p->cut, &p->ncut) : 0;
    s_echo_len[port] = 0;
    blog("app: E%d mode %d gap %d reply %u\r\n", port, mode, p->gap_ms, (unsigned)p->reply_len);
    if (mode == 3)
        xQueueSend(s_work, &port, 0);
}
