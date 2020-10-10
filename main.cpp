//
// Created by Thien Nguyen on 10/8/20.
//

#include "reliable_multicast.h"

long num_msg_tosend = 0;
const char * hostFileName;
void handle_param(int argc,  char* argv[]);

int main(int argc, char* argv[]){
    handle_param(argc, argv);  // first we obtain the count and hostFileName
    udp_client_server::UDP_Server comm(SERVER_PORT);
    ReliableMulticast reliableMulticast(hostFileName, comm);  // this will perform the processing and communicating

    // constructing that will also start the receiver thread for this process
    std::thread receiver_thread(ReliableMulticast::start_msg_receiver, &reliableMulticast);
    for (int i = 0; i<num_msg_tosend; i++){
        reliableMulticast.multicast_datamsg(i*198%27);  // semi arbitrary data
//        sleep(1);
    }
    receiver_thread.join();
}


void handle_param(int argc,  char* argv[]){
    // here we handle params given to us and set global vars accordingly
    if (argc < 5 || argc%2 == 0){
        printf("Usage: %s -h <hostfile> -c <send_msg_count>\n", argv[0]);
        exit(1);
    }
    for (int i = 1; i<argc; i+=2){
        if (strcmp(argv[i],"-h") == 0){
            hostFileName = argv[i + 1];
        }
        if (strcmp(argv[i],"-c") == 0){
            num_msg_tosend = strtol(argv[i + 1], nullptr, 10);
            if (num_msg_tosend < 0){
                perror("Invalid count argument. Exiting...");
                exit(1);
            }
        }
    }
}