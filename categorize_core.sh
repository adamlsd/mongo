#!/bin/sh
gdb --core $1 -batch | tail -3 | head -1 | awk '{print $5}' | sed -e 's/,//'
