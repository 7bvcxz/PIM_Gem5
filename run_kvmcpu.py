import os

#print("num_cpu : ", end="")
#num_cpu = str(input())

num_cpu = str(4)
options = "--cpu-clock=4GHz --caches --l2cache --cacheline_size=32 --l1i_size=32kB --l1d_size=32kB --l2_size=256kB --l1d_assoc=8 --l1i_assoc=8 --l2_assoc=4"

os.system('sudo ./build/X86/gem5.opt configs/example/fs.py --disk-image ../gem5-22.0.0/disks/ubuntu-5.4.49.img --kernel ../gem5-22.0.0/disks/vmlinux-5.4.49 --mem-size=8GB --cpu-type=X86KvmCPU '+options+' -n '+ num_cpu)
