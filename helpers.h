#pragma once

#include <stdint.h>

struct sockaddr_in;

int tcpServer(const char *, uint16_t);

void report_connection(const sockaddr_in &peer);

void set_nonblock(int);
