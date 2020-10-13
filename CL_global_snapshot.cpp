//
// Created by Thien Nguyen on 10/12/20.
//

#include "CL_global_snapshot.h"
#include "reliable_multicast.h"

CL_Global_Snapshot::CL_Global_Snapshot(ReliableMulticast *rm, const client_server::TCP_Server& serv)
: rm(rm), server(serv), locsnap{}, amInitiator(false), inboundMessageBuffer(), outboundMessageBuffer() {
    num_hosts = rm->num_hosts;
    hostNames = rm->hostNames;
    curr_container_id = rm->current_container_id;
    curr_container_name = rm->current_container_name;
}

CL_Global_Snapshot::CL_Global_Snapshot(ReliableMulticast *rm)
        : rm(rm), server(SNAP_SHOT_PORT, BACKLOG), locsnap{}, amInitiator(false) {
    if (rm == nullptr)
        return;
    num_hosts = rm->num_hosts;
    hostNames = rm->hostNames;
    curr_container_id = rm->current_container_id;
    curr_container_name = rm->current_container_name;
}

void CL_Global_Snapshot::set_rm(ReliableMulticast *p) {
    rm = p;
    num_hosts = rm->num_hosts;
    hostNames = rm->hostNames;
    curr_container_id = rm->current_container_id;
    curr_container_name = rm->current_container_name;
}

int CL_Global_Snapshot::initiate_snapshot() {
    /* We tell rm to record its local state */
//    printf("[debug clgs:initiate_snapshot]: Initiating a snapshot!\n");
    locsnap = rm->get_local_state_snapshot();
//    printf("[debug clgs:initiate_snapshot]: Got local_state_snapshot!\n");
    amInitiator =  true;
    tell_rm_to_start_recording_channel();  // this will start pushing messages into inboundMessageBuffer and outboutMessageBuffer
//    printf("[debug clgs:initiate_snapshot]: Preparing to broadcast markers!\n");
    broadcast_markers();
//    printf("[debug clgs:initiate_snapshot]: Broadcasted markers!\n");
    return 0;
}

void CL_Global_Snapshot::broadcast_markers() const {
    /* now we send a marker to everybody. we send our id as a marker */
    std::stringstream ss; ss << curr_container_id; std::string our_id = ss.str();
    const char * our_ids = our_id.c_str();
//    printf("[debug broadcast_marker] preparing to send packet %s.\n", our_ids);
    for (int i = 0; i < num_hosts; i++){
        if (strcmp(hostNames[i], curr_container_name) != 0){
//            printf("[debug broadcast_marker] Found host %s to send to.\n", hostNames[i]);
//            int to_send_id = extract_int_from_string(std::string(hostNames[i]));
            int to_send_sock = server.connect_and_get_socket(hostNames[i]);
//            hostToSockMutex.lock();
//            if (hostToSockFD.count(to_send_id) == 0){  // if we haven't had a channel to talk with this hostID
//                printf("[debug clgs:broadcast_marker]: haven't found hostToSockFd for %s\n",
//                       hostNames[i]);
//                to_send_sock = server.connect_and_get_socket(hostNames[i]); // get a socket
//                hostToSockFD.insert(std::make_pair(to_send_id, to_send_sock));
//            } else{  // else we have it
//                printf("[debug clgs:broadcast_marker]: found socket %d for host %s with id %d!\n",
//                       hostToSockFD[to_send_id], hostNames[i], to_send_id);
//                to_send_sock = hostToSockFD[to_send_id];
//            }
//            hostToSockMutex.unlock();
            int rv = client_server::TCP_Server::sendtcp(to_send_sock, our_ids, strlen(our_ids));  // send our id as a marker
            if (rv == -1){
                perror("broadcast marker: Send tcp failed:"); exit(1);
            }
//            printf("[debug broadcast_marker] sent packet %s to %s.\n", our_ids, hostNames[i]);
            close(to_send_sock);
        }
    }
}

