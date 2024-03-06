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
        benchmarking/test/io_uring/standard/standard_batching.c
        benchmarking/test/socket/server.c
        pktgen/newPacketGen/udpPacketGen.c
        pktgen/OldPacketGen/udpgen.c
        benchmarking/test/io_uring/kernelPooling/kernel_polling.c
        benchmarking/test/io_uring/submitAndWait/submit_and_wait.c
        benchmarking/test/io_uring/multishot/single_multishot.c
        benchmarking/test/io_uring/standard/standard_registered_file.c
        benchmarking/test/io_uring/multishot/ex.c
        benchmarking/test/io_uring/multishot/multiple_multishot.c
        benchmarking/test/io_uring/standard/standard_non_batching.c
        benchmarking/test/io_uring/standard/standard_single_issue.c
        benchmarking/test/io_uring/standard/standard_coopTaskrun.c
        benchmarking/test/io_uring/standard/standard_single_and_coop.c
)
