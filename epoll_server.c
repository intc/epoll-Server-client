#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
			/* For strlen(): */
#include <string.h>
			/* For fcntl(): */
#include <fcntl.h>
			/* For errno: */
#include <errno.h>
			/* For signal(), sig_atomic_t: */
#include <signal.h>

#define MAX_EVENTS 20
#define EVENT_ARRAY_SIZE sizeof(struct epoll_event) * MAX_EVENTS
#define EPOLL_TIMEOUT 500
#define CBUF_ELEMENTS 1024

#define BUF_SIZE  sizeof(char) * (CBUF_ELEMENTS - 1)

typedef struct ep_args {
	int epfd;                           /* epoll file descriptor */
	struct epoll_event *epoll_ev_ar;    /* array of events */
	struct epoll_event t;               /* temporary event structure */
	int event_n;                        /* number of currently active events */
	int listen_sock;                    /* server listening socket */
} ep_args;

int counter = 0;
			/* volatile for denying compiler to reorder r/w to the variable
			 * sig_atomic_t for selecting type which is not reordered by
			 * platforms CPU (load/store are atomic). */
volatile sig_atomic_t sig_flag = 0;

void sig_handler(int);
int fd_nonblock(int);
int startup(int);
void set_epoll_ctl(int, int, int, struct epoll_event *, int);
void ep_event_handler(ep_args *);
void ep_args_init(ep_args *);

void sig_handler(int sig) {
	signal(SIGTERM, SIG_IGN);
	signal(SIGINT, SIG_IGN);
	sig_flag = sig;
}

int fd_nonblock(int fd) {

	int fd_flags = 0;
			/* Get file descriptor flags:
			 * ( arg (3rd argument) will be ignored ) */
	if ( ( fd_flags = fcntl(fd, F_GETFL, NULL) ) == -1 ) {
		perror("fcntl - F_GETFL");
		return -1;
	}

	fd_flags |= O_NONBLOCK;

	if ( fcntl(fd, F_SETFL, fd_flags) ) {
		perror("fnctl - F_SETFL");
		return -1;
	}

	return 0;
}

int startup(int port) {

	int sock = socket(AF_INET,SOCK_STREAM,0);

	if( sock < 0 ) {
		perror("socket");
		exit(3);
	}

	int opt = 1;
	setsockopt(sock,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));

	struct sockaddr_in local;
	local.sin_family = AF_INET;
	local.sin_addr.s_addr = htonl(INADDR_ANY);
	local.sin_port = htons(port);

	if( bind(sock,(struct sockaddr*)&local, sizeof(local)) < 0 ) {
		perror("bind");
		exit(4);
	}

	if( listen(sock, 5) <0 ) {
		perror("listen");
		exit(5);
	}

	printf("Now listeing: %i - READY\n", port);
	return sock;
}

void set_epoll_ctl(int epfd, int ctl_act, int fd, struct epoll_event *t, int t_events) {
	t->data.fd = fd;
	t->events = t_events;
	epoll_ctl(epfd, ctl_act, t->data.fd, t);
}

