#!/bin/sh
CORE=$1
echo $CORE
BIN=`file $CORE | awk '{print $17}' | sed -e "s/'//"`
#BIN=mongo
echo "Bin: " $BIN

echo "Checking core: " $CORE " on binary " $BIN
gdb --core $CORE $BIN
echo "You finished checking core: " $CORE " on binary " $BIN
