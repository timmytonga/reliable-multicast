//
// Created by Thien Nguyen on 10/8/20.
//

#include "reliable_multicast.h"

ReliableMulticast::ReliableMulticast(const char *hostFileName, const udp_client_server::UDP_Server& comm)
        : communicator(comm), deliveryQueue{}, ackHistory(100){
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
        if (numbytes == -1) {perror("recvfrom error..."); exit(1);}
        DPRINTF(("*** Received a message!\n"));
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
    while(true){sleep(10);}
}


void ReliableMulticast::handle_datamsg(const DataMessage &dataMessage){
//    DPRINTF(("*** Inside handle_datamsg: type %d with sender_id %d and msg_id %d and data %d\n"
//            , dataMessage.type, dataMessage.sender, dataMessage.msg_id, dataMessage.data));
    // we need to add the message in the queue (with the latest sequence number + 1) and marking it undeliverable
    QueuedMessage toQueue;
    toQueue.data = dataMessage.data;
    toQueue.msg_id = dataMessage.msg_id;
    toQueue.sender = dataMessage.sender;
    toQueue.proposer = current_container_id;
    toQueue.status = UNDELIVERABLE;
    toQueue.sequence_number = curr_seq_number;
    deliveryQueue.push(toQueue);

    // then we send that latest sequence number as an acknowledgement to the sender of the message (along with our id)
    AckMessage ackMessage;
    ackMessage.proposer = current_container_id;
    ackMessage.msg_id = dataMessage.msg_id;
    ackMessage.sender = dataMessage.sender;
    ackMessage.proposed_seq = curr_seq_number;
    ackMessage.type = ACKMSG_TYPE;
    // packing the message
    unsigned char serialized_packet[MAX_STRUCT_SIZE];
    DPRINTF(("PREPARING TO REPLY ACK: type %d, sender %d, msg_id %d, proposed_seq %d, proposer %d\n",
            ackMessage.type, ackMessage.sender, ackMessage.msg_id, ackMessage.proposed_seq, ackMessage.proposer));
    serialize_ack_message(ackMessage, serialized_packet);
    // send it back to the sender
    curr_seq_number++;
    communicator.reply(reinterpret_cast<const char *>(serialized_packet), sizeof(serialized_packet));
    // then we are supposed to hear back from the sender a final sequence number (which we can then handle elsewhere)
    // so this process is receiving a message from some other process
    // suppose we don't hear back after a while....
    //  --> we should send the ack again (bc the ack might be dropped or the seq might be dropped)
}


void ReliableMulticast::handle_ackmsg(const AckMessage &ackMessage){
    // here we are receiving an AckMessage for some dataMessage that we sent out
    // once we have collected n-1 Acks (we should have a way to notify delayed processes)
    // we pick the max (noting the proposer of the max) and then send out a final sequence to everybody
    DPRINTF(("*** Inside handle_ackmsg: type %d with sender_id %d, msg_id %d, seq %d, and proposer %d\n"
            , ackMessage.type, ackMessage.sender, ackMessage.msg_id, ackMessage.proposed_seq, ackMessage.proposer));
    //  ackHistory[ackMessage.msg_id] contains the history of this msg.
    // if the Ack is for an older msg, we resend the sequence number. Otherwise we handle the new one:
    // if that ack has already been received (for the newest msg), then we ignore it...
    ackHistory[ackMessage.msg_id].insert(std::make_pair(ackMessage.proposer, ackMessage.proposed_seq));

}


void ReliableMulticast::handle_seqmsg(const SeqMessage &seqMessage){
    // here we are receiving the final sequence for some message in our delivery queue
    // note that the first element in our queue is the smallest seq number msg (that is also undeliverable -- otherwise it would've been delivered
    // if the seqmessage's message is not in our delivery queue (it must've been delivered already), then we simply ignore it
    // otherwise we reorder the queue based on the message's new seq number and mark it deliverable
    // then we peek at the top of the queue and pop all deliverable messages until they are no longer deliverable

}


void ReliableMulticast::multicast_datamsg(uint32_t data){
    /* we wish to multicast a message to all other messages with total ordering guarantee
     * we must take note of which message has been sent (probably using msgid) and wait to collect ack after sending out
     * now, we must take into account that our msg is dropped. hence, we spawn a thread (watchdog) per other process that
     * -- after a certain timeout, check to see if an ack for this msg has been received from the proc. it's responsible for
     * -- if not, then we resend the data msg to that process and repeat but only for that process.
     * -- after repeating too many times, we declare that process dead and find a way to gracefully terminate (or notify)
     * */
    DataMessage dataMessage;
    dataMessage.type = DATAMSG_TYPE;
    dataMessage.msg_id = curr_msg_id++;
    dataMessage.data = data;
    dataMessage.sender = current_container_id;

    ProposerSeq ackHistForThisMes;
    ackHistory.push_back(ackHistForThisMes);

    for (int i =0; i< num_hosts; i++){
        if (strcmp(hostNames[i], current_container_name) != 0){
            unsigned char serialized_packet[MAX_STRUCT_SIZE];
            serialize_data_message(dataMessage, serialized_packet);
//            printf("Packed data message: %s. Sending...\n", serialized_packet);
            communicator.send_to(hostNames[i], reinterpret_cast<const char *>(serialized_packet), sizeof(serialized_packet));  // -1 to use strlen
            DPRINTF(("*** Multicasted message of type %d with sender_id %d and msg_id %d and data %d to %s\n"
                    , dataMessage.type, dataMessage.sender, dataMessage.msg_id, dataMessage.data, hostNames[i]));
        }
    }
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

