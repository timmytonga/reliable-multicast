//
// Created by Thien Nguyen on 10/8/20.
//

#include "reliable_multicast.h"

ReliableMulticast::ReliableMulticast(const char *hostFileName,
                                     const client_server::UDP_Server& comm,
                                     double drop_rate, int delay_in_ms)
        : communicator(comm), deliveryQueue{}, ackHistory{}, drop_rate(drop_rate),
        delay_in_ms(delay_in_ms), snapshot(nullptr){
    // user should make sure drop_rate and delay_in_ms are reasonable values.
    hostNames = new char*[MAX_NUM_HOSTS];
    num_hosts = wait_to_sync::read_from_file(hostFileName, hostNames);
    // we wait for all the hosts to be ready before sending msgs
    current_container_name = wait_to_sync::waittosync(hostNames, num_hosts);
    if (current_container_name == nullptr){perror("Obtaining current container's name failed (from wait to sync). Exiting.\n");exit(1);}
    // that also extracts the id
    current_container_id = extract_int_from_string(std::string(current_container_name));
    for (int i = 0; i<num_hosts; i++){
        hostIDtoHostName.insert(std::make_pair(extract_int_from_string(hostNames[i]), std::string(hostNames[i])));
    }
    printf("Current container's name: %s and id: %d\n", current_container_name, current_container_id);
    /* global snapshot */
    recordMessages = false;
    snapshot.set_rm(this);
    std::thread snapshotListener(&CL_Global_Snapshot::listen_for_incoming_connections, &snapshot);
    snapshotListener.detach();  // maybe join elsewhere?
}

[[noreturn]] void ReliableMulticast::msg_receiver(){
    int numbytes;
    unsigned long int type;
    DataMessage dataMessage;
    AckMessage ackMessage;
    SeqMessage seqMessage;
    unsigned char msg_buf[MAX_STRUCT_SIZE];
    while (recv_cap < RECV_CAP){
        DPRINTF(("Waiting for new msg...\n"));
        numbytes = communicator.recv(reinterpret_cast<char *>(msg_buf), MAX_MSG_SIZE);
        if (numbytes == -1) {perror("msg_receiver: recvfrom error..."); exit(1);}
        recordMessagesMutex.lock();  // this is for global snapshot
        if (recordMessages){  // receiving msgs
//            printf("[debug msg_receiver] we are told to record msgs!\n");
            snapshot.inboundMessageBufferMutex.lock();
            snapshot.inboundMessageBuffer.push(ByteVector(msg_buf, msg_buf+MAX_STRUCT_SIZE));
            snapshot.inboundMessageBufferMutex.unlock();
//            printf("[debug msg_receiver] successful recorded a msg!\n");
        }
        recordMessagesMutex.unlock();
        type = unpacku32(&msg_buf[0]);
//        DPRINTF(("Received msg is of type: %lu\n", type));
        switch (type) {
            case DATAMSG_TYPE:
                deserialize_data_message(msg_buf, dataMessage);
                handle_datamsg(dataMessage);
                break;
            case ACKMSG_TYPE:
                deserialize_ack_message(msg_buf, ackMessage);
                handle_ackmsg(ackMessage);
                break;
            case SEQMSG_TYPE:
                deserialize_seq_message(msg_buf, seqMessage);
                handle_seqmsg(seqMessage);
                break;
            default:
                fprintf(stderr, "Received message wrong type: %lu....\n", type);
                exit(1);
        }
        recv_cap++;
    }
    while(true){printf("Receiver received MAX timeout... Please exit.\n");sleep(100);}  // for no return...
}


