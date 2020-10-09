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
#include <unistd.h>
#include <arpa/inet.h>
#include <cerrno>

#define DEBUG


#ifdef DEBUG
# define DPRINTF(arg) printf arg
#else
# define DPRINTF(arg)
#endif


namespace udp_client_server
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
        int                 reply(const char *msg);
        int                 send_to(const char * destination, const char * msg, size_t msg_size) const;
//        int                 send_to(const char * destination, const char * msg, size_t msg_size = -1) const;
        int                 timed_recv(char *msg, size_t max_size, int max_wait_ms);

    private:
        int                 sockfd;
        int                 f_port;
        struct addrinfo *   f_addrinfo;
        struct sockaddr_storage their_addr{};

    };

} // namespace udp_client_server

#endif //PRJ1_NETWORKAGENT_H
