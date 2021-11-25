#pragma once

#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <absl/strings/string_view.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fmt/ostream.h>
#include <fmt/printf.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int tcpServer(const char *, uint16_t);

void report_connection(const sockaddr_in &peer);
