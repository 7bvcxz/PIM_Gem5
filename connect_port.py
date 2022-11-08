import os
import time

for port in range(3456, 3472):
    #print("port : ", port, end="\t")
    os.system('./util/term/m5term '+str(port))
    #print()
    
