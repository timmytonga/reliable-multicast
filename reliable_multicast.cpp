//
// Created by Thien Nguyen on 10/8/20.
//

#include "reliable_multicast.h"

ReliableMulticast::ReliableMulticast(const char *hostFileName,
                                     const udp_client_server::UDP_Server& comm,
                                     double drop_rate, int delay_in_ms)
        : communicator(comm), deliveryQueue{}, ackHistory{}, drop_rate(drop_rate), delay_in_ms(delay_in_ms){
    // user should make sure drop_rate and delay_in_ms are reasonable values.
    hostNames = new char*[MAX_NUM_HOSTS];
    num_hosts = wait_to_sync::read_from_file(hostFileName, hostNames);
    // we wait for all the hosts to be ready before sending msgs
    current_container_name = wait_to_sync::waittosync(hostNames, num_hosts);
    if (current_container_name == nullptr){perror("Obtaining current container's name failed (from wait to sync). Exiting.\n");exit(1);}
    // that also extracts the id
    current_container_id = extract_int_from_string(std::string(current_container_name));

    printf("Current container's name: %s and id: %d\n", current_container_name, current_container_id);

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
        type = unpacku32(&msg_buf[0]);
        DPRINTF(("Received msg is of type: %lu\n", type));
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
//    DPRINTF(("*** Inside handle_datamsg: type %d with sender_id %d and msg_id %d and data %d\n"
//            , dataMessage.type, dataMessage.sender, dataMessage.msg_id, dataMessage.data));
    // we need to add the message in the queue (with the latest sequence number + 1) and marking it undeliverable
    QueuedMessage toQueue = make_queued_msg(curr_seq_number, UNDELIVERABLE, dataMessage.sender,
                                            dataMessage.msg_id,dataMessage.data,current_container_id);
    deliveryQueueMutex.lock();
    push_msg_to_deliveryqueue(toQueue);
    deliveryQueueMutex.unlock();

    // then we send that latest sequence number as an acknowledgement to the sender of the message (along with our id)
    AckMessage ackMessage = make_ack_msg(dataMessage.sender, dataMessage.msg_id, curr_seq_number, current_container_id);
    // packing the message
    unsigned char serialized_packet[MAX_STRUCT_SIZE];
//    DPRINTF(("PREPARING TO REPLY ACK: type %d, sender %d, msg_id %d, proposed_seq %d, proposer %d\n",
//            ackMessage.type, ackMessage.sender, ackMessage.msg_id, ackMessage.proposed_seq, ackMessage.proposer));
    serialize_ack_message(ackMessage, serialized_packet);
    // send it back to the sender
    communicator.reply(reinterpret_cast<const char *>(serialized_packet), sizeof(serialized_packet));
    curr_seq_number++;
    // then we are supposed to hear back from the sender a final sequence number (which we can then handle elsewhere)
    // so this process is receiving a message from some other process
    // suppose we don't hear back after a while....
    //  --> we should send the ack again (bc the ack might be dropped or the seq might be dropped)
}