void ep_event_handler(ep_args *epa) {

	if ( epa->event_n > 4 ) {
		fprintf(stderr,"Processing %i out of %i events.\n", epa->event_n, MAX_EVENTS);
	}

	for( int i = 0 ; i < epa->event_n ; i++ ) {

		struct epoll_event *ev = &epa->epoll_ev_ar[i];
			/* ev.data.fd is assigned by epoll_wait to listening socket */
		if ( ev->data.fd == epa->listen_sock && (ev->events & EPOLLIN) ) {
			struct sockaddr_in client; memset(&client, 0, sizeof(client));
			socklen_t len = sizeof(client);
			/* Accept reassignes ev.data.fd to the connected socket: */
			if ( (ev->data.fd = accept(ev->data.fd, (struct sockaddr*)&client, &len)) < 0 ) {
				perror("accept");
				continue;
			}
			fprintf(stderr, "INFO: New connection. Connected socket fd: %i\n", ev->data.fd);
			/* Register new clinet socket to epfd epoll instance
			 * with read (EPOLLIN) association: */
			set_epoll_ctl(epa->epfd, EPOLL_CTL_ADD, ev->data.fd, &epa->t, EPOLLIN);
			/* Let's switch the client socket to non blocking mode */
			fd_nonblock(ev->data.fd);
			continue;
		}
		if ( ev->events & EPOLLIN ) {
			char buf[CBUF_ELEMENTS];
			while(1) {
				ssize_t s = read(ev->data.fd, buf, BUF_SIZE);
				if ( s > 0 ) {
					buf[s]='\0'; printf("%s", buf);
				}
				else if ( s == 0 ) {
			/* EOF -> Close server end: */
					fprintf(stderr, "INFO: Client disconnected\n");
					close(ev->data.fd);
					epoll_ctl(epa->epfd, EPOLL_CTL_DEL, ev->data.fd, NULL);
			/* EXIT POINT (while) */
					break;
				}
				else {
					if ( errno == EWOULDBLOCK ) {
			/* Reading is ready. Switch over to sending: */
						set_epoll_ctl(epa->epfd, EPOLL_CTL_MOD, ev->data.fd, &epa->t, EPOLLOUT);
			/* EXIT POINT (while): */
						break;
					} else if ( errno == ECONNRESET) {
						perror("read");
						fprintf(stderr, "INFO: Client disconnected\n");
						close(ev->data.fd);
						epoll_ctl(epa->epfd, EPOLL_CTL_DEL, ev->data.fd, NULL);
			/* EXIT POINT (while): */
						break;
					} else {
						perror("read");
						close(ev->data.fd);
						epoll_ctl(epa->epfd, EPOLL_CTL_DEL, ev->data.fd, NULL);
			/* EXIT POINT (while) - Process will be terminated */
						exit(1);
					}
				}
			}
			continue;
		}
		if ( ev->events & EPOLLOUT ) {
			char echo[1024];
			counter++;
			sprintf(echo, "HTTP/1.1 200 OK\r\n\r\n<html>Hi From Epoll %i!\
					</html>\n", counter);
			if ( write(ev->data.fd, echo, strlen(echo)) < 0 ) {
				perror("write");
				exit(1);
			}
#ifdef PERSISTENT_CONN
			/* modify back to read(): */
			set_epoll_ctl(epa->epfd, EPOLL_CTL_MOD, ev->data.fd, &epa->t, EPOLLIN);
#else
			close(ev->data.fd);
			fprintf(stderr, "INFO: NON-PERSISTENT mode. Disconnecting socket %i\n", ev->data.fd);
			/* Remove this client socket ev.data.fd */
			epoll_ctl(epa->epfd, EPOLL_CTL_DEL, ev->data.fd, NULL);
#endif
		}
	}
}

void ep_args_init(ep_args *epa) {
			/* Using memset on all structures which are passed
			 * to system calls in order to ensure that padded
			 * bytes are set to zero. */
	memset(&epa->t, 0, sizeof(epa->t));
	epa->epoll_ev_ar = malloc(EVENT_ARRAY_SIZE);
	memset(epa->epoll_ev_ar, 0, EVENT_ARRAY_SIZE);
}

int main(int argc,char* argv[])
{
	if ( argc != 2 ) {
		printf("Usage:%s port\n",argv[0]);
		return 1;
	}

	printf("BUF_SIZE: %li\n", BUF_SIZE);

	ep_args epa;
	ep_args_init(&epa);

	if ( (epa.epfd = epoll_create(256)) < 0 ) {
		perror("epoll_create");
		return 2;
	}

	epa.listen_sock = startup(atoi(argv[1]));

	set_epoll_ctl(epa.epfd, EPOLL_CTL_ADD, epa.listen_sock, &epa.t, EPOLLIN);

	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);

	for( ;; ) {
		if ( sig_flag )  {
			fprintf(stderr, "Got signal %i. Exit.\n", sig_flag);
			break;
		}
		switch( (epa.event_n = epoll_wait(epa.epfd, epa.epoll_ev_ar, MAX_EVENTS, EPOLL_TIMEOUT)) ){
			case -1:
				perror("epoll_wait");
				break;
			case 0:
			/* While idling the execution will
			 * reach here after EPOLL_TIMEOUT */
				break;
			default:
				ep_event_handler(&epa);
				break;
		}
	}
	
	free(epa.epoll_ev_ar);
	close(epa.epfd);
	close(epa.listen_sock);
	return 0;
}
