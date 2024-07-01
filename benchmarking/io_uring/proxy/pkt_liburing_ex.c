/* SPDX-License-Identifier: MIT */

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/udp.h>
#include <arpa/inet.h>
#include <getopt.h>
#include <liburing/io_uring.h>
#include <liburing.h>
#include <sys/socket.h>
#include <signal.h>
#include <linux/ip.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include "liburing.h"

#define QD 64
#define BUF_SHIFT 12 /* 4k */
#define CQES (QD * 16)
#define BUFFERS CQES
#define CONTROLLEN 0

int start = 0;
long pkt_count = 0;
int sockfd;
struct ctx* ctxs;
int coop = 0;
int single = 0;
int napi = 0;
int napi_timeout = 0;
int batching = 1;
struct ifreq if_idx;
struct ifreq if_mac;
int size = 64;

struct sendmsg_ctx {
    struct msghdr msg;
    struct iovec iov;
};

void print_usage(){
      printf("-p  port\n-b  receiving batching size\n-b  log2(BufferSize) NOTE: the buffer will "
             "include the io_uring_recvmsg_out struct so it needs to be bigger than the packet size (ex -b 8 for 64 bytes packets)\n"
             "-C  enable coop option\n-S  enable single issue option\n-N  enable napi\n-n  napi timeout");
}

struct ctx {
    struct io_uring ring;
    struct io_uring_buf_ring *buf_ring;
    unsigned char *buffer_base;
    struct msghdr msg;
    int buf_shift;
    int af;
    bool verbose;
    struct sendmsg_ctx send[BUFFERS];
    size_t buf_ring_size;
    int duration;
};

static size_t buffer_size(struct ctx *ctx)
{
      return 1U << ctx->buf_shift;
}

static unsigned char *get_buffer(struct ctx *ctx, int idx)
{
      return ctx->buffer_base + (idx << ctx->buf_shift);
}

static int setup_buffer_pool(struct ctx *ctx)
{
      int ret, i;
      void *mapped;
      struct io_uring_buf_reg reg = { .ring_addr = 0,
              .ring_entries = BUFFERS,
              .bgid = 0 };

      ctx->buf_ring_size = (sizeof(struct io_uring_buf) + buffer_size(ctx)) * BUFFERS;
      mapped = mmap(NULL, ctx->buf_ring_size, PROT_READ | PROT_WRITE,
                    MAP_ANONYMOUS | MAP_PRIVATE, 0, 0);
      if (mapped == MAP_FAILED) {
            fprintf(stderr, "buf_ring mmap: %s\n", strerror(errno));
            return -1;
      }
      ctx->buf_ring = (struct io_uring_buf_ring *)mapped;

      io_uring_buf_ring_init(ctx->buf_ring);

      reg = (struct io_uring_buf_reg) {
              .ring_addr = (unsigned long)ctx->buf_ring,
              .ring_entries = BUFFERS,
              .bgid = 0
      };
      ctx->buffer_base = (unsigned char *)ctx->buf_ring +
                         sizeof(struct io_uring_buf) * BUFFERS;

      ret = io_uring_register_buf_ring(&ctx->ring, &reg, 0);
      if (ret) {
            fprintf(stderr, "buf_ring init failed: %s\n"
                            "NB This requires a kernel version >= 6.0\n",
                    strerror(-ret));
            return ret;
      }

      for (i = 0; i < BUFFERS; i++) {
            io_uring_buf_ring_add(ctx->buf_ring, get_buffer(ctx, i), buffer_size(ctx), i,
                                  io_uring_buf_ring_mask(BUFFERS), i);
      }
      io_uring_buf_ring_advance(ctx->buf_ring, BUFFERS);

      return 0;
}

