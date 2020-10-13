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

ENTRYPOINT ["/app/prj1"]
