import os
import time

print("test_target : ", end="")
outdir = str(input())

#print("num_cpu : ", end="")
#num_cpu = str(input())

num_cpu = str(4)
options = "--cpu-clock=4GHz --caches --l2cache --cacheline_size=32 --l1i_size=32kB --l1d_size=32kB --l2_size=256kB --l1d_assoc=8 --l1i_assoc=8 --l2_assoc=4"

start = time.time()
os.system('sudo ./build/X86/gem5.opt --outdir=/home/kkm0411/gem5-22.0.0/m5out/'+outdir+' configs/example/fs.py --disk-image ../gem5-22.0.0/disks/ubuntu-5.4.49.img --kernel ../gem5-22.0.0/disks/vmlinux-5.4.49 --cpu-type=O3CPU --mem-type=PIMsim --mem-size=8GB --ini-path=ext/PIMsim/PIMsim/configs/HBM2_8GB.ini '+options+' -n '+num_cpu+' -r 1 --restore-with-cpu="X86KvmCPU"')
end = time.time()
print("time : {:.2f} sec".format(end-start))
