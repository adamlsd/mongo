#!/bin/sh

if [ $# -eq 0 ] 
then
    CORES=`ls core.*`
else
    CORES=$*
fi

for core in $CORES
do
    ./check-core.sh $core
    #echo "Do you want to keep this core? (Press n to delete this core)"
    #read keep
    #if [ $keep = "n" ] ; then rm $core; fi
done

echo "Checked these cores: " $CORES
