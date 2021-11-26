#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "fmt/ostream.h"
#include "fmt/printf.h"
#include "helpers.h"

constexpr int MAX_BUF = 1024;
constexpr int EPOLL_SIZE = 2048;

enum class State {
  INIT_CONN,
  WAIT_FOR_MESSAGE,
  IN_MESSAGE,
};

struct Connection {
  State state;
  char send_buf[MAX_BUF];
  int send_end;
  int send_pos;
  int fd;
};

absl::Status serve(int fd);
void on_connect(int ep_fd, int sock_fd, const sockaddr_in &addr, socklen_t len);
void on_receive(int ep_fd, Connection *conn);
void on_send(int ep_fd, Connection *conn);

int main() {
  auto sock_fd = tcpServer("0.0.0.0", 9990);
  set_nonblock(sock_fd);

  auto ep_fd = epoll_create(EPOLL_SIZE);

  epoll_event accept_event;
  memset(&accept_event, 0, sizeof(epoll_event));
  accept_event.data.fd = sock_fd;
  accept_event.events = EPOLLIN;
  if (epoll_ctl(ep_fd, EPOLL_CTL_ADD, sock_fd, &accept_event) < 0) {
    fmt::printf("epoll_ctl: %s\n", strerror(errno));
    exit(-1);
  }

  epoll_event *events =
      static_cast<epoll_event *>(calloc(EPOLL_SIZE, sizeof(epoll_event)));
  if (events == nullptr) {
    fmt::printf("failed to allocate for epoll events");
    exit(-1);
  }

  while (1) {
    int nready = epoll_wait(ep_fd, events, EPOLL_SIZE, -1);
    for (int i = 0; i < nready; ++i) {
      if (events[i].data.fd == sock_fd) {  // new connection
        sockaddr_in peer_addr;
        socklen_t peer_addr_len = sizeof(peer_addr);

        int client_fd = accept(
            sock_fd, reinterpret_cast<sockaddr *>(&peer_addr), &peer_addr_len);
        if (client_fd < 0) {
          // if (errno == EAGAIN || errno == EWOULDBLOCK) {}
          fmt::printf("accept: %s\n", strerror(errno));
          exit(-1);
        } else {  // ready to connect
          on_connect(ep_fd, client_fd, peer_addr, peer_addr_len);
        }
      } else {
        if (events[i].events & EPOLLIN) {
          on_receive(ep_fd, reinterpret_cast<Connection *>(events[i].data.ptr));
        } else if (events[i].events & EPOLLOUT) {
          on_send(ep_fd, reinterpret_cast<Connection *>(events[i].data.ptr));
        }
      }
    }
  }
  return 0;
}

void on_connect(int ep_fd, int sock_fd, const sockaddr_in &addr,
                socklen_t len) {
  if (sock_fd > EPOLL_SIZE) {
    fmt::printf("too many fds\n");
    exit(-1);
  }
  report_connection(addr);
  set_nonblock(sock_fd);
  auto conn = new Connection;
  conn->send_buf[0] = '*';
  conn->send_pos = 0;
  conn->send_end = 1;
  conn->fd = sock_fd;
  conn->state = State::INIT_CONN;
  epoll_event event;
  memset(&event, 0, sizeof(epoll_event));
  event.data.fd = sock_fd;
  event.data.ptr = conn;
  event.events = EPOLLOUT;

  if (epoll_ctl(ep_fd, EPOLL_CTL_ADD, sock_fd, &event) < 0) {
    fmt::printf("epoll add: %s\n", strerror(errno));
    exit(-1);
  }
}

void on_receive(int ep_fd, Connection *conn) {
  int fd = conn->fd;
  if (conn->state == State::INIT_CONN || conn->send_pos < conn->send_end) {
    epoll_event event;
    memset(&event, 0, sizeof(event));
    event.data.fd = fd;
    event.data.ptr = conn;
    event.events |= EPOLLOUT;
    if (epoll_ctl(ep_fd, EPOLL_CTL_MOD, fd, &event) < 0) {
      fmt::printf("epoll mod: %s\n", strerror(errno));
      exit(-1);
    }
    return;
  }
  char buf[MAX_BUF];
  int nread = recv(fd, buf, MAX_BUF, 0);
  if (nread == 0) {  // remote closed
    fmt::printf("remote peer closed.\n");
    epoll_ctl(ep_fd, EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
    delete conn;
    return;
  } else if (nread < 0) {
    // if (errno == EAGAIN || errno == EWOULDBLOCK) {}
    fmt::printf("recv: %s\n", strerror(errno));
  }
  // sometimes we have data received but no message will be send
  bool ready_to_send = false;
  for (int i = 0; i < nread; ++i) {
    switch (conn->state) {
      case State::INIT_CONN:  // unreachable
        break;
      case State::WAIT_FOR_MESSAGE:
        if (buf[i] == '^') {
          conn->state = State::IN_MESSAGE;
        }
        break;
      case State::IN_MESSAGE:
        if (buf[i] == '$') {
          conn->state = State::WAIT_FOR_MESSAGE;
        } else {
          ready_to_send = true;
          conn->send_buf[conn->send_end++] = buf[i] + 1;
        }
        break;
    }
  }
  epoll_event event;
  memset(&event, 0, sizeof(event));
  event.data.fd = fd;
  event.data.ptr = conn;
  if (ready_to_send) {
    event.events |= EPOLLOUT;
  } else {
    event.events |= EPOLLIN;
  }
  if (epoll_ctl(ep_fd, EPOLL_CTL_MOD, fd, &event) < 0) {
    fmt::printf("epoll mod: %s\n", strerror(errno));
    exit(-1);
  }
}

void on_send(int ep_fd, Connection *conn) {
  int fd = conn->fd;
  if (conn->send_pos >= conn->send_end) {
    epoll_event event;
    memset(&event, 0, sizeof(event));
    event.data.fd = fd;
    event.data.ptr = conn;
    event.events |= EPOLLIN;
    if (epoll_ctl(ep_fd, EPOLL_CTL_MOD, fd, &event) < 0) {
      fmt::printf("epoll mod: %s\n", strerror(errno));
      exit(-1);
    }
    return;
  }

  int avaliable = conn->send_end - conn->send_pos;
  int nsend = send(fd, &conn->send_buf[conn->send_pos], avaliable, 0);
  if (nsend == -1) {
    // if(errno == EAGAIN || errno == EWOULDBLOCK){}
    fmt::printf("send: %s\n", strerror(errno));
  }
  bool continue_to_write = false;
  if (nsend < avaliable) {
    continue_to_write = true;
    conn->send_pos += nsend;
  } else {
    conn->send_pos = 0;
    conn->send_end = 0;
    if (conn->state == State::INIT_CONN) {
      conn->state = State::WAIT_FOR_MESSAGE;
    }
  }
  epoll_event event;
  memset(&event, 0, sizeof(epoll_event));
  event.data.fd = fd;
  event.data.ptr = conn;
  if (continue_to_write) {
    event.events |= EPOLLOUT;
  } else {
    event.events |= EPOLLIN;
  }
  if (epoll_ctl(ep_fd, EPOLL_CTL_MOD, fd, &event) < 0) {
    fmt::printf("epoll ctl: %s\n", strerror(errno));
    exit(-1);
  }
}