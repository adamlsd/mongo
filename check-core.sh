#!/bin/sh
CORE=$1
echo $CORE
BIN=`file $CORE | cut -b 152-158`
echo "Bin: " $BIN

gdb --core $CORE $BIN
