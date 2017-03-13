
 pgcc -Mpreprocess -DACC_NVIDIA=1 -DGPU -DUSE_CUFFT -Minfo=accel -Mcudalib=cufft -ta=tesla:pinned,fastmath -fast -tp=p7 -lrt PFDD_Final_GPU.c -I/usr/local/include -L/usr/local/lib -lfftw3f -lfftw3

COMPILER ?= gcc
FFT ?= FOURN
ACC ?= none

FFTW_DIR := /opt/local
FFTW_INCLUDES := -I$(FFTW_DIR)/include
FFTW_LIBS := -L$(FFTW_DIR)/lib -lfftw3f

ifeq ($(COMPILER),pgi)
	CC := pgcc -Mpreprocess
#	CFLAGS_WARN := -Minfo=all
	CFLAGS_OPT := -fast -tp=p7
	ACC_FLAGS_HOST := -Minfo=accel -ta=host -DGPU
	ACC_FLAGS_MULTICORE := -Minfo=accel -ta=multicore -DGPU
	ACC_FLAGS_NVIDIA := -Minfo=accel -ta=tesla:pinned,fastmath -DGPU
else ifeq ($(COMPILER),gcc)
	CC := gcc
	CFLAGS_WARN := -Wall
	CFLAGS_OPT := -g -O2
	ACC_FLAGS_HOST := -fopenacc -DGPU
else
$(error unknown COMPILER $(COMPILER))
endif

ifeq ($(FFT),FFTW)
	CFLAGS_FFT := -DUSE_FFTW $(FFTW_INCLUDES)
	LIBS_FFT := $(FFTW_LIBS)
else ifeq ($(FFT),FOURN)
	CFLAGS_FFT := -DUSE_FOURN
else ifeq ($(FFT),CUFFT)
	CFLAGS_FFT := -DUSE_CUFFT -Minfo=accel -ta=tesla:pinned,fastmath
	LDFLAGS_FFT := -Mcudalib=cufft
else 
$(error unknown FFT $(FFT)) 
endif

ifeq ($(ACC),nvidia)
	CFLAGS_ACC := $(ACC_FLAGS_NVIDIA) -DACC_NVIDIA=1
	LDFLAGS_ACC := $(ACC_FLAGS_NVIDIA)
else ifeq ($(ACC),host)
	CFLAGS_ACC := $(ACC_FLAGS_HOST) -DACC_HOST=1
	LDFLAGS_ACC := $(ACC_FLAGS_HOST)
else ifeq ($(ACC),multicore)
	CFLAGS_ACC := $(ACC_FLAGS_MULTICORE) -DACC_HOST=1
	LDFLAGS_ACC := $(ACC_FLAGS_MULTICORE)
else ifeq ($(ACC),none)
else
$(error unknown ACC $(ACC))
endif

CFLAGS := $(CFLAGS_OPT) $(CFLAGS_WARN) $(CFLAGS_FFT) $(CFLAGS_ACC)
LDFLAGS := $(LDFLAGS_ACC) $(LDFLAGS_FFT)
LOADLIBES := $(LIBS_FFT) -lm

all: PFDD_Final_GPU

PFDD_Final_GPU: PFDD_Final_GPU.o

clean:
	rm -f PFDD_Final_GPU *.o