void ReliableMulticast::handle_ackmsg(const AckMessage &ackMessage){
    // here we are receiving an AckMessage for some dataMessage that we sent out
    DPRINTF(("*** Inside handle_ackmsg: type %d with sender_id %d, msg_id %d, seq %d, and proposer %d\n"
            , ackMessage.type, ackMessage.sender, ackMessage.msg_id, ackMessage.proposed_seq, ackMessage.proposer));
    //  ackHistory[ackMessage.msg_id] contains the history of received acks for this msg.
    // if the Ack is for an older msg, we resend the sequence number. Otherwise we handle the new one:
    uint32_t msg_id = ackMessage.msg_id;
    ackHistoryMutex.lock();
    if (ackHistory[msg_id].count(ackMessage.proposer) == 0){  // this means we haven't receive this ack before
        // we add it to the history
        ackHistory[msg_id].insert(std::make_pair(ackMessage.proposer, ackMessage.proposed_seq));
        if(ackHistory[msg_id].size() == (num_hosts-1)){  // we have collected enough ACKs for this msg
//            DPRINTF(("[handle_ACKmsg] we have received enough ACKS. Attempting to add and deliver.\n"));print_ack_history();
            // we pick the max (noting the proposer of the max) and then send out a final sequence to everybody
            std::pair<uint32_t, uint32_t> finalSeqAndProposer = get_max_sequence_from_proposerseq_map(ackHistory[msg_id]);
            uint32_t finalseq = finalSeqAndProposer.first;
            uint32_t finalseq_proposer = finalSeqAndProposer.second;
            SeqMessage seqMessage = make_seq_msg(ackMessage.sender, ackMessage.msg_id, finalseq, finalseq_proposer);
            broadcast_seq_msg(seqMessage);  // this sends the seqMessage to everybody --> they should perform the step below
            // now we need to update our own delivery queue with this max number -- it should be deliverable now
            dataHistoryMutex.lock();
            QueuedMessage queuedMessage = make_queued_msg(seqMessage.final_seq, DELIVERABLE, seqMessage.sender,
                                                          seqMessage.msg_id, dataHistory[seqMessage.msg_id],
                                                          seqMessage.final_seq_proposer);
            dataHistoryMutex.unlock();
            deliveryQueueMutex.lock();
            push_msg_to_deliveryqueue(queuedMessage);
            deliveryQueueMutex.unlock();
            // now that we've changed the deliveryqueue, we attempt to deliver new messages
            deliver_msg_from_deliveryqueue();
        }
    } // otherwise if we've seen it then we see if it's from an ACK sending process that hasn't received final seq after a while
    else if (ackHistory[msg_id].size() == (num_hosts-1)){ // this means we have finalized and sent the seq before
        // resend final sequence number
    }
    ackHistoryMutex.unlock();

}


void ReliableMulticast::handle_seqmsg(const SeqMessage &seqMessage){
    // here we are receiving the final sequence for some message in our delivery queue
    // note that the first element in our queue is the smallest seq number msg (that is also undeliverable -- otherwise it would've been delivered
    // if the seqmessage's message is not in our delivery queue (it must've been delivered already), then we simply ignore it
    // otherwise we reorder the queue based on the message's new seq number and mark it deliverable
    // then we peek at the top of the queue and pop all deliverable messages until they are no longer deliverable
//    DPRINTF(("INSIDE handle_SEQmsg with msg_id %d, sender %d, seq %d, proposer %d\n",
//            seqMessage.msg_id, seqMessage.sender, seqMessage.final_seq, seqMessage.final_seq_proposer));
//    print_delivery_queue();
    int rv = change_queued_msg_seq_and_status(seqMessage.sender, seqMessage.msg_id,
                                              seqMessage.final_seq, seqMessage.final_seq_proposer, DELIVERABLE);
    if (rv == -1){  // we didn't find it in the deliveryqueue... it must've been in our deliveredMessage list
        for (QueuedMessage qm : deliveredMessage){
            if (qm.msg_id == seqMessage.msg_id && qm.sender == seqMessage.sender){
                DPRINTF(("handle_seqmsg received duplicate seqmessage for sender %d and msg_id %d with finalsequence %d\n",
                        seqMessage.sender, seqMessage.msg_id, seqMessage.final_seq_proposer));
                return;
            }
        } // so we couldn't find it in the deliveredMessage list also... we throw an error just to be safe
        perror("handle_seqmsg ERROR: COULDN'T LOCATE MESSAGE FOR INCOMING SEQMESSAGE. EXITING...\n");
        exit(1);
    }
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


    // first serialize the data message before multicast
    unsigned char serialized_packet[MAX_STRUCT_SIZE];
    serialize_data_message(dataMessage, serialized_packet);
    int rv;
    for (int i =0; i< num_hosts; i++){
        if (strcmp(hostNames[i], current_container_name) != 0){
            rv = send_msg_with_drop_and_delay(hostNames[i], serialized_packet);
//          rv =  communicator.send_to(hostNames[i], reinterpret_cast<const char *>(serialized_packet), sizeof(serialized_packet));  // -1 to use strlen
            DPRINTF(("*** Multicasted message of type %d with sender_id %d and msg_id %d and data %d to %s\n"
                    , dataMessage.type, dataMessage.sender, dataMessage.msg_id, dataMessage.data, hostNames[i]));
            if (rv == -1){perror("Error sending message. Exiting...\n"); exit(1);}
            if (rv == -22) printf("Message to %s was dropped\n", hostNames[i]);
            
        }
    }
}


