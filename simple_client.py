import socket
import argparse
import logging
import time
import threading


class Receiver(threading.Thread):
    def __init__(self, name, sock_fd) -> None:
        super().__init__()
        self.name = name
        self.sock_fd = sock_fd
        self.bufsize = 1024

    def run(self):
        received = b''
        while True:
            buf = self.sock_fd.recv(self.bufsize)
            logging.info(f'{self.name} recv {buf} from remote')
            received += buf
            if b'1' in received:
                break


def make_new_worker(name, ip, port):
    sock_fd = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        sock_fd.connect((ip, port))
    except :
        print('socket error')
        return
    if sock_fd.recv(1) != b'*':
        logging.error('cannot receive * from remote')
    logging.info(f'{name} connected')

    t = Receiver(name, sock_fd)
    t.start()

    s = b'abc^def$gh^ij$k'
    logging.info(f'{name} sending {s}')
    sock_fd.send(s)
    time.sleep(1.0)

    s = b'abc^123'
    logging.info(f'{name} sending {s}')
    sock_fd.send(s)
    time.sleep(0.5)

    s = b'abc$^xy123$okok'
    logging.info(f'{name} sending {s}')
    sock_fd.send(s)
    time.sleep(0.1)

    s = b'$^00000$'
    logging.info(f'{name} will close')
    sock_fd.send(s)

    t.join()

    sock_fd.close()
    logging.info(f'{name} disconnect from remote')


def main():
    argparser = argparse.ArgumentParser('tcp client')
    argparser.add_argument('ip', help='remote ip')
    argparser.add_argument('port', type=int, help='remote port')
    argparser.add_argument('-n', type=int, default=1,
                           help='num of concurrent connection', dest='num_concurrent')
    argparser.add_argument(
        '--level', choices=['info', 'error'], default='info', help='log level')

    args = argparser.parse_args()

    level = logging.INFO
    if args.level == 'error':
        level = logging.ERROR
    logging.basicConfig(
        level=level, format='%(levelname)s:%(asctime)s: %(message)s')

    start = time.time()
    workers = []
    for i in range(args.num_concurrent):
        name = f'conn{i}'
        t = threading.Thread(target=make_new_worker,
                             args=(name, args.ip, args.port))
        t.start()
        workers.append(t)

    for w in workers:
        w.join()

    print(f"total running time: {time.time() - start:.2f}s")


if __name__ == '__main__':
    start = time.time()
    main()