static int setup_context(struct ctx *ctx)
{
      struct io_uring_params params;
      int ret;

      memset(&params, 0, sizeof(params));
      params.cq_entries = QD * 8;
      params.flags = IORING_SETUP_SUBMIT_ALL | IORING_SETUP_CQSIZE;
      if(coop)
            params.flags |= IORING_SETUP_COOP_TASKRUN;
      if(single)
            params.flags |= IORING_SETUP_SINGLE_ISSUER;

      ret = io_uring_queue_init_params(QD, &ctx->ring, &params);
      if (ret < 0) {
            fprintf(stderr, "queue_init failed: %s\n"
                            "NB: This requires a kernel version >= 6.0\n",
                    strerror(-ret));
            return ret;
      }

      ret = setup_buffer_pool(ctx);
      if (ret)
            io_uring_queue_exit(&ctx->ring);

      memset(&ctx->msg, 0, sizeof(ctx->msg));
      ctx->msg.msg_namelen = sizeof(struct sockaddr_ll);
      ctx->msg.msg_controllen = CONTROLLEN;
      return ret;
}

static int setup_sock(int af, int port)
{
      int socketfd;
      int opt = 1;
      struct sockaddr_in add;
      struct sockaddr_ll bind_ll;
      long res;
      char interface[] = "ens1f1np1";

      if((socketfd = socket(AF_PACKET,SOCK_RAW,htons(ETH_P_IP)))<0){
            perror("socket");
            return 0;
      }


      memset(&if_idx, 0, sizeof(struct ifreq));
      strncpy(if_idx.ifr_name, interface ,9);
      if (ioctl(socketfd, SIOCGIFINDEX, &if_idx) < 0)
            perror("SIOCGIFINDEX");
      memset(&if_mac, 0, sizeof(struct ifreq));

      strncpy(if_mac.ifr_name, interface, 9);
      if (ioctl(socketfd, SIOCGIFHWADDR, &if_mac) < 0)
            perror("SIOCGIFHWADDR");


      bind_ll.sll_family = AF_PACKET;
      bind_ll.sll_protocol = htons(ETH_P_IP);
      bind_ll.sll_ifindex = if_idx.ifr_ifindex;

      printf("INDEX OF INTERFACE %d\n",if_idx.ifr_ifindex);

      if((res = bind(socketfd, (struct sockaddr*)&bind_ll, sizeof(struct sockaddr_ll)))<0){
            printf("bind error:%ld\n",res);
      }

      return socketfd;
}

static void cleanup_context(struct ctx *ctx)
{
      munmap(ctx->buf_ring, ctx->buf_ring_size);
      io_uring_queue_exit(&ctx->ring);
}

static bool get_sqe(struct ctx *ctx, struct io_uring_sqe **sqe)
{
      *sqe = io_uring_get_sqe(&ctx->ring);

      if (!*sqe) {
            io_uring_submit(&ctx->ring);
            *sqe = io_uring_get_sqe(&ctx->ring);
      }
      if (!*sqe) {
            fprintf(stderr, "cannot get sqe\n");
            return true;
      }
      return false;
}

static int add_recv(struct ctx *ctx, int idx)
{
      struct io_uring_sqe *sqe;

      if (get_sqe(ctx, &sqe))
            return -1;

      io_uring_prep_recvmsg_multishot(sqe, idx, &ctx->msg, MSG_TRUNC);
      sqe->flags |= IOSQE_FIXED_FILE;

      sqe->flags |= IOSQE_BUFFER_SELECT;
      sqe->buf_group = 0;
      io_uring_sqe_set_data64(sqe, BUFFERS + 1);
      return 0;
}

static void recycle_buffer(struct ctx *ctx, int idx)
{
      io_uring_buf_ring_add(ctx->buf_ring, get_buffer(ctx, idx), buffer_size(ctx), idx,
                            io_uring_buf_ring_mask(BUFFERS), 0);
      io_uring_buf_ring_advance(ctx->buf_ring, 1);
}

static int process_cqe_send(struct ctx *ctx, struct io_uring_cqe *cqe)
{
      int idx = cqe->user_data;
      pkt_count++;

      if (cqe->res < 0)
            fprintf(stderr, "bad send %s\n", strerror(-cqe->res));
      recycle_buffer(ctx, idx);
      return 0;
}

