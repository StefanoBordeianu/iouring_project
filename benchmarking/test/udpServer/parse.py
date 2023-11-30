
filename = input()


f = open(filename, "r")
res = open("parsed_" + filename, "a")

res.write("         20        50       100      250        500     1000      1450\n")

pkt_acc = 0
rate_acc = 0

for i in range(7) :
    size = f.readline()
    res.write(size + "     ")
    for j in range(7):
        batch  = f.readline()
        for k in range(10):
            pkts = int(f.readline().rstrip('\n'))
            rate = float(f.readline().rstrip('\n'))
            pkt_acc += pkts
            rate_acc += rate
        median_rate = rate_acc / 10
        median_pkts = pkt_acc / 10
        res.write(median_pkts + "     ")
    res.write("\n")
        