void ReliableMulticast::handle_datamsg(const DataMessage &dataMessage){
    /* This process is receiving a data message from some other process.
     * If we have seen this before (i.e. a duplicate message), we resend the old ack
     * otherwise we send a new ack
     * */
    DPRINTF(("*** Received data message: type %d with sender_id %d and msg_id %d and data %d\n"
            , dataMessage.type, dataMessage.sender, dataMessage.msg_id, dataMessage.data));
    for (AckMessage am : alreadyAckedMessages){  // check to see if we have already acked this message
        if (am.sender == dataMessage.sender && am.msg_id == dataMessage.msg_id){  // this dataMessage has already been acked
            // we resend it
            unsigned char serialized_packet[MAX_STRUCT_SIZE];
            serialize_ack_message(am, serialized_packet);
            reply_msg_with_drop_and_delay(serialized_packet);
            return;
        }
    }
    // we need to add the message in the queue (with the latest sequence number + 1) and marking it undeliverable
    QueuedMessage toQueue = make_queued_msg(curr_seq_number, UNDELIVERABLE, dataMessage.sender,
                                            dataMessage.msg_id,dataMessage.data,current_container_id);
    deliveryQueueMutex.lock();
    push_msg_to_deliveryqueue(toQueue);
    deliveryQueueMutex.unlock();

    // then we send that latest sequence number as an acknowledgement to the sender of the message (along with our id)
    AckMessage ackMessage = make_ack_msg(dataMessage.sender, dataMessage.msg_id, curr_seq_number, current_container_id);
    alreadyAckedMessages.push_back(ackMessage);
    // packing the message
    unsigned char serialized_packet[MAX_STRUCT_SIZE];
//    DPRINTF(("PREPARING TO REPLY ACK: type %d, sender %d, msg_id %d, proposed_seq %d, proposer %d\n",
//            ackMessage.type, ackMessage.sender, ackMessage.msg_id, ackMessage.proposed_seq, ackMessage.proposer));
    serialize_ack_message(ackMessage, serialized_packet);
    // send it back to the sender
    reply_msg_with_drop_and_delay(serialized_packet);
    curr_seq_number++;
    // then we are supposed to hear back from the sender a final sequence number (which we can then handle elsewhere)
    // suppose we don't hear back after a while....
    //  --> we should send the ack again (bc the ack might be dropped or the seq might be dropped)
    DPRINTF(("handle_datamsg: spawning watchdog thread for sent out ack...\n"));
    const char * rep_host_name = hostIDtoHostName[dataMessage.sender].c_str();
    // spawning this watchdog to resend ackmsg correspondingly
    std::thread watchdog(&ReliableMulticast::ackmsg_watchdog, this, ackMessage, rep_host_name); watchdog.detach();
}


void ReliableMulticast::ackmsg_watchdog(const AckMessage &ackMessage, const char * hostName){
    /* This watchdog watches for whether the message corresponding to the ackMessage has received a final sequence
     * After a timeout, we check if we have received a responding seqMessage from host
     * If not, we resend the ackMessage to the host and wait again */
    int watchdog_resend_cap = 0;
    while (watchdog_resend_cap++ < WATCHDOG_RESEND_CAP){
        DPRINTF(("[ackmsg_watchdog] Attempt %d for msg (%d, %d) from host %s. Sleeping for %d miliseconds...\n",
                watchdog_resend_cap, ackMessage.msg_id, ackMessage.sender, hostName, TIMEOUT));
        usleep(TIMEOUT*1000);  // sleep for TIMEOUT miliseconds
        // when wake up, we check if we have received a responding seq message for this msg
        seqMessageHistoryMutex.lock();
        for (SeqMessage sm : seqMessageHistory){  // a seq is in here if we receive a seqmsg or sent out one
            if (sm.sender == ackMessage.sender && sm.msg_id == ackMessage.msg_id){  // we have found it in the SeqHistory
                DPRINTF(("[ackmsg_WATCHDOG FINISHED] Found an SEQ for msg (%d, %d) and host %s. Terminating!\n",
                        ackMessage.msg_id, ackMessage.sender, hostName));
                seqMessageHistoryMutex.unlock();
                return;
            }
        }
        seqMessageHistoryMutex.unlock();
        // we get here when we weren't able to find a corresponding seq msg for the ack in the seqhistory
        // we resend the ack to the host
        DPRINTF(("[ackmsg_WATCHDOG TIMEOUT] Haven't received Ack for msg (%d, %d) from host %s. Resending ack and Resleeping.\n ",
                ackMessage.msg_id, ackMessage.sender, hostName));
        unsigned char serialized_packet[MAX_STRUCT_SIZE];
        serialize_ack_message(ackMessage, serialized_packet);
        int rv = send_msg_with_drop_and_delay(hostName, serialized_packet);
        if (rv == -1){perror("Error sending message. Exiting...\n");exit(1);}
        if (rv == -22) DPRINTF(("[FROM datamsg_WATCHDOG] Message (%d, %d) to %s was dropped\n",
                    ackMessage.msg_id, ackMessage.sender, hostName));
    }
    printf("ackmsg_WATCHDOG WAITED MAXIMUM TIMES! SOMETHING WENT WRONG...HOST %s EITHER CRASHED OR NETWORK PROBLEM\n", hostName);
}


