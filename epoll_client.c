#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<sys/socket.h>
#include<string.h>
#include<netinet/in.h>
			/* inet_pton(): */
#include<arpa/inet.h>

int main(int argc,char* argv[])
{
	if(argc!=3)
	{
		printf("Usage :./select_client [server_address] [server_port]\n");
		return 1;
	}

	struct sockaddr_in client_s;
	client_s.sin_port = htons(atoi(argv[2]));
	client_s.sin_family = AF_INET;
			 /* Convert dotted IPv4 address to binary (network byte order): */
	if ( inet_pton( AF_INET, argv[1], &client_s.sin_addr ) == 0 ) {
		fprintf(stderr, "illegal [server_address] - exit (1)\n");
		return -1;
	}
	int n = 0;
	while(1){
		n++;
			/* Create socket: */
		int sock = socket(AF_INET,SOCK_STREAM,0);
		if(sock<0)
		{
			perror("socket");
			return 1;
		}
			/* 连接服务器 - Connect to server: */
		if( connect( sock, (struct sockaddr*) &client_s, sizeof(client_s) )<0 )
		{
			perror("connect");
			return 1;
		}
		int g = 0;
		for(;;)
		{
			g++;
			/* 输入消息并刷新缓冲区 */
			printf("client >");
			fflush(stdout);
			/* 将消息读到buf里 */
			char buf[10240];
			sprintf(buf, "testing n: %i g: %i\n", n, g);
			/* 将消息写给文件描述符 */
			if(write(sock,buf,strlen(buf))<0){
				perror("write");
				continue;
			}
			/* 将服务器返回的消息写到buf里 */
			int ret = read(sock,buf,sizeof(buf)-1);
			if(ret<0){
				perror("read");
				break;
			}
			if(ret==0)
			{
				printf("server close\n");
				break;
			}
			/* Ensure null termination (for printf): */
			buf[ret]='\0';
			printf("server:%s\n",buf);
		}
		close(sock);
			/* END OF while(1) */

	}
	return 0;
}
