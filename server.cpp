#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <string>
#include <thread>
#include <fstream>
#include <boost/regex.hpp>

#define MAX_EVENTS 1024

int set_nonblock(int fd)
{
    int flags;
#if defined(O_NONBLOCK)
    if (-1 == (flags = fcntl(fd, F_GETFL, 0)))
        flags = 0;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#else
    flags = 1;
    return ioctl(fd, FIOBIO, &flags);
#endif
}

std::string working_dir;

void epoll_handler(int listen_sock);

void connection_handler(int sock);

std::stringstream get_response(const std::string& uri);

std::string get_uri(const char* client_message);

int sendall(int s, const char* buf, size_t* len);

bool daemonize();

int main(int argc, char* argv[])
{
    std::string host;
    int port{8080};
    int num_threads{2};
    bool demonize{false};

    int opt;
    while ((opt = getopt(argc, argv, "h:p:d:t:")) != -1) {
        switch (opt) {
        case 'h':
            host = optarg;
            break;
        case 'p':
            port = atoi(optarg);
            break;
        case 'd':
            working_dir = optarg;
            break;
        case 's':
            demonize = true;
            break;
        case 't':
            num_threads = atoi(optarg);
            break;
        default:
            std::cerr << "Usage: final -h <ip> -p <port> -d <directory>" << std::endl;
            return 1;
        }
    }

    if (demonize && !daemonize()) {
        std::cerr << "Failed to become daemon" << std::endl;
        return 1;
    }

    //Create socket
    int listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock == -1) {
        printf("Could not create socket");
        return 1;
    }

    //Prepare the sockaddr_in structure
    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, host.c_str(), &(server.sin_addr));

    //Bind
    if (bind(listen_sock, (struct sockaddr*) &server, sizeof(server)) < 0) {
        puts("bind failed");
        return 1;
    }

    set_nonblock(listen_sock);

    //Listen
    listen(listen_sock, SOMAXCONN);

    std::thread t[num_threads];
    for (auto i = 0; i < num_threads; ++i) {
        t[i] = std::thread(epoll_handler, listen_sock);
    }

    //Join the threads with the main thread
    for (auto i = 0; i < num_threads; ++i) {
        t[i].join();
    }

    return 0;
}

void epoll_handler(int listen_sock)
{
    int epoll_fd;
    if ((epoll_fd = epoll_create1(0)) == -1) {
        exit(EXIT_FAILURE);
    }

    struct epoll_event event;
    event.data.fd = listen_sock;
    event.events = EPOLLIN;

    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_sock, &event);

    struct epoll_event Events[MAX_EVENTS];

    //Accept incoming connection
    while (true) {
        int nfds = epoll_wait(epoll_fd, Events, MAX_EVENTS, -1);
        if (nfds == -1) {
            perror("epoll_pwait");
            exit(EXIT_FAILURE);
        }
        for (unsigned int i = 0; i < nfds; ++i) {
            if (Events[i].data.fd == listen_sock) {
                int slave_socket = accept(listen_sock, 0, 0);

                set_nonblock(slave_socket);

                struct epoll_event Event;
                Event.data.fd = slave_socket;
                Event.events = EPOLLIN | EPOLLET;

                epoll_ctl(epoll_fd, EPOLL_CTL_ADD, slave_socket, &Event);
            }
            else {
                connection_handler(Events[i].data.fd);
            }
        }
    }
}

void connection_handler(int socket)
{
    size_t len = 8 * 1024;
    char message[len];

    //Receive a message from client
    recv(socket, message, len, 0);

    //Parse request
    std::string uri{get_uri(message)};

    //Prepare response
    auto response = get_response(uri);

    //Send the message back to client
    size_t buffer_len = response.str().size();
    sendall(socket, response.str().c_str(), &buffer_len);

    close(socket);
}

std::string get_uri(const char* client_message)
{
    std::string http_header;

    std::stringstream resp{client_message};
    getline(resp, http_header);

    const std::string pattern{"GET (?<url>[-.\\/\\w]+)"};
    boost::smatch what;
    if (regex_search(http_header, what, boost::regex(pattern))) {
        return what["url"];
    }
    else {
        return "/";
    }
}

std::stringstream get_response(const std::string& uri)
{
    std::stringstream response;

    std::ifstream file(working_dir + uri);
    if (file.good() && uri != "/") {
        std::stringstream content;
        content << file.rdbuf();

        response << "HTTP/1.0 200 OK\r\n"
                "Content-length: " << content.str().size() << "\r\n"
                "Connection: close\r\n"
                "Content-Type: text/html\r\n"
                "\r\n"
                << content.str();
    }

    else {
        const char not_found[] = "HTTP/1.0 404 NOT FOUND\r\nContent-length: 0\r\nContent-Type: text/html\r\n\r\n";
        response.str(not_found);
    }

    return response;
}

int sendall(int s, const char* buf, size_t* len)
{
    size_t total{0};          // how many bytes we've sent
    size_t bytes_left = *len; // how many we have left to send

    int n{0};
    while (total < *len) {
        n = (int) send(s, buf + total, bytes_left, 0);
        if (n == -1) { break; }
        total += n;
        bytes_left -= n;
    }

    *len = total; // return number actually sent here

    return n == -1 ? -1 : 0; // return -1 on failure, 0 on success
}

/* This code is based on Stevens Advanced Programming in the UNIX
 * Environment. */
bool daemonize(void)
{
    pid_t pid;

    /* Separate from our parent via fork, so init inherits us. */
    if ((pid = fork()) < 0)
        return false;
    /* use _exit() to avoid triggering atexit() processing */
    if (pid != 0)
        _exit(0);

    /* Don't hold files open. */
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    /* Many routines write to stderr; that can cause chaos if used
     * for something else, so set it here. */
    if (open("/dev/null", O_WRONLY) != 0)
        return false;
    if (dup2(0, STDERR_FILENO) != STDERR_FILENO)
        return false;
    close(0);

    /* Session leader so ^C doesn't whack us. */
    if (setsid() == (pid_t) -1)
        return false;
    /* Move off any mount points we might be in. */
    if (chdir("/") != 0)
        return false;

    /* Discard our parent's old-fashioned umask prejudices. */
    umask(0);
    return true;
}