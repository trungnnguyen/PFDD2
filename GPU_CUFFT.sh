set -x
pgcc -Mpreprocess -DACC_NVIDIA=1 -DGPU -mcmodel=medium -Mlarge_arrays -DUSE_CUFFT -Minfo=accel -Mcudalib=cufft -ta=tesla:pinned,fastmath -fast -tp=p7 -lrt PFDD_Final_GPU.c -I/usr/local/include -L/usr/local/lib -lfftw3f -lfftw3  
