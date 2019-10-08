/*
 * TEMPLATE 
 */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include <stdio.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <netinet/in.h>


#include <rpc/xdr.h>

#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../errlib.h"
#include "../sockwrap.h"

#define MAXBUF_OUT 2048
#define MAXBUF_IN 100

char* prog_name;

int service(int s, char cmd[], char filename[]);
void err_send(int s);
void err_close(FILE *fp, char *str_err);

int main (int argc, char *argv[])
{
	int sock_in, sock_out,bklog = 2, stop;
	short port;
	struct sockaddr_in servaddr, cliaddr;
	socklen_t cli_siz;
	fd_set fd;
    struct timeval t_val;
	char buf_rec[MAXBUF_IN], cmd[MAXBUF_IN], filename[MAXBUF_IN];
	
	prog_name = argv[0]; // program name is necessary for errlib

	if(argc != 2){
		err_quit("Usage: ./progname port\n");
	}

	printf("creating socket...\n");
    sock_in = Socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    printf("done, socket number %u\n",sock_in);

	port=atoi(argv[1]);

	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(port);
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);

	Bind(sock_in, (SA*) &servaddr, sizeof(servaddr));
	printf("Socket binded at port %d\n", port);

    Listen(sock_in, bklog);
	printf ("Listening at socket %d with backlog = %d \n",sock_in, bklog);

	while(1){
		printf("Waiting for a connection..\n");
		memset(&cli_siz, 0, sizeof(cli_siz));
		int sock_out = Accept(sock_in, (SA*) &cliaddr, &cli_siz);
		stop = 0;
		printf("============================================================================\n");
		printf("Accepted connection from %s:%d\n", inet_ntoa(cliaddr.sin_addr), ntohs(cliaddr.sin_port));
		// Set time of 15 to receive the GET
		FD_ZERO(&fd);
    	FD_SET(sock_out, &fd);
    	t_val.tv_sec = 15; t_val.tv_usec = 0;
		printf("Waiting GET message from client...\n");
		while(stop == 0){
			int n = Select(FD_SETSIZE, &fd, NULL, NULL, &t_val);
			if (n > 0){
				if(FD_ISSET(sock_out, &fd)){
					int len = readline(sock_out, buf_rec, MAXBUF_IN);
					if(len > 0){
						if(sscanf(buf_rec, "%s %s\r\n", cmd, filename)!=2){
							printf("ERROR: Wrong format MESSAGE\n");
							err_send(sock_out);
							stop = 1;
						}
						else{
							if(service(sock_out, cmd, filename)<0){
								err_msg("ERROR: Service not done\n");
								err_send(sock_out);
								stop = 1;
							}
							else{
								printf("Service performed correctly\n");
								printf("============================================================================\n");
							}
						}
					}
					else if (len == 0)
					{
						close(sock_out);
						stop = 1;
						printf("Requests finished\n");
						printf("============================================================================\n");
					}
					else if (len < 0)
					{
						err_msg("ERROR: System call send()\n");
						err_send(sock_out);
						stop = 1;
					}
				}
			}
			else{
				err_msg("ERROR: client did not send anything\n");
				err_send(sock_out);
				stop = 1;
			}
			bzero(buf_rec, MAXBUF_OUT);
		}
	}

	return 0;
}
// Error function to send "-ERR" and close connection
void err_send(int s){
	char err[6]="-ERR\r\n";
	send(s, err, sizeof(err), MSG_NOSIGNAL);
	close(s);
}
// function to serve client: read command and send file
int service(int s, char cmd[], char filename[]){
	FILE *fp;
	char response[9], timemod[4];
	char buf_out[MAXBUF_OUT];
	struct stat filestat;
	u_int32_t  mod, size;
	int f_block_sz;

	printf("Request received: %s %s\n", cmd, filename);

	if(strcmp(cmd, "GET")!=0){
		err_msg("ERROR: Wrong command\n");
		return -1;
	}
	fp = fopen(filename, "r");\
	if(fp == NULL){
		err_msg("ERROR: File could not be opened");
		return -1;
	}
	if(stat(filename,&filestat)<0){
		err_close(fp, "ERROR: File stat not avaible\n");
		return -1;
	}
	size = htonl(filestat.st_size);

	printf("Sending OK and size: %u\n", ntohl(size));
	sprintf(response, "+OK\r\n");
	memcpy(response+5, &size, 4);
	mod = htonl(filestat.st_mtime);
	memcpy(response+9, &mod, 4);
	int l = send(s, response, sizeof(response), MSG_NOSIGNAL); //send OK before starting
	if(l != sizeof(response)){
		err_close(fp, "ERROR: size not sent\n");
		return -1;
	}

	bzero(buf_out, MAXBUF_OUT);
	printf("Sending file...\n");
	while((f_block_sz = fread(buf_out, sizeof(char), MAXBUF_OUT, fp))>0)
	{
		if(send(s, buf_out, f_block_sz, MSG_NOSIGNAL) <= 0)
		{
			err_close(fp,"ERROR: Failed to send file\n");
			return -1;
		}
		bzero(buf_out, MAXBUF_OUT);
	}
	printf("File %s sent to client\n", filename);
	fclose(fp);
	printf("Sending time from epoch\n");
	
	mod = htonl(filestat.st_mtime);
	memcpy(timemod, &mod, 4);

	l = send(s, timemod, sizeof(timemod), MSG_NOSIGNAL); //send time from epoch*/
	if(l != sizeof(timemod)){
		err_msg("ERROR: timemod not sent\n");
		return -1;
	}
	
	return 0;
}

// Function to close file before returning to avoid memory leakage 
void err_close(FILE *fp, char *str_err){
	err_msg("%s\n", str_err);
	fclose(fp);
}