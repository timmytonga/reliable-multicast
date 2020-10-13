//
// Created by Thien Nguyen on 10/7/20.
//

#ifndef PRJ1_NETWORKAGENT_H
#define PRJ1_NETWORKAGENT_H

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdexcept>
#include <cstring>
#include <csignal>
#include <unistd.h>
#include <arpa/inet.h>
#include <cerrno>
#include <random>
#include <sys/wait.h>

#define BACKLOG 20   // how many pending connections queue will hold

namespace client_server
{
    void *get_in_addr(struct sockaddr *sa);

    class UDP_Server
    {
    public:
        explicit UDP_Server(int port);
        ~UDP_Server();

        int                 get_socket() const;
        int                 get_port() const;
        struct sockaddr_storage get_their_addr() const;


        int                 recv(char *msg, size_t max_size);
        int                 reply(const char *msg, size_t msg_size);
        int                 send_to(const char * destination, const char * msg, size_t msg_size) const;
//        int                 send_to(const char * destination, const char * msg, size_t msg_size = -1) const;
        int                 timed_recv(char *msg, size_t max_size, int max_wait_ms);

    private:
        int                 sockfd;
        int                 f_port;
        struct addrinfo *   f_addrinfo;
        struct sockaddr_storage their_addr{};

    };

    class TCP_Server{
    public:
        explicit TCP_Server(int port, int backlog=BACKLOG);
        ~TCP_Server();

        int                 get_socket() const;
        int                 get_port() const;

        int                 accept_and_recv(char * msg, size_t max_size) const;
        int                 connect_and_get_socket(const char * destination) const;
        static int          sendtcp(int sock, const char * msg, size_t msg_size) ;


    private:
        int                 sockfd;
        int                 f_port;
        struct addrinfo *   f_addrinfo;
    };

} // namespace client_server

#endif //PRJ1_NETWORKAGENT_H
