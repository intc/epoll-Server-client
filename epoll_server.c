#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<sys/epoll.h>
#include<sys/types.h>
#include<netinet/in.h>
#include<arpa/inet.h>

			/* For strlen(): */
#include<string.h>

			/* For fcntl(): */
#include<fcntl.h>

			/* For errno: */
#include<errno.h>

#define MAX_EVENTS 20
#define EVENT_ARRAY_SIZE sizeof(struct epoll_event) * MAX_EVENTS
#define EPOLL_TIMEOUT 1000
#define CBUF_ELEMENTS 1024

#define BUF_SIZE  sizeof(char) * (CBUF_ELEMENTS - 1)

int counter = 0;

int fd_nonblock(int);
int startup(int);
void handler_events(int, struct epoll_event *, int, int);

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

	struct sockaddr_in local; memset(&local, 0, sizeof(local));
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

void handler_events(int epfd, struct epoll_event *epoll_events, int event_n, int listen_sock) {

	if ( event_n > 4 ) fprintf(stderr,"Processing %i out of %i events.\n", event_n, MAX_EVENTS);

	for( int i = 0 ; i < event_n ; i++ ) {

		struct epoll_event ev = epoll_events[i];
			/* ev.data.fd is assigned by epoll_wait to listening socket */
		if ( ev.data.fd == listen_sock && (ev.events & EPOLLIN) ) {
			struct sockaddr_in client; memset(&client, 0, sizeof(client));
			socklen_t len = sizeof(client);
			/* Accept reassignes ev.data.fd to the connected socket: */
			if ( (ev.data.fd = accept(ev.data.fd, (struct sockaddr*)&client, &len)) < 0 ) {
				perror("accept");
				continue;
			}

			fprintf(stderr, "INFO: New connection. Connected socket fd: %i\n", ev.data.fd);

			struct epoll_event t; memset(&t, 0, sizeof(t));
			t.events = EPOLLIN; t.data.fd = ev.data.fd;
			/* Register new clinet socket to epfd epoll instance
			 * with read (EPOLLIN) association: */
			epoll_ctl(epfd, EPOLL_CTL_ADD, t.data.fd, &t);

			/* Let's switch the client socket to non blocking mode */
			fd_nonblock(ev.data.fd);

			continue;
		}
		if ( ev.events & EPOLLIN ) {
			char buf[CBUF_ELEMENTS];
			while(1) {
				ssize_t s = read(ev.data.fd, buf, BUF_SIZE);
				if ( s > 0 ) {
					buf[s]='\0'; printf("%s", buf);
				}
				else if ( s == 0 ) {
			/* EOF -> Close server end: */
					fprintf(stderr, "INFO: Client disconnected\n");
					close(ev.data.fd);
					epoll_ctl(epfd, EPOLL_CTL_DEL, ev.data.fd, NULL);
			/* EXIT POINT (while) */
					break;
				}
				else {
					if ( errno == EWOULDBLOCK ) {
			/* Reading is ready. Switch over to sending: */
						struct epoll_event t; memset(&t, 0, sizeof(t));
						t.events = EPOLLOUT; t.data.fd = ev.data.fd;
						epoll_ctl(epfd, EPOLL_CTL_MOD, t.data.fd, &t);
			/* EXIT POINT (while): */
						break;
					} else if ( errno == ECONNRESET) {
						perror("read");
						fprintf(stderr, "INFO: Client disconnected\n");
						close(ev.data.fd);
						epoll_ctl(epfd, EPOLL_CTL_DEL, ev.data.fd, NULL);
			/* EXIT POINT (while): */
						break;
					} else {
						perror("read");
						close(ev.data.fd);
						epoll_ctl(epfd, EPOLL_CTL_DEL, ev.data.fd, NULL);
			/* EXIT POINT (while) - Process will be terminated */
						exit(1);
					}
				}
			}
			continue;
		}
		if ( ev.events & EPOLLOUT ) {
			char echo[1024];
			counter++;
			sprintf(echo, "HTTP/1.1 200 OK\r\n\r\n<html>Hi From Epoll %i!\
					</html>\n", counter);
			if ( write(ev.data.fd, echo, strlen(echo)) < 0 ) {
				perror("write");
				exit(1);
			}
#ifdef PERSISTENT_CONN
			struct epoll_event t; memset(&t, 0, sizeof(t));
			t.events = EPOLLIN; t.data.fd = ev.data.fd;
			/* modify back to read(): */
			epoll_ctl(epfd, EPOLL_CTL_MOD, t.data.fd, &t);
#else
			close(ev.data.fd);
			/* Remove this client socket ev.data.fd */
			epoll_ctl(epfd, EPOLL_CTL_DEL, ev.data.fd, NULL);
#endif
		}
	}
}

int main(int argc,char* argv[])
{
	if ( argc != 2 ) {
		printf("Usage:%s port\n",argv[0]);
		return 1;
	}

	printf("BUF_SIZE: %li\n", BUF_SIZE);

	int epfd = epoll_create(256);//绝对是3

	if ( epfd < 0 ) {
		perror("epoll_create");
		return 2;
	}

	int listen_sock = startup(atoi(argv[1]));
			/* Using memset on all structures which are passed
			 * to system calls in order to enusre that padded
			 * bytes are set to zero. */
	struct epoll_event ev; memset(&ev, 0, sizeof(ev));
	ev.events = EPOLLIN;
	ev.data.fd = listen_sock;//把listen_sock托管起来

	epoll_ctl(epfd, EPOLL_CTL_ADD, listen_sock, &ev);

	struct epoll_event epoll_events[MAX_EVENTS];
	memset(epoll_events, 0, EVENT_ARRAY_SIZE);

	int event_n = 0;

	for( ;; ) {
		switch( (event_n = epoll_wait(epfd, epoll_events, MAX_EVENTS, EPOLL_TIMEOUT)) ){
			case -1:
				perror("epoll_wait");
				break;
			case 0:
				/* Execution visits here with an interval of EPOLL_TIMEOUT */
				break;
			default:
				handler_events(epfd, epoll_events, event_n, listen_sock);
				break;
		}
	}

	close(epfd);
	close(listen_sock);
	return 0;
}