void ReliableMulticast::handle_ackmsg(const AckMessage &ackMessage){
    /* here we are receiving an AckMessage for some dataMessage that we sent out
    *  ackHistory[ackMessage.msg_id] contains the history of received acks for this msg.
    ** if the Ack is for an older msg, we resend the sequence number. Otherwise we handle the new one: */

    DPRINTF(("*** Received ACK MSG with sender_id %d, msg_id %d, seq %d, and proposer %d\n"
        , ackMessage.sender, ackMessage.msg_id, ackMessage.proposed_seq, ackMessage.proposer));

    uint32_t msg_id = ackMessage.msg_id;
    ackHistoryMutex.lock();
    if (ackHistory[msg_id].count(ackMessage.proposer) == 0){  // this means we haven't receive this ack before
        // we add it to the history
        curr_seq_number++;  // to avoid clashing
        ackHistory[msg_id].insert(std::make_pair(ackMessage.proposer, ackMessage.proposed_seq));
        if(ackHistory[msg_id].size() == (num_hosts-1)){  // we have collected enough ACKs for this msg
//            DPRINTF(("[handle_ACKmsg] we have received enough ACKS. Attempting to add and deliver.\n"));print_ack_history();
            // we pick the max (noting the proposer of the max) and then send out a final sequence to everybody
            std::pair<uint32_t, uint32_t> finalSeqAndProposer = get_max_sequence_from_proposerseq_map(ackHistory[msg_id]);
            uint32_t finalseq = finalSeqAndProposer.first;
            uint32_t finalseq_proposer = finalSeqAndProposer.second;
            SeqMessage seqMessage = make_seq_msg(ackMessage.sender, ackMessage.msg_id, finalseq, finalseq_proposer);
            seqMessageHistoryMutex.lock();
            seqMessageHistory.push_back(seqMessage);
            seqMessageHistoryMutex.unlock();
            broadcast_seq_msg(seqMessage);  // this sends the seqMessage to everybody --> they should perform the step below
            // now we need to update our own delivery queue with this max number -- it should be deliverable now
            deliveryQueueMutex.lock();
            change_queued_msg_seq_and_status(seqMessage.sender, seqMessage.msg_id, finalseq,
                                             finalseq_proposer, DELIVERABLE);
            deliveryQueueMutex.unlock();
            // now that we've changed the deliveryqueue, we attempt to deliver new messages
            deliver_msg_from_deliveryqueue();
        }
    } // otherwise if we've seen it then we see if it's from an ACK sending process that hasn't received final seq after a while
    else if (ackHistory[msg_id].size() == (num_hosts-1)){ // this means we have finalized and sent the seq before
        // resend final sequence number
        DPRINTF(("RECEIVED A DUPLICATE ACK FROM %d FOR MSG (%d, %d). RESENDING SEQ...\n",
                ackMessage.proposer, ackMessage.msg_id, ackMessage.sender));
        int found = 0;
        seqMessageHistoryMutex.lock();
        for (SeqMessage sm : seqMessageHistory){
            if(sm.msg_id == msg_id && sm.sender == ackMessage.sender){
                found = 1;
                unsigned char serialized_packet[MAX_STRUCT_SIZE];
                serialize_seq_message(sm, serialized_packet);
                int rv = reply_msg_with_drop_and_delay(serialized_packet);
                if (rv == -1){perror("[handle_ackmsg] Error sending message. Exiting...\n"); seqMessageHistoryMutex.unlock();
                exit(1);}
                if (rv == -22) printf("[handle_ackmsg] Resending SeqMessage for (%d, %d) to process_id %d was dropped\n",
                                      sm.msg_id, sm.sender, ackMessage.proposer);
                break;
            }
        }
        seqMessageHistoryMutex.unlock();
        if (found == 0){  // we haven't found the seqMessage.... strange
            perror("[handle_ackmsg] Cannot find seqmsg in history. Some weird error happened. Exiting...\n");
            ackHistoryMutex.unlock(); exit(1);
        }
    }
    ackHistoryMutex.unlock();
}


