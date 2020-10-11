# Total-order Reliable Multicast and Chandi-Lamport Global Snapshot Systems

## Algorithms details

### Total-order Reliable Multicast
- We implement the reliable multicast protocol that guarantees the total ordering of the sent messages (i.e. each process will have the same ordering of the delivered messages). This implementation can tolerate message drop/delay (by resending acknowledgement/data message after a set timeout) but does not tolerate process failure/crashing.

- This is done by implementing the total order algorithm which uses sequence numbers (starting from 1 and increments everytime an event happens) to order incoming Data Message. Each process has a delivery queue that holds message along with their sequence number. In this project, each process spawns a receiving thread that performs a specific task depending on what type the message it receives is.

	- If it's a Data Message, it replies with an Acknowledgement Message along with a sequence number that is used for final ordering.
	- If it's an Acknowledgement Message, it stores it in a map that keeps track of the received acknowledgement so far. If it receives enough acknowledgements (i.e. from all other processes), it picks the max sequence number and broadcast that to everybody (with a Sequence Message) signifying the final sequence choice for such message.
	- Finally, if the incoming message is a Sequence Message, the process reorders the delivery queue based on this final sequence. Then it delivers as many messages (in front of the queue) with a final sequence number as possible. Messages that do not have a final sequence number yet (arise from said process sending out Data Message but with a smaller sequence number) can block the delivery of messages with sequence numbers already. 

### Handling message dropped and delayed
- We use "watchdog" threads with timeout to handle message drops and delays. 

## Running the code

### Turning on and off DEBUG messages 

### Example standard program run

### Adding simulated message drops and delays

### Specifying which process to send

