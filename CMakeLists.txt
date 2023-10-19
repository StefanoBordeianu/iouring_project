cmake_minimum_required(VERSION 3.26)
project(iouring_project C)

set(CMAKE_C_STANDARD 11)

add_executable(iouring_project
        garbageTests/socket/socketClient.c
        garbageTests/socket/socketServer.c
        garbageTests/io_uring/server/server.c
        garbageTests/io_uring/server/example.c
        garbageTests/io_uring/newServer/tcpServer.c
        garbageTests/io_uring/client/tcpClient.c
        garbageTests/io_uring/newServer/udpServer.c
        garbageTests/io_uring/client/udpClient.c
        benchmarking/test/udpServer/udpServer.c
        benchmarking/test/udpServer/batchingUdpServer.c
        benchmarking/test/udpServer/batchingUdpServer.c
        benchmarking/test/udpServer/batchingUdpServer.c
)
