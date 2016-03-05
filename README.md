# Simple http server

## Technology
    
    epoll, std::thread

## Usage

    cmake .
    make
    final -h <ip> -p <port> -d <directory>
    
## Advanced options
    -s - demonize main thread
    -t <num_threads> - num threads (each thread has its own epoll fd, default 2)