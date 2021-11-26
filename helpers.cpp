#include "helpers.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "fmt/ostream.h"
#include "fmt/printf.h"

static absl::StatusOr<int> tcp_server(fmt::string_view addr, uint16_t port) {
  int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd < 0) {
    return absl::UnknownError(fmt::format("socket: {}", strerror(errno)));
  }
  int opt = 1;
  setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(int));
  sockaddr_in listen_addr;
  listen_addr.sin_family = AF_INET;
  listen_addr.sin_port = htons(port);
  if (!inet_pton(AF_INET, addr.data(), &listen_addr.sin_addr)) {
    return absl::InvalidArgumentError(
        "addr is not a valid address representation");
  }
  if (bind(listen_fd, reinterpret_cast<sockaddr *>(&listen_addr),
           sizeof(sockaddr_in)) != 0) {
    return absl::UnknownError(fmt::format("bind: {}", strerror(errno)));
  }
  if (listen(listen_fd, 5) != 0) {
    return absl::UnknownError(fmt::format("listen: {}", strerror(errno)));
  }
  return listen_fd;
}

int tcpServer(const char *addr, uint16_t port) {
  auto r = tcp_server(addr, port);
  if (!r.ok()) {
    fmt::print(stderr, "{}\n", r.status());
    exit(-1);
  }
  return r.value();
}

void report_connection(const sockaddr_in &peer) {
  char peer_addr_p[INET_ADDRSTRLEN] = {0};
  inet_ntop(AF_INET, &peer.sin_addr, peer_addr_p, INET_ADDRSTRLEN);
  fmt::printf("connection from %s:%d\n", peer_addr_p, ntohs(peer.sin_port));
}

void set_nonblock(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1) {
    exit(-1);
  }

  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
    exit(-1);
  }
}