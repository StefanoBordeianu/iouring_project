for a in 20 50 100 250 500 1000 1450
do
    printf "s-%s\n" $a >> standardServerResults.txt
    printf "s-%s\n" $a >> socketServerResults.txt
    printf "s-%s\n" $a >> waitServerResults.txt
    for y in {1..10}
    do
        ./udpServer/standard/p -p 2020 -d 115 -s $a -b 1
        sleep 5
        ./socket/server 2020 115
        sleep 5
        ./udpServer/submitAndWait/p -p 2020 -d 115 -s $a -w 1
        sleep 5
    done
    sleep 300
done