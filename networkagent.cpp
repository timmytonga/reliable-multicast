//
// Created by Thien Nguyen on 10/7/20.
//

#include "networkagent.h"


#define THREAD_SLEEP_TIME 3
#define DEBUG
#define MAX_BUF_LEN 100


namespace udp_client_server
{

    // get sockaddr, IPv4 or IPv6:
    void *get_in_addr(struct sockaddr *sa)
    {
        if (sa->sa_family == AF_INET) {
            return &(((struct sockaddr_in*)sa)->sin_addr);
        }
        return &(((struct sockaddr_in6*)sa)->sin6_addr);
    }


    UDP_Client::UDP_Client(const char * hostname, int port)
            : f_port(port)
            , f_addr(hostname)
    {
        struct addrinfo hints{}, *hostai;  // ai stands for addrinfo
        int rv;
        char decimal_port[16];
        snprintf(decimal_port, sizeof(decimal_port), "%d", f_port);
        decimal_port[sizeof(decimal_port) / sizeof(decimal_port[0]) - 1] = '\0';

        // getaddrinfo to check for host
        memset(&hints, 0, sizeof hints);
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        // we get the host addrinfo
        while ((rv = getaddrinfo(hostname, decimal_port, &hints, &hostai)) != 0) {
            DPRINTF(("Failed to getaddrinfo for %s (%s). Trying again after %d seconds...\n",
                    hostname, gai_strerror(rv), THREAD_SLEEP_TIME))
            sleep(THREAD_SLEEP_TIME);  // try again until we find
        }
        f_addrinfo = hostai;

        // Creating socket file descriptor
        if ( (sockfd = socket(hostai->ai_family, hostai->ai_socktype, hostai->ai_protocol)) < 0  ) {
            perror("socket creation failed");
            exit(EXIT_FAILURE);
        }

    }

    UDP_Client::~UDP_Client()
    {
        freeaddrinfo(f_addrinfo);
        close(sockfd);
    }

    int UDP_Client::get_socket() const
    {
        return sockfd;
    }

    int UDP_Client::get_port() const
    {
        return f_port;
    }

    const char * UDP_Client::get_addr() const
    {
        return f_addr;
    }

/**
 * This function sends \p msg through the UDP client socket. The function
 * cannot be used to change the destination as it was defined when creating
 * the UDP_Client object.
 *
 * \return -1 if an error occurs, otherwise the number of bytes sent. errno
 * is set accordingly on error.
 */
    int UDP_Client::send(const char *msg, size_t size)
    {
        return sendto(sockfd, msg, size, 0, f_addrinfo->ai_addr, f_addrinfo->ai_addrlen);
    }

// ========================= SEVER =========================
    UDP_Server::UDP_Server(int port)
            : f_port(port), their_addr()
    {
        char decimal_port[16];
        snprintf(decimal_port, sizeof(decimal_port), "%d", f_port);
        decimal_port[sizeof(decimal_port) / sizeof(decimal_port[0]) - 1] = '\0';

        struct addrinfo hints{}, *servinfo, *p;
        int rv;  // return value

        /* first we get a socket to receive message from */
        memset(&hints, 0, sizeof hints);
        hints.ai_family = AF_UNSPEC; // set to AF_INET to force IPv4
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_flags = AI_PASSIVE; // use my IP
        if ((rv = getaddrinfo(nullptr, decimal_port, &hints, &servinfo)) != 0) {
            fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
            return;
        }

        f_addrinfo = servinfo;
        // loop through all the results and bind to the first we can
        for(p = servinfo; p != nullptr; p = p->ai_next) {
            if ((sockfd = socket(p->ai_family, p->ai_socktype,
                                 p->ai_protocol)) == -1) {
                perror("Server: socket error");
                continue;
            }
            if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
                close(sockfd);
                perror("Server: bind error");
                continue;
            }
            break;
        }
        if (p == nullptr) {
            fprintf(stderr, "Server: failed to bind socket\n");
            exit(2);
        }
    }


    UDP_Server::~UDP_Server(){
        freeaddrinfo(f_addrinfo);
        close(sockfd);
    }

    int UDP_Server::get_socket() const{
        return sockfd;
    }

    int UDP_Server::get_port() const{
        return f_port;
    }

    struct sockaddr_storage UDP_Server::get_their_addr() const {
        return their_addr;
    }

/** \brief Wait on a message.
 *
 * Wait until receive a message. Store the sender's address in their_addr
 *
 * \return The number of bytes read or -1 if an error occurs.
 */
    int UDP_Server::recv(char *msg, size_t max_size)
    {
        char s[INET6_ADDRSTRLEN];
        socklen_t addr_len;
        struct sockaddr_storage rep_addr{};
        int numbytes;
        numbytes = recvfrom(sockfd, msg, max_size-1 , 0, (struct sockaddr *)&rep_addr, &addr_len);
        const char * their_ip = inet_ntop(rep_addr.ss_family, get_in_addr((struct sockaddr *)&rep_addr), s, sizeof s);
        printf("DEBUG [UDP_Server::recv] received msg %s from %s.\n", msg, their_ip);
        memcpy(&their_addr, &rep_addr, sizeof(sockaddr_storage));
        return numbytes;
    }

    int UDP_Server::reply(const char* msg){
        printf("UDP_Server::reply\n");
        return sendto(sockfd, msg, strlen(msg), 0, (const struct sockaddr *) &their_addr, sizeof(their_addr));
    }

    int  UDP_Server::send_to(const char * destination, const char * msg) const{
        struct addrinfo hints{}, *hostai;  // ai stands for addrinfo
        int rv;
        char decimal_port[16];
        snprintf(decimal_port, sizeof(decimal_port), "%d", f_port);
        decimal_port[sizeof(decimal_port) / sizeof(decimal_port[0]) - 1] = '\0';

        // getaddrinfo to check for host
        memset(&hints, 0, sizeof hints);
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        // we get the host addrinfo
        if ((rv = getaddrinfo(destination, decimal_port, &hints, &hostai)) != 0) {
            printf("Failed to getaddrinfo for %s (%s). \n", destination, gai_strerror(rv));
            return -1;
        }
        // then we send
        return sendto(sockfd, msg, strlen(msg), 0, hostai->ai_addr, hostai->ai_addrlen);
    }


    int UDP_Server::timed_recv(char *msg, size_t max_size, int max_wait_ms)
    {
        fd_set s;
        FD_ZERO(&s);
        FD_SET(sockfd, &s);
        struct timeval timeout{};
        timeout.tv_sec = max_wait_ms / 1000;
        timeout.tv_usec = (max_wait_ms % 1000) * 1000;
        int retval = select(sockfd + 1, &s, &s, &s, &timeout);
        if(retval == -1)
        {
            // select() set errno accordingly
            return -1;
        }
        if(retval > 0)
        {
            // our socket has data
            return ::recv(sockfd, msg, max_size, 0);
        }

        // our socket has no data
        errno = EAGAIN;
        return -1;
    }

} // namespace udp_client_server