int ReliableMulticast::send_msg_with_drop_and_delay(const char *hostname, unsigned char (&serialized_packet)[MAX_STRUCT_SIZE]) {
    // this function also implements any delay and msg drop if applicable
    usleep(delay_in_ms*1000);  // sleep for duration
    if (random_uniform_from_0_to_1() < drop_rate){
        DPRINTF(("[Testing] Message to %s was dropped!\n", hostname));
        return -22;
    }
    int rv =  communicator.send_to(hostname, reinterpret_cast<const char *>(serialized_packet), sizeof(serialized_packet));  // -1 to use strlen
    return rv;
}


void ReliableMulticast::broadcast_seq_msg(const SeqMessage &seqMessage){
    // first pack the message
    unsigned char serialized_packet[MAX_STRUCT_SIZE];
    serialize_seq_message(seqMessage, serialized_packet);
    // then send it to everybody
    for (int i =0; i< num_hosts; i++){
        if (strcmp(hostNames[i], current_container_name) != 0){
            communicator.send_to(hostNames[i], reinterpret_cast<const char *>(serialized_packet), sizeof(serialized_packet));
        }
    }
}


double ReliableMulticast::random_uniform_from_0_to_1() {
//    std::random_device generator;
//    std::uniform_real_distribution<> dis(0.0, 1.0);
//    return dis(generator);
    return (double)rand() / (double)RAND_MAX;
}


void ReliableMulticast::push_msg_to_deliveryqueue(QueuedMessage qm){
    // this guarantees that our deliveryqueue is indeep a minheap w.r.t. the sequence number and then sender_id
   deliveryQueue.push_back(qm);
   std::push_heap(deliveryQueue.begin(), deliveryQueue.end(), cmp);
}


void ReliableMulticast::print_delivery_queue(){
    printf("=== deliveryQueue (size %lu) ====\n", deliveryQueue.size());
    for (const QueuedMessage &qm: deliveryQueue){
        printf("\tseq %d, msg_id %d, sender %d, status %d\n",
               qm.sequence_number, qm.msg_id, qm.sender, qm.status);
    }
    printf("=================================\n");
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



void ReliableMulticast::print_delivered_messages() {
    printf("=== delivered messages so far (size %lu) ====\n", deliveredMessage.size());
    int i = 0;
    for (const QueuedMessage &qm: deliveredMessage){
        printf("\t%d: seq (%d, %d), msg_id & sender (%d, %d)\n", i++,
               qm.sequence_number, qm.proposer, qm.msg_id, qm.sender);
    }
    printf("=================================\n");
}


void ReliableMulticast::deliver_msg_from_deliveryqueue() {
    // we check if the front of the deliveryQueue (assumed it's a heap from the other operations)
    // -- if the front is DELIVERABLE then we deliver it and then pop it from the queue
    // -- we repeat until the front is UNDELIVERABLE
//    DPRINTF(("INSIDE deliver_msg_from_deliveryqueue. Trying to deliver:\n")); print_delivery_queue();
    while((!deliveryQueue.empty()) && deliveryQueue[0].status == DELIVERABLE){  // we found a deliverable msg with the smallest seq number
        QueuedMessage delivered_msg = deliveryQueue[0];
        deliveredMessage.push_back(delivered_msg);  // we deliver it in the queue
        printf("ProcessID %d: Processed message %d from sender %d with seq (%d, %d).\n", current_container_id,
               delivered_msg.msg_id, delivered_msg.sender, delivered_msg.sequence_number, delivered_msg.proposer);
        // then we pop the first element
        std::pop_heap(deliveryQueue.begin(), deliveryQueue.end(), cmp);
        deliveryQueue.pop_back();
    }
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

