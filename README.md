# Total-order Reliable Multicast and Chandi-Lamport Global Snapshot Systems

## Total-order Reliable Multicast

### Algorithms overview
- We implement the reliable multicast protocol that guarantees the total ordering of the sent messages (i.e. each process will have the same ordering of the delivered messages). This implementation can tolerate message drop/delay (by resending acknowledgement/data message after a set timeout) but does not tolerate process failure/crashing.

- This is done by implementing the total order algorithm which uses sequence numbers (starting from 1 and increments everytime an event happens) to order incoming Data Message. Each process has a delivery queue that holds message along with their sequence number. In this project, each process spawns a receiving thread that performs a specific task depending on what type the message it receives is.

	- If it's a Data Message, it replies with an Acknowledgement Message along with a sequence number that is used for final ordering.
	- If it's an Acknowledgement (ACK) Message, it stores it in a map that keeps track of the received acknowledgement so far. If it receives enough acknowledgements (i.e. from all other processes), it picks the max sequence number and broadcast that to everybody (with a Sequence Message) signifying the final sequence choice for such message.
	- Finally, if the incoming message is a Sequence Message, the process reorders the delivery queue based on this final sequence. Then it delivers as many messages (in front of the queue) with a final sequence number as possible. Messages that do not have a final sequence number yet (arise from said process sending out Data Message but with a smaller sequence number) can block the delivery of messages with sequence numbers already. 

- It can be shown that this numbering scheme of messages (with tie-breaking using proposer id) provides both total-ordering and agreement of the messages' sequence. In which the process of delivering messages through a priority queue guarantees that the delivery is monotonically increasing (w.r.t. the sequence number/sender id). 

### Handling message dropped and delayed
- We use "watchdog" threads with timeout to handle message drops and delays. 
- There are three possible places where messages can be dropped:
	1. The sending of data messages
	2. The sending of ack messages
	3. The sending of seq messages
- For sending Data Messages, associating with each Data Message is a thread keeping track of whether an ACK from a certain process has been received. If an ACK from a certain process p in the group has not been received after a certain timeout, it's either that process p has never received the Data message (case 1) or that the Ack was dropped (or just delayed). In either case, we resend the Data message in which the receiving process can finally receive the data message and respond with an ack, OR resend the old ACK if it's a duplicate Data Message. 

- Now, associating with each ACK is another watchdog process waiting for a corresponding sequence message. If after a certain timeout, the watchdog thread notices that it hasn't seen a corresponding sequence message for such message, it assumes that either the Ack was dropped or the sequence message was dropped. In either case it resends the ACK until it receives a sequence message, where the process receiving a duplicate ACK simply resends the sequence message. 

### Program outline and implementation details
- First each container waits for the other container to connect to the network (so nobody send until every process specified in the Hostfile is "ready" i.e. have sent and received I'm alive messages from all other processes). 
- 


## Running the code

### Naming requirement (important)
- This program assumes that each container name is uniquely identified by the ending numbers. So valid container names can be: ```container1, container2, container3, ...``` but invalid container names are like ``` cat, dog, elephant, ...```

### Compiling and setting up containers
- Below is an example of a Dockerfile to compile. We require g++ and the Pthread flag to compile.
```

FROM ubuntu

RUN apt-get update && \
apt-get install -y g++

ADD ./*.cpp /app/
ADD ./*.h /app/
ADD Hostfile /app/
ADD rprj1.sh /app/
ADD countdelaydroprate.sh /app/

WORKDIR /app/

RUN g++ -pthread networkagent.cpp waittosync.cpp reliable_multicast.cpp main.cpp -o prj1
 ```
 - Then we can run ```docker build . -t prj1``` to compile.
 - Then running ```docker run -it --name <container_name> --network <networkname> prj1 ``` will create an interactive node. This is where we can run each node and observe its output (along with any debug messages). 
 - We can run the program following instructions of the section below.

#### Turning on and off DEBUG messages 
- Debug messages are displayed when the ``` #define DEBUG``` line in ```reliable_multicast.h``` is uncommented. We would need to recompile in order to add debug messages (this includes watchdog information, message drop information, delivery_queue, and any error/recovery messages).

### Adjusting parameters
- Parameters like watchdog-timeout and maximum number of timesouts (until declaring a process has failed) can be changed through tweaking ``` #define``` field in ```reliable_multicast.h```. 
- Note this program spawns ```total message count * number of processes ``` threads total. If this become problematic, one can adjust the ```MAX_NUM_THREADS```  parameter in ``` reliable_multicast.h```.

### Running the program
- The usage is specified as ```./prj1 -h Hostfile -c <count> [ -t <delay_in_ms> -d <droprate>] ``` where ```<count>``` is the number of messages for the running process to multicast to the other processes.
- Hence, by setting count to be either 0 or a positive integer, we can **specify whether a process is a sender/receiver or purely a receiver**. This program supports any arbitrary number of senders at the same time. 
#### Running multiple containers
- This was written to be run interactively on the terminal. So it's best to run each container separately and observe the output separately. For each terminal (say from using Tmux or iTerm) that we spawn, after building, we can run the following to enter the interactive shell:
```   docker run -it --name <container_name> --network <networkname> prj1```
- Then once we are in the interactive shell, we can run each container as detailed in the next section.


### Full run command with simulated message drops and delays
- The full run command is: 
```./prj1 -h Hostfile -c <count> [ -t <delay_in_ms> -d <droprate>] ```.
- Adjusting the message drops and delays can be done by setting the ```-t``` and ```-d``` parameters in the usage.


### Specifying which process to send
- This can be done by setting the count of receiving processes to 0 and count of sending processes to a positive integer. 

## Chandi-Lamport Global Snapshot 
We implement the Chandi-Lamport Global Snapshot algorithm. 

### Updated Dockerfile
- We now update the Dockerfile to include the Global Snapshot
```
FROM ubuntu

RUN apt-get update && \
    apt-get install -y g++

ADD ./*.cpp /app/
ADD ./*.h /app/
ADD Hostfile /app/
ADD rprj1.sh /app/
ADD countdelaydroprate.sh /app/
RUN mkdir playground

WORKDIR /app/


RUN g++ -pthread networkagent.cpp waittosync.cpp CL_global_snapshot.cpp reliable_multicast.cpp main.cpp -o prj1

```