static int process_cqe_recv(struct ctx *ctx, struct io_uring_cqe *cqe,
                            int fdidx)
{
      int ret, idx;
      struct io_uring_recvmsg_out *o;
      struct io_uring_sqe *sqe;

      if (!start) {
            start = 1;
            alarm(ctx->duration);
      }

      if (!(cqe->flags & IORING_CQE_F_MORE)) {
            ret = add_recv(ctx, fdidx);
            if (ret)
                  return ret;
      }

      if (cqe->res == -ENOBUFS)
            return 0;

      if (!(cqe->flags & IORING_CQE_F_BUFFER) || cqe->res < 0) {
            fprintf(stderr, "recv cqe bad res %d\n", cqe->res);
            if (cqe->res == -EFAULT || cqe->res == -EINVAL)
                  fprintf(stderr,
                          "NB: This requires a kernel version >= 6.0\n");
            return -1;
      }
      idx = cqe->flags >> 16;

      o = io_uring_recvmsg_validate(get_buffer(ctx, cqe->flags >> 16),
                                    cqe->res, &ctx->msg);
      if (!o) {
            fprintf(stderr, "bad recvmsg\n");
            return -1;
      }
      if (o->namelen > ctx->msg.msg_namelen) {
            fprintf(stderr, "truncated name\n");
            recycle_buffer(ctx, idx);
            return 0;
      }
      if (o->flags & MSG_TRUNC) {
            unsigned int r;

            r = io_uring_recvmsg_payload_length(o, cqe->res, &ctx->msg);
            fprintf(stderr, "truncated msg need %u received %u\n",
                    o->payloadlen, r);
            recycle_buffer(ctx, idx);
            return 0;
      }

      if (get_sqe(ctx, &sqe))
            return -1;

      ctx->send[idx].iov = (struct iovec) {
              .iov_base = io_uring_recvmsg_payload(o, &ctx->msg),
              .iov_len =
              io_uring_recvmsg_payload_length(o, cqe->res, &ctx->msg)
      };
      ctx->send[idx].msg = (struct msghdr) {
              .msg_namelen = o->namelen,
              .msg_name = io_uring_recvmsg_name(o),
              .msg_control = NULL,
              .msg_controllen = 0,
              .msg_iov = &ctx->send[idx].iov,
              .msg_iovlen = 1
      };

      struct sockaddr_ll* addr = ctx->send[idx].msg.msg_name;
      char* buffer = ctx->send[idx].iov.iov_base;
      struct ether_header *eh = (struct ether_header *) buffer;
      struct iphdr *iph = (struct iphdr *) (buffer + sizeof(struct ether_header));
      struct udphdr *udph = (struct udphdr *) (buffer + sizeof(struct iphdr) + sizeof(struct ether_header));

      eh->ether_dhost[0] = eh->ether_shost[0];
      eh->ether_dhost[1] = eh->ether_shost[1];
      eh->ether_dhost[2] = eh->ether_shost[2];
      eh->ether_dhost[3] = eh->ether_shost[3];
      eh->ether_dhost[4] = eh->ether_shost[4];
      eh->ether_dhost[5] = eh->ether_shost[5];
      eh->ether_shost[0] = ((uint8_t *)&if_mac.ifr_hwaddr.sa_data)[0];
      eh->ether_shost[1] = ((uint8_t *)&if_mac.ifr_hwaddr.sa_data)[1];
      eh->ether_shost[2] = ((uint8_t *)&if_mac.ifr_hwaddr.sa_data)[2];
      eh->ether_shost[3] = ((uint8_t *)&if_mac.ifr_hwaddr.sa_data)[3];
      eh->ether_shost[4] = ((uint8_t *)&if_mac.ifr_hwaddr.sa_data)[4];
      eh->ether_shost[5] = ((uint8_t *)&if_mac.ifr_hwaddr.sa_data)[5];

      addr->sll_ifindex = if_idx.ifr_ifindex;
      addr->sll_protocol = htons(ETH_P_IP);
      addr->sll_halen = ETH_ALEN;
      addr->sll_addr[0] = eh->ether_dhost[0];
      addr->sll_addr[1] = eh->ether_dhost[1];
      addr->sll_addr[2] = eh->ether_dhost[2];
      addr->sll_addr[3] = eh->ether_dhost[3];
      addr->sll_addr[4] = eh->ether_dhost[4];
      addr->sll_addr[5] = eh->ether_dhost[5];

      io_uring_prep_sendto(sqe,sockfd,buffer,size,0,(struct sockaddr*)addr,sizeof(struct sockaddr_ll));
      io_uring_sqe_set_data64(sqe, idx);
      sqe->flags |= IOSQE_FIXED_FILE;

      return 0;
}
static int process_cqe(struct ctx *ctx, struct io_uring_cqe *cqe, int fdidx)
{
      if (cqe->user_data < BUFFERS)
            return process_cqe_send(ctx, cqe);
      else
            return process_cqe_recv(ctx, cqe, fdidx);
}