void CL_Global_Snapshot::listen_for_incoming_connections() {
    /* Marker receiving rule (for this process j):
     * - if it's the first marker ever (say from process i), record local state
     * -- turn on recording channel for all channels except (i), and send out markers
     * - We need to keep a list of markers already received say alreadyReceivedProc
     * - When we receive a message (from the object we manage):
     * -- we check if it's from a process that we have received a marker before
     * -- if we have received a marker from that process, we ignore the incoming msgs
     * -- otherwise, we record it in a channel state
     * - When we receive a marker (say from process k) after recording our state:
     * -- We add process j to alreadyReceivedProc and finalize its channel state
     * --  If we have received all n-1 markers, we send our local snapshot to the initiator */
    printf("[debug clgs:listening_for]: hello i am listening for incoming markers\n");
    char buf[MAX_MARKER_SIZE];  // we just need to receive the id of the other host
    int firstSockfd = server.accept_and_recv(buf, MAX_MARKER_SIZE);
//    printf("[debug clgs:listening_for]: i got a marker from %s\n", buf);
    close(firstSockfd);
    if (!amInitiator){  // if i am not the initiator then
//        printf("[debug clgs:listening_for]: this is the first marker!\n");
        // this is the first msg
        int initiator_id = atoi(buf);
        // so we already received from this... no need to record channel state from this channel
        alreadyReceivedProc.push_back(initiator_id);
        // first we record the local_state as soon as we receive the snapshot.
        locsnap = rm->get_local_state_snapshot();
        // Then we turn on recording for all channels except i
        tell_rm_to_start_recording_channel();  // this will start pushing messages into inboundMessageBuffer and outboutMessageBuffer
        // Then we send out markers to everybody and wait
        broadcast_markers();
        if (alreadyReceivedProc.size()  < num_hosts-1){
            firstSockfd = server.accept_and_recv(buf, MAX_MARKER_SIZE);
            close(firstSockfd);
        }
    }
    // we get here once we receive a second marker from some process.
    // we process all pending messages in the buffers (inbound and outbound)
    // then we mark the marker sending process as received (so we stop adding those messages in the future)
    int sofar = 2;
    while(alreadyReceivedProc.size()  < num_hosts-1){
//        printf("[debug clgs:listening_for]: this is the %d marker!\n", sofar);
        inboundMessageBufferMutex.lock();
        while(!inboundMessageBuffer.empty()){
            unsigned char temp[MAX_STRUCT_SIZE];
            ByteVector b = inboundMessageBuffer.front();
            for (int i = 0; i < MAX_STRUCT_SIZE; i++){
                temp[i] = b[i];
            }
            handle_message(temp, INBOUND);
            inboundMessageBuffer.pop();
        }
        inboundMessageBufferMutex.unlock();

        outboundMessageBufferMutex.lock();
        while(!outboundMessageBuffer.empty()){
            unsigned char temp[MAX_STRUCT_SIZE];
            ByteVector b = outboundMessageBuffer.front();
            for (int i = 0; i < MAX_STRUCT_SIZE; i++){
                temp[i] = b[i];
            }
            handle_message(temp, OUTBOUND);
            outboundMessageBuffer.pop();
        }
        outboundMessageBufferMutex.unlock();

        // after processing all messages from all channels, we process the marker
        int marker_id = atoi(buf);
        alreadyReceivedProc.push_back(marker_id);
        sofar++;
        if (alreadyReceivedProc.size()  == num_hosts-1){
            break;
        }
//        printf("[debug clgs:listening_for]: waiting for new marker!");
        firstSockfd = server.accept_and_recv(buf, MAX_MARKER_SIZE);
        close(firstSockfd);
    }
    DPRINTF(("Finished obtaining local snapshot!\n"));
    tell_rm_to_stop_recording();
    print_local_snapshot();
    if (!amInitiator){
        // send local snapshot to initiator to collect and make global snapshot
    }
}

void CL_Global_Snapshot::add_msg_to_inbound(int sender, const std::string& s) {
    if (inboundChannelState.count(sender) == 0){  // if haven't encountered
        inboundChannelState.insert(std::make_pair(sender, std::vector<std::string>()));
    }  // we only add the message to inbound if we haven't received a marker for it

    for (const auto &p : alreadyReceivedProc){
        if (sender == p) return;
    }
    inboundChannelState[sender].push_back(s);
}