void ReliableMulticast::handle_seqmsg(const SeqMessage &seqMessage){
    /* here we are receiving the final sequence for some message in our delivery queue
    * note that the first element in our queue is the smallest seq number msg (that is also undeliverable -- otherwise it would've been delivered
    * if the seqmessage's message is not in our delivery queue (it must've been delivered already), then we simply ignore it
    * otherwise we reorder the queue based on the message's new seq number and mark it deliverable
    ** then we peek at the top of the queue and pop all deliverable messages until they are no longer deliverable*/
    DPRINTF(("*** Received SEQmsg with msg_id %d, sender %d, seq %d, proposer %d\n",
        seqMessage.msg_id, seqMessage.sender, seqMessage.final_seq, seqMessage.final_seq_proposer));
#ifdef DEBUG
    print_delivery_queue();
#endif
    deliveryQueueMutex.lock();
    int rv = change_queued_msg_seq_and_status(seqMessage.sender, seqMessage.msg_id,
                                              seqMessage.final_seq, seqMessage.final_seq_proposer, DELIVERABLE);
    deliveryQueueMutex.unlock();

    deliveredMessageMutex.lock();
    if (rv == -1){  // we didn't find it in the deliveryqueue... it must've been in our deliveredMessage list
        for (QueuedMessage qm : deliveredMessage){
            if (qm.msg_id == seqMessage.msg_id && qm.sender == seqMessage.sender){
                DPRINTF(("handle_seqmsg received duplicate seqmessage for sender %d and msg_id %d with finalsequence %d\n",
                        seqMessage.sender, seqMessage.msg_id, seqMessage.final_seq_proposer));
                deliveredMessageMutex.unlock();
                return;
            }
        } // so we couldn't find it in the deliveredMessage list also... we throw an error just to be safe
        perror("handle_seqmsg ERROR: COULDN'T LOCATE MESSAGE FOR INCOMING SEQMESSAGE. EXITING...\n");
        deliveredMessageMutex.unlock();
        exit(1);
    }
    deliveredMessageMutex.unlock();

    // add it to the history if we haven't received it... then attempt to deliver
    seqMessageHistoryMutex.lock();
    seqMessageHistory.push_back(seqMessage);
    seqMessageHistoryMutex.unlock();
    deliver_msg_from_deliveryqueue();
}


void ReliableMulticast::multicast_datamsg(uint32_t data){
    /* we wish to multicast a message to all other messages with total ordering guarantee
     * we must take note of which message has been sent (probably using msgid) and wait to collect ack after sending out
     * now, we must take into account that our msg is dropped. hence, we spawn a thread (watchdog) per other process that
     * -- after a certain timeout, check to see if an ack for this msg has been received from the proc. it's responsible for
     * -- if not, then we resend the data msg to that process and repeat but only for that process.
     * -- after repeating too many times, we declare that process dead and find a way to gracefully terminate (or notify)
     * */
//    DPRINTF(("INSIDE multicast_datamsg: Sending data %d with delay %d and drop rate %.6f \n", data, delay_in_ms, drop_rate));

    DataMessage dataMessage;
    dataMessage.type = DATAMSG_TYPE;
    dataMessage.msg_id = curr_msg_id++;
    dataMessage.data = data;
    dataMessage.sender = current_container_id;
    // add this to the queuedmessage for self-delivery... but undeliverable
    dataHistoryMutex.lock();
    dataHistory.insert(std::make_pair(dataMessage.msg_id, dataMessage.data));
    dataHistoryMutex.unlock();

    ProposerSeq ackHistForThisMes;
    ackHistoryMutex.lock();
    ackHistory.insert(std::make_pair(dataMessage.msg_id, ackHistForThisMes));
    ackHistoryMutex.unlock();

    QueuedMessage queuedMessage = make_queued_msg(curr_seq_number++, UNDELIVERABLE, dataMessage.sender, dataMessage.msg_id,
                                                  dataMessage.data, current_container_id);
    deliveryQueueMutex.lock();
    push_msg_to_deliveryqueue(queuedMessage);
    deliveryQueueMutex.unlock();


    // first serialize the data message before multicast
    unsigned char serialized_packet[MAX_STRUCT_SIZE];
    serialize_data_message(dataMessage, serialized_packet);
    int rv;
    for (int i =0; i< num_hosts; i++){
        if (strcmp(hostNames[i], current_container_name) != 0){
            rv = send_msg_with_drop_and_delay(hostNames[i], serialized_packet);
            if (rv == -1){perror("Error sending message. Exiting...\n"); exit(1);}
            if (rv == -22)
                DPRINTF(("[multicast_datamsg] Message (%d) to %s was dropped\n", dataMessage.msg_id, hostNames[i]));
            else
                DPRINTF(("*** Multicasted message of type %d with sender_id %d and msg_id %d and data %d to %s\n", dataMessage.type, dataMessage.sender, dataMessage.msg_id, dataMessage.data, hostNames[i]));
            // after sending out a message, we must make sure that we receive an ack after a certain timeout
            // -- this can be done by spawning a watch_dog thread that sleeps for the TIMEOUT period
            // -- then after that period, it would check ackHistory[msg_id] that the hostID has some entry...
            // -- if it's empty then we resend and repeat (maybe we have a cap and then declare the process dead)
            // note we can make this thread detach since the main thread never terminates unless some severe error.
            DPRINTF(("multicast_datamsg: spawning watchdog thread...\n"));
            std::thread watchdog(&ReliableMulticast::datamsg_watchdog, this, dataMessage, hostNames[i]); watchdog.detach();
        }
    }
}


