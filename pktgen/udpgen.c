#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <getopt.h>

#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <sys/fcntl.h>

#include <poll.h>

#include <sched.h>
#include <pthread.h>

#include "include/log.h"

#define BUFSIZE 1024
#define TIMOUT_INTERVAL 2000

struct arguments {
	int target_port;
	char *target_addr;
	int rate;
	int max_pending;
	int threads;
	int duration;
	char one_dir;
	int pkt_size;
};

struct worker {
	pthread_t thread;
	int sock_fd;
	volatile int run;
	int rate;
	long long int recv; /* responses received from server */
	long long int sent;     /* messages sent */

	int pending;

	char *buf;
	int bufsize;
	int remaining;
	int written;
};

static struct arguments args;

struct request {
	int req_type;
	unsigned int payload_length;
} __attribute__((__packed__));

static void usage()
{
	INFO("Usage:\n"
		"* --help     -h: show this message\n"
		"* --port     -p: set the server port     (8080)\n"
		"* --ip       -i: set the server ip addr  (127.0.0.1)\n"
		/* "* --rate  -R: set target request rate\n" */
		"* --pending  -P: set maximum number of pending \n"
		"                 request for each thread (4)\n"
		"* --thread   -t: set number of threads   (1)\n"
		"* --duration -d: set experiment duration (10)\n"
		"* --one        : run experiment in one direction\n"
	);
}

static int parse_args(int argc, char *argv[])
{
	int ret;

	struct option long_opts[] = {
		{"help",     no_argument,       NULL, 'h'},
		{"port",     required_argument, NULL, 'p'},
		{"ip",       required_argument, NULL, 'i'},
		{"rate",     required_argument, NULL, 'R'},
		{"pendign",  required_argument, NULL, 'P'},
		{"thread",   required_argument, NULL, 't'},
		{"duration", required_argument, NULL, 'd'},
		{"one",      no_argument,       NULL, 129},
		{"size",     required_argument, NULL, 's'},
		/* End of option list ------------------- */
		{NULL, 0, NULL, 0},
	};

	/* Default values */
	args.target_port = 8080;
	args.target_addr = "127.0.0.1";
	args.rate = 1000; /* Req / sec */
	args.max_pending = 4;
	args.threads = 1;
	args.duration = 10; /* seconds */
	args.one_dir = 0;
	args.pkt_size = 20;

	while(1) {
		ret = getopt_long(argc, argv, "hp:i:R:P:t:d:s:", long_opts, NULL);
		if (ret == -1)
			break;
		switch (ret) {
			case 'p':
				args.target_port = atoi(optarg);
				break;
			case 'i':
				args.target_addr = optarg;
				break;
			case 'R':
				args.rate = atoi(optarg);
				ERROR("Rate limiting has not implemented yet!\n");
				exit(EXIT_FAILURE);
				break;
			case 'P':
				args.max_pending = atoi(optarg);
				break;
			case 't':
				args.threads = atoi(optarg);
				break;
			case 'd':
				args.duration = atoi(optarg);
				break;
			case 129:
				INFO("Uni-direction Experiment: Client do not wait for response from server!\n");
				args.one_dir = 1;
				break;
			case 's':
				args.pkt_size = atoi(optarg);
				break;
			case 'h':
				usage();
				return 1;
			default:
				usage();
				ERROR("Unknown argument '%s'!\n", argv[optind-1]);
				return 1;
		}
	}

	INFO("Experiment for %d sec.    Server %s:%d\n", args.duration,
			args.target_addr, args.target_port);

	return 0;
}

#define PAYLOAD_SIZE (args.pkt_size-1)
static int prepare_request(void *buf, int len)
{
	struct request *header = buf;
	char *payload = (char *)(header + 1);

	/* TODO: make this configurable */
	/* NOTE: Hard coded protocl */
	header->req_type = 2;
	header->payload_length = PAYLOAD_SIZE;
	memcpy(payload, "a", PAYLOAD_SIZE);

	/* How many bytes to send (header + payload) */
	return sizeof(header) + PAYLOAD_SIZE;
}

static int end_of_response(char *buf, unsigned int len)
{
	/* printf("%s\n", buf); */
	if (buf[len-5] == 'E' && buf[len-4] == 'N' && buf[len-3] == 'D' && buf[len-2] == '\r' && buf[len-1] == '\n') {
		return 1;
	}
	return 0;
}

static inline void prepare_for_new_req(struct worker *wrk, struct pollfd * pfd)
{
	pfd->events = POLLOUT | POLLIN;
	wrk->remaining = prepare_request(wrk->buf, wrk->bufsize);
	wrk->written = 0;
}

