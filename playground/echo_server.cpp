//
// Created by Thien Nguyen on 10/7/20.
//

#include <cstdio>
#include "networkagent.h"

#define SERVER_PORT 4646
#define MAX_BUF_LEN 256

using namespace client_server;

int main() {
    TCP_Server echo_server(SERVER_PORT, 20);
    int numbytes;
    printf("[server] waiting for client...\n");
#pragma clang diagnostic push
#pragma ide diagnostic ignored "EndlessLoop"
    while(true){
        printf("[server] waiting for client...\n");
        char buf[MAX_BUF_LEN];
        int sockfd = echo_server.accept_and_recv(buf, MAX_BUF_LEN);
//        numbytes = echo_server.recv(buf, MAX_BUF_LEN);
//        if(numbytes == -1) {perror("recvfrom error.... trying again..."); exit(1);}
//        const sockaddr_storage &their_addr = echo_server.get_their_addr();
//        const char * their_ip = inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s, sizeof s);
        printf("[server] got packet with content %s... Replying by echoing\n", buf);
//        // replying...
        numbytes = send(sockfd, buf, strlen(buf), 0);
        if (numbytes == -1){
            printf("[server] Sent reply failed... try again later\n");
            continue;
        }
        printf("Replied success\n");
    }
#pragma clang diagnostic pop
}
