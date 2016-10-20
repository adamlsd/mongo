if [ $# -eq 0 ] 
then
    CORES=`ls core.*`
else
    CORES=$*
fi

for core in $CORES
do
    ./check-core.sh $core
done
