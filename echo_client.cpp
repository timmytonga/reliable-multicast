//
// Created by Thien Nguyen on 10/7/20.
//

#include <cstdio>
#include <iostream>
#include "networkagent.h"

#define SERVER_PORT 4646
#define MAX_BUF_LEN 256

using namespace client_server;

int main(int argc, char* argv[]){
    /* first check for appropriate input */
    if (argc != 2){
        printf("Usage: ./%s <server_name>\n", argv[0]);
        return 1;
    }

    TCP_Server echo_client(SERVER_PORT, 20);

    char msg[MAX_BUF_LEN], buf[MAX_BUF_LEN], s[INET6_ADDRSTRLEN];
    int numbytes;
//    UDP_Server echo_client(SERVER_PORT);
    std::cout << "Enter a msg to echo server: ";
    std::cin >> msg;
    int sockfd = echo_client.connect_and_get_socket(argv[1]);
    client_server::TCP_Server::sendtcp(sockfd, msg, strlen(msg));

//    echo_client.send_to(argv[1],msg, strlen(msg));
    std::cout << "Waiting for reply...\n";
    numbytes = recv(sockfd, buf, MAX_BUF_LEN, 0);

//    numbytes = echo_client.recv(buf, MAX_BUF_LEN);
    buf[numbytes] = '\0';
    printf("[client] got packet with content %s... \n",  buf);

    return 0;
}