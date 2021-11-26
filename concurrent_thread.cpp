#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <thread>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "fmt/ostream.h"
#include "fmt/printf.h"
#include "helpers.h"

constexpr int MAX_BUF = 1024;

enum class State {
  WAIT_FOR_MESSAGE,
  IN_MESSAGE,
};

absl::Status serve(int);

int main() {
  auto sock_fd = tcpServer("0.0.0.0", 9990);

  while (1) {
    sockaddr_in peer_addr;
    socklen_t peer_addr_len = sizeof(peer_addr);

    int client_fd = accept(sock_fd, reinterpret_cast<sockaddr *>(&peer_addr),
                           &peer_addr_len);
    if (client_fd < 0) {
      perror("accept");
      exit(-1);
    }
    report_connection(peer_addr);
    new std::thread([client_fd]() {
      auto status = serve(client_fd);
      if (!status.ok()) {
        fmt::print(stderr, "{}\n", status.ToString());
      }
      fmt::printf("peer done\n");
      close(client_fd);
    });
  }
  return 0;
}

absl::Status serve(int client_fd) {
  if (send(client_fd, "*", 1, 0) < 1) {
    return absl::UnknownError(strerror(errno));
  }

  auto state = State::WAIT_FOR_MESSAGE;
  while (1) {
    char buf[MAX_BUF];
    int len = recv(client_fd, buf, MAX_BUF, 0);
    if (len < 0) {
      return absl::UnknownError(strerror(errno));
    } else if (len == 0) {
      break;
    }
    for (int i = 0; i < len; ++i) {
      switch (state) {
        case State::WAIT_FOR_MESSAGE:
          if (buf[i] == '^') {
            state = State::IN_MESSAGE;
          }
          break;
        case State::IN_MESSAGE: {
          if (buf[i] == '$')
            state = State::WAIT_FOR_MESSAGE;
          else {
            buf[i] += 1;
            if (send(client_fd, &buf[i], 1, 0) < 1) {
              return absl::UnknownError(strerror(errno));
            }
          }
          break;
        }
      }
    }
  }
  return absl::OkStatus();
}