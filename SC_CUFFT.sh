pgcc -Mpreprocess -DGPU -DUSE_CUFFT -Minfo=accel -Mcudalib=cufft -ta=host -fast -tp=p7 -lrt PFDD_Final_GPU.c -I/usr/local/include -L/usr/local/lib -lfftw3f -lfftw3  
