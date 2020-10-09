/*
** prj0.c
** Read from a host file containing n-1 other hosts
** Spawn n-1 thread to send out "I'm alive" signals and watch for acks... do this periodically.
** Spawn another thread to listen for incoming signal and only stop why
*/
#define DEBUG   // comment this to turn off debug

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "prj0.h"
// socket programming stuff
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>  // gethostname
#include <poll.h>


#define SERVERPORT "4950"

#define ARRAY_MAX_SIZE 16  // max 16 hosts
#define MAX_HOST_NAME 256
#define THREAD_SLEEP_TIME 3 // in seconds
#define MAX_BUF_LEN 100
#define TIMEOUT 3000 // max time to receive ACK

pthread_mutex_t num_hosts_mutex = PTHREAD_MUTEX_INITIALIZER;  // so we read properly
int num_hosts = 1;  // global var for threads to read from


int main(int argc, char* argv[]){
    /* first check for appropriate input */
    if (argc != 2){
        printf("Usage: ./%s <hostfile_name>\n", argv[0]);
        return 1;
    }
    // then we read from the files into our array
    const char*  fileName = argv[1];
    char* hostNames[ARRAY_MAX_SIZE];
    int numHosts = read_from_file(fileName, hostNames);
    num_hosts = numHosts;  // only place. no need for mutex
    /* now we spawn threads to go out and send message to the other hosts */
    pthread_t workers[numHosts];
    int ret[numHosts];
    for (int i = 0; i < numHosts; i++){
        ret[i] = pthread_create(&workers[i], NULL, check_up_with_host, (void*) hostNames[i]);
    }

    for (int i = 0; i < numHosts; i++){
        pthread_join(workers[i], NULL);
    }

    for (int i = 0; i < numHosts; i++){
        DPRINTF(("Thread %d returns: %d\n", i, ret[i]));
    }

    printf("READY\n");

    /* clean up */
    for (int i =0; i< numHosts; i++){
        free(hostNames[i]);
    }
    return 0;
}

// this is the main function that thread will use to communicate with other processes
void * check_up_with_host(void * ptr){
    char* hostname = (char*) ptr;
    struct hostent *host_entry;
    int sockfd;
    struct addrinfo hints, *hostai, *p;  // ai stands for addrinfo
    int rv, num;
    /* first get localhost for debugging */
    char hostipstr[INET6_ADDRSTRLEN], ipbuf[INET6_ADDRSTRLEN];
    char *localipstr;
    gethostname(ipbuf, sizeof(ipbuf));
    host_entry = gethostbyname(ipbuf);
    if (host_entry == NULL){perror("gethostbyname"); exit(1);}  // error checking
    localipstr = inet_ntoa(*((struct in_addr*)host_entry->h_addr_list[0]));

    // getaddrinfo to check for host
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    // we get the host addrinfo
    while ((rv = getaddrinfo(hostname, SERVERPORT, &hints, &hostai)) != 0) {
        DPRINTF(("[Thread(%s)] Failed to getaddrinfo for %s (%s). Trying again after %d seconds...\n", localipstr, hostname, gai_strerror(rv), THREAD_SLEEP_TIME));
        sleep(THREAD_SLEEP_TIME);  // try again until we find
    }
    // first we need to check whether if this hostname is ours (so we do a different job)
    // get the str to print debug
    struct sockaddr_in* host_sai = (struct sockaddr_in *)hostai->ai_addr;
    inet_ntop(AF_INET, &(host_sai->sin_addr), hostipstr, sizeof hostipstr);
    DPRINTF(("Thread: local (%s) vs. host (%s)\n", localipstr, hostipstr));
    if(strcmp(localipstr, hostipstr) == 0){
        DPRINTF(("Thread(%s): Same!\n", localipstr));
        /* This is the receiving thread. We wait for incoming signals and send them acks. We stop after we have received n-1 signals and send out n-1 acks. */
        receive_signals_and_send_ack();
    } else {
        DPRINTF(("Thread(%s): Different!\n", localipstr));
        send_signals_and_wait_for_acks(hostai, hostname);
    }

    freeaddrinfo(hostai); // free the linked list
    return NULL;
}


/* read from hostFileName and store each line into lineArray */
int read_from_file(const char* fileName, char* lineArray[]){
    int numHosts = 0;
    int currSize = ARRAY_MAX_SIZE;
    FILE* file = fopen(fileName, "r");
    if(file == NULL) {
        fprintf(stderr, "Error reading file %s", fileName);
        exit(1);
    }
    char line[MAX_HOST_NAME];
    while (fgets(line, sizeof(line), file)) {
        if (numHosts == ARRAY_MAX_SIZE) {
            perror("Exceeded maximum hosts threshold. Please fix parameter in prj0.c");
            exit(1);
        }
        line[strcspn(line, "\r\n")] = 0;
        lineArray[numHosts] = malloc(sizeof(char)*MAX_HOST_NAME);
        strcpy(lineArray[numHosts], line);
        numHosts++;
    }
    // for (int i = 0; i < numHosts; i++){
    //     DPRINTF(("%d: %s\n", i, lineArray[i]));
    // }
    fclose(file);
    return numHosts;
}

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

