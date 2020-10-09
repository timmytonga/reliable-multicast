#pragma once

#ifdef DEBUG
# define DPRINTF(arg) printf arg
#else
# define DPRINTF(arg)
#endif

#include <netdb.h>

/* Read from a file line by line and store each line into an array. Returns the size of the array i.e. number of line */
int read_from_file(const char* fileName, char* lineArray[]);

void * check_up_with_host(void * ptr);

/* perform message collecting and synchronization (for threads) */
void receive_signals_and_send_ack();
void send_signals_and_wait_for_acks(struct addrinfo * hostai, const char * hostname);