void CL_Global_Snapshot::add_msg_to_outbound(int sender, const std::string& s) {
    if (outboundChannelState.count(sender) == 0){  // if haven't encountered
        outboundChannelState.insert(std::make_pair(sender, std::vector<std::string>()));
    }  // we only add the message to inbound if we haven't received a marker for it

    for (const auto &p : alreadyReceivedProc){
        if (sender == p) return;
    }
    outboundChannelState[sender].push_back(s);
}

void CL_Global_Snapshot::handle_message(unsigned char *msg, int inorout) {
    unsigned long type = unpacku32(&msg[0]);
//        DPRINTF(("Received msg is of type: %lu\n", type));
    DataMessage dataMessage;
    AckMessage ackMessage;
    SeqMessage seqMessage;
    int sender;
    char buff[100];
    switch (type) {
        case DATAMSG_TYPE:
            deserialize_data_message(msg, dataMessage);
            sprintf(buff, "DataMessage: sender %d, msg_id %d, data %d",
                    dataMessage.sender, dataMessage.msg_id, dataMessage.data);
            sender = dataMessage.sender;
            break;
        case ACKMSG_TYPE:
            deserialize_ack_message(msg, ackMessage);
            sprintf(buff, "AckMessage: sender %d, msg_id %d, seq %d, proposer %d",
                    ackMessage.sender, ackMessage.msg_id, ackMessage.proposed_seq, ackMessage.proposer);
            sender = ackMessage.proposer;
            break;
        case SEQMSG_TYPE:
            deserialize_seq_message(msg, seqMessage);
            sprintf(buff, "SeqMessage: sender %d, msg_id %d, seq %d, proposer %d",
                    seqMessage.sender, seqMessage.msg_id, seqMessage.final_seq, seqMessage.final_seq_proposer);
            sender = seqMessage.sender;
            break;
        default:
            fprintf(stderr, "Received message wrong type: %lu....\n", type);
            exit(1);
    }
    if (inorout == INBOUND) add_msg_to_inbound(sender, std::string(buff));
    else add_msg_to_outbound(sender, std::string(buff));
}



void CL_Global_Snapshot::tell_rm_to_start_recording_channel() const {
    rm->recordMessagesMutex.lock();
    rm->recordMessages = true;
    rm->recordMessagesMutex.unlock();
}

void CL_Global_Snapshot::tell_rm_to_stop_recording() const {
    rm->recordMessagesMutex.lock();
    rm->recordMessages = false;
    rm->recordMessagesMutex.unlock();
}


void CL_Global_Snapshot::print_local_snapshot() {
    printf("************************ LOCALSNAPSHOT **********************\n");
    auto deliveredMessage = locsnap.deliveredMessage;
    auto deliveryQueue = locsnap.deliveryQueue;

    printf("=== deliveryQueue (min-heap of size %lu) ====\n", deliveryQueue.size());
    for (const QueuedMessage &qm: deliveryQueue){
        printf("\tseq/proposer (%d, %d), msg_id/sender (%d, %d)\n",
               qm.sequence_number, qm.proposer, qm.msg_id, qm.sender);
    }
    printf("=================================\n\n");

    printf("=== delivered messages so far (size %lu) ====\n", deliveredMessage.size());
    int i = 0;
    for (const QueuedMessage &qm: deliveredMessage){
        printf("\t%d: seq/proposer (%d, %d), msg_id/sender (%d, %d)\n", i++,
               qm.sequence_number, qm.proposer, qm.msg_id, qm.sender);
    }
    printf("=================================\n\n");


    printf("================== CHANNEL STATES:============== \n");
    printf("Inbound channels: \n");
    for (const auto& kv : inboundChannelState){
        printf("\tFrom %d:\n", kv.first);
        for (const auto& m : kv.second){
            printf("\t\t%s\n", m.c_str());
        }
    }
    printf("\n\nOutbound channels: \n");
    for (const auto& kv : outboundChannelState){
        printf("\tTo %d:\n", kv.first);
        for (const auto& m : kv.second){
            printf("\t\t%s\n", m.c_str());
        }
    }
    printf("************************ END LOCAL SNAPSHOT *************************\n");

}


CL_Global_Snapshot::~CL_Global_Snapshot() {


}

