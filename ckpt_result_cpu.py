comp = input("target computation : ")

n_arr = 'm5out/'+comp+'/stats.txt'

f = open(n_arr, 'r')
lines = f.readlines()

cnt = 0
prev_sec = 0
print("\nSimulation Time")
for line in lines:
    if "simSeconds" in line:
        #print(line, end="")
        item = line.split()
        sec = float(item[1]) - prev_sec
        prev_sec = float(item[1])
        cnt = cnt + 1
        if cnt%4 == 0:
            print(cnt//4, "\t", sec)

cnt = 0
prev_insts = 0
print("\nSimulation Instructions")
for line in lines:
    if "simInsts" in line:
        #print(line, end="")
        item = line.split()
        insts = int(item[1]) - prev_insts
        prev_insts = int(item[1])
        cnt = cnt + 1
        if cnt%4 == 0:
            print(cnt//4, "\t", insts)

cnt = 0
prev_cycles = 0
print("\nSimulation CPU Cycles")
for line in lines:
    if "switch_cpus0.numCycles" in line:
        #print(line, end="")
        item = line.split()
        cycles = int(item[1]) - prev_cycles
        prev_cycles = int(item[1])
        cnt = cnt + 1
        if cnt%4 == 0:
            print(cnt//4, "\t", cycles)

cnt = 0
print("\nMissRatio (l1dcache, l1icache, l2cache)")
for line in lines:
    if "overallMissRate::total" in line:
        #print(line, end="")
        item = line.split()
        hit = float(item[1])
        cnt = cnt + 1
        if cnt%24 == 19:
            print((cnt-1)//24+1, "l1d\t", hit)
        elif cnt%24 == 21:
            print((cnt-1)//24+1, "l1i\t", hit)
        elif cnt%24 == 0:
            print((cnt-1)//24+1, "l2\t", hit)
            print()

cnt = 0
d_prev_misses = 0
d_prev_hits = 0
i_prev_misses = 0
i_prev_hits = 0
l2_prev_misses = 0
l2_prev_hits = 0

print("\nMiss Ratio (l1dcache, l1icache, l2cache)")
for line in lines:
    if "overallHits::total" in line:
        item = line.split()

        if "dcache" in line:
            cnt = cnt + 1
            d_hits = int(item[1]) - d_prev_hits
            d_prev_hits = int(item[1])

        elif "icache" in line:
            i_hits = int(item[1]) - i_prev_hits
            i_prev_hits = int(item[1])

        elif "l2" in line:
            l2_hits = int(item[1]) - l2_prev_hits
            l2_prev_hits = int(item[1])
    
    elif "overallMisses::total" in line:
        item = line.split()

        if "dcache" in line:
            d_misses = int(item[1]) - d_prev_misses
            d_prev_misses = int(item[1])
            if cnt%4 == 0:
               print(cnt//4, "\t", d_misses / (d_misses + d_hits))

        elif "icache" in line:
            i_misses = int(item[1]) - i_prev_misses
            i_prev_misses = int(item[1])
            if cnt%4 == 0: 
                print(cnt//4, "\t", i_misses / (i_misses + i_hits))

        elif "l2" in line:
            l2_misses = int(item[1]) - l2_prev_misses
            l2_prev_misses = int(item[1])
            if cnt%4 == 0:
                print(cnt//4, "\t", l2_misses / (l2_misses + l2_hits))
                print()
