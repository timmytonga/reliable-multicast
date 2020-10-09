// prj1 cs7610 by Thien Nguyen
#include <cstdio>
#include <string>
#include <thread>         // std::thread
#include <queue>
#include <functional>

#include "networkagent.h"
#include "waittosync.h"
#include "prj1.h"

#define SERVER_PORT 4646
#define MAX_NUM_HOSTS 16  // max 16 hosts
#define MAX_HOST_NAME 256
#define MAX_STRUCT_SIZE 20  // 20 bytes for 5 uint32_t
#define TIMEOUT_MS 3000 // max time to receive ACK

using namespace udp_client_server;


/* set by user */
int num_hosts = 0;          // read by threads?
long num_msg_tosend = 0;    // read by threads?

const char* hostFileName;   // for initializing parameters and read in host names
const char * current_container_name = nullptr;
int current_container_id;
int curr_msg_id = 1;
char* hostNames[MAX_NUM_HOSTS];
UDP_Server communicator(SERVER_PORT);
auto cmp = [](QueuedMessage left, QueuedMessage right){
    return left.sequence_number > right.sequence_number;
};
std::priority_queue<QueuedMessage, std::vector<QueuedMessage>, decltype(cmp)> q(cmp);


int main(int argc, char* argv[]){
    // first... input will be a Hostfile containing the names of the other hosts.
    // we will read the names of the hosts and create a vector of hostnames
    handle_param(argc, argv);

    // then we read from the files into our array
    int numHosts = wait_to_sync::read_from_file(hostFileName, hostNames);
    num_hosts = numHosts;  // only place. no need for mutex

    // we wait for all the hosts to be ready before sending msgs
    current_container_name = wait_to_sync::waittosync(hostNames, numHosts);
    if (current_container_name == nullptr){
        perror("Obtaining current container's name failed (from wait to sync). Exiting.\n");
        exit(1);
    }
    // that also extracts the id
    current_container_id = extract_int_from_string(std::string(current_container_name));
    printf("Current container's name: %s and id: %d\n", current_container_name, current_container_id);

    // now that we have a list of hosts, we begin sending messages as well as receiving messages
    std::thread receive_thread(msg_receiver);  // receiving thread handles all incoming messages
    // send our messages... we can add delay
    printf("========== Sending %ld messages. ==========\n", num_msg_tosend);
    for (int i = 0; i<num_msg_tosend; i++){
        multicast_datamsg(i*198%27);  // semi arbitrary data
//        sleep(1);
    }
    receive_thread.join();  // never join unless exits...
    for (int i =0; i< numHosts; i++){
        delete [] hostNames[i];
    }
    return 0;
}

[[noreturn]] void msg_receiver(){
    int numbytes;
    unsigned long int type;
    DataMessage dataMessage;
    AckMessage ackMessage;
    SeqMessage seqMessage;
    unsigned char msg_buf[MAX_STRUCT_SIZE];
    while (true){
        printf("Receiving new msg...\n");
        numbytes = communicator.recv(reinterpret_cast<char *>(msg_buf), MAX_MSG_SIZE);
        if (numbytes == -1) {perror("recvfrom error..."); exit(1);}
        printf("Received a message!\n");
        type = unpacku32(&msg_buf[0]);
        printf("Received msg is of type: %lu\n", type);
        switch (type) {
            case 1:
                deserialize_data_message(msg_buf, dataMessage);
                handle_datamsg(dataMessage);
                break;
            case 2:
                deserialize_ack_message(msg_buf, ackMessage);
                handle_ackmsg(ackMessage);
                break;
            case 3:
                deserialize_seq_message(msg_buf, seqMessage);
                handle_seqmsg(seqMessage);
                break;
            default:
                fprintf(stderr, "Received message wrong type: %lu....\n", type);
//                exit(1);
        }
    }
}


void handle_datamsg(const DataMessage &dataMessage){
    // so this process is receiving a message from some other process
    // we need to add the message in the queue (with the latest sequence number + 1) and marking it undeliverable
    // then we send that latest sequence number as an acknowledgement to the sender of the message (along with our id)
    // then we are supposed to hear back from the sender a final sequence number (which we can then handle elsewhere)
    // suppose we don't hear back after a while....
    //  --> we should send the ack again (bc the ack might be dropped or the seq might be dropped)

}

void handle_ackmsg(const AckMessage &ackMessage){
    // here we are receiving an AckMessage for some dataMessage that we sent out
    // if the Ack is for an older msg, we resend the sequence number. Otherwise we handle the new one:
    //  **(we should have a way to keep track of the previously collected Ack Seq number)
    // if that ack has already been received (for the newest msg), then we ignore it...
    // once we have collected n-1 Acks (we should have a way to notify delayed processes)
    // we pick the max (noting the proposer of the max) and then send out a final sequence to everybody

}

void handle_seqmsg(const SeqMessage &seqMessage){
    // here we are receiving the final sequence for some message in our delivery queue
    // note that the first element in our queue is the smallest seq number msg (that is also undeliverable -- otherwise it would've been delivered
    // if the seqmessage's message is not in our delivery queue (it must've been delivered already), then we simply ignore it
    // otherwise we reorder the queue based on the message's new seq number and mark it deliverable
    // then we peek at the top of the queue and pop all deliverable messages until they are no longer deliverable

}

void multicast_datamsg(uint32_t data){
    /* we wish to multicast a message to all other messages with total ordering guarantee
     * we must take note of which message has been sent (probably using msgid) and wait to collect ack after sending out
     * now, we must take into account that our msg is dropped. hence, we spawn a thread (watchdog) per other process that
     * -- after a certain timeout, check to see if an ack for this msg has been received from the proc. it's responsible for
     * -- if not, then we resend the data msg to that process and repeat but only for that process.
     * -- after repeating too many times, we declare that process dead and find a way to gracefully terminate (or notify)
     * */
    DataMessage dataMessage;
    dataMessage.type = 1;
    dataMessage.msg_id = curr_msg_id++;
    dataMessage.data = data;
    dataMessage.sender = current_container_id;

    for (int i =0; i< num_hosts; i++){
        if (strcmp(hostNames[i], current_container_name) != 0){
            unsigned char serialized_packet[MAX_STRUCT_SIZE];
            serialize_data_message(dataMessage, serialized_packet);
//            printf("Packed data message: %s. Sending...\n", serialized_packet);
            communicator.send_to(hostNames[i], reinterpret_cast<const char *>(serialized_packet), sizeof(serialized_packet));  // -1 to use strlen
            DPRINTF(("*** Multicasted message of type %d with sender_id %d to %s\n", dataMessage.type, dataMessage.sender, hostNames[i]));
        }
    }
}


/* utilities */

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
            if (num_msg_tosend <= 0){
                perror("Invalid count argument. Exiting...");
                exit(1);
            }
        }
    }

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


