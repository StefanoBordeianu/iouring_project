#!/bin/bash
for a in 20 50 100 250 500 1000 1450
do
    printf "s-%s\n" $a >> standard/standardServerResults.txt
    printf "s-%s\n" $a >> kernelPooling/kernelServerResults.txt
    printf "s-%s\n" $a >> submitAndWait/waitServerResults.txt
    for x in 10 30 60 100 250 500 1000
    do
        printf "b-%s\n" $x >> standard/standardServerResults.txt
        printf "b-%s\n" $x >> kernelPooling/kernelServerResults.txt
        printf "w-%s\n" $x >> submitAndWait/waitServerResults.txt
        for y in {1..10}
        do
            ./standard/p -p 2020 -d 120 -s $a -b $x
            ./kernelPooling/p -p 2020 -d 120 -s $a -b $x
            ./submitAndWait/p -p 2020 -d 120 -s $a -w $x
        done
    done
    sleep 300;
done
