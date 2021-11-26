#include "fmt/printf.h"
#include "helpers.h"
#include "uv.h"

constexpr int MAX_BUF = 1024;

enum class State
{
    INIT_CONN,
    WAIT_FOR_MESSAGE,
    IN_MESSAGE,
};

struct Connection
{
    State state;
    char send_buf[MAX_BUF];
    int send_end;
    uv_tcp_t *client;
};

constexpr int BACKLOG = 5;
uv_loop_t *loop;

#define CHECK_STATUS(status, msg) fmt::printf("[ERROR][%s:%d]%s: %s\n", __FILE__, __LINE__, msg, uv_strerror(status))

void on_connected(uv_stream_t *server, int status);
void on_wrote_init(uv_write_t *req, int status);
void on_alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf);
void on_peer_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf);
void on_wrote_buf(uv_write_t *req, int status);
void on_client_close(uv_handle_t *handle);

int main()
{
    loop = uv_default_loop();

    int rc;
    uv_tcp_t server;

    rc = uv_tcp_init(loop, &server);
    if (rc < 0)
    {
        CHECK_STATUS(rc, "uv_tcp_init");
        exit(rc);
    }

    sockaddr_in server_addr;
    rc = uv_ip4_addr("127.0.0.1", 9990, &server_addr);
    if (rc < 0)
    {
        CHECK_STATUS(rc, "uv_ip4_addr");
        exit(rc);
    }

    rc = uv_tcp_bind(&server, reinterpret_cast<sockaddr *>(&server_addr), 0);
    if (rc < 0)
    {
        CHECK_STATUS(rc, "uv_tcp_bind");
        exit(rc);
    }

    rc = uv_listen(reinterpret_cast<uv_stream_t *>(&server), BACKLOG, on_connected);
    if (rc < 0)
    {
        CHECK_STATUS(rc, "uv_listen");
        exit(rc);
    }

    uv_run(loop, UV_RUN_DEFAULT);

    return uv_loop_close(loop);
}

void on_connected(uv_stream_t *server, int status)
{
    if (status < 0)
    {
        // 连接未建立，直接返回
        CHECK_STATUS(status, "on_connect");
        return;
    }

    uv_tcp_t *client = new uv_tcp_t();
    int rc;
    rc = uv_tcp_init(loop, client);
    if (rc < 0)
    {
        CHECK_STATUS(rc, "uv_tcp_init");
        return;
    }

    if (uv_accept(server, reinterpret_cast<uv_stream_t *>(client)) == 0) // 连接已accept，出错需要uv_close
    {
        sockaddr_in peer_addr;
        int peer_addr_len = sizeof(sockaddr_in);
        rc = uv_tcp_getpeername(client, reinterpret_cast<sockaddr *>(&peer_addr), &peer_addr_len);
        if (rc < 0)
        {
            CHECK_STATUS(rc, "uv_tcp_getpeername");
            uv_close(reinterpret_cast<uv_handle_t *>(client), on_client_close);
            return;
        }
        report_connection(peer_addr);

        auto conn = new Connection();
        conn->state = State::INIT_CONN;
        conn->send_buf[0] = '*';
        conn->send_end = 1;
        conn->client = client;
        client->data = conn;

        uv_buf_t write_buf = uv_buf_init(conn->send_buf, conn->send_end);

        uv_write_t *req = new uv_write_t();
        req->data = conn;
        rc = uv_write(req, reinterpret_cast<uv_stream_t *>(client), &write_buf, conn->send_end, on_wrote_init);
        if (rc < 0)
        {
            CHECK_STATUS(rc, "uv_write");
            delete req;
            uv_close(reinterpret_cast<uv_handle_t *>(client), on_client_close);
        }
    }
    else
    {
        // uv_close(reinterpret_cast<uv_handle_t *>(client), on_client_close);
    }
}

void on_wrote_init(uv_write_t *req, int status)
{
    auto conn = reinterpret_cast<Connection *>(req->data);
    if (status < 0)
    {
        CHECK_STATUS(status, "on_wrote_init");
        delete req;
        uv_close(reinterpret_cast<uv_handle_t *>(conn->client), on_client_close);
        return;
    }
    conn->state = State::WAIT_FOR_MESSAGE;
    conn->send_end = 0;

    int rc = uv_read_start(reinterpret_cast<uv_stream_t *>(conn->client), on_alloc_buffer, on_peer_read);
    if (rc < 0)
    {
        CHECK_STATUS(rc, "uv_read_start");
        uv_close(reinterpret_cast<uv_handle_t *>(conn->client), on_client_close);
    }
    delete req;
}

void on_alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf)
{
    buf->base = new char[suggested_size];
    buf->len = suggested_size;
}

void on_peer_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf)
{
    auto conn = reinterpret_cast<Connection *>(stream->data);
    if (nread < 0)
    {
        if (nread != UV_EOF)
        {
            CHECK_STATUS(nread, "on_peer_read");
        }
        uv_close(reinterpret_cast<uv_handle_t *>(conn->client), on_client_close);
    }
    else if (nread == 0)
    {
        // do nothing
    }
    else
    {
        if (conn->state == State::INIT_CONN)
        {
            delete[] buf->base;
            return;
        }
        for (int i = 0; i < nread; ++i)
        {
            switch (conn->state)
            {
            case State::INIT_CONN: // unreachable
                break;
            case State::WAIT_FOR_MESSAGE:
                if (buf->base[i] == '^')
                    conn->state = State::IN_MESSAGE;
                break;
            case State::IN_MESSAGE:
                if (buf->base[i] == '$')
                    conn->state = State::WAIT_FOR_MESSAGE;
                else
                {
                    conn->send_buf[conn->send_end++] = buf->base[i] + 1;
                }
                break;
            }
        }
        if (conn->send_end > 0)
        {
            uv_buf_t write_buf = uv_buf_init(conn->send_buf, conn->send_end);
            uv_write_t *wreq = new uv_write_t();
            wreq->data = conn;
            int rc = uv_write(wreq, reinterpret_cast<uv_stream_t *>(conn->client), &write_buf, 1, on_wrote_buf);
            if (rc < 0)
            {
                CHECK_STATUS(rc, "uv_write");
                delete wreq;
                uv_close(reinterpret_cast<uv_handle_t *>(conn->client), on_client_close);
            }
        }
    }
    delete[] buf->base;
}

void on_wrote_buf(uv_write_t *req, int status)
{
    auto conn = reinterpret_cast<Connection *>(req->data);
    if (status < 0)
    {
        CHECK_STATUS(status, "on_wrote_buf");
        delete req;
        uv_close(reinterpret_cast<uv_handle_t *>(conn->client), on_client_close);
        return;
    }
    if (conn->send_buf[0] == 'S')
    {
        delete conn;
        delete req;
        uv_stop(loop);
        return;
    }
    conn->send_end = 0;
    delete req;
}

void on_client_close(uv_handle_t *handle)
{
    auto conn = reinterpret_cast<Connection *>(handle->data);
    if (conn != nullptr)
    {
        delete conn;
    }
    delete handle;
}