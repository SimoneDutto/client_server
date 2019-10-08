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

#define MAXBUF_IN 2048
#define MAXBUF_OUT 100

int readFile(int s, char * filename);
int select_custom(int s);

char *prog_name;

int main(int argc, char* argv[]){
    int s, stop = 0;
    struct sockaddr_in servaddr;
    char buf_out[MAXBUF_OUT], buf_in[MAXBUF_IN];
    fd_set fd;
    struct timeval t_val;
    short port;

    if(argc < 4){
        err_quit("Usage ./prog_name addr port filename1 [filename2]\n");
    }
    port = atoi(argv[2]);
    /* create the socket */
    printf("Creating socket\n");
    s = Socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    printf("done. Socket fd number: %d\n",s);

    /* prepare address structure */
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port);
    servaddr.sin_addr.s_addr = inet_addr(argv[1]);

    /* connect */
    Connect(s, (struct sockaddr *) &servaddr, sizeof(servaddr));
    printf("Connected to %s %d.\n", argv[1], port);
    printf("======================================================================\n");
    for(int i = 3; i < argc && stop == 0; i++){
        char *filename = argv[i];
        printf("Send GET %s\n", filename);
        sprintf(buf_out, "GET %s\r\n", filename);
        Send(s, buf_out, strlen(buf_out), 0);
        
        printf("Waiting OK by Server message from client...\n");
        if(select_custom(s)){
            bzero(buf_in, MAXBUF_IN);
            Read(s, buf_in, 5);
            if(strcmp(buf_in,"+OK\r\n")==0){
                if(readFile(s, filename)<0){
                    err_msg("ERROR: File not saved in memory correctly\n");
                    stop = 1;
                    close(s);
                }
            }
            else{
                err_msg("ERROR: File request refused by the server\n");
                stop = 1;
                close(s);
            }
            printf("======================================================================\n");
            bzero(buf_out, MAXBUF_OUT);
        }
        else{
            close(s);
            err_msg("Server did not respond\n");
            stop = 1;
        }
    }
    close(s);
}

int readFile(int s, char *filename){
    char buf_in[MAXBUF_IN], msg[4];
    int  maxread;
    u_int32_t size, remain, len, lastmod;
    FILE *fp;

    //filename[0] = '0'; // for debug

    fp = fopen(filename, "w");
    if(fp == NULL){
        err_msg("ERROR: Couldn't open the file\n");
        return -1;
    }
    if(select_custom(s)){
        int n = Read(s, msg, 4);
        if(n != 4){
            err_msg("Size not received\n");
            return -1;
        }
    }
    else{
        err_msg("ERROR: Size did not received\n");
    }
    memcpy(&size, msg, 4);
    remain = ntohl(size);
    bzero(buf_in, MAXBUF_IN);
    printf("Reading and saving file %s into memory, size: %u\n", filename, remain);
    maxread = MAXBUF_IN;
    while(remain != 0){
        if(remain < MAXBUF_IN)
            maxread = remain;
        if(select_custom(s)){
            len = Read(s, buf_in, maxread);
            if(len > 0){
                fwrite(buf_in, len, 1, fp);
                remain -= len;
            }
            else{
                err_ret("ERROR: not possible to receive file\n");
                return -1;
            }
            bzero(buf_in, MAXBUF_IN);
        }
        else{
            err_msg("ERROR: Server not sending file\n");
            return -1;
        }
    }
    fclose(fp);
    printf("File %s written on memory\n", filename);
    if(select_custom(s)){
        int n = Read(s, &lastmod, 4);
        if(n != 4){
            err_msg("Last modified not received\n");
            return -1;
        }
        printf("File last modified %d\n", ntohl(lastmod));
    }
    else{
        err_msg("ERROR: Server not sending last modified\n");
        return -1;
    }
    return 0;
}

int select_custom(int s){
    fd_set fd;
    struct timeval t_val;
    
    memset(&t_val, 0, sizeof(t_val));
    FD_ZERO(&fd);
    FD_SET(s, &fd);
    t_val.tv_sec = 15; t_val.tv_usec = 0;
    
    int n = Select(FD_SETSIZE, &fd, NULL, NULL, &t_val);
    if (n > 0){
        if(FD_ISSET(s, &fd)){
            return 1;
        }
    }
    return 0;
}
