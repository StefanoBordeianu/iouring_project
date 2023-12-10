#!/bin/bash
for a in 20 50 100 250 500 1000 1450
do
    printf "s-%s\n" $a >> standardRegisteredServerResults.txt
    printf "s-%s\n" $a >> waitRegisteredServerResults.txt
    for x in 1 10 30 60 100 250 500 1000
    do
        printf "b-%s\n" $x >> standardServerResults.txt
        printf "w-%s\n" $x >> waitServerResults.txt
        for y in {1..10}
        do
            ./standard/r -p 2020 -d 115 -s $a -b $x
            sleep 5
            ./submitAndWait/r -p 2020 -d 115 -s $a -w $x
            sleep 5
        done
    done
    sleep 300;
done