void ReliableMulticast::datamsg_watchdog(const DataMessage &dataMessage, const char * hostName)  {
    /* The reason why we haven't received an ACK can be from:
     *  1. The dataMessage was dropped (in case we resend)
     *  or 2. The ACK was dropped.
     * This can be fixed by resending the dataMessage to cure case 1 or to signal for resending ACK */
    int watchdog_resend_cap = 0;
    while (watchdog_resend_cap++ < WATCHDOG_RESEND_CAP){
        DPRINTF(("[Attempt %d] Hello this is datamsg_watchdog for msg (%d, %d) and host %s. Sleeping for %d miliseconds...\n",
                watchdog_resend_cap, dataMessage.msg_id, dataMessage.sender, hostName, TIMEOUT));
        usleep(TIMEOUT*1000);  // first we sleep for TIMEOUT miliseconds
        // when we wake up, we check if that message has been acked by this host yet
        ackHistoryMutex.lock();
        auto historyfordm = ackHistory.find(dataMessage.msg_id);
        if (historyfordm == ackHistory.end()){
            fprintf(stderr, "data_msg WATCHDOG: Cannot find data_msg %d in AckHistory!!!\n", dataMessage.msg_id);
            ackHistoryMutex.unlock();
            exit(1);
        }
        int hostID = extract_int_from_string(hostName);
        if (historyfordm->second.count(hostID) == 0){  // this means we haven't received an Ack for that host
            // we resend the data message and wait again...
            DPRINTF(("[datamsg_WATCHDOG TIMEOUT] Haven't received Ack for msg_id %d from host %s. Resending datamessage and Resleeping.\n ",
                    dataMessage.msg_id, hostName));
            unsigned char serialized_packet[MAX_STRUCT_SIZE];
            serialize_data_message(dataMessage, serialized_packet);
            int rv = send_msg_with_drop_and_delay(hostName, serialized_packet);
            if (rv == -1){
                perror("Error sending message. Exiting...\n");
                ackHistoryMutex.unlock();
                exit(1);
            }
            if (rv == -22) DPRINTF(("[FROM datamsg_WATCHDOG] Message (%d, %d) to %s was dropped\n",
                    dataMessage.msg_id, dataMessage.sender, hostName));
        } else { // this means we have received an ACK !!! we can terminate
            DPRINTF(("[datamsg_WATCHDOG FINISHED] Found an ACK for msg_id %d and host %s. Terminating!\n", dataMessage.msg_id, hostName));
            ackHistoryMutex.unlock();
            return;
        }
        ackHistoryMutex.unlock();
    }
    printf("datamsg_WATCHDOG WAITED MAXIMUM TIMES! SOMETHING WENT WRONG...HOST %s EITHER CRASHED OR NETWORK PROBLEM\n", hostName);
}


int ReliableMulticast::reply_msg_with_drop_and_delay(unsigned char (&serialized_packet)[MAX_STRUCT_SIZE]) {
    start_delay();
    if (random_uniform_from_0_to_1() < drop_rate){
        DPRINTF(("[Testing] Replying dropped msg!\n"));
        return -22;
    }
    recordMessagesMutex.lock();
    if (recordMessages){
//        printf("[debug reply] we are told to record msgs!\n");
        snapshot.outboundMessageBufferMutex.lock();
        snapshot.outboundMessageBuffer.push(ByteVector(serialized_packet, serialized_packet+MAX_STRUCT_SIZE));
        snapshot.outboundMessageBufferMutex.unlock();
//        printf("[debug reply] done recording msg!\n");
    }
    recordMessagesMutex.unlock();
    return communicator.reply(reinterpret_cast<const char *>(serialized_packet), sizeof(serialized_packet));
}