void *worker_entry(void *_arg)
{
	int ret;
	int i;
	struct worker *wrk = _arg;
	int fd = wrk->sock_fd;
	char *buf;
	struct sockaddr_in server_addr;
	socklen_t addr_len;

	struct pollfd poll_list[2];
	int num_events;
	int num_sockets;

	num_sockets = 1;
	poll_list[0].fd = fd;
	poll_list[0].events = POLLOUT | POLLIN;

	server_addr.sin_family = AF_INET;
	inet_pton(AF_INET, args.target_addr, &server_addr.sin_addr);
	server_addr.sin_port = htons(args.target_port);

	buf = wrk->buf = malloc(BUFSIZE);
	if (!buf) {
		ERROR("Failed to allocate the worker send buffer\n");
		goto err;
	}
	wrk->bufsize = BUFSIZE;

	wrk->recv = 0;
	wrk->sent = 0;
	wrk->pending = 0;

	/* Send a new request */
	prepare_for_new_req(wrk, &poll_list[0]);

	while (wrk->run) {
		num_events = poll(poll_list, num_sockets, TIMOUT_INTERVAL);
		if (num_events < 0) {
			ERROR("Polling failed! (%s)\n", strerror(errno));
			goto err;
		}

		if (num_events < 0) {
			/* probably the request was lost retransmit */
			/* Send a new request */
			wrk->pending = 0;
			prepare_for_new_req(wrk, &poll_list[0]);
		}

		/* INFO("num events: %d\n", num_events); */

		for (i = 0; i < num_sockets && num_events > 0; i++) {
			if (poll_list[i].revents == 0) {
				continue;
			}

			if (poll_list[i].revents & POLLHUP ||
					poll_list[i].revents & POLLERR ||
					poll_list[i].revents & POLLNVAL) {
				goto err;
				continue;
			}

			if (poll_list[i].revents & POLLOUT) {
				/* INFO("can write\n"); */
				ret = sendto(fd, buf + wrk->written, wrk->remaining, 0,
						(struct sockaddr *)&server_addr,
						sizeof(server_addr));

				if (ret < 0) {
					if (errno != EWOULDBLOCK) {
						ERROR("Failed to send data\n %s", strerror(errno));
						goto err;
					}
					continue;
				}
				wrk->written += ret;
				wrk->remaining -= ret;


				if (wrk->remaining == 0) {
					wrk->pending++;
					wrk->sent++;
					if (!args.one_dir && wrk->pending >= args.max_pending) {
						/* Do not send */
						poll_list[i].events = POLLIN;
					} else {
						prepare_for_new_req(wrk, &poll_list[0]);
					}
					/* INFO("SEND\n"); */
				} else if (wrk->remaining < 0) {
					ERROR("Unexpected! message length is negative!\n");
					goto err;
				} else {
					/* Need to send more data */
					continue;
				}
			}

			if (poll_list[i].events & POLLIN) {
				ret = recvfrom(fd, buf, BUFSIZE, 0, NULL, NULL);
				if (ret == 0)
					continue;
				if (ret < 0) {
					if (errno != EWOULDBLOCK) {
						ERROR("Unexpected return value when reading the socket");
						goto err;
					}
					continue;
				}
				/* buf[ret] = '\0'; */
				if (end_of_response(buf, ret)) {
					/* Send a new request */
					prepare_for_new_req(wrk, &poll_list[0]);
					wrk->pending--;
					wrk->recv++;
				}
			}
		}
	}

	close(fd);
	return NULL;
err:
	free(wrk->buf);
	close(fd);
	return (void *)-1;
}

static int launch_worker(struct worker *wrk)
{
	int ret;
	int opt_val;
	int sk_fd;
	struct sockaddr_in sk_addr;

	sk_fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sk_fd < 1) {
		ERROR("Failed to open a socket!\n");
		return 1;
	}

	/* opt_val = 1; */
	/* ret = setsockopt(sk_fd, SOL_SOCKET, SO_REUSEPORT, &opt_val, sizeof(opt_val)); */
	/* if (ret) { */
	/* 	ERROR("Failed to set SO_REUSEPORT\n"); */
	/* 	return 1; */
	/* } */
	/* ret = setsockopt(sk_fd, SOL_SOCKET, SO_REUSEADDR, &opt_val, sizeof(opt_val)); */
	/* if (ret) { */
	/* 	ERROR("Failed to set SO_REUSEADDR\n"); */
	/* 	return 1; */
	/* } */
	if (fcntl(sk_fd, F_SETFL, O_NONBLOCK)) {
		ERROR("Failed to set O_NONBLOCK flag!\n");
		return 1;
	}

	wrk->sock_fd = sk_fd;
	wrk->run = 1;
	pthread_create(&wrk->thread, NULL, worker_entry, wrk);
	return 0;
}

int main(int argc, char *argv[])
{
	int i;
	struct worker *workers;

	if (parse_args(argc, argv)) {
		return EXIT_FAILURE;
	}

	workers = calloc(args.threads, sizeof(struct worker));
	if (!workers) {
		ERROR("Failed to allocate the workers array!");
		return EXIT_FAILURE;
	}

	for (i = 0; i < args.threads; i++) {
		if (launch_worker(&workers[i]) != 0) {
			goto err;
		}
	}

	/* TODO also end with SIGINT */
	/* Wait for some time */
	sleep(args.duration);

	/* Stop all threads */
	for (i = 0; i < args.threads; i++) {
		workers[i].run = 0;
	}
	for (i = 0; i < args.threads; i++) {
		pthread_join(workers[i].thread, NULL);
	}

	int total_recv = 0;
	int total_sent = 0;
	printf("          RECV        SENT\n");
	for (i = 0; i < args.threads; i++) {
		total_recv += workers[i].recv;
		total_sent += workers[i].sent;
		INFO("thread %d: %d        %d\n", i, workers[i].recv, workers[i].sent);
	}
	INFO("total recv: %d\n", total_recv);
	INFO("total sent: %d\n", total_sent);
	INFO("duration: %d\n", args.duration);
	INFO("Recv Throughput: %.2f\n", (double)total_recv / (double)args.duration);
	INFO("Sent Throughput: %.2f\n", (double)total_sent / (double)args.duration);

	return 0;

err:
	return EXIT_FAILURE;
}
