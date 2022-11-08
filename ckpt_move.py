import os

print("target : ", end="")
outdir = str(input())

os.system('sudo mv cpt* '+outdir+'/cpt.1')

