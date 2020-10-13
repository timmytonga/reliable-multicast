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

docker run -t --name container3 --network $NETWORKNAME prj1 -h $HOSTFILENAME -c 0      -t $DELAY2 -d $DROPRATE2 | tee output/container3.log &

docker run -t --name container4 --network $NETWORKNAME prj1 -h $HOSTFILENAME -c 0      -t $DELAY2 -d $DROPRATE2 | tee output/container4.log &