int ReliableMulticast::send_msg_with_drop_and_delay(const char *hostname, unsigned char (&serialized_packet)[MAX_STRUCT_SIZE]) {
    // this function also implements any delay and msg drop if applicable
    start_delay();
    if (random_uniform_from_0_to_1() < drop_rate){
//        DPRINTF(("[Testing] Message to %s was dropped!\n", hostname));
        return -22;
    }
    recordMessagesMutex.lock();
    if (recordMessages){
//        printf("[debug send_msg] we are told to record msgs!\n");
        snapshot.outboundMessageBufferMutex.lock();
        snapshot.outboundMessageBuffer.push(ByteVector(serialized_packet, serialized_packet+MAX_STRUCT_SIZE));
        snapshot.outboundMessageBufferMutex.unlock();
//        printf("[debug msg_receiver] recorded msg!\n");
    }
    recordMessagesMutex.unlock();
    return communicator.send_to(hostname, reinterpret_cast<const char *>(serialized_packet), sizeof(serialized_packet));  // -1 to use strlen
}


void ReliableMulticast::broadcast_seq_msg(const SeqMessage &seqMessage){
    // first pack the message
    unsigned char serialized_packet[MAX_STRUCT_SIZE];
    serialize_seq_message(seqMessage, serialized_packet);
    // then send it to everybody
    int rv;
    for (int i =0; i< num_hosts; i++){
        if (strcmp(hostNames[i], current_container_name) != 0){
//            rv = communicator.send_to(hostNames[i], reinterpret_cast<const char *>(serialized_packet), sizeof(serialized_packet));
            rv = send_msg_with_drop_and_delay(hostNames[i], serialized_packet);
            if (rv == -1){perror("Error sending message. Exiting...\n"); exit(1);}
            if (rv == -22) printf("SeqMessage for (%d, %d) to %s was dropped\n", seqMessage.msg_id, seqMessage.sender, hostNames[i]);

        }
    }
}


void ReliableMulticast::push_msg_to_deliveryqueue(QueuedMessage qm){
    // this guarantees that our deliveryqueue is indeep a minheap w.r.t. the sequence number and then sender_id
   deliveryQueue.push_back(qm);
   std::push_heap(deliveryQueue.begin(), deliveryQueue.end(), cmp);
}


void ReliableMulticast::print_delivery_queue(){
    deliveryQueueMutex.lock();
    printf("=== deliveryQueue (min-heap of size %lu) ====\n", deliveryQueue.size());
    for (const QueuedMessage &qm: deliveryQueue){
        printf("\tseq/proposer (%d, %d), msg_id/sender (%d, %d)\n",
               qm.sequence_number, qm.proposer, qm.msg_id, qm.sender);
    }
    printf("=================================\n");
    deliveryQueueMutex.unlock();
}

void ReliableMulticast::print_ack_history(){
    printf("=== ackHistory (size %lu) ====\n", ackHistory.size());
    for (const auto &seq: ackHistory){
        printf("\tmsg_id %d", seq.first);
        for (const auto &kv : seq.second){
            printf("prop %d seq %d, ", kv.first, kv.second);
        }
        printf("\n");
    }
    printf("=================================\n");
}


void ReliableMulticast::start_delay() const{
    if (delay_in_ms == 0) return;
    DPRINTF(("Delaying %d ms...\n", delay_in_ms));
    usleep(delay_in_ms*1000);  // sleep for duration
}


double ReliableMulticast::random_uniform_from_0_to_1() {
    return (double)rand() / (double)RAND_MAX;
}


void ReliableMulticast::print_delivered_messages() {
    deliveredMessageMutex.lock();
    printf("=== delivered messages so far (size %lu) ====\n", deliveredMessage.size());
    int i = 0;
    for (const QueuedMessage &qm: deliveredMessage){
        printf("\t%d: seq/proposer (%d, %d), msg_id/sender (%d, %d)\n", i++,
               qm.sequence_number, qm.proposer, qm.msg_id, qm.sender);
    }
    deliveredMessageMutex.unlock();
    printf("=================================\n");
}


