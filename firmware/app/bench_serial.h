// Bench (Plan 07a): one serial port as the other's counterpart (the two RS485 ports wired together).
#pragma once
void bench_serial_init(void);       // after serial_start
void bench_serial_cmd(char cmd, const char *line);  // 'E' or 'F' and the rest of its console line
