import os, sys
OBJS=[]
for obj in sys.argv: 
    if obj.endswith('.o'):
        if obj.endswith('main/main.o'):
            continue
        if obj.endswith('tcop/postgres.o'):
    	    continue
        OBJS.append(obj)


print("LINKING: ", " ".join(OBJS))
os.system(f'emar cr ../../libpglite.a {" ".join(OBJS)}')

