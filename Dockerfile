FROM ubuntu

RUN apt-get update && \
    apt-get install -y g++

ADD echo_client.cpp /app/
ADD echo_server.cpp /app/
ADD networkagent.cpp /app/
ADD ./*.h /app/

WORKDIR /app/
RUN g++ echo_client.cpp networkagent.cpp -o echo_client
RUN g++ echo_server.cpp networkagent.cpp -o echo_server


