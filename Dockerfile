FROM ubuntu

RUN apt-get update && \
    apt-get install -y g++

ADD ./*.cpp /app/
ADD ./*.h /app/
ADD Hostfile /app/
ADD rprj1.sh /app/

WORKDIR /app/

RUN g++ -pthread networkagent.cpp waittosync.cpp reliable_multicast.cpp main.cpp -o prj1
#RUN g++ -pthread main.cpp -o prj1



# ENTRYPOINT /app/prj1