void ReliableMulticast::deliver_msg_from_deliveryqueue() {
    // we check if the front of the deliveryQueue (assumed it's a heap from the other operations)
    // -- if the front is DELIVERABLE then we deliver it and then pop it from the queue
    // -- we repeat until the front is UNDELIVERABLE
//    DPRINTF(("INSIDE deliver_msg_from_deliveryqueue. Trying to deliver:\n"));
#ifdef DEBUG
    print_delivery_queue();
#endif
    deliveryQueueMutex.lock();
    deliveredMessageMutex.lock();
    while((!deliveryQueue.empty()) && deliveryQueue[0].status == DELIVERABLE){  // we found a deliverable msg with the smallest seq number
        QueuedMessage delivered_msg = deliveryQueue[0];
        deliveredMessage.push_back(delivered_msg);  // we deliver it in the queue
        printf("ProcessID %d: Processed message %d from sender %d with seq (%d, %d).\n", current_container_id,
               delivered_msg.msg_id, delivered_msg.sender, delivered_msg.sequence_number, delivered_msg.proposer);
        // then we pop the first element
        std::pop_heap(deliveryQueue.begin(), deliveryQueue.end(), cmp);
        deliveryQueue.pop_back();
    }
    deliveredMessageMutex.unlock();
    deliveryQueueMutex.unlock();
    print_delivered_messages();
//    DPRINTF(("EXIT deliver_msg_from_deliveryqueue\n"));
}


int ReliableMulticast::change_queued_msg_seq_and_status(uint32_t sender, uint32_t msg_id, uint32_t seq_to_change, uint32_t seq_proposer, unsigned char status){
    /* return 0 for success and -1 for failure (i.e. cannot find a matching msg with sender and msg_id */
    // we find in the deliveryqueue with sender_id and msg_id and then change their seq and status correspondingly
//    DPRINTF(("----- Inside change_queuedmsg_seq_and_status -----\n "));
    for (QueuedMessage &qm : deliveryQueue){
        if (qm.sender == sender && qm.msg_id == msg_id){  // we found the msg
//            DPRINTF(("FOUND sender %d and msg_id %d. Change seq number from %d to %d and status to %d\n",
//                    sender, msg_id, qm.sequence_number, seq_to_change, status));
            qm.sequence_number = seq_to_change;
            qm.status = status;
            qm.proposer = seq_proposer;
            // now that we changed sequence number, we must make it a heap again
            std::make_heap(deliveryQueue.begin(), deliveryQueue.end(), cmp);

            return 0;
        }
    }  // we found nothing...
    return -1;
}


std::pair<uint32_t, uint32_t> ReliableMulticast::get_max_sequence_from_proposerseq_map(const ProposerSeq &pm){
    uint32_t result_seq         = 0;
    uint32_t result_proposer    = 0;
    for (auto const& kv: pm){
        uint32_t proposer = kv.first;
        uint32_t sequence_num = kv.second;
        if (sequence_num > result_seq){
            result_seq = sequence_num;
            result_proposer = proposer;
        }
    }
    return std::make_pair(result_seq, result_proposer);
}


AckMessage ReliableMulticast::make_ack_msg(uint32_t sender, uint32_t msg_id, uint32_t proposed_seq, uint32_t proposer){
    AckMessage ackMessage;
    ackMessage.type = ACKMSG_TYPE;
    ackMessage.proposer = proposer;
    ackMessage.msg_id = msg_id;
    ackMessage.sender = sender;
    ackMessage.proposed_seq = proposed_seq;
    return ackMessage;
}


SeqMessage ReliableMulticast::make_seq_msg(uint32_t sender, uint32_t msg_id, uint32_t final_seq, uint32_t final_seq_proposer){
    SeqMessage seqMessage;
    seqMessage.type = SEQMSG_TYPE;
    seqMessage.sender = sender;
    seqMessage.msg_id = msg_id;
    seqMessage.final_seq = final_seq;
    seqMessage.final_seq_proposer = final_seq_proposer;
    return seqMessage;
}

QueuedMessage ReliableMulticast::make_queued_msg(uint32_t sequence_number, unsigned char status, uint32_t sender,
                                     uint32_t msg_id, uint32_t data, uint32_t proposer){
    QueuedMessage toQueue;
    toQueue.data = data;
    toQueue.msg_id = msg_id;
    toQueue.sender = sender;
    toQueue.proposer = proposer;
    toQueue.status = status;
    toQueue.sequence_number = sequence_number;
    return toQueue;
}

void ReliableMulticast::start_msg_receiver(ReliableMulticast* rm) {
    /* to be called via a receiver thread in main
     *    std::thread receiver_thread(ReliableMulticast::start_msg_receiver, reliableMulticast);
     *    <in between...>
     *    receiver_thread.join();
     * */
    rm->msg_receiver();
}


ReliableMulticast::~ReliableMulticast() {
    for (int i =0; i< num_hosts; i++){
        delete [] hostNames[i];
    }
    delete [] hostNames;
}

