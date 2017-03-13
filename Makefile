
ACC_FLAGS := -fopenacc

FFTW_DIR := /opt/local
FFTW_INCLUDES := -I$(FFTW_DIR)/include
FFTW_LIBS := -L$(FFTW_DIR)/lib -lfftw3f

CC := gcc

ifeq ($(FFT),FFTW)
	CFLAGS_FFT := -DUSE_FFTW $(FFTW_INCLUDES)
	LIBS_FFT := $(FFTW_LIBS)
else
	CFLAGS_FFT := -DUSE_FOURN
endif

ifeq ($(ACC),nvidia)
	CFLAGS_ACC := $(ACC_FLAGS) -DACC_NVIDIA=1
	LDFLAGS_ACC := $(ACC_FLAGS)
else ifeq ($(ACC),host)
	CFLAGS_ACC := $(ACC_FLAGS) -DACC_HOST=1
	LDFLAGS_ACC := $(ACC_FLAGS)
endif

CFLAGS := -g -O2 -Wall $(CFLAGS_FFT) $(CFLAGS_ACC)
LDFLAGS := $(LDFLAGS_ACC)
LOADLIBES := $(LIBS_FFT)

all: PFDD_Final_GPU

PFDD_Final_GPU: PFDD_Final_GPU.o

clean:
	rm -f PFDD_Final_GPU *.o
