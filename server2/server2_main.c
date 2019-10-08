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
#include<sys/wait.h> 

#include "../errlib.h"
#include "../sockwrap.h"

#define MAXBUF_OUT 512
#define MAXBUF_IN 100

char* prog_name;
int pid;

int service(int s, char cmd[], char filename[]);
void err_send(int s, char *str_err);
void err_close(FILE *fp, char *str_err);
void child_service(int s);
void sigchild_handler(int signum);

int main (int argc, char *argv[])
{
	int sock_in, s,bklog = 2, stop, childpid;
	short port;
	struct sockaddr_in servaddr, cliaddr;
	socklen_t cli_siz;
	
	signal(SIGCHLD, sigchild_handler); 

	if(argc != 2){
		err_quit("Usage: ./progname port\n");
	}

	printf("0: creating socket...\n");
    sock_in = Socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    printf("0: done, socket number %u\n",sock_in);

	port=atoi(argv[1]);

	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(port);
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);

	Bind(sock_in, (SA*) &servaddr, sizeof(servaddr));
	printf("0: Socket binded at port %d\n", port);

    Listen(sock_in, bklog);
	printf ("0: Listening at socket %d with backlog = %d \n",sock_in, bklog);

	while(1){
		memset(&cli_siz, 0, sizeof(cli_siz));
		int s = Accept(sock_in, (SA*) &cliaddr, &cli_siz);
		printf("0: Accepted connection from %s:%d\n", inet_ntoa(cliaddr.sin_addr), ntohs(cliaddr.sin_port));

		if((childpid=fork())<0) { 
			err_msg("0: fork() failed");
			close(s);
		}
		else if (childpid > 0)
		{ 
			/* parent process */
			close(s);	/* close connected socket */
		}
		else
		{
			/* child process */
			close(sock_in);	/* close passive socket */
			child_service(s);

		}
	}
	return 0;
}

void child_service(int s){
	int stop = 0;
	fd_set fd;
    struct timeval t_val;
	char buf_rec[MAXBUF_IN], cmd[MAXBUF_IN], filename[MAXBUF_IN];
	
	pid = getpid();
	// Set time of 15 to receive the GET
	FD_ZERO(&fd);
	FD_SET(s, &fd);
	t_val.tv_sec = 15; t_val.tv_usec = 0;
	
	while(stop == 0){
		printf("[%d]: Waiting GET message from client...\n", pid);
		int n = Select(FD_SETSIZE, &fd, NULL, NULL, &t_val);
		if (n > 0){
			if(FD_ISSET(s, &fd)){
				int len = readline(s, buf_rec, MAXBUF_IN);
				if(len > 0){
					if(sscanf(buf_rec, "%s %s\r\n", cmd, filename)!=2){
						err_send(s, "ERROR: Wrong format MESSAGE\n");
						stop = 1;
					}
					else{
						if(service(s, cmd, filename)<0){
							err_send(s, "ERROR: Service not done\n");
						}
						else{
							printf("[%d]: Service performed correctly\n", pid);
						}
					}
				}
				else if (len == 0)
				{
					close(s);
					printf("[%d]: Requests finished\n", pid);
					stop = 1;
				}
				else if (len < 0)
				{
					err_send(s, "ERROR: System call send() failed\n");
					stop = 1;
				}
			}
		}
		else{
			err_send(s, "ERROR: client did not send anything\n");
			stop = 1;

		}
		bzero(buf_rec, MAXBUF_OUT);
	}
	exit(0);
}
// function to serve client: read command and send file
int service(int s, char cmd[], char filename[]){
	FILE *fp;
	char response[9], timemod[4];
	char buf_out[MAXBUF_OUT];
	struct stat filestat;
	u_int32_t  mod, size;
	int f_block_sz;

	printf("[%d]: Request received: %s %s\n", pid, cmd, filename);

	if(strcmp(cmd, "GET")!=0){
		err_msg("[%d]: ERROR: Wrong command\n", pid);
		return -1;
	}
	fp = fopen(filename, "r");
	if(fp == NULL){
		err_msg("[%d]: ERROR: File could not be opened", pid);
		return -1;
	}
	if(stat(filename,&filestat)<0){
		err_close(fp, "ERROR: File stat not avaible\n");
		return -1;
	}
	size = htonl(filestat.st_size);

	printf("[%d]: Sending OK and size: %u\n", pid, ntohl(size));
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
	printf("[%d]: Sending file...\n", pid);
	while((f_block_sz = fread(buf_out, sizeof(char), MAXBUF_OUT, fp))>0)
	{
		if(send(s, buf_out, f_block_sz, MSG_NOSIGNAL) <= 0)
		{
			err_close(fp,"ERROR: Failed to send file\n");
			return -1;
		}
		bzero(buf_out, MAXBUF_OUT);
	}
	printf("[%d]: File %s sent to client\n", pid, filename);
	fclose(fp);
	printf("[%d]: Sending time from epoch\n", pid);
	
	mod = htonl(filestat.st_mtime);
	memcpy(timemod, &mod, 4);

	l =send(s, timemod, sizeof(timemod), MSG_NOSIGNAL); //send time from epoch*/
	if(l != sizeof(timemod)){
		err_msg("ERROR: timemod not sent\n");
		return -1;
	}
	return 0;
}

// Function to close file before returning to avoid memory leakage 
void err_close(FILE *fp, char *str_err){
	err_msg("[%d]: %s\n", pid, str_err);
	fclose(fp);
}

void sigchild_handler(int signum){
	wait(NULL);
}

// Error function to send "-ERR" and close connection
void err_send(int s, char *str_err){
	char err[6]="-ERR\r\n";
	err_msg("[%d]: %s", str_err, pid);
	send(s, err, sizeof(err), MSG_NOSIGNAL);
	close(s);
	exit(-1);
}

