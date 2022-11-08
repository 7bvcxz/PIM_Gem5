import os

print("target : ", end="")
outdir = str(input())

os.system('sudo mv m5out/cpt* m5out/'+outdir+'/cpt.1')

