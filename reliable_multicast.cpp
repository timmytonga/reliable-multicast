//
// Created by Thien Nguyen on 10/8/20.
//

#include "reliable_multicast.h"

ReliableMulticast::ReliableMulticast(const char *hostFileName, const udp_client_server::UDP_Server& communicator)
        : communicator(communicator) {
    hostNames = new char*[MAX_NUM_HOSTS];
    num_hosts = wait_to_sync::read_from_file(hostFileName, hostNames);
    // we wait for all the hosts to be ready before sending msgs
    current_container_name = wait_to_sync::waittosync(hostNames, num_hosts);
    if (current_container_name == nullptr){
        perror("Obtaining current container's name failed (from wait to sync). Exiting.\n");
        exit(1);
    }
    // that also extracts the id
    current_container_id = extract_int_from_string(std::string(current_container_name));
    printf("Current container's name: %s and id: %d\n", current_container_name, current_container_id);

    q = std::priority_queue<QueuedMessage, std::vector<QueuedMessage>, decltype(cmp)>(cmp);
}

ReliableMulticast::~ReliableMulticast() {
    delete [] hostNames;
}