void sig_handler(int signum){
      printf("packet rate %ld pkts/s\n",pkt_count/(long)ctxs->duration);
      cleanup_context(ctxs);
      close(sockfd);
}

int main(int argc, char *argv[])
{
      struct ctx ctx;
      int ret;
      int port = -1;
      int opt;
      ctxs = &ctx;

      struct io_uring_cqe *cqes[CQES];
      unsigned int count, i;

      memset(&ctx, 0, sizeof(ctx));
      ctx.verbose = false;
      ctx.af = AF_INET;
      ctx.buf_shift = BUF_SHIFT;
      ctx.duration = 10;

      while ((opt = getopt(argc, argv, "6vp:b:d:CSNn:h")) != -1) {
            switch (opt) {
                  case 'C':
                        coop = 1;
                        break;
                  case 'S':
                        single = 1;
                        break;
                  case 'N':
                        napi = 1;
                        break;
                  case 'n':
                        napi_timeout = atoi(optarg);
                        break;
                  case 'b':
                        batching = atoi(optarg);
                        break;
                  case '6':
                        ctx.af = AF_INET6;
                        break;
                  case 'p':
                        port = atoi(optarg);
                        break;
                  case 'B':
                        ctx.buf_shift = atoi(optarg);
                        break;
                  case 's':
                        size = atoi(optarg);
                        break;
                  case 'v':
                        ctx.verbose = true;
                        break;
                  case 'd':
                        ctx.duration = atoi(optarg);
                        break;
                  case 'h':
                        print_usage();
                        exit(-1);
            }
      }

      signal(SIGALRM,sig_handler);
      sockfd = setup_sock(ctx.af, port);
      if (sockfd < 0)
            return 1;

      if (setup_context(&ctx)) {
            close(sockfd);
            return 1;
      }

      ret = io_uring_register_files(&ctx.ring, &sockfd, 1);
      if (ret) {
            fprintf(stderr, "register files: %s\n", strerror(-ret));
            return -1;
      }

      if (napi) {
            struct io_uring_napi n = {
                    .prefer_busy_poll = napi > 1 ? 1 : 0,
                    .busy_poll_to = napi_timeout,
            };

            ret = io_uring_register_napi(&ctx.ring, &n);
            if (ret) {
                  fprintf(stderr, "io_uring_register_napi: %d\n", ret);
                  if (ret != -EINVAL)
                        return 1;
                  fprintf(stderr, "NAPI not available, turned off\n");
            }
      }

      ret = add_recv(&ctx, 0);
      if (ret)
            return 1;

      while (true) {
            ret = io_uring_submit_and_wait(&ctx.ring, batching);
            if (ret == -EINTR)
                  continue;
            if (ret < 0) {
                  fprintf(stderr, "submit and wait failed %d\n", ret);
                  break;
            }

            count = io_uring_peek_batch_cqe(&ctx.ring, &cqes[0], CQES);
            for (i = 0; i < count; i++) {
                  ret = process_cqe(&ctx, cqes[i], 0);
                  if (ret)
                        goto cleanup;
            }
            io_uring_cq_advance(&ctx.ring, count);
      }

      cleanup:
      cleanup_context(&ctx);
      close(sockfd);
      return ret;
}