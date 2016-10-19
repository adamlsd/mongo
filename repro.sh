#!/bin/sh
ulimit -c unlimited && ulimit -a && \rm -rf $HOME/var/data/* && python buildscripts/resmoke.py --suites=concurrency_sharded --basePort=30000 --storageEngine=wiredTiger --excludeWithAnyTags=requires_mmapv1 --dbpathPrefix=$HOME/var/data --repeat=200 --continueOnFailure | tee resmoke.logs/`date +A0%Y%m%d%H%M%S` 2>&1
