comp = input("target computation : ")

n_arr = comp + '/stats.txt'

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
        if cnt == 2:
            print("\t", sec)

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
        if cnt == 2:
            print("\t", insts)

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
        if cnt == 2:
            print("\t", cycles)
