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
        benchmarking/io_uring/udp/standard/standard_batching.c
        benchmarking/socket/server.c
        pktgen/newPacketGen/udpPacketGen.c
        pktgen/OldPacketGen/udpgen.c
        benchmarking/io_uring/udp/kernelPooling/kernel_polling.c
        benchmarking/io_uring/udp/submitAndWait/submit_and_wait.c
        benchmarking/io_uring/udp/multishot/single_multishot.c
        benchmarking/io_uring/udp/multishot/ex.c
        benchmarking/io_uring/udp/multishot/multiple_multishot.c
        benchmarking/io_uring/udp/multishot/multi_multishot.c
        benchmarking/io_uring/udp/standardWork/standard_batching.c
        benchmarking/io_uring/udp/standardWork/socket.c
        benchmarking/io_uring/tcp/standard/standard_batching.c
        benchmarking/io_uring/tcp/standard/multiple_conn.c
        benchmarking/io_uring/udp/standard/mutiple_sockets.c
        benchmarking/io_uring/tcp/multishot/mshot_single_conn.c
        benchmarking/io_uring/udp/liburing_ex.c
        benchmarking/socket/forw.c
        benchmarking/io_uring/udp/standard/forwarder.c
        benchmarking/io_uring/udp/standard/forwarder.c
        benchmarking/io_uring/udp/standard/forwarder.c
        benchmarking/socket/forward/raw_proxy.c
)
