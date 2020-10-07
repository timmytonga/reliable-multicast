//
// Created by Thien Nguyen on 10/7/20.
//

#include <cstdio>
#include "networkagent.h"

#define SERVER_PORT 4646
#define MAX_BUF_LEN 256

using namespace udp_client_server;

int main() {
    UDP_Server echo_server(SERVER_PORT);
    int numbytes;
    printf("[server] waiting for client...\n");
#pragma clang diagnostic push
#pragma ide diagnostic ignored "EndlessLoop"
    while(true){
        printf("[server] waiting for client...\n");
        char buf[MAX_BUF_LEN];
        char s[INET6_ADDRSTRLEN];
        numbytes = echo_server.recv(buf, MAX_BUF_LEN);
        if(numbytes == -1) {perror("recvfrom error.... trying again..."); exit(1);}
        const sockaddr_storage &their_addr = echo_server.get_their_addr();
        const char * their_ip = inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s, sizeof s);
        buf[numbytes] = '\0';
        printf("[server] got packet from %s with content %s... Replying by echoing\n", their_ip, buf);
        // replying...
        numbytes = echo_server.reply(buf);
        if (numbytes == -1){
            printf("[server] Sent reply failed... try again later\n");
            continue;
        }
        printf("Replied success\n");
    }
#pragma clang diagnostic pop
}
