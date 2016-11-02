#!/bin/sh

if [ $# -eq 0 ] 
then
    CORES=`ls core.*`
else
    CORES=$*
fi



for core in $CORES
do
    fault=`./categorize_core.sh $core`
    echo $core":" $fault
done
