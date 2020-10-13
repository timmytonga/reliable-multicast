README
 
## Running the test cases
- We provide 2 sample test scripts to test the program: ```testone.sh and testtwo.sh``` where testone.sh has 1 sender set by default and testtwo.sh has 2 senders by default for convenience. However, this program was designed to support any number of senders sending at the same time. All these parameters can be adjusted in the script. 

### Running the script
- One can run any test script as follows: 
```testone.sh [-X <snapshot_time>]``` where snapshot time is the number of data messages already sent for which the snapshot will initiate.
- One can adjust the count, delay, and droprate by adjusting those parameters in the test script itself. 
- The script looks as follows: 
```
# test script for 1 process sending to 4 containers

# please set network name appropriately
NETWORKNAME=mynetwork
HOSTFILENAME=Hostfile
COUNT=10

DELAY1=100       # miliseconds
DROPRATE1=0.3    # must be between 0 and 1

DELAY2=200       # miliseconds
DROPRATE2=0.4    # must be between 0 and 1

DELAY3=300       # miliseconds
DROPRATE3=0.5    # must be between 0 and 1

DELAY4=400       # miliseconds
DROPRATE4=0.2    # must be between 0 and 1


docker build . -t prj1


docker rm -f container1
docker rm -f container2 
docker rm -f container3
docker rm -f container4

rm *.log

docker run -t --name container1 --network $NETWORKNAME prj1 -h $HOSTFILENAME -c $COUNT -t $DELAY1 -d $DROPRATE1 $1 $2 | tee output/container1.log &  # where $1 and $2 are -X <snapshot> params.

docker run -t --name container2 --network $NETWORKNAME prj1 -h $HOSTFILENAME -c 0      -t $DELAY2 -d $DROPRATE2 | tee output/container2.log &

docker run -t --name container3 --network $NETWORKNAME prj1 -h $HOSTFILENAME -c 0      -t $DELAY3 -d $DROPRATE3 | tee output/container3.log &

docker run -t --name container4 --network $NETWORKNAME prj1 -h $HOSTFILENAME -c 0      -t $DELAY4 -d $DROPRATE4 | tee output/container4.log &

```
- There we can modify the count, delay and droprate by changing the parameters. 

### Changing the total number of containers
- This requires more modification as we have to change the Hostfile as well as the test script.
- Say we want to add "container5" to our program. We first add container5 to our hostfile as well as adding:
```docker run -t --name container5 --network $NETWORKNAME prj1 -h $HOSTFILENAME -c 0      -t $DELAY5 -d $DROPRATE5 | tee output/container5.log & ```
along with the appropriate ```DELAY5``` and ```DROPRATE5``` for this process.

### Examining the output
- The output per container will be stored in a log file named ```container<ID>.log``` inside a folder named ```output```. So container1's log will be in container1.log and so on.
- The local snapshot will be output in the log file (where it records stdout) as well as in an optional localsnapshot file named "container1.localsnapshot" and "container2.localsnapshot" and so on. Combining each localsnapshot in one big file will give us a global snapshot.

### A note on channel state in snapshot 
- Since the snapshot algorithm is implemented with TCP (with no delay/dropping), when we run the total-order multicast algorithm with delay and drop-rate, we will not observe (most of the time) any messages in the channel state of the snapshot. The reason for this is due to the snapshot algorithm finishes way before any messages from the reliable multicast algorithm can be sent out (since they have delays and there's no delay in the snapshot algorithm). This can be adjusted to add delays to the snapshot algorithm.
 

 ### Killing the containers
 - A script was provided to kill all containers: ```./killcontainers.sh ``` 


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
RUN mkdir playground

WORKDIR /app/


RUN g++ -pthread networkagent.cpp waittosync.cpp CL_global_snapshot.cpp reliable_multicast.cpp main.cpp -o prj1

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

