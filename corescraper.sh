#!/bin/sh

while true
do
    sleep 1
    if [ -e core ]
    then
        chmod 664 core
        mv core core.`date +A0%Y%m%d%H%M%S`
    fi
done
