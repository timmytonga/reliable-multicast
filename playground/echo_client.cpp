//
// Created by Thien Nguyen on 10/7/20.
//

#include <cstdio>
#include <iostream>
#include "../networkagent.h"

#define SERVER_PORT 4646
#define MAX_BUF_LEN 256

using namespace udp_client_server;

int main(int argc, char* argv[]){
    /* first check for appropriate input */
    if (argc != 2){
        printf("Usage: ./%s <server_name>\n", argv[0]);
        return 1;
    }
    char msg[MAX_BUF_LEN], buf[MAX_BUF_LEN], s[INET6_ADDRSTRLEN];
    int numbytes;
    UDP_Server echo_client(SERVER_PORT);
    std::cout << "Enter a msg to echo server: ";
    std::cin >> msg;
    echo_client.send_to(argv[1],msg);
    std::cout << "Waiting for reply...\n";
    numbytes = echo_client.recv(buf, MAX_BUF_LEN);
    const sockaddr_storage &their_addr = echo_client.get_their_addr();
    const char * their_ip = inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s, sizeof s);
    buf[numbytes] = '\0';
    printf("[client] got packet from %s with content %s... \n", their_ip, buf);

    return 0;
}