void packi32(unsigned char *buf, unsigned long int i)
{
    *buf++ = i>>24; *buf++ = i>>16;
    *buf++ = i>>8;  *buf++ = i;
}


unsigned long int unpacku32(unsigned char *buf)
{
    return ((unsigned long int)buf[0]<<24) |
           ((unsigned long int)buf[1]<<16) |
           ((unsigned long int)buf[2]<<8)  |
           buf[3];
}

void serialize_data_message(const DataMessage &dataMessage, unsigned char * buf){
    packi32(&buf[0], dataMessage.type);
    packi32(&buf[4], dataMessage.sender);
    packi32(&buf[8], dataMessage.msg_id);
    packi32(&buf[12], dataMessage.data);
}

void deserialize_data_message(unsigned char * buf, DataMessage &dataMessage){
    dataMessage.type = unpacku32(&buf[0]);
    dataMessage.sender = unpacku32(&buf[4]);
    dataMessage.msg_id = unpacku32(&buf[8]);
    dataMessage.data = unpacku32(&buf[12]);
}

void serialize_ack_message(const AckMessage &ackMessage, unsigned char * buf){
    packi32(&buf[0], ackMessage.type);
    packi32(&buf[4], ackMessage.sender);
    packi32(&buf[8], ackMessage.msg_id);
    packi32(&buf[12], ackMessage.proposed_seq);
    packi32(&buf[16], ackMessage.proposer);
}

void deserialize_ack_message(unsigned char * buf, AckMessage &ackMessage){
    ackMessage.type = unpacku32(&buf[0]);
    ackMessage.sender = unpacku32(&buf[4]);
    ackMessage.msg_id = unpacku32(&buf[8]);
    ackMessage.proposed_seq = unpacku32(&buf[12]);
    ackMessage.proposer = unpacku32(&buf[16]);
}

void serialize_seq_message(const SeqMessage &seqMessage, unsigned char * buf){
    packi32(&buf[0], seqMessage.type);
    packi32(&buf[4], seqMessage.sender);
    packi32(&buf[8], seqMessage.msg_id);
    packi32(&buf[12], seqMessage.final_seq);
    packi32(&buf[16], seqMessage.final_seq_proposer);
}

void deserialize_seq_message(unsigned char * buf, SeqMessage &seqMessage){
    seqMessage.type = unpacku32(&buf[0]);
    seqMessage.sender = unpacku32(&buf[4]);
    seqMessage.msg_id = unpacku32(&buf[8]);
    seqMessage.final_seq = unpacku32(&buf[12]);
    seqMessage.final_seq_proposer = unpacku32(&buf[16]);
}

int extract_int_from_string(std::string str){
    // For atoi, the input string has to start with a digit, so lets search for the first digit
    size_t i = 0;
    for ( ; i < str.length(); i++ ){ if ( isdigit(str[i]) ) break; }

    // remove the first chars, which aren't digits
    str = str.substr(i, str.length() - i );

    // convert the remaining text to an integer
    int id = atoi(str.c_str());
    return id;
}

int ReliableMulticast::get_delay() const {
    return delay_in_ms;
}


/* For Global Snapshot */
LocalStateSnapshot ReliableMulticast::get_local_state_snapshot() {
//    printf("[debug getlocalstatesnapshot]: prepping to copy\n");
    LocalStateSnapshot result;
    deliveryQueueMutex.lock();
    result.deliveryQueue = std::vector<QueuedMessage>(deliveryQueue);
    deliveryQueueMutex.unlock();
//    printf("[debug getlocalstatesnapshot]: copied deliveryQueue\n");
//    printf("[debug  getlocalstatesnapshot]=== deliveryQueue (min-heap of size %lu) ====\n", result.deliveryQueue.size());
//    for (const QueuedMessage &qm: result.deliveryQueue){
//        printf("\tseq/proposer (%d, %d), msg_id/sender (%d, %d)\n",
//               qm.sequence_number, qm.proposer, qm.msg_id, qm.sender);
//    }
//    printf("=================================\n\n");

    deliveredMessageMutex.lock();
    result.deliveredMessage = std::vector<QueuedMessage>(deliveredMessage);
    deliveredMessageMutex.unlock();
//    printf("[debug getlocalstatesnapshot]: copied deliveryQueue\n");

    return result;
}


void ReliableMulticast::initiate_snapshot() {
    snapshot.initiate_snapshot();
}