void receive_signals_and_send_ack(){
    int sockfd;  // the socket for receiving incoming msg
    struct addrinfo hints, *servinfo, *p;
    int rv;  // return value
    int numbytes;
    struct sockaddr_storage their_addr;
    char buf[MAX_BUF_LEN];
    socklen_t addr_len;
    char s[INET6_ADDRSTRLEN];

    // first obtain num_hosts-1
    pthread_mutex_lock(&num_hosts_mutex);
    int numHost = num_hosts;
    pthread_mutex_unlock(&num_hosts_mutex);

    /* first we get a socket to receive message from */
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC; // set to AF_INET to force IPv4
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE; // use my IP
    if ((rv = getaddrinfo(NULL, SERVERPORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return;
    }
    // loop through all the results and bind to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                             p->ai_protocol)) == -1) {
            perror("listener: socket error");
            continue;
        }
        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("listener: bind error");
            continue;
        }
        break;
    }
    if (p == NULL) {
        fprintf(stderr, "listener: failed to bind socket\n");
        exit(2);
    }
    freeaddrinfo(servinfo);

    /* NOW WE ARE READY TO RECEIVE MESSAGES AND REPLY WITH ACKS*/
    int num_encountered_ips = 0;
    char * encountered_ips[numHost-1];
    addr_len = sizeof their_addr;
    int numHostminus1 = numHost -1;
    while(num_encountered_ips < numHostminus1){
        // listen
        DPRINTF(("[Listener] Listening for input (num_encountered_ips = %d vs. numHostminus1 = %d)...\n", num_encountered_ips, numHostminus1));
        if ((numbytes = recvfrom(sockfd, buf, MAX_BUF_LEN-1 , 0, (struct sockaddr *)&their_addr, &addr_len)) == -1) {perror("recvfrom error.... trying again..."); continue;}
        // GOT A PACKET!
        const char * their_ip = inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s, sizeof s);
        buf[numbytes] = '\0';
        DPRINTF(("[Listener] got packet from %s with content %s\n", their_ip, buf));
        // Now we send an ACK back. If we haven't send an ACK to them before, we increment num_encountered_ips
        struct sockaddr * theirs = (struct sockaddr *)&their_addr;
        if ((numbytes = sendto(sockfd, "ACK", strlen("ACK"), 0, (const struct sockaddr *) &their_addr, sizeof(their_addr))) == -1){
            DPRINTF(("[Listener] Sent ACK failed... try again later"));
            continue;
        };
        int already_added = 0;
        for (int i = 0; i<num_encountered_ips; i++){  // check if we have seen this ip before
            if (strcmp(encountered_ips[i], their_ip) == 0){  // this means it has already been added
                DPRINTF(("WARNING: IP %s is already encountered.\n", encountered_ips[i]));
                already_added = 1;
                break;
            }
        }
        if (already_added == 0){ // if we haven't added this ip
            encountered_ips[num_encountered_ips] = malloc(sizeof(char)*50);
            strcpy(encountered_ips[num_encountered_ips], inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s, sizeof s));
            DPRINTF(("[Listerner] Adding ip %s to encountered list\n", encountered_ips[num_encountered_ips]));
            num_encountered_ips++;
        }
    }

    /* cleanup */
    for (int i =0; i < numHostminus1; i++){
        free(encountered_ips[i]);
    }
    close(sockfd);
}

void send_signals_and_wait_for_acks(struct addrinfo *hostai, const char * hostname){
    // we need to create a socket
    int sockfd;
    char buf[MAX_BUF_LEN], s[INET6_ADDRSTRLEN];
    struct sockaddr_storage their_addr;
    socklen_t addr_len;

    // Creating socket file descriptor
    if ( (sockfd = socket(hostai->ai_family, hostai->ai_socktype, hostai->ai_protocol)) < 0 ) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }
    const char * msg = "hi";
    int numbytes;
    int received_ack = 0;

    struct pollfd pfds[1]; // More if you want to monitor more

    pfds[0].fd = sockfd;          // Standard input
    pfds[0].events = POLLIN; // Tell me when ready to read

    while (received_ack == 0){
        if ((numbytes = sendto(sockfd, msg, strlen(msg), 0, hostai->ai_addr, hostai->ai_addrlen)) == -1) { DPRINTF(("Sent to host failed... try again later"));}
        else{  // it sent through... wait to receive msg. Will timeout after TIMEOUT msec if does not receive ack.
            int num_events = poll(pfds, 1, TIMEOUT);
            if (num_events == 0) {
                DPRINTF(("Poll timed out! Waiting for %s\n", hostname));
            }
            else {
                int pollin_happened = pfds[0].revents & POLLIN;
                if (pollin_happened) {
                    if ((numbytes = recvfrom(sockfd, buf, MAX_BUF_LEN-1 , 0, (struct sockaddr *)&their_addr, &addr_len)) == -1) {perror("recvfrom error.... trying again..."); continue;}
                    // GOT A PACKET!
                    const char * their_ip = inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s, sizeof s); buf[numbytes] = '\0';
                    printf("[POLL for %s] Received msg %s from %s\n", hostname, buf, their_ip);
                    received_ack = 1;
                } else {
                    printf("[POLL for %s] Unexpected event occurred: %d\n", hostname, pfds[0].revents);
                }
            }
        }
    }
}


