//
// Created by Thien Nguyen on 10/7/20.
//

#ifndef PRJ1_WAITTOSYNC_H
#define PRJ1_WAITTOSYNC_H

#ifdef DEBUG
# define DPRINTF(arg) printf arg
#else
# define DPRINTF(arg)
#endif

#include <netdb.h>
namespace wait_to_sync {
    int read_from_file(const char *fileName, char *lineArray[]);
    const char *waittosync(char **hostNames, int numHosts);
    void *check_up_with_host(void *ptr);
    /* perform message collecting and synchronization (for threads) */
    void receive_signals_and_send_ack();
    void send_signals_and_wait_for_acks(struct addrinfo *hostai, const char *hostname);
}
#endif //PRJ1_WAITTOSYNC_H
