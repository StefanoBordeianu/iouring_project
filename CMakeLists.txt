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
        benchmarking/test/udpServer/standard/udpServer.c
        benchmarking/test/socket/server.c
        pktgen/newPacketGen/udpPacketGen.c
        pktgen/OldPacketGen/udpgen.c
        benchmarking/test/udpServer/kernelPooling/udpServer.c
        benchmarking/test/udpServer/submitAndWait/udpServer.c
        benchmarking/test/udpServer/multishot/udpServer.c
        benchmarking/test/udpServer/submitAndWait/registeredFile.c
        benchmarking/test/udpServer/standard/registeredFile.c
        benchmarking/test/udpServer/multishot/ex.c
        benchmarking/test/udpServer/standard/provided.c
)
