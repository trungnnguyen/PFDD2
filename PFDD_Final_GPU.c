/*
 *  xtal.c
 *  energy
 *
 *  Created by marisol on 11/17/08.
 *  Copyright 2008 Purdue. All rights reserved.
 *
 */

//1000 timesteps not converging for CDv 0.2.(esp sigma 33, very large overall) trying larger NT 5000

#include<math.h>
#include<stdio.h>
#include<stdlib.h>
#ifdef _OPENACC
#include <openacc.h>
#endif
#ifdef USE_CUFFT
#include <cufft.h> 
#endif
#ifdef USE_FFTW
#include <fftw3.h>
#endif
#include <assert.h> 

#include <sys/time.h>

static inline double
WTime(void)
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return tv.tv_sec + tv.tv_usec / 1e6;
}

#define sqr(x) ((x) * (x))

#define I5V(i,j,k,m,n) (((((n) * NSV + (m)) * N1 + (i)) * N2 + (j)) * N3 + (k))
#define I5(i,j,k,m,n)  (((((n) * NS  + (m)) * N1 + (i)) * N2 + (j)) * N3 + (k))
#define I4(i,j,k,m)   I5(i,j,k,m,0)
#define I3(i,j,k)     I4(i,j,k,0)

#define C4(data, i,j,k,m, c) (data[2*I4(i, j, k, m) + c])
#define D3(data, i,j,k) (data[I3(i, j, k)])
#define D4(data, i,j,k,m) (data[I4(i,j,k,m)])
#define D5(data, i,j,k,m,n) (data[I5(i,j,k,m,n)])

#define DATABETA(k1,k2,k3,i,j, c)   C4(databeta , k1,k2,k3, i + j*ND, c)
#define DATAEPS(k1,k2,k3,i,j, c)    C4(dataeps  , k1,k2,k3, i + j*ND, c)
#define DATAEPSD(k1,k2,k3,i,j, c)   C4(dataepsd , k1,k2,k3, i + j*ND, c)
#define DATASIGMA(k1,k2,k3,i,j, c)  C4(datasigma, k1,k2,k3, i + j*ND, c)
#define SIGMAV(k1,k2,k3,i,j, c)     C4(sigmav   , k1,k2,k3, i + j*ND, c)

#define FF(k1,k2,k3,ka,i,j)  D4(FF , k1,k2,k3, ka + i*NS  + j*NS*ND)
#define FFv(k1,k2,k3,ka,i,j) D4(FFv, k1,k2,k3, ka + i*NV  + j*NV*ND)
#define DD(k1,k2,k3,ka,i,j)  D4(DD , k1,k2,k3, ka + i*NSV + j*NSV*ND)

#define BB(k1,k2,k3,ka,kb)   D5(BB , k1,k2,k3, ka, kb)
#define GG(k1,k2,k3,ka,kb)   D5(GG , k1,k2,k3, ka, kb)

#define DATA(k1,k2,k3,m, c)  C4(data , k1,k2,k3,m, c)
#define DATA2(k1,k2,k3,m, c) C4(data2, k1,k2,k3,m, c)
#define DATAG(k1,k2,k3,m, c) C4(datag, k1,k2,k3,m, c)
#define XI(k1,k2,k3,m, c)    C4(xi   , k1,k2,k3,m, c)
#define XI_BC(k1,k2,k3,m, c) C4(xi_bc, k1,k2,k3,m, c)

float *
array4d_alloc(int nx, int ny, int nz, int m)
{
  float *arr = calloc(2 * nx * ny * nz * m, sizeof(*arr));
  return arr;
}

float *
array5d_alloc(int nx, int ny, int nz, int m, int n)
{
  float *arr = calloc(2 * nx * ny * nz * m * n, sizeof(*arr));
  return arr;
}

#define MT 1	    //Material type: 1 - isotropic; 2 - cubic
#define NMAT 2     //number of materials, each with defined grain orientation: 1 - homogeneous
#define N1 32      //N1 dimension
#define N2 2       //N2 dimension
#define N3 32      //N3 dimension
#define NS1 1 //4   /*# of slip systems for each material. 12 number of slip systems for FCC */
#define NS (NS1*NMAT) //total number of slip systems for all materials
#define NV 9          /*number of virtual strain systems*/
#define NSV (NS+NV)
#define ND 3         //3-dimensional
#define NTD 0//300//100 //4 //0 doesn't evolve dislocations
#define NT (1+NTD)
#define NP 1//21//201
#define NP2 1
#define pi 3.141592654

// ----------------------------------------------------------------------
// FOURN

void fourn(float *, int nn[4], int, int);  //Fourier Transform

static void
fft_forward_fourn(float *data)
{
  int nf[4] = { 0, N1, N2, N3 };
  fourn(data - 1, nf, 3, -1);
}

static void
fft_backward_fourn(float *data)
{
  int nf[4] = { 0, N1, N2, N3 };
  fourn(data - 1, nf, 3, 1);
}

// ----------------------------------------------------------------------
// FFTW

#ifdef USE_FFTW

static void
fft_forward_fftw(float *data)
{
  static fftwf_plan plan;
  if (!plan) {
    plan = fftwf_plan_dft_3d(N1,
			     N2,
			     N3,
			     (fftwf_complex *) data,
			     (fftwf_complex *) data,
			     FFTW_FORWARD,
			     FFTW_ESTIMATE);
  }

  fftwf_execute_dft(plan, (fftwf_complex *) data,
		    (fftwf_complex *) data);
}

static void
fft_backward_fftw(float *data)
{
  static fftwf_plan plan;
  if (!plan) {
    plan = fftwf_plan_dft_3d(N1,
			     N2,
			     N3,
			     (fftwf_complex *) data,
			     (fftwf_complex *) data,
			     FFTW_BACKWARD,
			     FFTW_ESTIMATE);
  }

  fftwf_execute_dft(plan, (fftwf_complex *) data,
		    (fftwf_complex *) data);
}

#endif

// ----------------------------------------------------------------------
// CUFFT

#ifdef USE_CUFFT

static void
fft_forward_cufft(float *data, int batch)
{
  int stride = 2 * N1 * N2 * N3;
  int i;

  static cufftHandle planc2c;
  if (!planc2c) {
    cufftPlan3d(&planc2c, N1,N2,N3, CUFFT_C2C);
  }

  {
    for (i = 0; i < batch; i++) {
      int rc = cufftExecC2C(planc2c, (cufftComplex *)(data + i * stride),
			    (cufftComplex *)(data + i * stride),
			    CUFFT_FORWARD);
      assert(rc == CUFFT_SUCCESS);
    }
  }
}

static void
fft_backward_cufft(float *data, int batch)
{
  int stride = 2 * N1 * N2 * N3;
  int i;

  static cufftHandle planc2c;
  if (!planc2c) {
    cufftPlan3d(&planc2c, N1,N2,N3, CUFFT_C2C);
  }

  {
    for (i = 0; i < batch; i++) {
      int rc = cufftExecC2C(planc2c, (cufftComplex *)(data + i * stride),
			    (cufftComplex *)(data + i * stride),
			    CUFFT_INVERSE);
      assert(rc == CUFFT_SUCCESS);
    }
  }
}
#endif

// ----------------------------------------------------------------------

static void
fft_forward(float *data, int batch)
{
  int stride = 2 * N1 * N2 * N3;
  int i;

  static double acc_time = 0;
  static int acc_n = 0;
  double tmstart = WTime();
  
#if defined(USE_FOURN)
  for (i = 0; i < batch; i++) {
    fft_forward_fourn(data + i * stride);
  }
#elif defined(USE_FFTW)
  for (i = 0; i < batch; i++) {
    fft_forward_fftw(data + i * stride);
  }
#elif defined(USE_CUFFT)
  fft_forward_cufft(data, batch);
#else
#error I do not know which FFT to use  
#endif

  double now = WTime();
  acc_time += now - tmstart;
  acc_n++;

  if (0 && acc_n % 10 == 0) {
    printf("avg FFT FORWARD time : called %d times %g total time %g\n", acc_n, acc_time / acc_n, acc_time);
  }
}

static void
fft_backward(float *data, int batch)
{
  int stride = 2 * N1 * N2 * N3;
  int i;

  static double acc_time = 0;
  static int acc_n = 0;
  double tmstart = WTime();

#if defined(USE_FOURN)
  for (i = 0; i < batch; i++) {
    fft_backward_fourn(data + i * stride);
  }
#elif defined(USE_FFTW)
  for (i = 0; i < batch; i++) {
    fft_backward_fftw(data + i * stride);
  }
#elif defined(USE_CUFFT)
  fft_backward_cufft(data, batch);
#else
#error I do not know which FFT to use  
#endif

  double now = WTime();
  acc_time += now - tmstart;
  acc_n++;

  if (0 && acc_n % 10 == 0) {
    printf("avg FFT BACKWARD time : called %d times %g total time %g\n", acc_n, acc_time / acc_n, acc_time);
  }
}



long lrint(double);

void CoFactor(double **a,int n,double **b);
void Transpose(double **a,int n);          //Matrix Transpose?
double Determinant(double **a,int n);      //Matrix Determinant?
void v2matsig(double sig[6], double sig2[ND][ND]);
int Indmat2v(int i, int j);
void Indv2mat(int i, int ind[2]);
float minimum(float a[9]);
float maximum(float a[9]);

// ======================================================================
// material

struct material {
  double C11, C12, C44;
};

static void
material_set(struct material *mc)
{
  /* Set material constants for material 0 */
  //mc->C11 = 246.5E9; //Ni
  //mc->C12 = 147.3E9;
  //mc->C44 = 49.6E9;
  mc->C11=168.4E9;//Cu
  mc->C12=121.4E9;
  mc->C44=23.5E9;
    
    
  //mc->C11=124.0E9;//Ag122.E9;//Ag_Low124.E9;//Ag_High
  //mc->C12=93.4E9;//Ag92.E9;//Ag_Low93.4E9;//Ag_High
  //mc->C44=15.3E9;//Ag15.E9;//Ag_Low15.3E9;//Ag_High
  //mc->C11=192.9E9;//Au186.E9;//Au_Low191.E9;//Au_High
  //mc->C12=163.8E9;//Au157.E9;//Au_Low162.E9;//Au_High
  //mc->C44=14.55E9;//Au14.5E9;//Au_Low14.5E9;//Au_High
  //mc->C11=107.3E9;//Al
  //mc->C12=60.9E9;
  //mc->C44=23.2E9;
  //mc->C11 = 346.7E9; //Pt
  //mc->C12 = 250.7E9;
  //mc->C44 = 48.E9;
  //mc->C11 = 227.1E9; //Pd
  //mc->C12 = 176.E9;
  //mc->C44 = 25.55E9;
  //mc->C11 = 580.E9; //Ir
  //mc->C12 = 242.E9;
  //mc->C44 = 169.E9;
	
}

void Bmatrix (double *BB, double *fx, double *fy, double *fz,
	      double eps[NS][ND][ND], double epsv[NV][ND][ND],
	      double d1, double d2,double d3,
	      const struct material *mc, int *xi_o)
{
  printf("set B matrix \n");
#define 	DELTA(i, j)   ((i==j)?1:0)
#define		DELTA4(i,j,k,l) (((i==j) && (j==k) && (k==l))?1:0)
  int i,j,k,l,m,n, u, v, k1,k2,k3,ka,kb;
  double C[ND][ND][ND][ND];
  double kk;
  
  // double mu = C44-(2.0*C44+C12-C11)/5.0;
  // double ll = C12-(2.0*C44+C12-C11)/5.0;
  // double young = mu*(3*ll+2*mu)/(ll+mu);
  // double xnu = young/2.0/mu-1.0;
  
  double mu = mc->C44, mu_inv = 1. / mu;
  double ll = mc->C12;
  double mup = mc->C11 - mc->C12 - 2*mc->C44;
  
  /* set Cijkl*/
  
  for (i=0; i<ND; i++) {
    for (j=0; j<ND; j++) {
      for (k=0; k<ND; k++) {
	for (m=0; m<ND; m++) {
	  C[i][j][k][m] = mu * (DELTA(i,k)*DELTA(j,m)+DELTA(i,m)*DELTA(j,k))+ll*DELTA(i,j)*DELTA(k,m)+mup*DELTA4(i,j,k,m);
	}
      }
    }
  }
  
  /* set A, Green function and B matrix*/
	
  for(k1=0;k1<N1;k1++){
    for(k2=0;k2<N2;k2++){
      for(k3=0;k3<N3;k3++){
	double fk[3] = { D3(fx, k1,k2,k3), D3(fy, k1,k2,k3), D3(fz, k1,k2,k3) };
	double fk2 = fk[0]*fk[0] + fk[1]*fk[1] + fk[2]*fk[2];
	//double fk4 = fk2*fk2;
	for(ka=0;ka<NS;ka++){
	  for(kb=0;kb<NSV;kb++){
	    BB(k1,k2,k3,ka,kb) = 0.;
	  }
	}
	if(fk2>0){
	  double z2[3] = { fk[0]*fk[0]/fk2, fk[1]*fk[1]/fk2, fk[2]*fk[2]/fk2 };
	  kk  = 1.0 + (mu+ll) * (1/(mu+mup*z2[0])*z2[0] + 1/(mu+mup*z2[1])*z2[1] + 1/(mu+mup*z2[2])*z2[2]);
	  for (m=0; m<ND; m++) {
	    for (n=0; n<ND; n++) {
	      for (u=0; u<ND; u++) {
		for (v=0; v<ND; v++) {
		  double A_mnuv = 0.;
		  for (i=0; i<ND; i++) {
		    for (j=0; j<ND; j++) {
		      for (k=0; k<ND; k++) {
			//		G_ki = (2.0 * DELTA(i,k)/fk2-1.0/(1.0-xnu)*fk[i]*fk[k]/fk4)/(2.0*mu);
			double G_ki = 1/fk2 * (DELTA(i,k)/(mu+mup*z2[k]) - fk[i]*fk[k]/fk2/(mu + mup*z2[i])/(mu + mup*z2[k]) * (mu+ll)/kk);
			for (l=0; l<ND; l++) {
			  A_mnuv -= C[k][l][u][v]*C[i][j][m][n]*G_ki*fk[j]*fk[l] ;
			}
		      }
		    }
		  }
		  A_mnuv += C[m][n][u][v];

		  for(ka=0;ka<NS;ka++){
		    for(kb=0;kb<NSV;kb++){
		      if (kb < NS) {
			D5(BB, k1,k2,k3,ka,kb) += A_mnuv*eps[ka][m][n]*eps[kb][u][v] * mu_inv;
		      } else {
			D5(BB, k1,k2,k3,ka,kb) += A_mnuv*eps[ka][m][n]*epsv[kb-NS][u][v] * mu_inv;
		      }
		    }
		  }
		}
	      }
	    }
	  }
	} /*if fk2 */
      }/*k3*/
    }/*k2*/
  }/*k1*/
}

void Fmatrix(double *FF, double *DD, double *FFv, double *fx, double *fy, double *fz,
	     double eps[NS][ND][ND], double epsv[NV][ND][ND], double d1, double d2,double d3,
	     struct material *mc, int *xi_o, double theta1[NMAT][ND][ND])
{
  printf("set F matrix \n");
#define 	DELTA(i, j)   ((i==j)?1:0)
#define		DELTA4(i,j,k,l) (((i==j) && (j==k) && (k==l))?1:0)
  int i,j,k,l,m,n, u, v, k1,k2,k3,ka;
  
  static double C[ND][ND][ND][ND];
  static float F[NS][ND][ND], Fv[NV][ND][ND];
  static float D[NSV][ND][ND];
  static float G[ND][ND];
  double z2[ND];
  double ll, mu, mup, kk;
  static float A[ND][ND][ND][ND];
  static double C_rotate[ND][ND][ND][ND];
  
  //  printf("Here:line 1447\n");

  for (i=0; i<ND; i++) {
    for (j=0; j<ND; j++) {
      for (k=0; k<ND; k++) {
	for (l=0; l<ND; l++) {
	  C_rotate[i][j][k][l] = 0.0;
	}
      }
    }
  }
  
  //  printf("Here:line 1457\n");
  
  
  	
  //	mu = C44-(2.0*C44+C12-C11)/5.0;
  //	ll = C12-(2.0*C44+C12-C11)/5.0;
  //	young = mu*(3*ll+2*mu)/(ll+mu);
  //	xnu = young/2.0/mu-1.0;
  
  mu = mc->C44;
  ll = mc->C12;
  mup= mc->C11 - mc->C12 - 2*mc->C44;
  
  /* set Cijkl*/
  
  for (i=0; i<ND; i++) {
    for (j=0; j<ND; j++) {
      G[i][j]=0.0;
      for (k=0; k<ND; k++) {
	for (m=0; m<ND; m++) {
	  C[i][j][k][m] = mu * (DELTA(i,k)*DELTA(j,m)+DELTA(i,m)*DELTA(j,k))+ll*DELTA(i,j)*DELTA(k,m)+mup*DELTA4(i,j,k,m);
	  A[i][j][k][m] = 0.0;
	  //printf("in Fmatrix check: C[%d][%d][%d][%d]=%e\n",i,j,k,m,C[i][j][k][m]);
	}
      }
    }
  }
  //    printf("Here:line 1484");
  //Rotate C for material 0 due to orientation
  for (i=0; i<ND; i++) {
    for (j=0; j<ND; j++) {
      for (k=0; k<ND; k++) {
	for (l=0; l<ND; l++) {
	  for (m=0; m<ND; m++) {
	    for (n=0; n<ND; n++) {
	      for (u=0; u<ND; u++) {
		for (v=0; v<ND; v++) {
		  C_rotate[i][j][k][l] += C[m][n][u][v]*theta1[0][i][m]*theta1[0][j][n]*theta1[0][k][u]*theta1[0][l][v];
		}
	      }
	    }
	  }
	  if (MT==1) {
	    C_rotate[i][j][k][l] = C[i][j][k][l];
	    //printf("in Fmatrix check: this is an isotropic material. Rotation does not affect C\n");
	  }
	  //printf("in Fmatrix check: C_rotate[%d][%d][%d][%d]=%e\n",i,j,k,l,C_rotate[i][j][k][l]);
	}
      }
    }
  }
    
  // printf("Here:line 1509");	
 
  for(k1=0;k1<N1;k1++) {
    for(k2=0;k2<N2;k2++) {
      for(k3=0;k3<N3;k3++){
	for (ka=0; ka<NSV; ka++) {	//set D, F, Fv to zero
	  for (i=0; i<ND; i++) {
	    for (j=0; j<ND; j++) {
	      if (ka<NS)	{F[ka][i][j]= 0.0;}
	      D[ka][i][j]=0.0;
	      if(ka<NV) {Fv[ka][i][j]=0.0;}			  
	    }
	  }
	}
	double fk[3] = { D3(fx, k1,k2,k3), D3(fy, k1,k2,k3), D3(fz, k1,k2,k3) };
	double fk2 = fk[0]*fk[0] + fk[1]*fk[1] + fk[2]*fk[2];
	//double fk4 = fk2*fk2;
	
	if(fk2>0) {
	  z2[0] = fk[0]*fk[0]/fk2;
	  z2[1] = fk[1]*fk[1]/fk2;
	  z2[2] = fk[2]*fk[2]/fk2;
	  kk  = 1.0 + (mu+ll) * (1/(mu+mup*z2[0])*z2[0] + 1/(mu+mup*z2[1])*z2[1] + 1/(mu+mup*z2[2])*z2[2]);
	  //	  printf("Here:line 1535");
	  for (m=0; m<ND; m++) {
	    for (n=0; n<ND; n++) {
	      for (u=0; u<ND; u++) {
		for (v=0; v<ND; v++) {
		  A[m][n][u][v] = 0.0;
		  
		  for	(i=0; i<ND; i++) {
		    for (j=0; j<ND; j++) {
		      for (k=0; k<ND; k++) {
			//	G[k][i] = (2.0 * DELTA(i,k)/fk2-1.0/(1.0-xnu)*fk[i]*fk[k]/fk4)/(2.0*mu);
			G[k][i] = 1/fk2 * (DELTA(i,k)/(mu+mup*z2[k]) - fk[i]*fk[k]/fk2/(mu + mup*z2[i])/(mu + mup*z2[k]) * (mu+ll)/kk);
			for	(l=0; l<ND; l++) {
			  A[m][n][u][v] = A[m][n][u][v] - C_rotate[k][l][u][v]*C_rotate[i][j][m][n]*G[k][i]*fk[j]*fk[l] ;
			}	
		      }
		    }
		  }
		  A[m][n][u][v] = A[m][n][u][v]+C_rotate[m][n][u][v];
		}
	      }
	    }
	  }
	  //	  printf("Here:line 1558");
	  //				} /*if fk2 */		
	  
	  for (ka=0; ka<NSV; ka++) {
	    //		int NS1 = NS/NMAT;
	    //		kam = (ka)/NS1;
	    //		kas = ka%NS1;
	    for (i=0; i<ND; i++) {
	      for (j=0; j<ND; j++) {		  
		for	(k=0; k<ND; k++) {
		  for (l=0; l<ND; l++) {
		    for (m=0; m<ND; m++) {
		      for (n=0; n<ND; n++) {
			if(ka<NV) {
			  Fv[ka][i][j] = Fv[ka][i][j] + C_rotate[k][l][m][n]*G[j][k]*epsv[ka][m][n]*fk[i]*fk[l] ;
			  /*printf(" in F %d  %d %d  %d %d %d %d %lf %lf \n",ka, i,j,k,l,m,n, G[j][k],F[ka][i][j] );*/
			}
			if (ka<NS) {
			  F[ka][i][j] = F[ka][i][j] + C_rotate[k][l][m][n]*G[j][k]*eps[ka][m][n]*fk[i]*fk[l] ;
			}
		      }
		    }
		    if(ka<NS)   {
		      D[ka][i][j]=D[ka][i][j]+A[i][j][k][l]*eps[ka][k][l];
		    }
		    if(ka>=NS)  {
		      D[ka][i][j]=D[ka][i][j]+A[i][j][k][l]*epsv[ka-NS][k][l];
		    }
		  }
		}
	      }
	    }
	  }
	  
	  //	  printf("Here:line 1592");
	} /*if fk2 */			
	for (ka=0; ka<NSV; ka++) {
	  for (i=0; i<ND; i++) {
	    for (j=0; j<ND; j++) {										
	      if (ka<NS) {
		FF(k1,k2,k3,ka,i,j) = F[ka][i][j];
		/*printf(" in FF %d  %d %d %d %d %d %d %lf \n",ka, nb,k1, k2, k3, i,j,FF[nb]);*/
	      }
	      if (ka<NV) {
		FFv(k1,k2,k3,ka,i,j) = Fv[ka][i][j];
	      }
	      DD(k1,k2,k3,ka,i,j) = D[ka][i][j]/mu;
	    }
	  }
	}
	//	printf("Here:line 1611");
	
      }/*k3*/
    }/*k2*/	
  }/*k1*/
  return;
}

void Gmatrix (double *GG, double beta2, double xb[NS][ND], double xn[NS][ND], double *fx, double *fy, double *fz)
{
  
  int i,j,k,l,m,k1,k2,k3,ka,kb;
  int ep[ND][ND][ND];
  
  for(i=0;i<ND;i++){
    for(j=0;j<ND;j++){
      for(k=0;k<ND;k++){
	ep[i][j][k] = (i-j)*(j-k)*(k-i)/2;
      }
    }
  }
  
  for(k1=0;k1<N1;k1++){
    for(k2=0;k2<N2;k2++){
      for(k3=0;k3<N3;k3++){
	double fk[3] = { D3(fx, k1,k2,k3), D3(fy, k1,k2,k3), D3(fz, k1,k2,k3) };
	
	for(ka=0;ka<NS;ka++){
	  for(kb=0;kb<NS;kb++){
	    D5(GG, k1,k2,k3,ka,kb) = 0.;
	    for (i=0; i<ND; i++){
	      for (j=0; j<ND; j++){
		for (k=0; k<ND; k++){
		  for (l=0; l<ND; l++){
		    for (m=0; m<ND; m++){
		      //GG(k1,k2,k3,ka,kb) += beta2*fk[j]*fk[l]*(xb[ka][i]*xb[kb][k]+xb[ka][k]*xb[kb][i]);
		      GG(k1,k2,k3,ka,kb) += 0.5*beta2*(ep[i][j][k]*ep[i][l][m]*xn[ka][j]*xn[kb][l] +ep[i][j][k]*ep[i][l][m]*xn[kb][j]*xn[ka][l])*fk[k]*fk[m];
		    }
		  }
		}
	      }
	    }
	  }
	}
      }/*k3*/
    }/*k2*/	
  }/*k1*/	
}

float avestrain(double avepsd[ND][ND], double avepst[N1][N2][N3][ND][ND], double eps[NS][ND][ND], double epsv[NV][ND][ND], double *xi, int nsize, double sigma[N1][N2][N3][ND][ND], double S11, double S12, double S44, double mu,float *energy_in4, float *strain_average,int border,double interface_n[ND], int ppoint[3])
{
  
  int i,j,k,l,is,k1,k2,k3,u,v;
  double S[ND][ND][ND][ND];
  float energy_stressTimePf;
  double strain_p[ND][ND],strain_v[ND][ND];
  energy_stressTimePf = 0.0;
#define 	DELTA(i, j)   ((i==j)?1:0)
#define		DELTA4(i,j,k,l) (((i==j) && (j==k) && (k==l))?1:0)
  
  /*set S matrix*/	
  for (i=0; i<ND; i++) {
    for (j=0; j<ND; j++) {
      for (k=0; k<ND; k++) {
	for (l=0; l<ND; l++) {
	  //S[i][j][k][l] = S44/4.0 * (DELTA(i,k)*DELTA(j,l)+DELTA(i,l)*DELTA(j,k))+S12*DELTA(i,j)*DELTA(k,l);
	  //mark change S? Have checked. Both works for Isotropic
	  S[i][j][k][l] = (S11+2*S12)/3.0 * DELTA(i,j)*DELTA(k,l) + S44/4.0*(DELTA(i,k)*DELTA(j,l)+DELTA(i,l)*DELTA(j,k)) - S44/2.0*DELTA4(i,j,k,l) + (S11-S12)*(DELTA4(i,j,k,l) - DELTA(i,j)*DELTA(k,l)/3.0);
	}
      }
    }
  }
  
  /*calculating average stress free strain, avepsd*/
  for(is=0;is<NSV;is++){		    
    for (i=0; i<ND; i++) {
      for (j=0; j<ND; j++) {
	for(k1=0;k1<N1;k1++){
	  for(k2=0;k2<N2;k2++){
	    for(k3=0;k3<N3;k3++){
	      if (is<NS) {
		avepsd[i][j] += eps[is][i][j]     * XI(k1,k2,k3, is, 0)/nsize;
	      }	else {
		avepsd[i][j] += epsv[is-NS][i][j] * XI(k1,k2,k3, is, 0)/nsize;
	      }
	    }
	  }
	}
      }
    }
  }
  
  // mark energy total field strain integration part
  //mark local stress sigma will change energy formalism, some term should be intergrated over the whole domain
  for (k1=0; k1<N1; k1++) {
    for (k2=0; k2<N2; k2++) {
      for (k3=0; k3<N3; k3++) {
	
	// mark
	// rewrite energy_stress*pf's pf strain
	// rewrite energy_stress*virtual's virtual strain
	for (u=0; u<ND; u++) {
	  for (v=0; v<ND; v++) {
	    strain_p[u][v] = 0.0;
	    strain_v[u][v] = 0.0;
	    for (is=0; is<NS; is++) {
	      strain_p[u][v] += XI(k1,k2,k3, is, 0)*eps[is][u][v];
	    }
	    strain_v[u][v] = epsv[v+u*ND][u][v]*XI(k1,k2,k3, NS+v+u*ND, 0);
	  }
	}
        
        
	for (i=0; i<ND; i++) {
	  for (j=0; j<ND; j++) {
	    if (fabs(interface_n[0]*(k1-ppoint[0]) + interface_n[1]*(k2-ppoint[1]) + interface_n[2]*(k3-ppoint[2]))<=1.0&&(k1==ppoint[0])) {
	      //energy_stressTimePf += -avepsd[i][j]*sigma[k1][k2][k3][i][j];
	      energy_stressTimePf += -(strain_p[i][j]+strain_v[i][j])*(sigma[k1][k2][k3][i][j]);
	    }
	    for (k=0; k<ND; k++) {
	      for (l=0; l<ND; l++) {
		//mark energy from external applied stress
		if (fabs(interface_n[0]*(k1-ppoint[0]) + interface_n[1]*(k2-ppoint[1]) + interface_n[2]*(k3-ppoint[2]))<=1.0&&(k1==ppoint[0])) {
		  *(energy_in4) += -S[i][j][k][l]*sigma[k1][k2][k3][k][l]*sigma[k1][k2][k3][i][j]*0.5*mu;
		}
	      }
	    }
	  }
	}
      }
    }
  }
  
    
  /*calculating microscopic strain, avepst*/
  
  for (k1=0; k1<N1; k1++) {
    for (k2=0; k2<N2; k2++) {
      for (k3=0; k3<N3; k3++) {
	for (i=0; i<ND; i++) {
	  for (j=0; j<ND; j++) {
	    for (k=0; k<ND; k++) {
	      for (l=0; l<ND; l++) {
		avepst[k1][k2][k3][i][j] += S[i][j][k][l]*sigma[k1][k2][k3][k][l]*mu;
	      }
	    }
	    avepst[k1][k2][k3][i][j] += avepsd[i][j];
	    *(strain_average) += avepst[k1][k2][k3][i][j];
	  }
	}
      }
    }
  }
  *(strain_average) = *(strain_average)/nsize;
  
  return energy_stressTimePf;
  
}


void strain(float *databeta, float *dataeps, float *data, double *FF, double *FFv, double epsv[NV][ND][ND], double d1, double d2, double d3, double size3, FILE *of3,int it, int itp, double avepst[N1][N2][N3][ND][ND])
{
  int i,j,is,k1,k2,k3;
  
  for (i=0; i < 2*N1*N2*N3*ND*ND; i++){
    databeta[i] = 0.;
    dataeps[i]  = 0.;
  }
  
  /*calculate the total strain */
  for(is=0;is<NSV;is++){
    for (i=0; i<ND; i++) {
      for (j=0; j<ND; j++) {
	for(k1=0;k1<N1;k1++){
	  for(k2=0;k2<N2;k2++){
	    for(k3=0;k3<N3;k3++){
	      if(is<NS){
		DATABETA(k1,k2,k3,i,j, 0) += DATA(k1,k2,k3, is, 0) * D4(FF, k1,k2,k3, is + i*NS + j*NS*ND);
		DATABETA(k1,k2,k3,i,j, 1) += DATA(k1,k2,k3, is, 1) * D4(FF, k1,k2,k3, is + i*NS + j*NS*ND);
	      } else {
		DATABETA(k1,k2,k3,i,j, 0) += DATA(k1,k2,k3, is, 0) * D4(FFv, k1,k2,k3, is-NS + i*NV + j*NV*ND);
		DATABETA(k1,k2,k3,i,j, 1) += DATA(k1,k2,k3, is, 1) * D4(FFv, k1,k2,k3, is-NS + i*NV + j*NV*ND);
	      }
	    }
	  }
	}
      }
    }
  }
    
#ifdef USE_CUFFT
#pragma acc data copy(databeta[0:2*N1*N2*N3*ND*ND])
#pragma acc host_data use_device(databeta)
#endif
  fft_backward(databeta, ND*ND);	/* inverse FFT for strain*/
    
  for (i = 0; i < 2*N1*N2*N3*ND*ND; i++){
    databeta[i] = databeta[i] / (N1*N2*N3);
  }

  //add in other two terms in strain
  for(i=0;i<ND;i++){
    for (j=0;j<ND;j++){
      for(k1=0;k1<N1;k1++){
	for(k2=0;k2<N2;k2++){
	  for (k3=0;k3<N3;k3++){	    
	    DATABETA(k1,k2,k3,i,j, 0) += avepst[k1][k2][k3][i][j];
	  }
	}
      }
    }
  }
    
  for(i=0;i<ND;i++){
    for (j=0;j<ND;j++){
      for(k1=0;k1<N1;k1++){
	for(k2=0;k2<N2;k2++){
	  for (k3=0;k3<N3;k3++){
	    DATAEPS(k1,k2,k3,i,j, 0) = (DATABETA(k1,k2,k3,i,j, 0) + DATABETA(k1,k2,k3,j,i, 0)) / 2.;
	    DATAEPS(k1,k2,k3,i,j, 1) = (DATABETA(k1,k2,k3,i,j, 1) + DATABETA(k1,k2,k3,j,i, 1)) / 2.;
	  }
	}
      }
    }
  }
	
  if(it == NT-NTD-1){
    printf("printing strains %d %d\n",it, itp);
    //	fprintf(of3,"zone   I = %d J = %d K = %d \n", N1, N2,N3);
    fprintf(of3,"x,y,z,eps11,eps13,eps21,eps22,eps23,eps31,eps32,eps33\n");
    for(k1=0;k1<N1;k1++){
      for(k2=0;k2<N2;k2++){
	for(k3=0;k3<N3;k3++){
	  fprintf(of3,"%d,%d,%d,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf \n",k1,k2,k3,
		  DATAEPS(k1,k2,k3,0,0, 0), DATAEPS(k1,k2,k3,0,1, 0), DATAEPS(k1,k2,k3,0,2, 0),
		  DATAEPS(k1,k2,k3,1,0, 0), DATAEPS(k1,k2,k3,1,1, 0), DATAEPS(k1,k2,k3,1,2, 0),
		  DATAEPS(k1,k2,k3,2,0, 0), DATAEPS(k1,k2,k3,2,1, 0), DATAEPS(k1,k2,k3,2,2, 0));
	}
      }
    }

  }
  return;
}

void stress(float *dataepsd, float *datasigma, float *dataeps, float * sigmav,
	    double *xi, double eps[NS][ND][ND], double epsv[NV][ND][ND],
	    double C11, double C12, double C44, FILE *of5, int it, int itp,
	    double avesigma[ND][ND],double theta1[NMAT][ND][ND], double slipdirection[ND],
	    double xn[NS][ND], double *penetrationstress, double * penetrationstress2,
	    int t_bwvirtualpf, int border, int ppoint_x, int ppoint_y, int ppoint_z)
{
#define 	DELTA(i, j)   ((i==j)?1:0)
#define		DELTA4(i,j,k,l) (((i==j) && (j==k) && (k==l))?1:0)
  int i,j,k,l,m,k1,k2,k3,y;
  int is;
  double C[ND][ND][ND][ND];
  double mu, mup, ll;
  int  is_p0; // these should be the same as those in interfacevolv!!
  double resolve[ND][ND];
    

  is_p0 = 0;
  for (i=0; i<ND; i++) {
    for (j=0; j<ND; j++) {
      resolve[i][j] = 0.0;
      for (k=0; k<ND; k++) {
	for (l=0; l<ND; l++) {
	  resolve[i][j] += xn[is_p0][l]*slipdirection[k]*theta1[0][i][k]*theta1[0][j][l];
	}
      }
    }
  }
  /* set Cijkl*/
  
  
  	
  //	mu = C44-(2.0*C44+C12-C11)/5.0;
  //	ll = C12-(2.0*C44+C12-C11)/5.0;
  //	young = mu*(3*ll+2*mu)/(ll+mu);
  //	xnu = young/2.0/mu-1.0;

  mu = C44;
  ll = C12;
  mup= C11-C12-2*C44;

  
  for (i=0; i<ND; i++) {
    for (j=0; j<ND; j++) {
      for (k=0; k<ND; k++) {
	for (m=0; m<ND; m++) {
	  C[i][j][k][m] = mu * (DELTA(i,k)*DELTA(j,m)+DELTA(i,m)*DELTA(j,k))+ll*DELTA(i,j)*DELTA(k,m)+mup*DELTA4(i,j,k,m);
	}
      }
    }
  }
  for (i = 0; i < 2*N1*N2*N3*ND*ND; i++){
    datasigma[i] = 0.;
    dataepsd[i] = 0.;
  }
    
  for(is=0;is<NSV;is++){
    for (i=0; i<ND; i++) {
      for (j=0; j<ND; j++) {
	for(k1=0;k1<N1;k1++){
	  for(k2=0;k2<N2;k2++){
	    for(k3=0;k3<N3;k3++){
	      if (is < NS) {
		DATAEPSD(k1,k2,k3,i,j, 0) += eps[is][i][j]     * XI(k1,k2,k3, is, 0);
	      } else {
		DATAEPSD(k1,k2,k3,i,j, 0) += epsv[is-NS][i][j] * XI(k1,k2,k3, is, 0);
	      }
	    }
	  }
	}
      }
    }
  }
  for (i=0;i<ND;i++){
    for (j=0;j<ND;j++){
      for(k1=0;k1<N1;k1++){
	for(k2=0;k2<N2;k2++){
	  for(k3=0;k3<N3;k3++){
	    for (k=0;k<ND;k++){
	      for (l=0;l<ND;l++){
		DATASIGMA(k1,k2,k3,i,j, 0) += C[i][j][k][l]*(DATAEPS(k1,k2,k3,k,l, 0) - DATAEPSD(k1,k2,k3,k,l, 0));
	      }
	    }
	    avesigma[i][j] += DATASIGMA(k1,k2,k3,i,j, 0)/(N1*N2*N3);
	  }
	}
      }
    }
  }

  if (it == NT-1 && itp == NP-1) {
    printf("printing stress %d %d\n",it, itp);
    //    fprintf(of5,"zone   I = %d K = %d \n", N3,N1);
    fprintf(of5,"x,y,z,sigma11,sigma12,sigma13,sigma22,sigma23,sigma33,datasigma11,datasigma12,datasigma13,datasigma22,datasigma23,datasigma33 \n");
    for(k1=0;k1<N1;k1++){
      for(k2=0;k2<N2;k2++){
	for(k3=0;k3<N3;k3++){
	  if (k2==0) {
	    fprintf(of5,"%d,%d,%d,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf\n",
		    k1,k2,k3,
		    SIGMAV(k1,k2,k3,0,0, 0), SIGMAV(k1,k2,k3,0,1, 0), SIGMAV(k1,k2,k3,0,2, 0),
		    SIGMAV(k1,k2,k3,1,1, 0), SIGMAV(k1,k2,k3,1,2, 0), SIGMAV(k1,k2,k3,2,2, 0),
		    DATASIGMA(k1,k2,k3,0,0, 0), DATASIGMA(k1,k2,k3,0,1, 0), DATASIGMA(k1,k2,k3,0,2, 0), 
		    DATASIGMA(k1,k2,k3,1,1, 0), DATASIGMA(k1,k2,k3,1,2, 0), DATASIGMA(k1,k2,k3,2,2, 0));
	  }
	}
      }
    }
  }
    
  if ((it!=0)&&(it%t_bwvirtualpf==0)&&(it!=NT-1)) {
    for (y=0; y<N2; y++) {//only valid for N2 = 2
      penetrationstress[y] = 0.0;
      penetrationstress2[y] = 0.0;
      for (i=0; i<ND; i++) {
	for (j=0; j<ND; j++) {
	  penetrationstress[y] += SIGMAV(ppoint_x,y,ppoint_z,i,j, 0)*resolve[i][j];//mark: resolve shear stress in stress and calculateD4interface
	}
      }
      penetrationstress[y]  = SIGMAV(ppoint_x,y,ppoint_z,0,1, 0);// for edge, b=z=slip direction, m=x, l=y, resolve shear stress should be sigma_13
      penetrationstress2[y] = SIGMAV(ppoint_x,y,ppoint_z,0,1, 0);
            
      //mark: for ppointx-1, penetrationstress = 0.7*(ppointz-1) + 0.3*(ppointz), penetrationstress2 = ppoint_z+2
      //mark: for ppointx-2, penetrationstress = 0.9*(ppointz-1) + 0.1*(ppointz)
            
      printf("Calculated from resolved process penetrationstress = %lf and penetrationstress2 = %lf at x = %d, y = %d, z = %d\n",penetrationstress[y],penetrationstress2[y],ppoint_x,y,ppoint_z);
      printf("check neighbor: ppointz+2 = %lf and ppoint+3 = %lf and ppoint-1 = %lf\n",
	     SIGMAV(ppoint_x,y,ppoint_z+2,0,1, 0),
	     SIGMAV(ppoint_x,y,ppoint_z+3,0,1, 0),
	     SIGMAV(ppoint_x,y,ppoint_z-1,0,1, 0));
    }
        
  }
    
  return;
}

void in_virtual_void(int Rv, float * data, double * xi, double * xi_bc, int *xi_o)
{
  int ir, nao, is, i, j, k;
  
  Rv=(N1/6)*(N1/6);
  for(is=0;is<NSV;is++)  {
    for(i=0;i<N1;i++)	{	
      for(j=0;j<N2;j++) {
	for(k=0;k<N3;k++)	{				
	  nao = I3(i, j, k);
	  if (is>=NS){		  
	    XI(i,j,k,is, 0) = 0.0;
	    XI(i,j,k,is, 1) = 0.0;
	    XI_BC( i,j,k,is, 0) = 1.0;
	    XI_BC( i,j,k,is, 1) = 1.0;
	    
	    ir = (i-N1/2)*(i-N1/2)+(j-N2/2)*(j-N2/2)+(k-N3/2)*(k-N3/2);
	    if(ir<=Rv){
	      XI(i,j,k,is, 0) = 0.005;
	      XI(i,j,k,is, 1) = 0.0;
	      XI_BC( i,j,k,is, 0) = 0.0;
	      XI_BC( i,j,k,is, 1) = 0.0;
	      if(is == NS) xi_o[nao] = -1;
	    }								 
	    
	    DATA(i,j,k, is, 0) = XI(i,j,k,is, 0);
	    DATA(i,j,k, is, 1) = XI(i,j,k,is, 1);
	  }
	  if (is<NS){		  
	    ir = (i-N1/2)*(i-N1/2)+(j-N2/2)*(j-N2/2)+(k-N3/2)*(k-N3/2);
	    if(ir<=Rv){
	      XI(i,j,k,is, 0) = 0.0;
	      XI(i,j,k,is, 1) = 0.0;
	      XI_BC( i,j,k,is, 0) = 1.0;
	      XI_BC( i,j,k,is, 1) = 1.0;
	      DATA(i,j,k, is, 0) = XI(i,j,k,is, 0);
	      DATA(i,j,k, is, 1) = XI(i,j,k,is, 1);
	    }								 
	    
	  }
	}
      }
    }
  }
}

void in_virtual_cylinder(float *data,double *xi,double *xi_bc, int * xi_o)
{
  
  int nao, is, i, j, k;
  double ir, R;
  
  R = (double)(N1/4.0*N1/4.0);
  
  for(is=0;is<NSV;is++) {
    for(i=0;i<N1;i++)  	{	
      for(j=0;j<N2;j++)	{
	for(k=0;k<N3;k++)  {				
	  nao = I3(i, j, k);
	  ir = (double)((i-N1/2)*(i-N1/2)+(j-N2/2)*(j-N2/2));
	  if (is>=NS){		  
	    XI(i,j,k,is, 0) = 0.0;
	    XI(i,j,k,is, 1) = 0.0;
	    XI_BC( i,j,k,is, 0) = 1.0;
	    XI_BC( i,j,k,is, 1) = 1.0;
	    if(ir >= R){
	      XI(i,j,k,is, 0) = 0.005;
	      XI(i,j,k,is, 1) = 0.0;
	      XI_BC( i,j,k,is, 0) = 0.0;
	      XI_BC( i,j,k,is, 1) = 0.0;
	      if(is == NS) xi_o[nao] = -1;
	    }
	    DATA(i,j,k, is, 0) = XI(i,j,k,is, 0);
	    DATA(i,j,k, is, 1) = XI(i,j,k,is, 1);
	  }
	  if(is<NS){
	    if(ir >= R){
	      XI_BC( i,j,k,is, 0) = 1.0;
	      XI_BC( i,j,k,is, 1) = 1.0;
	      XI(i,j,k,is, 0) = 0.;
	      DATA(i,j,k, is, 0) = XI(i,j,k,is, 0);
	    }
	  }
	}
      }
    }
  }
}

void in_virtual_flat(float *data,double *xi,double *xi_bc, int * xi_o)
{
  
  int na0, na1,nad1,nad2, nao, is, i, j, k;
  
  //double R = (double)(N1/4.0*N1/4.0);
  
  for(is=0;is<NSV;is++) {
    for(i=0;i<N1;i++)	{	
      for(j=0;j<N2;j++)	{
	for(k=0;k<N3;k++)	{				
	  na0 = 2*I4(i, j, k, is);
	  //int na = 2*I3(i, j, k);
	  nao = I3(i, j, k);
	  na1 = na0+1;
	  nad1 = na0+1;
	  nad2 = na0+2;
	  //double ir = (double)((i-N1/2)*(i-N1/2)+(j-N2/2)*(j-N2/2));
	  if (is>=NS){		  
	    xi[na0] = 0.0;
	    xi[na1] = 0.0;
	    xi_bc[na0] = 1.0;
	    xi_bc[na1] = 1.0;
	    if(i >= N1*3/4){
	      xi[na0] = 0.0;
	      xi[na1] = 0.0;
	      xi_bc[na0] = 0.0;
	      xi_bc[na1] = 0.0;
	      if(is == NS) xi_o[nao] = -1;
	    }
	    data[nad1] = xi[na0];
	    data[nad2] = xi[na1];
	  }
	  if(is<NS){
	    if(i >= N1*3/4){
	      xi_bc[na0] = 1.0;
	      xi_bc[na1] = 1.0;
	      xi[na0] = 0.0;
	      data[nad1] = xi[na0];
	    }
	  }
	}
      }
    }
  }
  return;
}

void in_virtual_cylinvoid(int Rv, float * data, double * xi, double * xi_bc, int * xi_o)
{
  
  int ir, na0, na1,nad1,nad2, nao, is, i, j, k;
  
  Rv=10*10;//(N1/6)*(N1/6);
  for(is=0;is<NSV;is++)  {     
    for(i=0;i<N1;i++)	{	
      for(j=0;j<N2;j++){
	for(k=0;k<N3;k++)	{				
	  na0 = 2*I4(i, j, k, is);
	  //int na = 2*I3(i, j, k);
	  nao = I3(i, j, k);
	  na1 = na0+1;
	  nad1 = na0+1;
	  nad2 = na0+2;
	  if (is>=NS){		  
	    xi[na0] = 0.0;
	    xi[na1] = 0.0;
	    xi_bc[na0] = 1.0;
	    xi_bc[na1] = 1.0;
	    
	    ir = (i-N1/2)*(i-N1/2)+(k-N3/2)*(k-N3/2);
	    if(ir<=Rv){
	      xi[na0]=0.00;
	      xi[na1] = 0.0;
	      xi_bc[na0]= 0.0;
	      xi_bc[na1] = 0.0;
	      if(is == NS) xi_o[nao] = -1;
	    }								 
	    
	    data[nad1] = xi[na0];
	    data[nad2] = xi[na1];
	  }
	  if (is<NS){		  
	    ir = (i-N1/2)*(i-N1/2)+(k-N3/2)*(k-N3/2);
	    if(ir<=Rv){
	      xi[na0]=0.0;
	      xi[na1] = 0.0;
	      xi_bc[na0]= 1.0;
	      xi_bc[na1] = 1.0;
	      data[nad1] = xi[na0];
	      data[nad2] = xi[na1];
	    }								 
	    
	  }
	}
      }
    }
  }
  
  return;
}

void in_virtual_epitax(int Rv, float * data, double * xi, double * xi_bc, int * xi_o)
{
  
  int na0, na1,nad1,nad2, nao, is, i, j, k;
  double h;
  
  h = N2/2;//N2/2;//N3/2;//N3-8;//N2/2;
  //double R = (double)(N1/4.0*N1/4.0);
  
  for(is=0;is<NSV;is++) {
    for(i=0;i<N1;i++) {	
      for(j=0;j<N2;j++)	{
	for(k=0;k<N3;k++){				
	  na0 = 2*I4(i, j, k, is);
	  //int na = 2*I3(i, j, k);
	  nao = k+j*N3+i*N3*N2;
	  na1 = na0+1;
	  nad1 = na0+1;
	  nad2 = na0+2;
	  //	ir = (double)((i-N1/2)*(i-N1/2)+(j-N2/2)*(j-N2/2));
	  if (is>=NS){		  
	    xi[na0] = 0.0;
	    xi[na1] = 0.0;
	    xi_bc[na0] = 1.0;
	    xi_bc[na1] = 1.0;
	    //		if(j ==h && i>10 && i<N1-10){
	    if(j >= h-10 && j<= h+10){
	      //if(is==NS+2 || is==NS+6){
	      xi[na0] = 0.005*k;
	      xi[na1] = 0.0;
	      //}
	      xi_bc[na0] = 0.0;
	      xi_bc[na1] = 0.0;
	      if(is == NS) xi_o[nao] = -1;
	    }
	    data[nad1] = xi[na0];
	    data[nad2] = xi[na1];
	  }
	  if(is<NS){
	    //		if(j ==h && i>10 && i<N1-10){
	    if(j >= h-10 && j<= h+10){
	      xi_bc[na0] = 1.0;
	      xi_bc[na1] = 1.0;
	      xi[na0] = 0.0;
	      data[nad1] = xi[na0];
	    }
	  }
	}
      }
    }
  }
  return;
}

void in_virtual_homo(float * data, double * xi, double * xi_bc)
{
  
  int na0, na1,nad1,nad2, is, i, j, k;
  
  
  for(is=0;is<NSV;is++)  {
    for(i=0;i<N1;i++) {	
      for(j=0;j<N2;j++)	{
	for(k=0;k<N3;k++) {				
	  na0 = 2*I4(i, j, k, is);
	  na1 = na0+1;
	  nad1 = na0+1;
	  nad2 = na0+2;
	  if (is>=NS){		  
	    xi[na0] = 0.0;
	    xi[na1] = 0.0;
	    xi_bc[na0] = 1.0;
	    xi_bc[na1] = 1.0;
	    data[nad1] = xi[na0];
	    data[nad2] = xi[na1];
	  }
	}
      }
    }
  }
  
  return;
}

void initial(float *data, double * xi, double * xi_bc, double setobs, int * xi_o,int border,int ppoint_x, int ppoint_y, int ppoint_z){
  
  int nao, is, ism, iss, i, j, k;
    
  //double rmin1 = (double)N1/4.0-3.0;//(double)N1/6.0;
  //double rmin2 = (double)N1/6.0;
  //double rcylin=(double)N1/4.0;
  
  //double t1 = 45./180.*pi;
  // int NS1 = NS/NMAT;
  //mark
  //double yita = 2./2./(1-0.34); //for edge
  //   yita = 0.5 * 2.; //for screw *2 ~~d = 2b
  
  FILE *Order_Param1;
  Order_Param1 = fopen("Order_Parameter1.txt","w");
  fprintf(Order_Param1,"x,y,z,Order_Parameter\n");
  FILE *Order_Param2;
  Order_Param2 = fopen("Order_Parameter2.txt","w");
  fprintf(Order_Param2,"x,y,z,Order_Parameter\n");
#ifdef GPUU  
#pragma acc data copy(data[0:2*(NSV)*(N1)*(N2)*(N3)],xi[0:2*(NSV)*(N1)*(N2)*(N3)-1],xi_bc[0:2*(NSV)*(N1)*(N2)*(N3)-1],data_CUFFT[0:(NSV)*(N1)*(N2)*(N3)][0:1]) copyin(xi_o[0:(N1)*(N2)*(N3)])  
#endif 

#ifdef GPUU 
#pragma acc kernels
#endif  
  {  
    for(is=0;is<NSV;is++)  {
#ifndef _OPENACC
      printf("initial data for system %d\n",is);
#endif
      for(i=0;i<N1;i++) {	
	for(j=0;j<N2;j++)	{
	  for(k=0;k<N3;k++){		
	    nao = I3(i, j, k);
	    if(is<NS)    {
	      XI(i,j,k,is, 0) = 0.0;      //(k+1)+(j+1)*10.0+(i+1)*100.0
	      XI(i,j,k,is, 1) = 0.0;
	      XI_BC(i,j,k,is, 0) = 1.0;    // 1 non-evolve  0 evolve
	      XI_BC(i,j,k,is, 1) = 1.0;
	    
	      ism = is / NS1;
	      iss = is % NS1;
	    	    
	      //1st material
	      if (ism==0) {
		if (iss==0) {
		  if (i==ppoint_x+0) {
		    if (xi_o[nao]==0) {
		      //		    if(k>0 && k<N3/4)
		      if(k>0 && k<768) { 
			XI(i,j,k, is, 0) = 0.0;
		      }
		      //		    if(k>=N3/4 && k<N3/2 )
		      if(k>=768 && k<1024 ) { 
			XI(i,j,k, is, 0) = 1.0;
		      }
		    }                 
		  }           
		}
		/*  if (iss==1) {
		    if (i==ppoint_x) {
		  
		    if (k>=ppoint_z-40&&k<ppoint_z-3) {
		    XI(i,j,k, is, 0) = 1.0;
		    }
		    if (k>=ppoint_z-3) {//k<=border&&k>=ppoint_z-3
		    XI_BC(i,j,k, is, 0) = 0.0;
		    XI_BC(i,j,k, is, 1) = 0.0;
		    }
                  
		    }
		    }
		*/ 
		fprintf(Order_Param1,"%d,%d,%d,%lf\n",i,j,k,XI(i,j,k, is, 0));
	      }
	    
	      //2nd material
	      if (ism==1) {
		if (iss==0) {
		  if (i==ppoint_x+0) {
		    if (xi_o[nao]==1) {
		      //		   if(k>=N3/2 && k<3*N3/4)
		      if (k>=1024 && k<1280) { 
			XI(i,j,k, is, 0) = 1.0;
		      }
		      //		    if(k>=3*N3/4 && k<N3)
		      if (k>=1280 && k<2048) { 
			XI(i,j,k, is, 0) = 1.0;
		      }
		    }
		    /*  if ((xi_o[nao]==1 && xi_o[I3(i, j, k-1]==0) &&(k!=0&&i!=0)) {
			#ifndef _OPENACC
			printf("residual here, i=%d, j=%d, k=%d,is=%d\n",i,j,k,is);
			#endif
			XI(i,j,k, is, 0) = 0.4737370342414563;//Cu/Ni  0.9716099719347492;//Au/Ag  1.0292195725507935;//Ag/Au  2.110875713152033;//Ni/Cu   0.483474845511;//Al/Pt  2.068359934927055;//Pt/Al
                    
			} */
		  }
		}
		if (iss==1) {
		  if (i==ppoint_x) {
		    if (k>border&&k<N3-2) {
		      XI_BC(i,j,k, is, 0) = 0.0;
		      XI_BC(i,j,k, is, 1) = 0.0;
		    }
                  
		  }
		}
		fprintf(Order_Param2,"%d,%d,%d,%lf\n",i,j,k,XI(i,j,k, is, 0));
	      }
	   
	      //end inclined plane 
	    
	      // one screw dislocation, for xn = (001), xb = (010)
	      /*if(k==N3/2){
		if(i<N1/2){
		XI(i,j,k, is, 0) = 1.0;
		}
		}*/ //end of one dislocation
	    
	      // random obstacles
	      /*		if(iss == 0 && XI_BC(i,j,k, is, 0) == 0.0){
				double obs = 1.0 * (rand()/((double)RAND_MAX + 1));
				if(obs < setobs){
				XI_BC(i,j,k, is, 0) = 1.0;
				if(obs < setobs/2.0) XI_BC(i,j,k, is, 0) = 2.0;
				else XI(i,j,k, is, 0)=-2.0;
				//			XI(i,j,k, is, 0) = 0.0;
				}
				}
				else if(iss != 0){
				if(XI_BC(i,j,k, 0, 0) == 1.0){
				XI_BC(i,j,k, is, 0) = 1.0;
				XI(i,j,k, is, 0) = XI(i,j,k, 0, 0);
				//			XI(i,j,k, is, 0) = 0.0;
				}
				}    */    //end of random obstacles
	    
	      /* Set BC according to material type */
	    
	      if(ism != xi_o[nao]){
		XI_BC(i,j,k,is, 0) = 1.0;
		XI(i,j,k,is, 0) = 0.0;
	      }
	    
	      DATA(i,j,k,is, 0) = XI(i,j,k,is, 0);
	      DATA(i,j,k,is, 1) = XI(i,j,k,is, 1);
	    } else {  //is>=NS
	      if(xi_o[nao] == 0) XI_BC(i,j,k,is, 0) = 1.0;
	      else XI_BC(i,j,k,is, 0) = 0.0;
	    }
	  }
	}
      
      }
    }
  } // acc  
  
  printf("%g", data[4]);
}

void virtualevolv(float *data, float *data2, float * sigmav, double * DD, double * xi, double * xi_bc, double CDv, double * sigmal, int Rv, double d1, double d2, double d3, double size3, FILE * of7, int it, int itp, int vflag, double *II, int *xi_o, int ZLdC[NMAT][6], int prop[NMAT][6], double dS[NMAT][6][6])
{
  int isa, isb, i, j, k, u,v, naij, nao, indm[2];
  float sigr, sigi,dE,dE_imag;
  dE=0.0;
  dE_imag = 0.0;
  //float dE_min[9] = {};
  /* 
     for (i=1; i<2*N1*N2*N3*NSV+1; i++)
     {
     data2[i] = 0;
     } */
  //printf("virtual data2\n");
  //int itt = itp*NT+it;
  for(isa=0;isa<NSV;isa++) {
    for(isb=0;isb<NSV;isb++) {

      for(i=0;i<N1;i++) {
	for(j=0;j<N2;j++) {
	  for(k=0;k<N3;k++) {
	    if(isa >= NS) {
	      DATA2(i,j,k, isb, 0) += DATA(i,j,k, isb, 0) * DD[I5V(i, j, k, isb, isa-NS)];
	      DATA2(i,j,k, isb, 1) += DATA(i,j,k, isb, 1) * DD[I5V(i, j, k, isb, isa-NS)];
	    }
	  }
	}
      }
      
    }
  }

#ifdef USE_CUFFT
#pragma acc data copy(data2[0:2*N1*N2*N3*NSV])
#pragma acc host_data use_device(data2)
#endif
  fft_backward(data2, NSV);	/* inverse FFT*/
	
  //printf("virtual calculation");
  for(u=0;u<ND;u++)
    for(v=0;v<ND;v++)
      {
	//   if(it%200==0) fprintf(of7,"zone   I = %d J = %d \n", N1, N3);
	isa=u+v*ND+NS;
	for(i=0;i<N1;i++)
	  for(j=0;j<N2;j++)
	    for(k=0;k<N3;k++)
	      {
		nao = I3(i, j, k);
		naij = I4(i, j, k, u + v*ND);
		sigr = -(DATA2(i,j,k, isa, 0)/(N1*N2*N3)-sigmal[naij]);
		sigi = -(DATA2(i,j,k, isa, 1)/(N1*N2*N3));
		C4(sigmav, i,j,k, isa-NS, 0) = sigr;
		C4(sigmav, i,j,k, isa-NS, 1) = sigi;
		if(it < NT-NTD)
		  {
		    if (XI_BC(i,j,k, isa, 0) == 0.0)
		      {
					
			//	printf("nam= %d, u=%d, v=%d, dE1=%e, dE2=%e, dE3=%e\n",xi_o[nao],u,v,data2[nad1]/(N1*N2*N3),II[naij],-sigmal[naij]);
			if(xi_o[nao] == -1)
			  {
			    if(it==NT-NTD-1 && i==0 && j==0 && k>N3/2)
			      printf("%d (%d,%d,%d) %e %e %e \n",isa,i,j,k,DATA2(i,j,k, isa, 0)/(N1*N2*N3), -sigmal[naij],-CDv*(DATA2(i,j,k, isa, 0)/(N1*N2*N3)-sigmal[naij]));
			    XI(i,j,k, isa, 0) -= CDv*(DATA2(i,j,k, isa, 0)/(N1*N2*N3)-sigmal[naij]);///(ceil((it)/100)+1);
			    XI(i,j,k, isa, 1) -= CDv*(DATA2(i,j,k, isa, 0))/(N1*N2*N3);///(ceil((it)/100)+1);
			    //	  printf("%d %lf %lf\n",ceil((itt+1)/10),CDv/ceil((itt+1)/10),XI(i,j,k, isa, 0));
			    // xi_sum[na  ] += XI(i,j,k, isa, 0);
			    // xi_sum[na+1] += XI(i,j,k, isa, 1);
			    if(fabs(sigr) > 1E-11) {
			      vflag = 1;
			    }
			  }
			else
			  {
			    int indv=Indmat2v(u,v);
			    if(ZLdC[xi_o[nao]][indv]==1)
			      {
				if(dS[xi_o[nao]][indv][indv] > 0.0)
				  {
				    //printf("K<0!!! Material 0 should be softer\n");
				    dE      = DATA2(i,j,k, isa, 0)/(N1*N2*N3)+II[naij]-sigmal[naij];
				    dE_imag = DATA2(i,j,k, isa, 1)/(N1*N2*N3);

				    XI(i,j,k, isa, 0) += CDv*dE;
				    XI(i,j,k, isa, 1) += CDv*dE_imag;
				  }
				else
				  {
				    //printf("K>0!!! Material 0 should be stiffer\n");
				    dE      = DATA2(i,j,k, isa, 0)/(N1*N2*N3)+II[naij]-sigmal[naij];
				    dE_imag = DATA2(i,j,k, isa, 1)/(N1*N2*N3);
                                            
				    XI(i,j,k, isa, 0) -= CDv*dE;
				    XI(i,j,k, isa, 1) -= CDv*dE_imag;
				  }

				//mark 
				// isa   u   v   meaning
				// 2     0   0    strain11
				// 3     1   0     12
				// 4     2   0     13
				// 5     0   1     21
				// 6     1   1     22
				// 7     2   1     23
				// 8     0   2     31
				// 9     1   2     32
				// 10    2   2     33
			      }//ZLdC[xi_o[nao]][indv]==1
			    else if(ZLdC[xi_o[nao]][indv]==2)
			      {	//condition from dC
				Indv2mat(prop[xi_o[nao]][indv], indm);
				XI(i,j,k, isa, 0) = -XI(i, j, k, indm[0]+ND*indm[1]+NS, 0);
				XI(i,j,k, isa, 1) = -XI(i, j, k, indm[0]+ND*indm[1]+NS, 1);
			      }
			    if(fabs(XI(i,j,k, isa, 0))>1E10)
			      {
				printf("u=%d, v=%d, ZLdC= %d, dE1=%e, dE2= %e, dE3=%e \n",u,v,ZLdC[xi_o[nao]][indv],C4(data2, i,j,k, isa, 0)/(N1*N2*N3),II[naij],-sigmal[naij]);
				it=NT-1;
			      }
			  }//else
		      }//XI_BC==0.0
		  }//it < NT-NTD
		DATA(i,j,k, isa, 0) = XI(i,j,k, isa, 0);
		DATA(i,j,k, isa, 1) = XI(i,j,k, isa, 1);
	      }// end ijk
      } /* end u v */
}
	
void resolSS(double sigma[N1][N2][N3][ND][ND], double tau[N1][N2][N3][NS], double eps[NS][ND][ND], double * xi_bc, double * sigmal, int * xi_o)
{
  int is, i, j,k1,k2,k3, nao,naij;
  double xibc1[N1],xibc2[N2],xibc3[N3];
  //double min1=1.0*N2*N3,min2=1.0*N1*N3,min3=1.0*N1*N2;; //determine whether there is an infinite free surface parallel to one of the axes
  int flag=0; //indicate there is an infinite free surface when flag!=0
	
  for(k1=0;k1<N1;k1++){
    xibc1[k1]=0.0;
  }
  for(k2=0;k2<N2;k2++){
    xibc2[k2]=0.0;
  }
  for(k3=0;k3<N3;k3++){
    xibc3[k3]=0.0;
  }
  for(k1=0;k1<N1;k1++)
    for(k2=0;k2<N2;k2++)
      for(k3=0;k3<N3;k3++){
	nao = k3+(k2)*N3+(k1)*N3*N2;
	xibc1[k1] += (xi_o[nao] + 1.0);//xi_bc[na];
	xibc2[k2] += (xi_o[nao] + 1.0);//xi_bc[na];
	xibc3[k3] += (xi_o[nao] + 1.0);//xi_bc[na];
      }
  for(k1=0;k1<N1;k1++){
    //		if(xibc1[k1]<min1) min1=xibc1[k1];
    if(xibc1[k1]==0.0){ 
      flag=1;
      xibc1[k1]=-1;
    }
  }
  for(k2=0;k2<N2;k2++){
    //		if(xibc2[k2]<min2) min2=xibc2[k2];
    if(xibc2[k2]==0.0){ 
      flag=2;
      xibc2[k2]=-1;
    }
  }
  for(k3=0;k3<N3;k3++){
    //		if(xibc3[k3]<min3) min3=xibc3[k3];
    if(xibc3[k3]==0.0){ 
      flag=3;
      xibc3[k3]=-1;
    }
  }
  //	if(min1==0.0 || min2==0.0 || min3==0.0){
  if(flag!=0){
    printf("infinite free surface present in %d direction.\n",flag);
  }
	
  for (i=0;i<ND;i++)
    for(j=0;j<ND;j++)
      for(k1=0;k1<N1;k1++)
	for(k2=0;k2<N2;k2++)
	  for(k3=0;k3<N3;k3++){
	    naij=k3+k2*N3+k1*N2*N3+i*N1*N2*N3+j*N1*N2*N3*ND;
	    //int na=2*(k3+(k2)*N3+(k1)*N3*N2+(NSV-1)*N1*N2*N3);
	    if(flag==0) sigmal[naij]=sigma[k1][k2][k3][i][j];
	    else{
	      if(xibc1[k1]==-1 || xibc2[k2]==-1 || xibc3[k3]==-1) sigmal[naij]=0.0;
	      else sigmal[naij]=sigma[k1][k2][k3][i][j];
	    } 
	  }
	
  printf("Resolved shear stresses\n");
  //	for (nam=0;nam<NMAT;nam++){
  //
  for (is=0; is<NS; is++) {
    for (k1=0; k1<N1; k1++) {
      for (k2=0; k2<N2; k2++) {
	for (k3=0; k3<N3; k3++) {
	  tau[k1][k2][k3][is] = 0.0;
	  //if(is%NS1==0) printf("Material %d\n", is);
	  for (i=0; i<ND; i++) {
	    for (j=0; j<ND; j++) {
	      tau[k1][k2][k3][is] = tau[k1][k2][k3][is]+sigma[k1][k2][k3][i][j]*eps[is][i][j];
	    }
	  }
	  // printf("tau[%d] = %lf\n",is, tau[is]);
	}
      }
    }
  }
    
  /* Original
     for (is=0;is<NS;is++){
     tau[is] = 0.0;
     if(is%NS1==0) printf("Material %d\n", is);
     for(i=0;i<ND;i++){
     for(j=0;j<ND;j++){
     tau[is] = tau[is]+sigma[i][j]*eps[is][i][j];
     }	
     }
     printf("tau[%d] = %lf\n",is, tau[is]);
     }
  */
  return;
}

void set2D(double xn[NS][ND], double xb[NS][ND])
{
  int is;
  for(is=0;is<NS;is+=NS1){
    xn[is][0]=sqrt(3)/3.;
    xn[is][1]=sqrt(3)/3.;
    xn[is][2]=sqrt(3)/3.;
        
    xb[is][0]=-sqrt(2)/2.;
    xb[is][1]=0.;
    xb[is][2]=sqrt(2)/2.;
  }
  
  printf("Burgers vector b = (%lf , %lf , %lf )\n", xb[0][0],xb[0][1], xb[0][2]);	
  printf("Slip plane     n = [%lf,  %lf , %lf ]\n", xn[0][0],xn[0][1], xn[0][2]);
  return;
}

void set3D1sys(double xn[NS][ND], double xb[NS][ND])
{
	 
	
  int is;
  for(is=0;is<NS;is+=NS1){
#if 0  
    //for screw case:
		
    xn[is][0]=sqrt(3)/3.;
    xn[is][1]=sqrt(3)/3.;
    xn[is][2]=sqrt(3)/3.;
        
    xb[is][0]=-sqrt(2)/2.;
    xb[is][1]=sqrt(2)/2.;
    xb[is][2]=0.0;
#else
    //for edge case:
	      
    xn[is][0]=sqrt(3)/3.;
    xn[is][1]=sqrt(3)/3.;
    xn[is][2]=sqrt(3)/3.;      
	      
    xb[is][0]=1/sqrt(6.);
    xb[is][1]=1/sqrt(6.);
    xb[is][2]=-2/sqrt(6.);
              
#endif      
        
    //for mixed type case:
    //xn[is][0]=sqrt(3)/3.;
    //xn[is][1]=sqrt(3)/3.;
    //xn[is][2]=sqrt(3)/3.;
        
    //xb[is][0]=-sqrt(2)/2.;
    //xb[is][1]=0.0;
    //xb[is][2]=sqrt(2)/2.;
        
    //xn[is+1][0]=sqrt(3)/3.;
    //xn[is+1][1]=sqrt(3)/3.;
    //xn[is+1][2]=sqrt(3)/3.;
        
    //xb[is+1][0]=-sqrt(2)/2.;
    //xb[is+1][1]=0.0;
    //xb[is+1][2]=sqrt(2)/2.;
  }
  
  printf("Burgers vector b1 = (%lf , %lf , %lf )\n", xb[0][0],xb[0][1], xb[0][2]);
  printf("Burgers vector b2 = (%lf , %lf , %lf )\n", xb[1][0],xb[1][1], xb[1][2]);
  printf("Slip plane     n  = [%lf,  %lf , %lf ]\n", xn[0][0],xn[0][1], xn[0][2]);
  return;
}

void set3D2sys(double xn[NS][ND], double xb[NS][ND])
{
  if(NS1!=2){
    printf("# of slip systems do not agree with function (set3D2sys) called.\n");
    exit(1);
  }
	
  int is;
  for(is=0;is<NS;is+=NS1){
    //for screw case:
    xn[is][0]=sqrt(3)/3.;
    xn[is][1]=sqrt(3)/3.;
    xn[is][2]=sqrt(3)/3.;
        
    xb[is][0]=-sqrt(2)/2.;
    xb[is][1]=sqrt(2)/2.;
    xb[is][2]=0.0;
  
    xn[is+1][0]=sqrt(3)/3.;
    xn[is+1][1]=sqrt(3)/3.;
    xn[is+1][2]=sqrt(3)/3.;
        
    xb[is+1][0]=-sqrt(2)/2.;
    xb[is+1][1]=sqrt(2)/2.;
    xb[is+1][2]=0.0;
        
    //for mixed type case:
    //xn[is][0]=sqrt(3)/3.;
    //xn[is][1]=sqrt(3)/3.;
    //xn[is][2]=sqrt(3)/3.;
        
    //xb[is][0]=-sqrt(2)/2.;
    //xb[is][1]=0.0;
    //xb[is][2]=sqrt(2)/2.;
        
    //xn[is+1][0]=sqrt(3)/3.;
    //xn[is+1][1]=sqrt(3)/3.;
    //xn[is+1][2]=sqrt(3)/3.;
        
    //xb[is+1][0]=-sqrt(2)/2.;
    //xb[is+1][1]=0.0;
    //xb[is+1][2]=sqrt(2)/2.;
  }
  
  printf("Burgers vector b1 = (%lf , %lf , %lf )\n", xb[0][0],xb[0][1], xb[0][2]);
  printf("Burgers vector b2 = (%lf , %lf , %lf )\n", xb[1][0],xb[1][1], xb[1][2]);
  printf("Slip plane     n  = [%lf,  %lf , %lf ]\n", xn[0][0],xn[0][1], xn[0][2]);
  return;
}

void set3D4sys(double xn[NS][ND], double xb[NS][ND])
{
  if(NS1!=4){
    printf("# of slip systems do not agree with function (set3D4sys) called.\n");
    exit(1);
  }
	
  int is;
  for(is=0;is<NS;is+=NS1){
		
    xn[is][0]=1.0/sqrt(3);
    xn[is][1]=1.0/sqrt(3);
    xn[is][2]=1.0/sqrt(3);
	
    xb[is][0]=1.0/sqrt(2);
    xb[is][1]=0.0;
    xb[is][2]=-1.0/sqrt(2);
	
    xn[is+1][0]=1.0/sqrt(3);
    xn[is+1][1]=1.0/sqrt(3);
    xn[is+1][2]=1.0/sqrt(3);
	
    xb[is+1][0]=-1.0/sqrt(2);
    xb[is+1][1]=1.0/sqrt(2);
    xb[is+1][2]=0.0;
	
    xn[is+2][0]=1.0/sqrt(3);
    xn[is+2][1]=1.0/sqrt(3);
    xn[is+2][2]=1.0/sqrt(3);
	
    xb[is+2][0]=0.0;
    xb[is+2][1]=-1.0/sqrt(2);
    xb[is+2][2]=1.0/sqrt(2);

    xn[is+3][0]=0.0;
    xn[is+3][1]=0.0;
    xn[is+3][2]=1.0;
	
    xb[is+3][0]=0.0;
    xb[is+3][1]=1.0;
    xb[is+3][2]=0.0;
  }
	
  printf("Burgers vector b1 = (%lf , %lf , %lf )\n", xb[0][0],xb[0][1], xb[0][2]);
  printf("Burgers vector b2 = (%lf , %lf , %lf )\n", xb[1][0],xb[1][1], xb[1][2]);
  printf("Slip plane     n  = [%lf,  %lf , %lf ]\n", xn[0][0],xn[0][1], xn[0][2]);
  printf("Burgers vector b3 = (%lf , %lf , %lf )\n", xb[2][0],xb[2][1], xb[2][2]);
  printf("Burgers vector b4 = (%lf , %lf , %lf )\n", xb[3][0],xb[3][1], xb[3][2]);
  printf("Slip plane     n  = [%lf,  %lf , %lf ]\n", xn[2][0],xn[2][1], xn[2][2]);
  return;
}

void setfcc (double xn[NS][ND], double xb[NS][ND])
{
  if(NS1!=12){
    printf("# of slip systems do not agree with function (setfcc) called.\n");
    exit(1);
  }
  int i, j;
  float s3;
  
  s3 = sqrt(3);

  int is;
  for(is=0;is<NS;is+=NS1){

    for (i=0;i<03 ; i++) {
      xn[is+i][0]= 1.0/s3;
      xn[is+i][1]= 1.0/s3;
      xn[is+i][2]= 1.0/s3;
    }
  
    for (i=3;i<6 ; i++) {
      xn[is+i][0]= -1.0/s3;
      xn[is+i][1]= 1.0/s3;
      xn[is+i][2]= 1.0/s3;
    }
  
    for (i=6;i<9 ; i++) {
      xn[is+i][0]= 1.0/s3;
      xn[is+i][1]= -1.0/s3;
      xn[is+i][2]= 1.0/s3;
    }

    for (i=9;i<12 ; i++) {
      xn[is+i][0]= 1.0/s3;
      xn[is+i][1]= 1.0/s3;
      xn[is+i][2]= -1.0/s3;
    }

    xb[is+0][0] = -1.0;
    xb[is+0][1] = 1.0;
    xb[is+0][2] = 0.0;
  
    xb[is+1][0] = 0.0;
    xb[is+1][1] = -1.0;
    xb[is+1][2] = 1.0;
  
    xb[is+2][0] = 1.0;
    xb[is+2][1] = 0.0;
    xb[is+2][2] = -1.0;
  
    xb[is+3][0] = -1.0;
    xb[is+3][1] = -1.0;
    xb[is+3][2] = 0.0;
  
    xb[is+4][0] = 1.0;
    xb[is+4][1] = 0.0;
    xb[is+4][2] = 1.0;
  
    xb[is+5][0] = 0.0;
    xb[is+5][1] = -1.0;
    xb[is+5][2] = 1.0;
  
    xb[is+6][0] = -1.0;
    xb[is+6][1] = -1.0;
    xb[is+6][2] = 0.0;
  
    xb[is+7][0] = 1.0;
    xb[is+7][1] = 0.0;
    xb[is+7][2] = -1.0;
  
    xb[is+8][0] = 0.0;
    xb[is+8][1] = -1.0;
    xb[is+8][2] = -1.0;
  
    xb[is+9][0] = -1.0;
    xb[is+9][1] = 1.0;
    xb[is+9][2] = 0.0;
  
    xb[is+10][0] = 1.0;
    xb[is+10][1] = 0.0;
    xb[is+10][2] = 1.0;
  
    xb[is+11][0] = 0.0;
    xb[is+11][1] = -1.0;
    xb[is+11][2] = -1.0;
  }
	
  printf("Slip systems\n");

  for (i=0; i<12; i++){
    for (j=0; j<3; j++) {
      xb[i][j] = xb[i][j]/2.0;	
    }
    printf("b(%d) = (%lf , %lf , %lf )\n", i, xb[i][0],xb[i][1], xb[i][2]);	
    printf("n(%d) = [%lf,  %lf , %lf ]\n", i, xn[i][0],xn[i][1], xn[i][2]); 
  }
  return;	 
}


void seteps (double eps[NS][ND][ND], double epsv[NV][ND][ND],double xn[NS][ND], double xb[NS][ND],double dslip2[NS], double AA[NMAT][ND][ND], double theta1[NMAT][ND][ND])
{
  double xn1[NV][ND],xb1[NV][ND],epst[NS][ND][ND];
  int i, j, k, l, is, v, na;
	
  for (v=0;v<NV;v++){
    for(i=0;i<ND;i++){
      xn1[v][i]=0.0;
      xb1[v][i]=0.0;
    }	
  }
	
  for (v=0;v<NV;v++){
    if(v==0 || v==1 || v==2)
      {xn1[v][0]=1.0;}
    if(v==3 || v==4 || v==5)
      {xn1[v][1]=1.0;}
    if(v==6 || v==7 || v==8)
      {xn1[v][2]=1.0;}
    if(v==0 || v==3 || v==6)
      {xb1[v][0]=1.0;}
    if(v==1 || v==4 || v==7)
      {xb1[v][1]=1.0;}
    if(v==2 || v==5 || v==8)
      {xb1[v][2]=1.0;}
    //		 printf("%lf %lf %lf %lf %lf %lf \n", xn1[v][0],xn1[v][1],xn1[v][2],xb1[v][0],xb1[v][1],xb1[v][2]);
  }
      
  for (v=0; v<NV;v++){
    printf("strainsys (%d) \n", v);
    for (i=0; i<ND; i++) {
      for (j=0; j<ND; j++) {
	epsv[v][i][j]= xn1[v][i]*xb1[v][j];
	printf("%lf ", epsv[v][i][j]);
      }
      printf("\n");
    }
  }

  /*set eps */
	
  for (i=0; i<ND; i++) {
    for (j=0; j<ND; j++) {
      for (is=0; is<NS;is++){
	//eps[is][i][j]= (xb[is][i]*xn[is][j]+xb[is][j]*xn[is][i])/2.0	/ dslip2[is];
	epst[is][i][j]= xb[is][i]*xn[is][j]	/ dslip2[is];
      }
    }
  }
	
	
  for (is=0; is<NS;is++){
    printf("Eps0 for slip system %d \n",is);
    for (i=0; i<ND; i++) {
      for (j=0; j<ND; j++) {
	eps[is][i][j] = 0.0;
	na = is/NS1;
	for (k=0; k<ND;k++)
	  for (l=0; l<ND;l++){
	    //eps[is][i][j] += AA[na][i][k]*AA[na][j][l]*epst[is][k][l];
	    eps[is][i][j] += theta1[na][i][k]*theta1[na][j][l]*epst[is][k][l];
	  } //kl
	printf("%lf ",eps[is][i][j]);
      } //j
      printf("\n");
    } //i
  } //is
  return;
}

//mark: there are 3 coordinates system: global, material 0 and material 1
//final result is in global coordinates
//so all the initial x_b, x_n, C[][][][] should be in local coordinates
//interface_n in global coordinates
//theta1 in setorient should be ------ base in global * base in local coordinates
void setorient(double theta1[NMAT][ND][ND])
{
  int i,j,na;
  double r[NMAT][ND];
	
  //double t1=0.0;//15.0/180.0*pi;//45.0/180.0*pi;
  // set coordinate axes for different orienations
  // axis e_i for grain orientation na is stored in theta1[na][i][*]
	
  for (na=0;na<NMAT;na++){//{111}slip plane [110] slip direction {100} interface +z_global as Burgers Vector for Edge
    if(na==0){
      theta1[na][0][0] = sqrt(3.)/3.;
      theta1[na][0][1] = sqrt(3.)/3.;
      theta1[na][0][2] = sqrt(3.)/3.;
      theta1[na][1][0] = -sqrt(2.)/2.;
      theta1[na][1][1] = sqrt(2.)/2.;
      theta1[na][1][2] = 0.0;
      theta1[na][2][0] = -sqrt(6.)/6.;
      theta1[na][2][1] = -sqrt(6.)/6.;
      theta1[na][2][2] = sqrt(6.)/3.;
    }
    else if(na==1){
      theta1[na][0][0] = sqrt(3.)/3.;
      theta1[na][0][1] = sqrt(3.)/3.;
      theta1[na][0][2] = sqrt(3.)/3.;
      theta1[na][1][0] = -sqrt(2.)/2.;
      theta1[na][1][1] = sqrt(2.)/2.;
      theta1[na][1][2] = 0.0;
      theta1[na][2][0] = -sqrt(6.)/6.;
      theta1[na][2][1] = -sqrt(6.)/6.;
      theta1[na][2][2] = sqrt(6.)/3.;
    }
    else if(na%2 == 0){
      for(i=0;i<ND;i++)
	for(j=0;j<ND;j++){
	  theta1[na][i][j] = theta1[0][i][j];
	}
    }
    else {
      for(i=0;i<ND;i++)
	for(j=0;j<ND;j++){
	  theta1[na][i][j] = theta1[1][i][j];
	}
    }
  }
	
	
  //calculate the axes lengths and normalization
  for (na=0;na<NMAT;na++){
    for(i=0;i<ND;i++){
      r[na][i] = 0.0;
      for(j=0;j<ND;j++){
	r[na][i] += theta1[na][i][j] * theta1[na][i][j];
      }
    }
  }
	
  for (na=0;na<NMAT;na++){
    for(i=0;i<ND;i++){
      for(j=0;j<ND;j++){
	if(r[na][i] != 0.0) theta1[na][i][j] = theta1[na][i][j]/r[na][i];
	else{
	  printf("error in defining axes %d in grain orientation %d, axis length zero\n", i,na);
	  exit(1);
	} 
      }
    }
  }
	
  return;
}

void Amatrix(double AA[NMAT][ND][ND], double theta1[NMAT][ND][ND])
{
  
  int i,j,k,na;
  
  /* calculate the axis transformation matrix AA */
  for (na=0;na<NMAT;na++){
    printf("Amatrix for material %d\n", na);
    for(i=0;i<ND;i++){
      for(j=0;j<ND;j++){
	AA[na][i][j] = 0.0;
	for(k=0;k<ND;k++){
	  AA[na][i][j] += theta1[0][i][k]*theta1[na][j][k]; 
	}
	if(na!=0) printf("%lf ",AA[na][i][j]);
      }
      printf("\n");
    }
  }
  return;
}

void dSmat(double Cm[NMAT][3], double Sm[NMAT][3], double AA[NMAT][ND][ND], double KK[NMAT][6][6], double dS[NMAT][6][6], int ZLdC[NMAT][6], int prop[NMAT][6],double theta1[NMAT][ND][ND]){
#define 	DELTA(i, j)   ((i==j)?1:0)
#define		DELTA4(i,j,k,l) (((i==j) && (j==k) && (k==l))?1:0)
  int nam, i, j, k, l, m, n, u, v,j1,i1;
  double c[ND][ND][ND][ND], cp[ND][ND][ND][ND], c0[ND][ND][ND][ND], C11, C12, C44, mu, ** dC=NULL, ** C0=NULL, ll, mup,det;
  double ** CR=NULL, ** dCR=NULL, ** dSR=NULL, ** K=NULL ;
	
  dC = malloc((6)*sizeof(double *));
  C0 = malloc((6)*sizeof(double *));
  for (i=0;i<6;i++){
    dC[i] = malloc((6)*sizeof(double));
    C0[i] = malloc((6)*sizeof(double));
  }
	
  for(nam=0;nam<NMAT;nam++)
    {
      for(i=0;i<6;i++)
	{
	  ZLdC[nam][i] = 1;
	  prop[nam][i] = 0;
	  for(j=0;j<6;j++){
	    KK[nam][i][j] = 0.0;
	    dS[nam][i][j] = 0.0;
	  }
	}
      C11=Cm[nam][0];
      C12=Cm[nam][1];
      C44=Cm[nam][2];
      mu = C44;
      ll = C12;
      mup= C11-C12-2*C44;
      printf("mup is %e\n", mup);
		
      printf("Amatrix for material %d\n", nam);
      for(i=0;i<ND;i++){
	for(j=0;j<ND;j++){
	  if(nam!=0) printf("%lf ",AA[nam][i][j]);
	}
	printf("\n");
      }
		
      for(i=0;i<ND;i++)
	for(j=0;j<ND;j++)
	  for(k=0;k<ND;k++)
	    for(m=0;m<ND;m++){
	      c[i][j][k][m] = mu * (DELTA(i,k)*DELTA(j,m)+DELTA(i,m)*DELTA(j,k))+ll*DELTA(i,j)*DELTA(k,m)+mup*DELTA4(i,j,k,m);
	      //printf("in dSmat check: c[%d][%d][%d][%d] for material %d is %e\n",i,j,k,m,nam,c[i][j][k][m]);
	    }
      for(i=0;i<ND;i++)
	for(j=0;j<ND;j++)
	  for(k=0;k<ND;k++)
	    for(l=0;l<ND;l++){
	      cp[i][j][k][l] = 0.0;
	      for (m=0; m<ND; m++) 
		for (n=0; n<ND; n++) 
		  for (u=0; u<ND; u++) 
		    for (v=0; v<ND; v++) {
		      //cp[i][j][k][l] += AA[nam][i][m]*AA[nam][j][n]*AA[nam][k][u]*AA[nam][l][v]*c[m][n][u][v];
		      cp[i][j][k][l] += c[m][n][u][v]*theta1[nam][i][m]*theta1[nam][j][n]*theta1[nam][k][u]*theta1[nam][l][v];
		      //if(i==0 && j ==0 && k==1 && l==1 &&(AA[nam][i][m]*AA[nam][j][n]*AA[nam][k][u]*AA[nam][l][v]*c[m][n][u][v] != 0.0)) printf("cp[0][0][1][1] + %e, AA[%d][%d]= %e, AA[%d][%d]=%e, AA[%d][%d]=%e, AA[%d][%d]=%e, c[%d][%d][%d][%d] = %e\n", AA[nam][i][m]*AA[nam][j][n]*AA[nam][k][u]*AA[nam][l][v]*c[m][n][u][v], i,m,AA[nam][i][m],j,n,AA[nam][j][n],k,u,AA[nam][k][u],l,v,AA[nam][l][v],m,n,u,v,c[m][n][u][v]);
		    }
	      //	if(cp[i][j][k][l] !=0) printf("cp[%d][%d][%d][%d] = %e\n", i,j,k,l,cp[i][j][k][l]);
	      if (MT==1) {
		cp[i][j][k][l] = c[i][j][k][l];
	      }
	      if(nam==0) c0[i][j][k][l] = cp[i][j][k][l];
	      printf("in dSmat check: cp[%d][%d][%d][%d] for material %d is %e\n",i,j,k,l,nam,cp[i][j][k][l]);
                        
	    }
      dC[0][0] =  cp[0][0][0][0] - c0[0][0][0][0];
      dC[0][1] =  cp[0][0][1][1] - c0[0][0][1][1];
      dC[0][2] =  cp[0][0][2][2] - c0[0][0][2][2];
      dC[0][3] =  cp[0][0][1][2] - c0[0][0][1][2];
      dC[0][4] =  cp[0][0][2][0] - c0[0][0][2][0];
      dC[0][5] =  cp[0][0][0][1] - c0[0][0][0][1];
      dC[1][1] =  cp[1][1][1][1] - c0[1][1][1][1];
      dC[1][2] =  cp[1][1][2][2] - c0[1][1][2][2];
      dC[1][3] =  cp[1][1][1][2] - c0[1][1][1][2];
      dC[1][4] =  cp[1][1][2][0] - c0[1][1][2][0];
      dC[1][5] =  cp[1][1][0][1] - c0[1][1][0][1];
      dC[2][2] =  cp[2][2][2][2] - c0[2][2][2][2];
      dC[2][3] =  cp[2][2][1][2] - c0[2][2][1][2];
      dC[2][4] =  cp[2][2][2][0] - c0[2][2][2][0];
      dC[2][5] =  cp[2][2][0][1] - c0[2][2][0][1];
      dC[3][3] =  cp[1][2][1][2] - c0[1][2][1][2];
      dC[3][4] =  cp[1][2][2][0] - c0[1][2][2][0];
      dC[3][5] =  cp[1][2][0][1] - c0[1][2][0][1];
      dC[4][4] =  cp[2][0][2][0] - c0[2][0][2][0];
      dC[4][5] =  cp[2][0][0][1] - c0[2][0][0][1];
      dC[5][5] =  cp[0][1][0][1] - c0[0][1][0][1];
																											 
      C0[0][0] =  c0[0][0][0][0];
      C0[0][1] =  c0[0][0][1][1];
      C0[0][2] =  c0[0][0][2][2];
      C0[0][3] =  c0[0][0][1][2];
      C0[0][4] =  c0[0][0][2][0];
      C0[0][5] =  c0[0][0][0][1];
      C0[1][1] =  c0[1][1][1][1];
      C0[1][2] =  c0[1][1][2][2];
      C0[1][3] =  c0[1][1][1][2];
      C0[1][4] =  c0[1][1][2][0];
      C0[1][5] =  c0[1][1][0][1];
      C0[2][2] =  c0[2][2][2][2];
      C0[2][3] =  c0[2][2][1][2];
      C0[2][4] =  c0[2][2][2][0];
      C0[2][5] =  c0[2][2][0][1];
      C0[3][3] =  c0[1][2][1][2];
      C0[3][4] =  c0[1][2][2][0];
      C0[3][5] =  c0[1][2][0][1];
      C0[4][4] =  c0[2][0][2][0];
      C0[4][5] =  c0[2][0][0][1];
      C0[5][5] =  c0[0][1][0][1];
																											 
      for(i=0;i<6;i++)
	for(j=0;j<i;j++){
	  dC[i][j] = dC[j][i];
	  C0[i][j] = C0[j][i];
	}
		
      int countr=0;
      for(i=0;i<6;i++)
	{
	  int count=0;
	  for(j=0;j<6;j++){
	    if(fabs(dC[i][j]) < 1E-7*C44){ 
	      count++;
	      dC[i][j] = 0.0;
	    }
	  }
	  if (count==6) ZLdC[nam][i]=0;
	  if(ZLdC[nam][i] == 1) countr++;  //calculate rank of dC
	}
		
      printf("Material %d has dCij\n",nam);
      for(i=0;i<6;i++){
	for(j=0;j<6;j++){
	  printf("%e ",dC[i][j]);
	}
	printf("\n");
      }
		
      double base[6];
      //double ratio;
      for(i=0;i<3;i++){
	if(ZLdC[nam][i] == 1){
	  for(k=i+1;k<3;k++){
	    j1=0;
	    for(j=0;j<6;j++){
	      if(dC[i][j] != 0.0){
		base[j1] = dC[k][j]/dC[i][j];
		printf("base[%d]=%e\n",j1,base[j1]);
		j1++;
		printf("j1 is %d\n",j1);
	      }
	    }
	    if(j1>1){
	      int j2=j1;
	      for(j=1;j<j1;j++){
		printf("base[%d]=%ld ,base[%d] = %ld, %d\n",j,lrint(base[j]*1E6),j-1,lrint(base[j-1]*1E6),(lrint(base[j]*1E6)==lrint(base[j-1]*1E6)));
		if(lrint(base[j]*1E6)==lrint(base[j-1]*1E6)) j2--;
	      }
	      printf("j2 is %d\n",j2);
	      if(j2==1) {
		countr--;
		ZLdC[nam][k] = 2; //2 is for special lines that are dependent on another.
		if (base[0]!= -1){
		  printf("Error in ratio between lines of dC, %e\n",base[0]);
		}
		prop[nam][k] = i;
	      }
	    }//j1>1
	  }
	}
      }
      if(countr <0){
	printf("Error. Rank of dC less than zero.\n");
	exit(1);
      }
      if(countr == 0 && nam!=0 ){
	printf("Material %d is considered the same as material 0. \n",nam);
	//exit(1);
      }
      else if(nam!=0) printf("Rank of dC is %d\n",countr);
		
      if(countr!=0){
	dCR = malloc((countr)*sizeof(double *));
	dSR = malloc((countr)*sizeof(double *));
	CR = malloc((countr)*sizeof(double *));
	K = malloc((countr)*sizeof(double *));
	for (i=0;i<countr;i++){
	  dCR[i] = malloc((countr)*sizeof(double));
	  dSR[i] = malloc((countr)*sizeof(double *));
	  CR[i] = malloc((countr)*sizeof(double *));
	  K[i] = malloc((countr)*sizeof(double *));
	}
	printf("dCR is \n");																								 
	i1=0;
	for(i=0;i<6;i++)
	  {
	    j1=0;
	    if(ZLdC[nam][i] == 1){
	      for(j=0;j<6;j++){
		if(ZLdC[nam][j] == 1){
		  if(i1<countr && j1<countr){
		    dCR[i1][j1] = dC[i][j];
		    CR[i1][j1] = C0[i][j];
		    printf("%e ",dCR[i1][j1]);
		  }
		  else{
		    printf("Error in dC to dCR assignment, indices out of rank range\n");
		    exit(1);
		  }
		  j1++;		 
		}																								 
              }
	      printf("\n");																							 
	      i1++;																									 
	    }
	  }
		
	for(i=0;i<6;i++){
	  if(ZLdC[nam][i] == 2){
	    int row=0;
	    for(j=0;j<prop[nam][i];j++){
	      if(ZLdC[nam][j] == 1) row++;
	    }
	    j1=0;
	    for(j=0;j<6;j++){
	      if(ZLdC[nam][j] == 1) {
		CR[j1][row] -= C0[j][i];
		j1++;
	      }
	    }
	  }
	}
			
	printf("CR is %d\n", nam);
	for(i=0;i<countr;i++){
	  for(j=0;j<countr;j++){
	    printf("%e ",CR[i][j]);
	  }
	  printf("\n");
	}
		
	det=Determinant(dCR, countr);
	if(det == 0.0 && nam!=0 ){
	  printf("Error. Material %d has invertible dCR. \n",nam);
	  exit(1);
	}
	else{
	  CoFactor(dCR,countr,dSR);
	  Transpose(dSR,countr);
	  printf("dSR for material %d\n", nam);
	  for(i=0;i<countr;i++){
	    for(j=0;j<countr;j++){
	      dSR[i][j] = dSR[i][j]/det;
	      printf("%e ",dSR[i][j]);
	    }
	    printf("\n");
	  }
	}
		
	for(i=0;i<countr;i++)
	  for(l=0;l<countr;l++)
	    for(j=0;j<countr;j++)
	      for(k=0;k<countr;k++){
		K[i][l] += -CR[i][j]*dSR[j][k]*CR[k][l];
	      }
		
	i1=0;
	for(i=0;i<6;i++)
	  {
	    j1=0;
	    if(ZLdC[nam][i] == 1){
	      for(j=0;j<6;j++){
		if(ZLdC[nam][j] == 1){
		  if(i1<countr && j1 <countr){
		    KK[nam][i][j] = K[i1][j1];
		    dS[nam][i][j] = dSR[i1][j1];
		  }
		  else{
		    printf("Error in K to KK assignment, indices out of rank range\n");
		    exit(1);
		  }
		  j1++;		 
		}																								 
	      }
	      printf("\n");																							 
	      i1++;																									 
	    }
	  }
		
	printf("KK first term\n");
	for(i=0;i<6;i++){
	  for(j=0;j<6;j++){
	    printf("%e ",KK[nam][i][j]);
	  }
	  printf("\n");
	}
	printf("dS for material %d\n",nam);	
	for(i=0;i<6;i++){
	  for(j=0;j<6;j++){
	    printf("%e ",dS[nam][i][j]);
	  }	
	  printf("\n");
	}

	free(K);
	free(dCR);
	free(dSR);
	free(CR);
      }
		
      for(i=0;i<6;i++)
	for(l=0;l<6;l++){
	  KK[nam][i][l] += -C0[i][l];
	  //mark :   only for vacuum+material!! must be changed for general case!!!
	  // KK[nam][i][l] = 0.0;
	}
		
    }//nam
	
  printf("KK for material 1\n");
  for(i=0;i<6;i++){
    for(j=0;j<6;j++){
      printf("%e ",KK[1][i][j]);
    }
    printf("\n");
  }
	
  free(dC);
  free(C0);
  return;
}

// sedDorient ====set which part is material 1 and which is no.2 etc
void setDorient(int *xi_o,double *d_f, double *d_s,int border, double interface_n[ND],int ppoint_x, int ppoint_y, int ppoint_z)
{
  
  int i,j,k,na;
  FILE *MatType;
  MatType = fopen("Material_Type.txt","w");
  fprintf(MatType,"x,y,z,Material_Type\n");
  //int check = 0;
  (*d_f)=(double)(N3-border-1);
  (*d_s)=(double)(border);
  
  for(i=0;i<N1;i++){
    for(j=0;j<N2;j++){
      for(k=0;k<N3;k++){
	na = k+j*N3+i*N2*N3;
	xi_o[na] = 0;
	//	if((interface_n[0]*(i-ppoint_x) + interface_n[1]*(j-ppoint_y) + interface_n[2]*(k-ppoint_z))>0 && NMAT>=1){
        if(k>=N3/2){
	  xi_o[na] = 1;
	}
	fprintf(MatType,"%d,%d,%d,%d\n",i, j, k, xi_o[na]);
      }
    }
  }
  
  return;
}

void setMat(double C[NMAT][3], double S[NMAT][3], double b2[NS], double dslip2[NS],
	    const struct material *mc, double b, double dslip)
{
  for (int na = 0; na < NMAT; na++){	
    double lC11, lC12, lC44;
    if (na == 0) {  //should NOT change this part if material 0 is reference material
      lC11 = mc->C11;
      lC12 = mc->C12;
      lC44 = mc->C44;
    } else if (na == 1) {
      //   lC11 = 246.5E9; //Ni
      //   lC12 = 147.3E9;
      //   lC44 = 49.6E9;
      lC11 = 168.4E9;//Cu
      lC12 = 121.4E9;
      lC44 = 23.5E9;
      //lC11=124.0E9;//Ag122.E9;//Ag_Low124.E9;//Ag_High
      //lC12=93.4E9;//Ag92.E9;//Ag_Low93.4E9;//Ag_High92.E9;//Ag_Low
      //lC44=15.3E9;//Ag15.E9;//Ag_Low15.3E9;//Ag_High15.E9;//Ag_Low
      //lC11=192.9E9;//Au186.E9;//Au_Low191.E9;//Au_High
      //lC12=163.8E9;//Au157.E9;//Au_Low162.E9;//Au_High
      //lC44=14.55E9;//Au14.5E9;//Au_Low14.5E9;//Au_High
      //lC11=107.3E9;//Al
      //lC12=60.9E9;
      //lC44=23.2E9;
      //lC11 = 346.7E9; //Pt
      //lC12 = 250.7E9;
      //lC44 = 48.E9;
      //lC11 = 227.1E9; //Pd
      //lC12 = 176.E9;
      //lC44 = 25.55E9;
      //lC11 = 580.E9; //Ir
      //lC12 = 242.E9;
      //lC44 = 169.E9;
    } else {
      printf("Error in setMat. Material set(%d) larger than defined.\n",NMAT);
      exit(1);
    }
    
    C[na][0] = lC11;
    C[na][1] = lC12;
    C[na][2] = lC44;
    double lS11 = 1./3.*(1/(lC11+2*lC12) + 2/(lC11-lC12));
    double lS12 = 1./3.*(1/(lC11+2*lC12) - 1/(lC11-lC12));
    double lS44 = 1/lC44;
    S[na][0] = lS11;
    S[na][1] = lS12;
    S[na][2] = lS44;
    for (int i = 0; i < NS1; i++) {
      int nas = i+NS1*na;
      b2[nas] = b;
      dslip2[nas] = dslip;
    }

    printf("Material constants for material %d\n",na);
    printf("C11 = %e, C12 = %e, C44 = %e\n", lC11, lC12, lC44);
  }
}

float Imatrix(double *II, double *xi, double KK[NMAT][6][6], const struct material *mc,
	      int *xi_o, double interface_n[ND], int ppoint_x, int ppoint_y, int ppoint_z)
{
#define 	DELTA(i, j)   ((i==j)?1:0)
#define		DELTA4(i,j,k,l) (((i==j) && (j==k) && (k==l))?1:0)

  float energy_in2 = 0.;
  int i,j,k,l,k1,k2,k3, naij, nao;
  //double u, v, S11, S12, S44;
  //double c[ND][ND][ND][ND], ds[NMAT][ND][ND][ND][ND];
	
  double mu = mc->C44;
  //double ll = C12;
  //double mup = C11-C12-2*C44;
	
  /* for(nam = 0; nam<NMAT; nam++){
     for(i=0;i<ND;i++)
     for(j=0;j<ND;j++)
     for(k=0;k<ND;k++)
     for(m=0;m<ND;m++){
     if(nam == 0) c[i][j][k][m] = mu * (DELTA(i,k)*DELTA(j,m)+DELTA(i,m)*DELTA(j,k))+ll*DELTA(i,j)*DELTA(k,m)+mup*DELTA4(i,j,k,m);
     //	S11 = dS[nam][0];
     //	S12 = dS[nam][1];
     //	S44 = dS[nam][2];
     //	ds[nam][i][j][k][m] = (S11+2*S12)/3.0 * DELTA(i,j)*DELTA(k,m) + S44/4.0*(DELTA(i,k)*DELTA(j,m)+DELTA(i,m)*DELTA(j,k)) - S44/2.0*DELTA4(i,j,k,m) + (S11-S12)*(DELTA4(i,j,k,m) - DELTA(i,j)*DELTA(k,m)/3.0);
     ds[nam][i][j][k][m] = 0.0;
     }
     ds[nam][0][0][0][0] = dS[nam][0][0];
     ds[nam][0][0][1][1] = dS[nam][0][1];
     ds[nam][0][0][2][2] = dS[nam][0][2];
     ds[nam][0][0][1][2] = dS[nam][0][3]/2.0;
     ds[nam][0][0][2][0] = dS[nam][0][4]/2.0;
     ds[nam][0][0][0][1] = dS[nam][0][5]/2.0;
     ds[nam][1][1][1][1] = dS[nam][1][1];
     ds[nam][1][1][2][2]	=	dS[nam][1][2]	;
     ds[nam][1][1][1][2]	=	dS[nam][1][3]/2.0	;
     ds[nam][1][1][2][0]	=	dS[nam][1][4]/2.0	;
     ds[nam][1][1][0][1]	=	dS[nam][1][5]/2.0	;
     ds[nam][2][2][2][2]	=	dS[nam][2][2]	;
     ds[nam][2][2][1][2]	=	dS[nam][2][3]/2.0	;
     ds[nam][2][2][2][0]	=	dS[nam][2][4]/2.0	;
     ds[nam][2][2][0][1]	=	dS[nam][2][5]/2.0	;
     ds[nam][1][2][1][2]	=	dS[nam][3][3]/4.0	;
     ds[nam][1][2][2][0]	=	dS[nam][3][4]/4.0	;
     ds[nam][1][2][0][1]	=	dS[nam][3][5]/4.0	;
     ds[nam][2][0][2][0]	=	dS[nam][4][4]/4.0	;
     ds[nam][2][0][0][1]	=	dS[nam][4][5]/4.0	;
     ds[nam][0][1][0][1]	=	dS[nam][5][5]/4.0	;
     for(i=0;i<ND;i++)
     for(j=0;j<ND;j++)
     for(k=0;k<ND;k++)
     for(m=0;m<ND;m++){
     if(ds[nam][k][m][i][j]!=0.0) ds[nam][i][j][k][m] = ds[nam][k][m][i][j];
     else if(ds[nam][i][j][m][k]!=0.0) ds[nam][i][j][k][m] = ds[nam][i][j][m][k];
     else if(ds[nam][j][i][k][m]!=0.0) ds[nam][i][j][k][m] = ds[nam][j][i][k][m];
     else if(ds[nam][j][i][m][k]!=0.0) ds[nam][i][j][k][m] = ds[nam][j][i][m][k];
     else if(ds[nam][k][m][j][i]!=0.0) ds[nam][i][j][k][m] = ds[nam][k][m][j][i];
     else if(ds[nam][m][k][i][j]!=0.0) ds[nam][i][j][k][m] = ds[nam][m][k][i][j];
     else if(ds[nam][m][k][j][i]!=0.0) ds[nam][i][j][k][m] = ds[nam][m][k][j][i];
     }
		
     } */
	
  //	for (i=0;i<ND;i++)
  //		for(j=0;j<ND;j++)
  for(k1=0;k1<N1;k1++)
    for(k2=0;k2<N2;k2++)
      for(k3=0;k3<N3;k3++){
	//naij = k3+k2*N3+k1*N2*N3 + i*N1*N2*N3 + j*ND*N1*N2*N3;
	nao = k3+k2*N3+k1*N2*N3;
						
	//II[naij] = 0.0;
	double ev[6];
	double dE1[6];
	double sig[ND][ND];

	for(k=0;k<6;k++){
	  if(k == 0) ev[k] = XI(k1,k2,k3, NS+0, 0);
	  else if(k == 1) ev[k] = XI(k1,k2,k3, NS+1, 1);
	  else if(k == 2) ev[k] = XI(k1,k2,k3, NS+2, 2);
	  else if(k == 3) ev[k] = 2.*XI(k1,k2,k3, NS+1, 2);
	  else if(k == 4) ev[k] = 2.*XI(k1,k2,k3, NS+2, 0);
	  else            ev[k] = 2.*XI(k1,k2,k3, NS+0, 1);
	  dE1[k] = 0.0;
	  //check II difference
	  //                            printf("%d,%d,%d,ev1 =%e,ev2 =%e,ev3 =%e,ev4 =%e,ev5 =%e,ev6 =%e\n",k1,k2,k3,ev[0],ev[1],ev[2],ev[3],ev[4],ev[5]);
	}
	for(k=0;k<6;k++){
	  for(l=0;l<6;l++){
	    dE1[k] += KK[xi_o[nao]][k][l]*ev[l];
	    if(dE1[k]>1E20) printf("KK[%d][%d][%d] is %e, ev[%d] is %e\n",xi_o[nao], k,l,KK[xi_o[nao]][k][l],l,ev[l]);
	    // energy part 2 from delta S
	    if (fabs(interface_n[0]*(k1-ppoint_x) + interface_n[1]*(k2-ppoint_y) + interface_n[2]*(k3-ppoint_z))<=1.0&&(k1==ppoint_x)) {
	      energy_in2 += 0.5*KK[xi_o[nao]][k][l]*ev[l]*ev[k];
	    }
	  }
	}
						
	v2matsig(dE1, sig);

	for (i=0;i<ND;i++)
	  for(j=0;j<ND;j++){
	    naij = k3+k2*N3+k1*N2*N3 + i*N1*N2*N3 + j*ND*N1*N2*N3;
								
	    II[naij] = sig[i][j]/mu;
	    if(II[naij]>1E20) printf("II[%d] is %e\n",naij,II[naij]);
	  }
	//			II[naij] = II[naij]/mu;
      }
  energy_in2 = energy_in2/mu;
	
  return energy_in2;
}

void setq(double q[N1][N2][N3])
{
  int i,j,k,fflag;
  float rf,rf2,qf;
  FILE *of8;	
	
  of8 = fopen("fibers.dat","w");
  fprintf(of8,"zone   I = %d J = %d K = %d\n", N1, N2, N3);
  /* fflag = 0 straight fibers along z  90 degrees vol frac = 0.6 */
  /* fflag = 1 straight fibers along z  45 degrees vol frac = 0.6 */
  fflag =1;
  qf = 1.0;
  if (fflag == 0)
    {
      rf = 0.4 * (double)(N1)/2.0;
      rf2 = rf*rf;
	
      for(i=0;i<N1;i++)
	{
	  for(j=0;j<N2;j++)
	    {
	      for(k=0;k<N3;k++)
		{
		  q[i][j][k] = 1.0;
		  if(i*i+j*j<rf2) q[i][j][k] = qf;
		  if((i-N1/2)*(i-N1/2)+j*j<rf2) q[i][j][k] = qf;
		  if((i-N1)*(i-N1)+j*j<rf2) q[i][j][k] = qf;
		  if(i*i+(j-N2/2)*(j-N2/2)<rf2) q[i][j][k] = qf;
		  if((i-N1/2)*(i-N1/2)+(j-N2/2)*(j-N2/2)<rf2) q[i][j][k] = qf;
		  if((i-N1)*(i-N1)+(j-N2/2)*(j-N2/2)<rf2) q[i][j][k] = qf;
		  if(i*i+(j-N2)*(j-N2)<rf2) q[i][j][k] = qf;
		  if((i-N1/2)*(i-N1/2)+(j-N2)*(j-N2)<rf2) q[i][j][k] = qf;
		  if((i-N1)*(i-N1)+(j-N2)*(j-N2)<rf2) q[i][j][k] = qf;
		  fprintf(of8,"%d %d %d %lf \n", i,j,k,q[i][j][k]);
		}			
	    }
	}
    }
  if (fflag == 1)
    {
      rf = 0.6 * (double)(N1)/2.0;
      rf2 = rf*rf;
		
      for(i=0;i<N1;i++)
	{
	  for(j=0;j<N2;j++)
	    {
	      for(k=0;k<N3;k++)
		{
		  q[i][j][k] = 1.0;
		  if(i*i+j*j<rf2) q[i][j][k] = qf;
		  if((i-N1)*(i-N1)+j*j<rf2) q[i][j][k] = qf;
		  if((i-N1/2)*(i-N1/2)+(j-N2/2)*(j-N2/2)<rf2) q[i][j][k] = qf;
		  if(i*i+(j-N2)*(j-N2)<rf2) q[i][j][k] = qf;
		  if((i-N1)*(i-N1)+(j-N2)*(j-N2)<rf2) q[i][j][k] = qf;
		  fprintf(of8,"%d %d %d %lf \n", i,j,k,q[i][j][k]);
		}			
	    }
	}
    }
	
  return;	
}


void frec( double *fx,double *fy, 
	   double *fz, double d1, double d2, double d3)
{
  int i,j,k, nf;
  // FIXME, make these 1d arrays
	
#ifdef UGPU
#pragma acc data copyout(fx[0:N1*N2*N3],fy[0:N1*N2*N3],fz[0:N1*N2*N3])
  {
#endif


#ifdef UGPU
#pragma acc kernels  
    {
#endif

      for(i=0;i<N1;i++)
	{
	  for(j=0;j<N2;j++)
	    {
	      for(k=0;k<N3;k++)
		{
		  nf = I3(i, j, k);
		  /* frecuency in x */
		  if (i==0) {
		    fx[nf]= 0.0;
		  }
		  if (i >= 1 && i < N1/2 ) {
		    fx[nf]= (double)(i)/(double)(N1)/d1;
		  }
		  if (i >= N1/2) {
		    fx[nf]= ((double)(i)-(double)(N1))/(double)(N1)/d1;
				
		  }
		  /* frecuency in y */
		  if (j==0) {
		    fy[nf]= 0.0;
		  }
		  if (j >= 1 && j < N2/2 ) {
		    fy[nf]= (double)(j)/(double)(N2)/d2;
		  }
		  if (j >= N2/2) {
		    fy[nf]= ((double)(j)-(double)(N2))/(double)(N2)/d2;
		  }				
		  /* frecuency in z */
		  if (k==0) {
		    fz[nf]= 0.0;
		  }
		  if (k >= 1 && k < N3/2 ) {
		    fz[nf]= (double)(k)/(double)(N3)/d3;
		  }	
		  if (k >= N3/2) {
		    fz[nf]= ((double)(k)-(double)(N3))/(double)(N3)/d3;
		  }		
			
		  /*printf("%d %d %d    %lf %lf %lf \n", i, j, k, fx[nf], fy[nf],fz[nf]); 	*/

		}
	    }
	}
#ifdef UGPU 
    }//#pragma acc kernels 
#endif

#ifdef UGPU
  } //#pragma acc data copy(fx[0:N1*N2*N3],fy[0:N1*N2*N3],fz[0:N1*N2*N3])
#endif


  return;
}

#define SWAP(a,b) tempr=(a);(a)=(b);(b)=tempr

void fourn(float data[], int nn[], int ndim, int isign)
{
  int idim;
  unsigned long i1,i2,i3,i2rev,i3rev,ip1,ip2,ip3,ifp1,ifp2;
  unsigned long ibit,k1,k2,n,nprev,nrem,ntot;
  float tempi,tempr;
  double theta,wi,wpi,wpr,wr,wtemp;

  for (ntot=1,idim=1;idim<=ndim;idim++)
    ntot *= nn[idim];
  nprev=1;
  for (idim=ndim;idim>=1;idim--) {
    n=nn[idim];
    nrem=ntot/(n*nprev);
    ip1=nprev << 1;
    ip2=ip1*n;
    ip3=ip2*nrem;
    i2rev=1;
    /*printf("%u, %u, %u, %u\n", ip1, ip2, ip3, nprev);*/
    for (i2=1;i2<=ip2;i2+=ip1) {
      if (i2 < i2rev) {
	/*printf("%u, %u, %u, %u\n", i1, i2, i2rev, ibit);*/
	for (i1=i2;i1<=i2+ip1-2;i1+=2) {
	  /*printf("i1 %u, %u\n", i1, i2);*/
	  for (i3=i1;i3<=ip3;i3+=ip2) {
	    i3rev=i2rev+i3-i2;
	    SWAP(data[i3],data[i3rev]);
	    SWAP(data[i3+1],data[i3rev+1]);
	  }
	}
      }
      ibit=ip2 >> 1;
      /*printf("ibit %u\n", ibit);*/
      while (ibit >= ip1 && i2rev > ibit) {
	i2rev -= ibit;
	ibit >>= 1;
      }
      i2rev += ibit;
    }
    ifp1=ip1;
    while (ifp1 < ip2) {
      ifp2=ifp1 << 1;
      theta=isign*6.28318530717959/(ifp2/ip1);
      wtemp=sin(0.5*theta);
      wpr = -2.0*wtemp*wtemp;
      wpi=sin(theta);
      wr=1.0;
      wi=0.0;
      for (i3=1;i3<=ifp1;i3+=ip1) {
	for (i1=i3;i1<=i3+ip1-2;i1+=2) {
	  for (i2=i1;i2<=ip3;i2+=ifp2) {
	    k1=i2;
	    k2=k1+ifp1;
	    tempr=(float)wr*data[k2]-(float)wi*data[k2+1];
	    tempi=(float)wr*data[k2+1]+(float)wi*data[k2];
	    data[k2]=data[k1]-tempr;
	    data[k2+1]=data[k1+1]-tempi;
	    data[k1] += tempr;
	    data[k1+1] += tempi;
	  }
	}
	wr=(wtemp=wr)*wpr-wi*wpi+wr;
	wi=wi*wpr+wtemp*wpi+wi;
      }
      ifp1=ifp2;
    }
    nprev *= n;
    /*printf("nprev %u, %u\n", nprev, n);*/
  }
}


/*
  Recursive definition of determinate using expansion by minors.
*/
double Determinant(double **a,int n)
{
  int i,j,j1,j2;
  double det = 0;
  double **m = NULL;
	
  if (n < 1) { /* Error */
    printf("Error in passing matrix size, n= %d \n",n);
    exit(1);
  } else if (n == 1) { /* Shouldn't get used */
    det = a[0][0];
  } else if (n == 2) {
    det = a[0][0] * a[1][1] - a[1][0] * a[0][1];
  } else {
    det = 0;
    for (j1=0;j1<n;j1++) {
      m = malloc((n-1)*sizeof(double *));
      for (i=0;i<n-1;i++)
	m[i] = malloc((n-1)*sizeof(double));
      for (i=1;i<n;i++) {
	j2 = 0;
	for (j=0;j<n;j++) {
	  if (j == j1)
	    continue;
	  m[i-1][j2] = a[i][j];
	  j2++;
	}
      }
      det += pow(-1.0,j1+2.0) * a[0][j1] * Determinant(m,n-1);
      for (i=0;i<n-1;i++)
	free(m[i]);
      free(m);
    }
  }
  return(det);
}

/*
  Find the cofactor matrix of a square matrix
*/
void CoFactor(double **a,int n,double **b)
{
  int i,j,ii,jj,i1,j1;
  double det;
  double **c;
	
  c = malloc((n-1)*sizeof(double *));
  for (i=0;i<n-1;i++)
    c[i] = malloc((n-1)*sizeof(double));
	
  for (j=0;j<n;j++) {
    for (i=0;i<n;i++) {
			
      /* Form the adjoint a_ij */
      i1 = 0;
      for (ii=0;ii<n;ii++) {
	if (ii == i)
	  continue;
	j1 = 0;
	for (jj=0;jj<n;jj++) {
	  if (jj == j)
	    continue;
	  c[i1][j1] = a[ii][jj];
	  j1++;
	}
	i1++;
      }
			
      /* Calculate the determinate */
      det = Determinant(c,n-1);
			
      /* Fill in the elements of the cofactor */
      b[i][j] = pow(-1.0,i+j+2.0) * det;
    }
  }
  for (i=0;i<n-1;i++)
    free(c[i]);
  free(c);
}

/*
  Transpose of a square matrix, do it in place
*/
void Transpose(double **a,int n)
{
  int i,j;
  double tmp;
	
  for (i=1;i<n;i++) {
    for (j=0;j<i;j++) {
      tmp = a[i][j];
      a[i][j] = a[j][i];
      a[j][i] = tmp;
    }
  }
}

void v2matsig(double sig[6], double sig2[ND][ND])
{
  int i,j;
	
  sig2[0][0] = sig[0];
  sig2[1][1] = sig[1];
  sig2[2][2] = sig[2];
  sig2[1][2] = sig[3];
  sig2[0][2] = sig[4];
  sig2[0][1] = sig[5];
	
  for(i=0;i<ND;i++)
    for(j=0;j<i;j++)
      {
	sig2[i][j] = sig2[j][i];
      }
}

int Indmat2v(int i, int j)
{
  if(i==0)
    {
      if(j==0) return 0;
      else if(j==1) return 5;
      else if(j==2) return 4;
      else{
	printf("Error in Indmat2v, input indice out of range,j= %d.\n",j);
	exit(1);
      }
    }
  else if(i==1)
    {
      if(j==0) return 5;
      else if(j==1) return 1;
      else if(j==2) return 3;
      else{
	printf("Error in Indmat2v, input indice out of range,j= %d.\n",j);
	exit(1);
      }
    }
	
  else if(i==2)
    {
      if(j==0) return 4;
      else if(j==1) return 3;
      else if(j==2) return 2;
      else{
	printf("Error in Indmat2v, input indice out of range,j= %d.\n",j);
	exit(1);
      }
    }
  else{
    printf("Error in Indmat2v, input indice out of range,i= %d.\n",i);
    exit(1);
  }
}

void Indv2mat(int i, int ind[2])
{
  if(i==0)
    {
      ind[0]=0;
      ind[1]=0;
    }
  else if(i==1)
    {
      ind[0]=1;
      ind[1]=1;
    }
	
  else if(i==2)
    {
      ind[0]=2;
      ind[1]=2;
    }
  else if(i==3)
    {
      ind[0]=1;
      ind[1]=2;
    }
  else if(i==4)
    {
      ind[0]=0;
      ind[1]=2;
    }
  else if(i==5)
    {
      ind[0]=0;
      ind[1]=1;
    }
  else{
    printf("Error in Indv2mat, input indice out of range, %d.\n",i);
    exit(1);
  }
}

float Energy_calculation(double *fx, double *fy, double *fz, double eps[NS][ND][ND],
			 double epsv[NV][ND][ND],double C11, double C12, double C44,
			 float *data,double interface_n[ND],
			 int ppoint_x, int ppoint_y, int ppoint_z)
{
#define 	DELTA(i, j)   ((i==j)?1:0)
#define		DELTA4(i,j,k,l) (((i==j) && (j==k) && (k==l))?1:0)
  
  int i,j,k,l,m,n, u, v, k1,k2,k3,nfreq,index,u_p[NS],v_p[NS];
  double C[ND][ND][ND][ND];
  static float strainv_real[ND][ND][N1][N2][N3];
  static float strainv_imag[ND][ND][N1][N2][N3];
  // static  float strainp_real[NS][ND][ND][N1][N2][N3];
  // static  float strainp_imag[NS][ND][ND][N1][N2][N3];
  float G[ND][ND];
  double fk[ND];
  double ll, mu, fk2, fk4;
  float en;
  static	float A[N1][N2][N3][ND][ND][ND][ND];
  static    float E_real[N1][N2][N3];
  //    float E_imag[N1][N2][N3];

  static double acc_time;
  static int acc_n;
  double tmstart = WTime();
    
#ifdef GPU 
#pragma acc data copyin(fx[0:N1*N2*N3],fy[0:N1*N2*N3],fz[0:N1*N2*N3])  create(A) /*create(A)*/ create(E_real) /*copyin(data[0: 2*(NSV)*(N1)*(N2)*(N3)])*/  /*create(C)*/    
  //#pragma acc data /*create(strainv_real[0:NS][0:ND][0:ND][0:N1][0:N2][0:N3],strainv_imag[0:NS][0:ND][0:ND][0:N1][0:N2][0:N3])*/ /*copyin(eps[0:NS][0:ND][0:ND])  create(G[0:ND][0:ND])*/ 
  /*#pragma acc data copyout(A[0:N1][0:N2][0:N3][0:ND][0:ND][0:ND][0:ND])*/
  /*#pragma acc data copy(strainp_imag[0:NS][0:ND][0:ND][0:N1][0:N2][0:N3],strainp_real[0:NS][0:ND][0:ND][0:N1][0:N2][0:N3])
    #pragma acc data copyin(E_real[0:N1][0:N2][0:N3],interface_n[0:ND]) copyin(u_p[0:NS],v_p[0:NS])*/

  {
#endif
    
    
    
#ifdef UGPU 
#pragma acc kernels 
  {
#endif 
    
  /*    for(k1=0;k1<N1;k1++){  
	for(k3=0;k3<N3;k3++){	  
	for(k2=0;k2<N2;k2++){
	for (m=0; m<ND; m++) {			  
	for (n=0; n<ND; n++) {	  
	for (u=0; u<ND; u++) {
	for (v=0; v<ND; v++) {
	A[k1][k2][k3][m][n][u][v] = 0.0;
	}
	}
	}
	}
	}
	}
	}
	   
  */   
	       
#ifdef UGPU 
}    //#pragma acc kernels 
#endif    
    
    
    
     
    
#ifdef UGPU 
#pragma acc kernels 
  {
#endif  
  for (m=0; m<NS; m++) {
  for (i=0; i<ND; i++) {
  for (j=0; j<ND; j++) {
  if (eps[m][i][j]!=0.0) {
  u_p[m]=i;
  v_p[m]=j;
}
}
}
}
#ifdef UGPU 
}    //#pragma acc kernels 
#endif    
       
#ifdef UGPU 
#pragma acc kernels 
  {
#endif    
  for (m=0; m<NS; m++) {
  for (u=0; u<ND; u++) {
  for (v=0; v<ND; v++) {
  for (i=0; i<N1; i++) {
  for (j=0; j<N2; j++) {
  for (k=0; k<N3; k++) {
  nfreq = k + j*N3 + i*N2*N3;
  index = 2*(nfreq + m*N1*N2*N3)+1;
  if (u==u_p[m]&&v==v_p[m]) {
  //strainp_real[m][u][v][i][j][k]=data[index];
  //strainp_imag[m][u][v][i][j][k]=data[index+1];
}
  else{
  //strainp_real[m][u][v][i][j][k]=0.0;
  //strainp_imag[m][u][v][i][j][k]=0.0;
}
                            
}
}
}
}
}
}
#ifdef UGPU 
}    //#pragma acc kernels 
#endif    
    
    
  //*****SAFE on Multicore But fails on GPU!!!   
#ifdef UGPU 
#pragma acc kernels 
  {
#pragma acc loop        
#endif 
	 
    for (u=0; u<ND; u++) {
      for (v=0; v<ND; v++) {
	for (i=0; i<N1; i++) {
	  for (j=0; j<N2; j++) {
	    for (k=0; k<N3; k++) {
	      nfreq = k + j*N3 + i*N2*N3;
	      index = 2*(nfreq + (NS+u*ND+v)*N1*N2*N3);
	      strainv_real[u][v][i][j][k] = data[index  ];
	      strainv_imag[u][v][i][j][k] = data[index+1];
	    }
	  }
	}
      }
    }
#ifdef UGPU 
}    //#pragma acc kernels 
#endif   
  //*****SAFE on Multicore But fails on GPU!!!  



	
#ifdef UGPU 
#pragma acc kernels 
  {
#endif
  mu = C44;
  ll = C12;
  //int mup= C11-C12-2*C44;
  //	struct timespec now, tmstart;
  //        clock_gettime(CLOCK_REALTIME, &tmstart);
#ifdef UGPU 
}    //#pragma acc kernels 
#endif   
	
	
	
	
  /* set Cijkl*/
  //#ifdef UGPU
  //#pragma acc /*data create(C,A)*/ /*present(fx[0:N1*N2*N3],fy[0:N1*N2*N3],fz[0:N1*N2*N3])*/ 
  // {//#pragma acc data
  //   #endif
        
        
        
#ifdef UGPU 
#pragma acc kernels  
  {
#pragma acc loop collapse(2)  
#endif
  for (i=0; i<ND; i++) {
  for (j=0; j<ND; j++) {
  G[i][j]=0.0;	
  for (k=0; k<ND; k++) {
  for (m=0; m<ND; m++) {
				
  C[i][j][k][m] = mu * (DELTA(i,k)*DELTA(j,m)+DELTA(i,m)*DELTA(j,k))+ll*DELTA(i,j)*DELTA(k,m);
					
}
}
}
} 
#ifdef UGPU  
}//#pragma acc kernels 
#endif
	
#ifdef GPU
#pragma acc kernels  
  {
#pragma acc loop  collapse(3) gang(1024) /*gang vector(32)*/   /*gang worker(32)*/  private(k1,k2,k3,m,n,u,v,i,j,k,l,fk,nfreq,fk4,G)/*tile(2,1,2)*/ 
#endif
    for(k1=0;k1<N1;k1++)  
      {
	//#pragma acc loop private(k1,k2,k3,m,n,u,v,i,j,k,l,G,fk,nfreq,fk4,A)  
#ifdef GPUU
#pragma acc loop seq private(k1,k2,k3,m,n,u,v,i,j,k,l,fk,nfreq,fk4,G) /*collapse(3)*/ 
#endif
	for(k3=0;k3<N3;k3++)
	  {
	    //#pragma acc loop private(k1,k2,k3,m,n,u,v,i,j,k,l,G,fk,nfreq,fk4,A)  
#ifdef GPUU 
#pragma acc loop vector/*worker(32)*/ private(k1,k2,k3,m,n,u,v,i,j,k,l,fk,nfreq,fk4,G)/*tile(2,1,2)*/
#endif
			
			  
	    for(k2=0;k2<N2;k2++) {
				 
	      nfreq = k3+(k2)*N3+(k1)*N3*N2;
	      fk[0] = fx[nfreq];
	      fk[1] = fy[nfreq];
	      fk[2] = fz[nfreq];
	      fk2 = fk[0]*fk[0]+fk[1]*fk[1]+fk[2]*fk[2];
	      fk4 = fk2*fk2;
			 	
	      if(fk2>0)     		
		{
			       
		  //	{
		  /*	//#pragma acc loop collapse(4)
			#ifdef GPUU
			#pragma acc loop independent private(G,fk,nfreq,fk4,A)  
			#endif
			for (m=0; m<ND; m++) {
			#ifdef GPUU
			#pragma acc loop independent private(G,fk,nfreq,fk4,A)    
			#endif
			for (n=0; n<ND; n++) {
			#ifdef GPUU
			#pragma acc loop independent private(G,fk,nfreq,fk4,A)    
			#endif		  
			for (u=0; u<ND; u++) {
			#ifdef GPUU
			#pragma acc loop independent private(G,fk,nfreq,fk4,A)   
			#endif	
			for (v=0; v<ND; v++) {
				 				  
			A[k1][k2][k3][m][n][u][v] = 0.0;
			}
			}
			}
			}
		  */	
					
		  /*	#ifdef GPU
			#pragma acc loop independent private(G,fk,nfreq,fk4,A)  
			#endif
			for (m=0; m<ND; m++) {
			#ifdef GPU
			#pragma acc loop independent private(G,fk,nfreq,fk4,A)    
			#endif
			for (n=0; n<ND; n++) {
			#ifdef GPU
			#pragma acc loop independent private(G,fk,nfreq,fk4,A)  
			#endif		  
			for (u=0; u<ND; u++) {
			#ifdef GPU
			#pragma acc loop independent private(G,fk,nfreq,fk4,A)   
			#endif	
			for (v=0; v<ND; v++) {
			#ifdef GPU
			#pragma acc loop independent private(G,fk,nfreq,fk4,A)  
			#endif	
					
					
			for	(i=0; i<ND; i++) {
			#ifdef GPU
			#pragma acc loop independent private(G,fk,nfreq,fk4,A)   
			#endif						  
			for (j=0; j<ND; j++) {
			#ifdef GPU
			#pragma acc loop independent private(G,fk,nfreq,fk4,A)   
			#endif							  
			for (k=0; k<ND; k++) {
			// for isotropic material
			G[k][i] = DELTA(i,k)/mu/fk2 - (ll+mu)/mu/(ll+2*mu)*fk[i]*fk[k]/fk4;
						
						
			}
			}
			}
			}
			}
			}
			}
		  */	
				 	
#ifdef GPU
#pragma acc loop vector private(G) collapse(4)    
#endif
		  for (m=0; m<ND; m++) {
#ifdef GPUU
#pragma acc loop private(A,G)  
#endif
		    for (n=0; n<ND; n++) {
#ifdef GPUU
#pragma acc loop private(A,G)   
#endif		  
		      for (u=0; u<ND; u++) {
#ifdef GPUU
#pragma acc loop private(A,G)  
#endif	
			for (v=0; v<ND; v++) {
				
			  A[k1][k2][k3][m][n][u][v] = 0.0;
#ifdef  GPU 
#pragma acc loop private(G)  collapse(3)   
#endif					
					
			  for	(i=0; i<ND; i++) {
#ifdef GPUU
#pragma acc loop private(A,G)  
#endif						  
			    for (j=0; j<ND; j++) {
#ifdef UGPU
#pragma acc loop private(G) 
#endif							  
			      for (k=0; k<ND; k++) {
											
											  
				G[k][i] = DELTA(i,k)/mu/fk2 - (ll+mu)/mu/(ll+2*mu)*fk[i]*fk[k]/fk4;  
											  
#ifdef  GPU
#pragma acc loop    
#endif	
					
				for	(l=0; l<ND; l++) {
				  A[k1][k2][k3][m][n][u][v] = A[k1][k2][k3][m][n][u][v] - C[k][l][u][v]*C[i][j][m][n]*G[k][i]*fk[j]*fk[l] ;
				}
			      }
			    }
			  }
			  A[k1][k2][k3][m][n][u][v] = A[k1][k2][k3][m][n][u][v]+C[m][n][u][v];
			}
		      }
		    }
		  }
					
		} /*if fk2 */
	    
	    }
	  }
      }
    
  
  
#ifdef GPU
  }//#pragma acc kernels
#endif

//*****SAFE on Multicore But fails on GPU!!!  
  

   
#ifdef GPU  
#pragma acc kernels  
  {
      
#endif 
   
#ifdef  GPU
#pragma acc loop gang  collapse(3)
#endif 
    for (k1=0; k1<N1; k1++) {
      for (k3=0;k3<N3;k3++) {
	for (k2=0;k2<N2;k2++) {
	  
	  E_real[k1][k2][k3]=0.0;
#ifdef GPU
#pragma acc loop vector  collapse(4) 
#endif	     
	  for (u=0; u<ND; u++) {
	    for (v=0; v<ND; v++) {
	      for (m=0; m<ND; m++) {
		for (n=0; n<ND; n++) {
	  
		  E_real[k1][k2][k3]+=0.5*A[k1][k2][k3][m][n][u][v]*(strainv_real[m][n][k1][k2][k3]*strainv_real[u][v][k1][k2][k3]+strainv_imag[m][n][k1][k2][k3]*strainv_imag[u][v][k1][k2][k3]);
		}
	      }
	    }
	  }
  
	}
      }
    }
#ifdef  GPU 
  } //#pragma acc kernels
#endif
//*****SAFE on Multicore But fails on GPU!!!  

   

 
    
#ifdef GPU
#pragma acc kernels 
  {
#endif
   
    en = 0.;
  
#ifdef GPU  
#pragma acc loop collapse(3)
#endif
    for (i=0; i<N1; i++) {
        
      for (k=0; k<N3; k++) {
	for (j=0; j<N2; j++) {
	  if (fabs(interface_n[0]*(i-ppoint_x) + interface_n[1]*(j-ppoint_y) + interface_n[2]*(k-ppoint_z))<=1.0&&(i==ppoint_x)) {
	    en += E_real[i][j][k];
	  }
	}
      }
    }
    en = en/mu;
#ifdef GPU
  }//#pragma acc kernels
#endif
    
      
    
    
#ifdef GPU 
} //#pragma acc data  
#endif
    
double now = WTime();
acc_time += now - tmstart;
acc_n++;

//printf("avg energy_calculation avg time: %g total time %g\n", acc_time / acc_n, acc_time);
    
printf("en %g\n", en);
   
return(en);
}


double plasticevolv(double * xi_bc,double * xi,double CD2[NS],float uq2,float *data2,double Asf2[NS],double tau[N1][N2][N3][NS],double dslip2[NS],float *datag,float *data,double * gamma1,int nsize,double a_f, double a_s, double C44, int it_plastic)
{
  int isa,is, i, j, k;
  double gamma;
  gamma = 0.0;
  for(is=0;is<NS;is++){
    gamma1[is] = 0.0;
  }
  for (isa=0; isa<NS; isa++) {
    for (i=0; i<N1; i++) {
      for (j=0; j<N2; j++) {
	for (k=0; k<N3; k++) {
	  if(XI_BC(i,j,k, isa, 0)==0){//evolve bulk phase field
	    if(0){	//does not apply gradient term
	      XI(i,j,k, isa, 0) -= CD2[isa]*(uq2*DATA2(i,j,k, isa, 0)/nsize+Asf2[isa]*pi*sin(2.0*pi*XI(i,j,k, isa, 0))-tau[i][j][k][isa]/dslip2[isa]+DATAG(i,j,k, isa, 0)/nsize);      /*(k+1)+(j+1)*10.0+(i+1)*100.0*/
	      XI(i,j,k, isa, 1) -= CD2[isa]*(uq2*DATA2(i,j,k, isa, 1)/nsize + DATAG(i,j,k, isa, 0)/nsize); // FIXME? real component of DATAG is correct?
	    }
	    else{
	      XI(i,j,k, isa, 0) -= CD2[isa]*(uq2*DATA2(i,j,k, isa, 0)/nsize+Asf2[isa]*pi*sin(2.0*pi*XI(i,j,k, isa, 0))-tau[i][j][k][isa]/dslip2[isa]);      /*(k+1)+(j+1)*10.0+(i+1)*100.0*/
	      XI(i,j,k, isa, 1) -= uq2*CD2[isa]*(DATA2(i,j,k, isa, 1)/nsize);
	    }
	  }
	  gamma += XI(i,j,k, isa, 0)/nsize;
	  gamma1[isa] += XI(i,j,k, isa, 0)/nsize;
	}//end i
      }//end j
    }//end k
  }// end isa loop

  return gamma;
    
}

void calculateD4int(double a_f, double a_s, double Cm[NMAT][3], double theta1[NMAT][ND][ND], double xb[NS][ND], double xn[NS][ND], double interface_n[ND],double *D00, double *D01, double *D10, double *D11,double *lam1, double *lam2, double *stressthreshold)
{
#define 	DELTA(i, j)   ((i==j)?1:0)
#define		DELTA4(i,j,k,l) (((i==j) && (j==k) && (k==l))?1:0)
  double mu,ll,mup, C11,C12,C44,c[ND][ND][ND][ND],c_rotate[NMAT][ND][ND][ND][ND]; // here can only deal with NMAT == 2
  int i,j,k,l,m,n,u,v,isa0,isa1,nam;
  double interface_n_rotate[ND],b0[ND],b1[ND],m0[ND],m1[ND];
  double D10_t,D11_t,D00_t,D01_t;
  double cosb0n,cosb1n,cos2theta;
  double energybarrier,delta;
  double lam1_temp,lam2_temp;
  *(D10) = 0.0;
  *(D11) = 0.0;
  *(D00) = 0.0;
  *(D01) = 0.0;
  D00_t = 0.0;
  D11_t = 0.0;
  D01_t = 0.0;
  D10_t = 0.0;
    
  isa0 = 0;
  isa1 = 2;
    
  for (i=0; i<ND; i++) {
    b0[i] = 0.0;
    b1[i] = 0.0;
    m0[i] = 0.0;
    m1[i] = 0.0;
    interface_n_rotate[i] = interface_n[i]; // interface_n is already in global coordinates, do not need to rotate
    for (j=0; j<ND; j++) {
      b0[i] += xb[isa0][j]*theta1[0][i][j];
      b1[i] += xb[isa1][j]*theta1[1][i][j];
      m0[i] += xn[isa0][j]*theta1[0][i][j];
      m1[i] += xn[isa1][j]*theta1[1][i][j];
            
    }
  }
    
  for (i=0; i<ND; i++) {
    printf("in calculateD4int check:  in material 0 coordinates:\n");
    printf("b0[%d] = %f, b1[%d] = %f, interface_n_rotate[%d] = %f\n",i,b0[i],i,b1[i],i,interface_n_rotate[i]);
  }
    
   
    
  for (nam=0; nam<NMAT; nam++) {//NMAT here equals 2
    C11=Cm[nam][0];
    C12=Cm[nam][1];
    C44=Cm[nam][2];
    mu = C44;
    ll = C12;
    mup= C11-C12-2*C44;
        
    for (i=0; i<ND; i++) {
      for (j=0; j<ND; j++) {
	for (k=0; k<ND; k++) {
	  for (l=0; l<ND; l++) {
	    c[i][j][k][l] = mu * (DELTA(i,k)*DELTA(j,l)+DELTA(i,l)*DELTA(j,k))+ll*DELTA(i,j)*DELTA(k,l)+mup*DELTA4(i,j,k,l);
	  }
	}
      }
    }
        
    for (i=0; i<ND; i++) {
      for (j=0; j<ND; j++) {
	for (k=0; k<ND; k++) {
	  for (l=0; l<ND; l++) {
	    c_rotate[nam][i][j][k][l] = 0.0;
	    for (m=0; m<ND; m++) {
	      for (n=0; n<ND; n++) {
		for (u=0; u<ND; u++) {
		  for (v=0; v<ND; v++) {
		    c_rotate[nam][i][j][k][l] += c[m][n][u][v]*theta1[nam][i][m]*theta1[nam][j][n]*theta1[nam][k][u]*theta1[nam][l][v];
		  }
		}
	      }
	    }
	    if (MT==1) {
	      c_rotate[nam][i][j][k][l] = c[i][j][k][l];
	    }
	  }
	}
      }
    }
        
        
  }
    
    
  for (i=0; i<ND; i++) {
    for (j=0; j<ND; j++) {
      for (k=0; k<ND; k++) {
	for (l=0; l<ND; l++) {
	  D00_t += a_s*c_rotate[0][i][j][k][l]*b0[i]*interface_n_rotate[j]*0.5*(b0[k]*m0[l]+b0[l]*m0[k]);
	  D11_t += a_f*c_rotate[1][i][j][k][l]*b1[i]*interface_n_rotate[j]*0.5*(b1[k]*m1[l]+b1[l]*m1[k]);
	  D10_t += a_s*c_rotate[1][i][j][k][l]*b0[i]*interface_n_rotate[j]*0.5*(b1[k]*m1[l]+b1[l]*m1[k]);
	  D01_t += a_f*c_rotate[0][i][j][k][l]*b1[i]*interface_n_rotate[j]*0.5*(b0[k]*m0[l]+b0[l]*m0[k]);
	}
      }
    }
  }
    
  printf("check D coefficients: D10 = %e, D01 = %e, D00 = %e, D11 = %e\n",D10_t,D01_t,D00_t,D11_t);
    
  *(D00) = D00_t;
  *(D11) = D11_t;
  *(D01) = D01_t;
  *(D10) = D10_t;
    
  cosb0n = sqrt(2)/2.;
  cosb1n = sqrt(2)/2.;
  cos2theta = 1.0; // 2(b0*b1)^2-1
    
  delta = (D10_t+D01_t)*(D10_t+D01_t)-4*D00_t*D11_t;
  energybarrier = fabs(delta)/4./D11_t;
  energybarrier =(D10_t+D01_t)*(D10_t+D01_t)/4./D11_t - D00_t;
    
  lam1_temp = (-D10_t-D01_t-sqrt(delta))/(-2*D11_t);
  lam2_temp = (-D10_t-D01_t+sqrt(delta))/(-2*D11_t);
    
  if (fabs(lam1_temp - a_s/a_f)<=0.001) {
    *(lam1) = lam1_temp;
    *(lam2) = lam2_temp;
  }else{
    *(lam1) = lam2_temp;
    *(lam2) = lam1_temp;
  }
    
    
   
  *(stressthreshold) = energybarrier/(a_s/2./cosb0n + a_f/2./cosb1n*cos2theta)/Cm[0][2];
  //*(stressthreshold) = 0.0916088843271;//mark: Cu/Ni  0.000100121503783;//mark:  Au/Ag  0.000100366899625;//mark: Ag/Au  0.0893250063245;//mark: Ni/Cu  0.0887352985832;//Al/Pt  0.0858870050484;//Pt/Al   0.0314379430673;//mark: Au/Al     0.0312067817212;//mark: Al/Au
    
  printf("check lambda1 and lambda2 and stressthreshold: lam1 = %e, lam2 = %e all xi0/xi1 and stressthreshold = xi_0* %f\n",*lam1,*lam2,*stressthreshold);
    
  printf("energy barrier = %lf   delta = %lf\n",energybarrier,delta);
    
  return;
}


void interfacevolv(double * xi, double * penetrationstress, double D00, double D01, double D10, double D11,double lam1,double lam2,double stressthreshold,int *checkpass,int border,int ppoint_x, int ppoint_y, int ppoint_z, double C44, double a_s, double a_f, double * penetrationstress2, int *it_checkEbarrier,FILE * ofcheckEbarrier)
{
  int is_p0,is_p1,is_r0,is_r1;
  int k,y;
  double xi0_ave[N2], xi0_r[N2];//only valid for N2=2
  double stressthreshold_final[N2];
    
  is_r0 = 0;
  is_r1 = 2;
  is_p0 = 1;
  is_p1 = 3;
  xi0_ave[0] = xi[2*(ppoint_z+ppoint_y*N3+(ppoint_x)*N3*N2+is_p0*N1*N2*N3)];
  xi0_ave[1] = xi[2*(ppoint_z+(ppoint_y+1)*N3+(ppoint_x)*N3*N2+is_p0*N1*N2*N3)];
  xi0_r[0] = xi[2*(ppoint_z+ppoint_y*N3+(ppoint_x)*N3*N2+is_r0*N1*N2*N3)];
  xi0_r[1] = xi[2*(ppoint_z+(ppoint_y+1)*N3+(ppoint_x)*N3*N2+is_r0*N1*N2*N3)];
  /* double xi1_r[N2]; */
  /* xi1_r[0] = xi[2*(ppoint_z+1+ppoint_y*N3+(ppoint_x)*N3*N2+is_r1*N1*N2*N3)]; */
  /* xi1_r[1] = xi[2*(ppoint_z+1+(ppoint_y+1)*N3+(ppoint_x)*N3*N2+is_r1*N1*N2*N3)]; */
   
  stressthreshold_final[0] = stressthreshold * xi0_ave[0] * xi0_ave[0];
  stressthreshold_final[1] = stressthreshold * xi0_ave[1] * xi0_ave[1];
  //printf("interfacevolv, check stress threshold 1:  %lf  %lf\n",stressthreshold_final[0],stressthreshold_final[1]);
  stressthreshold_final[0] = stressthreshold * (xi0_ave[0]+xi0_r[0]) * (xi0_ave[0]+xi0_r[0]);// only for slip through residual
  stressthreshold_final[1] = stressthreshold * (xi0_ave[1]+xi0_r[1]) * (xi0_ave[1]+xi0_r[1]);// only for slip through residual
  //printf("interfacevolv, check stress threshold 2:  %lf  %lf\n",stressthreshold_final[0],stressthreshold_final[1]);
  //printf("xi0_ave_y0 = %f\n",xi0_ave[0]);
  //printf("xi0_ave_y1 = %f\n",xi0_ave[1]);
  //printf("xi0_r_y0 = %f\n",xi0_r[0]);
  //printf("xi0_r_y1 = %f\n",xi0_r[1]);
  //printf("xi1_r_y0 = %f\n",xi1_r[0]);
  //printf("xi1_r_y1 = %f\n",xi1_r[1]);
  for (y=0; y<N2; y++) {
    //printf("penetration point resolved stress is %e at x=%d ,y=%d and z=%d\n",penetrationstress[y],ppoint_x,y,ppoint_z);
    //printf("stress threshold for x=%d ,y=%d and z=%d is: %f\n",ppoint_x,y,ppoint_z,stressthreshold_final[y]);
  }
    
    
  for (y=0; y<N2; y++) {// only valid for N2=2
    if (xi0_ave[y] >=1.0) {
      if (penetrationstress[y] >= stressthreshold_final[y]) {
	XI(ppoint_x, y, ppoint_x, is_p1, 0) =
	  (XI(ppoint_x, y, ppoint_z, is_p0, 0) + XI(ppoint_x, y, ppoint_z, is_r0, 0))*lam2
	  -XI(ppoint_x, y, ppoint_z+1, is_r1, 0);
	*checkpass = 0;
	printf("in interfacevolv: pass at y = %d!\n",y);
      } else {
	if ((*checkpass)==1) {
	  XI(ppoint_x, y, ppoint_z+1, is_p1, 0) =
	    (XI(ppoint_x, y, ppoint_z, is_p0, 0) + XI(ppoint_x, y, ppoint_z, is_r0, 0))*lam1
	    -XI(ppoint_x, y, ppoint_z+1, is_r1, 0);
	  for (k = ppoint_z+2; k<N3; k++) {
	    //mark: right now for xi1 in material 1,isa=2=is_p1 and i=N1/2=ppoint_x
	    XI(ppoint_x, y, k, is_p1, 0) = 0.0;
	  }
	  printf("in interfacevolv: not pass at y = %d:(\n",y);
	}
      }
            
      if ((*checkpass) == 0) {
	XI(ppoint_x, y, ppoint_z+1, is_p1, 0) =
	  (XI(ppoint_x, y, ppoint_z, is_p0, 0) + XI(ppoint_x, y, ppoint_z, is_r0, 0))*lam2
	  -XI(ppoint_x, y, ppoint_z+1, is_r1, 0);
	printf("in interfacevolv: pass! And leave a residual! at y = %d\n",y);
      }
    }
        
        
    if ((*checkpass)==1) {
      for (k = ppoint_z+2; k<N3; k++) {
	//mark: right now for xi1 in material 1,isa=2=is_p1 and i=N1/2=ppoint_x
	XI(ppoint_x, y, k, is_p1, 0) = 0.;
      }
      printf("in interfacevolv: not pass :( at y = %d\n",y);
    }
  }
    
  // E = 0.0;
  // if ((*checkpass)==1) {
  //     for (y=0; y<N2; y++) {
  //         E += (xi[2*(ppoint_z+y*N3+(ppoint_x)*N3*N2+is_p0*N1*N2*N3)]+xi[2*(ppoint_z+y*N3+(ppoint_x)*N3*N2+is_r0*N1*N2*N3)])*(xi[2*(ppoint_z+y*N3+(ppoint_x)*N3*N2+is_p0*N1*N2*N3)]+xi[2*(ppoint_z+y*N3+(ppoint_x)*N3*N2+is_r0*N1*N2*N3)])*fabs((D10+D01)*(D10+D01)-4*D11*D00)/4./D11+penetrationstress[y]*C44*a_s*xi[2*(ppoint_z+y*N3+(ppoint_x)*N3*N2+is_p0*N1*N2*N3)]-penetrationstress2[y]*C44*a_f*xi[2*(ppoint_z+1+y*N3+(ppoint_x)*N3*N2+is_p1*N1*N2*N3)];
  //         printf("check Ebarrier Int term by term:  %lf,%lf,%lf,%lf,%lf,%lf\n",xi[2*(ppoint_z+y*N3+(ppoint_x)*N3*N2+is_p0*N1*N2*N3)]+xi[2*(ppoint_z+y*N3+(ppoint_x)*N3*N2+is_r0*N1*N2*N3)],fabs((D10+D01)*(D10+D01)-4*D11*D00)/4./D11,penetrationstress[y],xi[2*(ppoint_z+y*N3+(ppoint_x)*N3*N2+is_p0*N1*N2*N3)],penetrationstress2[y],xi[2*(ppoint_z+1+y*N3+(ppoint_x)*N3*N2+is_p1*N1*N2*N3)]);
  //     }
  // fprintf(ofcheckEbarrier, "%d   %lf\n",*(it_checkEbarrier),E);
  // *(it_checkEbarrier) = *(it_checkEbarrier) + 1;
  //  }
}


int plasticconverge(double gamma,double gammalast,int it_plastic,int * testplastic,int *pcountgamma)
{
  int temp_test, flagpevolv,i;
  flagpevolv = 1;
  //mark begin of countgamma
  if (gamma != 0) {
    if(((((gamma-gammalast)/gamma)<0?-(gamma-gammalast)/gamma:(gamma-gammalast)/gamma) < 1E-8)||((gamma>0?gamma:-gamma)<1E-12)){
      testplastic[it_plastic] = 1;
      if (it_plastic>=29) {
	temp_test = 0;
	for (i=0; i<30; i++) {
	  temp_test = temp_test+testplastic[it_plastic-i];
	}
	if (temp_test==30) {
	  (*pcountgamma) = (*pcountgamma)+1;
	}
	else{
	  if ((*pcountgamma)!=0) {
	    (*pcountgamma) = 0;
	  }
	}
      }
      printf("(gamma-gammalast)/gamma = %e     %d\n",(gamma-gammalast)/gamma,*pcountgamma);
    }
  } else {
    if((((gamma-gammalast)<0?-(gamma-gammalast):(gamma-gammalast)) < 1E-15)||((gamma>0?gamma:-gamma)<1E-14)){
      testplastic[it_plastic] = 1;
      if (it_plastic>=29) {
	temp_test = 0;
	for (i=0; i<30; i++) {
	  temp_test = temp_test+testplastic[it_plastic-i];
	}
	if (temp_test==30) {
	  (*pcountgamma) = (*pcountgamma)+1;
	}
	else{
	  if ((*pcountgamma)!=0) {
	    (*pcountgamma) = 0;
	  }
	}
      }
      printf("gamma-gammalast = %e     %d\n",gamma-gammalast,*pcountgamma);
    }
  }
        
  if((*pcountgamma)==5){

    (*pcountgamma)= (*pcountgamma)+1;
    flagpevolv = 0;
  }
  //mark end of countgamma
        
  return flagpevolv;
}



float minimum(float a[9])
{
  int i,j;
  float temp;
  temp = a[0];
  j=0;
  for (i=1; i<9; i++) {
    if (temp > a[i]) {
      temp = a[i];
      j=i;
    }
  }
  return(a[j]);
}
float maximum(float a[9])
{
  int i,j;
  float temp;
  temp = a[0];
  j=0;
  for (i=1; i<9; i++) {
    if (temp < a[i]) {
      temp = a[i];
      j=i;
    }
  }
  return(a[j]);
}

//must be put after setMat

void setstress(double sigma[N1][N2][N3][ND][ND],double a_s, double a_f,double C[NMAT][3],int border,int ppoint_x, int ppoint_y, int ppoint_z, double interface_n[ND])
{
  
  int i,j,k,u,v,m,n,p,q;
  double E[NMAT],miu[NMAT],C_effective[NMAT],stressmisfit_max[NMAT];//0 is substrate, 1 is film
  double hc;
  double distance;
  double rotate_100_interface[ND][ND],sigma_temp[ND][ND];
  double maxreshear;
  
  maxreshear = 0.0;
  
  
  // {100}interface {111}slip plane [110]slip direction b_edge as +z axis
  rotate_100_interface[0][0] = sqrt(3.)/3.;
  rotate_100_interface[0][1] = sqrt(3.)/3.;
  rotate_100_interface[0][2] = sqrt(3)/3.;
  rotate_100_interface[1][0] = -sqrt(2.)/2.;
  rotate_100_interface[1][1] = sqrt(2.)/2.;
  rotate_100_interface[1][2] = 0.0;
  rotate_100_interface[2][0] = -sqrt(6)/6.;
  rotate_100_interface[2][1] = -sqrt(6)/6.;
  rotate_100_interface[2][2] = sqrt(6)/3.;
  
  for (i=0; i<ND; i++) {
    for (j=0; j<ND; j++) {
      sigma_temp[i][j] = 0.0;
    }
  }
    
  for (i=0; i<NMAT; i++) {
    E[i] = 2*C[i][2]*(C[i][0]+2*C[i][1])/(C[i][0]+C[i][1]);
    miu[i] = 1./(1.+C[i][0]/C[i][1]);
    //C_effective[i] = E[i]/(1+miu[i])/(1-2*miu[i]);// for plane strain
    C_effective[i] = E[i]/(1-miu[i]); //for plane stress
    printf("E[%d]=%f,miu[%d]=%f, C_effective[%d] = %f\n",i,E[i],i,miu[i],i,C_effective[i]);
  }
  stressmisfit_max[0] = (a_f-a_s)*C_effective[1]*C_effective[0]/(a_f*C_effective[0]+a_s*C_effective[1]);
  stressmisfit_max[1] = (a_s-a_f)*C_effective[1]*C_effective[0]/(a_f*C_effective[0]+a_s*C_effective[1]);
  //stressmisfit_max[0] = 1.5E9; //for Cu/Ag experiment data
  //stressmisfit_max[1] = -1.5E9; //for Cu/Ag experiment data
  printf("max misfit stress is %f\n",stressmisfit_max[0]);
  
  hc = 5.0;
  
  //N3*3/4 should be the same as the variable "border" in setDorient
  //mark: right now only valid for a_f>a_s
  for (i=0; i<N1; i++) {
    for (j=0; j<N2; j++) {
      for (k=0; k<N3; k++) {
	distance = fabs(interface_n[0]*(i-ppoint_x) + interface_n[1]*(j-ppoint_y) + interface_n[2]*(k-ppoint_z));
	//double dist_sign = (interface_n[0]*(i-ppoint_x) + interface_n[1]*(j-ppoint_y) + interface_n[2]*(k-ppoint_z));
	//applied stress:
	for (u=0; u<ND; u++) {
	  for (v=0; v<ND; v++) {
	    sigma[i][j][k][u][v] = 0.0;
	    if ((u==0&&v==1) || (u==1&&v==0)) {//Original tau_crit: CuNi: 0.101, NiCu: 0.106, AlPt: 0.106, PtAl: 0.106
	      sigma[i][j][k][u][v] = 0.0;//n:0.102   p:
	    }
	    if (u==2&&v==2) {
	      //sigma[i][j][k][u][v] = 0.02;
	    }
	  }
	}
	//misfit stress:
	if (distance <= hc) {
	  /* int na; */
	  /* if (dist_sign <= 0) { */
	  /*   na = 0; */
	  /* } */
	  /* else { */
	  /*   na = 1; */
	  /* } */
	  sigma_temp[0][0] = 0.0;//(-stressmisfit_max[na]*distance/hc+stressmisfit_max[na])/(E[0]/2./(1+miu[0]));
	  sigma_temp[1][1] = 0.0;//(-stressmisfit_max[na]*distance/hc+stressmisfit_max[na])/(E[0]/2./(1+miu[0]));
          
	  for (m=0; m<ND; m++) {
	    for (n=0; n<ND; n++) {
	      for (p=0; p<ND; p++) {
		for (q=0; q<ND; q++) {
		  sigma[i][j][k][m][n] += rotate_100_interface[m][p]*sigma_temp[p][q]*rotate_100_interface[n][q];
		  if (i==ppoint_x && j==ppoint_y && k==ppoint_z && m==0 && n==2) {
		    maxreshear += rotate_100_interface[m][p]*sigma_temp[p][q]*rotate_100_interface[n][q];
		  }
		}
	      }
	    }
	  }
	  if (i==ppoint_x && j==ppoint_y && k==ppoint_z) {
	    printf("max resolved shear stress is %f",maxreshear*E[0]/2./(1+miu[0])/1.E9);
	  }
          
	}
        
      }
    }
  }
  return;
}

float ResidualEnergy(double * xi, double interface_n[ND],int ppoint_x, int ppoint_y, int ppoint_z, double D00, double D01, double D10, double D11)
{
  int j,is,ism;
  double xi_0,xi_1;
  float en_Residual;
  en_Residual = 0.0;
  //printf("in ResidualEnergy, check D's:  D00 = %lf, D01 = %lf, D10 = %lf, D11 = %lf\n",D00,D01,D10,D11);
  //mark: only works for 2 materials
  for (j=0; j<N2; j++) {
    xi_0 = 0.0;
    xi_1 = 0.0;
    for (is=0; is<NS; is++) {
      ism = (is)/NS1;
      //double iss = is%NS1;
      if (ism==0) {
	xi_0 += XI(ppoint_x, j, ppoint_z, is, 0);
                
      }
      if (ism==1) {
	xi_1 += XI(ppoint_x, j, ppoint_z, is, 0);
      }
    }
    en_Residual = en_Residual + fabs(-D11*xi_1*xi_1+(D10+D01)*xi_1*xi_0-D00*xi_0*xi_0);
  }
    
    
  return en_Residual;
}


static FILE *
fopen_rw(const char *filename)
{
  FILE *of = fopen(filename, "r+");
  if (of) {
    return of;
  }
  of = fopen(filename, "w+");
  if (of) {
    return of;
  }

  printf("File could not be opened: %s\n", filename);
  exit(1);
}

void
input_xi_from_file(double *xi, float *data, size_t sizexi)
{
  FILE *ofFinalXi = fopen("FinalXi.dat","rb");
  assert(ofFinalXi);
  
  for (size_t i = 0; i < sizexi; i++) {
    fread(&xi[i],sizeof(double),1,ofFinalXi);
  }

  for (int is=0; is<NSV; is++) {
    for (int i=0; i<N1; i++) {
      for (int j=0; j<N2; j++) {
	for (int k=0; k<N3; k++) {
	  DATA(i,j,k, is, 0) = XI(i,j,k, is, 0);
	  DATA(i,j,k, is, 1) = XI(i,j,k, is, 1);
	}
      }
    }
  }
  fclose(ofFinalXi);
}

static void
zero_ave(double avesigma[ND][ND], double avepsd[ND][ND],
	 double avepst[N1][N2][N3][ND][ND], double aveps[ND][ND])
{
  for(int i=0;i<ND;i++){
    for(int j=0;j<ND;j++){
      avesigma[i][j] = 0.0;
      for (int k1=0; k1<N1; k1++) {
	for (int k2=0; k2<N2; k2++) {
	  for (int k3=0; k3<N3; k3++) {
	    avepst[k1][k2][k3][i][j] = 0.0;
	  }
	}
      }
      avepsd[i][j] = 0.0;
      aveps[i][j] = 0.0;
    }
  }
}

static void
calc_data2_datag(float *data2, float *datag, float *data, double *BB, double *GG)
{
  for (int i=0; i < 2*N1*N2*N3*NSV; i++) {
    data2[i] = 0.;
  }
  for (int i=0; i < 2*N1*N2*N3*NS; i++){
    datag[i] = 0.;
  }
  
  for(int isa=0;isa<NS;isa++){
    for(int isb=0;isb<NSV;isb++){
      for(int i=0;i<N1;i++){
	for(int j=0;j<N2;j++){
	  for(int k=0;k<N3;k++){
	    if (isa < NS){
	      DATA2(i,j,k, isa, 0) += DATA(i,j,k, isb, 0) * BB(i,j,k, isa,isb);
	      DATA2(i,j,k, isa, 1) += DATA(i,j,k, isb, 1) * BB(i,j,k, isa,isb);
	      if (isb < NS){
		DATAG(i,j,k, isa, 0) += DATA(i,j,k, isb, 0) * GG(i,j,k, isa,isb);
		DATAG(i,j,k, isa, 1) += DATA(i,j,k, isb, 1) * GG(i,j,k, isa,isb);
	      }
	    }
	  }
	}
      }
    }
  }
}

static void
calc_strain_stress(float *databeta, float *dataeps, float *dataepsd, float *datasigma, float *data,
		   float *sigmav, double *xi, double *FF, double *FFv,
		   double eps[NS][ND][ND], double epsv[NV][ND][ND], double d1, double d2, double d3,
		   int size3, FILE *of2, FILE *of3, FILE *of4, FILE*of5, FILE *of6,
		   int it, int itp, double avepst[N1][N2][N3][ND][ND],
		   double aveps[ND][ND], double avesigma[ND][ND], double theta1[NMAT][ND][ND],
		   double slipdirection[ND], double xn[NS][ND],
		   double *penetrationstress, double *penetrationstress2,
		   int t_bwvirtualpf, int border, int ppoint[3], double mu,
		   const struct material *mc)
{
  if ((it == NT-1 && itp == NP-1) || ((it!=0)&&(it%t_bwvirtualpf==0)&&(it!=NT-1))) {
    strain(databeta, dataeps, data, FF, FFv, epsv, d1, d2, d3, size3, of3,it,itp, avepst);
    if (it==NT-1) {
      printf("go till here!!!\n");
    }

    stress(dataepsd, datasigma, dataeps, sigmav, xi, eps, epsv,
	   mc->C11, mc->C12, mc->C44, of5, it,itp, avesigma,theta1,
	   slipdirection,xn,penetrationstress,penetrationstress2,
	   t_bwvirtualpf,border,ppoint[0],ppoint[1],ppoint[2]);

    /*average strain*/
    for(int i=0;i<ND;i++){
      for (int j=0;j<ND;j++){
	for(int k1=0;k1<N1;k1++){
	  for(int k2=0;k2<N2;k2++){
	    for (int k3=0;k3<N3;k3++){
	      aveps[i][j] += DATAEPS(k1,k2,k3,i,j, 0)/N1/N2/N3;		//aveps only appears here to get total average strain
	    }
	  }
	}
	fprintf(of2,"%e %e ", aveps[i][j],avesigma[i][j]/mu);
      }
    }
            
    fprintf(of4,"zone   I = %d \n", N3);
    for (int k1=0;k1<N1;k1++){
      fprintf(of4,"%d %lf \n",k1,DATAEPS(k1,N2/2,N3/2,2,2, 0));
    }
    fprintf(of6,"zone   I = %d \n", N3);
    for (int k1=0;k1<N1;k1++){
      fprintf(of6,"%d %lf \n", k1, SIGMAV(k1,N2/2,N3/2, 2,2, 0));
    }
  }
}

int main(void)
{
  int i, j, k, it, it_plastic, itp, itp2, is, nsize, vflag, choice,checkpevolv,countgamma,checkpass,plastic_max;
  int * pcountgamma;
  int t_bwvirtualpf,border,it_checkEbarrier;
  int ppoint[3];
  int checkvirtual;
  int isa, isb;
  static double eps[NS][ND][ND],sigma[N1][N2][N3][ND][ND],epsv[NV][ND][ND],q[N1][N2][N3], avesigma[ND][ND], avepsd[ND][ND], avepst[N1][N2][N3][ND][ND], aveps[ND][ND];
  static double xn[NS][ND], xb[NS][ND], tau[N1][N2][N3][NS], rho2[NS],interface_n[ND],slipdirection[ND];
  double *fx, *fy, *fz, *xi, *xi_sum, *xi_bc, *sigmal, *penetrationstress,*penetrationstress2;
  float *data, *data2, *datag, *databeta;
  float *dataeps, *dataepsd, *datasigma, *sigmav;
  double *f, *r;
  double d1, d2, d3, size3,ir,beta2;
  struct material mc;
  double S11, S12, S44, CD2[NS], CDv,Asf2[NS],b2[NS],dslip2[NS], b, dslip, mu, gs,a_f,a_s,d_f,d_s, D00, D11,D10,D01,lam1,lam2,stressthreshold;
  double *BB;
  double *FF;
  double *DD;
  double *GG;
  double *FFv;
  int Rv = 0,sizexi,sizexi_bc,countenergy,countstrain;
  double setobs, gamma, gamma1[NS], gammalast;
  float uq2,energy_in,energy_last,energy_in2,energy_in3,energy_in4,energy_intotal,strain_average,strain_last,energy_Residual;
  //    float energy_intotal_z[25];
  double theta1[NMAT][ND][ND], Cm[NMAT][3], Sm[NMAT][3], KK[NMAT][6][6], *II, AA[NMAT][ND][ND], dS[NMAT][6][6]; //Cm[0]: C11, Cm[1]: C12, Cm[2]: C44.
  int *xi_o, ZLdC[NMAT][6],prop[NMAT][6];
    
  FILE *of2, *of3, *of4, *of5, *of6, *of7, *ofrho,*ofxi,*ofxi_bc, *ofxi1, *ofxi_bc1,*ofEnergy,*ofFinalXi_w,*ofpfv, *ofpfout, *ofresidual,*ofcheckEbarrier,*ofzEdensity;
  
  /* data is offset by one respect to xi due to offset in fourn
     data goes to fft and is repalced by its fft
     xi is always in real space  and is updated every step*/
#ifdef ACC_NVIDIA
  acc_init(acc_device_nvidia);
  acc_device_t device_type = acc_get_device_type();
  if ( acc_device_nvidia == device_type ) {
    int num_devices=acc_get_num_devices(acc_device_nvidia); 
    acc_set_device_num(0,acc_device_nvidia); 
      
    printf("Number Of GPUs %d\n",num_devices);
  }
#endif 
     
  double tmstart = WTime();
       
  sizexi = 2*(NSV)*(N1)*(N2)*(N3);
  sizexi_bc = 2*(NSV)*(N1)*(N2)*(N3);

  data = array4d_alloc(N1, N2, N3, NSV);
  data2 = array4d_alloc(N1, N2, N3, NSV);
  datag = array4d_alloc(N1, N2, N3, NS);
  databeta = array5d_alloc(N1, N2, N3, ND, ND);
  dataeps = array5d_alloc(N1, N2, N3, ND, ND);
  dataepsd = array5d_alloc(N1, N2, N3, ND, ND);
  datasigma = array5d_alloc(N1, N2, N3, ND, ND);

  sigmav = malloc(2*N1*N2*N3*NV*sizeof(float));
  xi = malloc(2*(NSV)*(N1)*(N2)*(N3)*sizeof(double));
  xi_bc = malloc(2*NSV*N1*N2*N3*sizeof(double));
  xi_sum = malloc(2*(N1)*(N2)*(N3)*sizeof(double));
  fx = malloc((N1)*(N2)*(N3)*sizeof(double));
  fy = malloc((N1)*(N2)*(N3)*sizeof(double));
  fz = malloc((N1)*(N2)*(N3)*sizeof(double));
#ifdef  UGPU
#pragma acc enter data create(fx[0:N1*N2*N3],fy[0:N1*N2*N3],fz[0:N1*N2*N3])
#endif  
  f = malloc((NS)*(N1)*(N2)*(N3)*sizeof(double));
  r = malloc((ND)*sizeof(double));
  BB = malloc((NS)*(NSV)*(N1)*(N2)*(N3)*sizeof(double));
  FF = malloc((NS)*(N1)*(N2)*(N3)*(ND)*(ND)*sizeof(double));
  DD = malloc((NSV)*(N1)*(N2)*(N3)*(ND)*(ND)*sizeof(double));
  FFv = malloc((NV)*(N1)*(N2)*(N3)*(ND)*(ND)*sizeof(double));
  sigmal= malloc((N1)*(N2)*(N3)*(ND)*(ND)*sizeof(double));
  GG = malloc((2*(NS)*(NS)*(N1)*(N2)*(N3))*sizeof(double));
  xi_o = malloc((N1)*(N2)*(N3)*sizeof(int));
  II = malloc((ND)*(ND)*(N1)*(N2)*(N3)*sizeof(double));
  penetrationstress = malloc(N2*sizeof(double));// only valid for N2 = 2
  penetrationstress2 = malloc(N2*sizeof(double));// only valid for N2 = 2
    
#define REAL 0
#define IMAG 1
    
  /* print files */
  of2 = fopen("stress-strain.dat","w");
  of3 = fopen("strain.txt","w");
  of4 = fopen("strain1D.dat","w");
  of5 = fopen("stress.txt","w");
  of6 = fopen("stress1D.dat","w");
  of7 = fopen("virtual_strain.dat","w");
  ofrho = fopen("rho.dat","w");
  ofEnergy = fopen("checkEnergy.dat","w");
  ofpfv = fopen("phasefield.dat","w");
  ofpfout = fopen("pfout.dat","w");
  ofresidual = fopen("residual.dat","w");
  ofcheckEbarrier = fopen("checkEbarrier.dat","w");
  ofzEdensity = fopen("zE.dat","w");
	
  choice=1; //choice=1 start w/o reading data; choice=2, read in xi and xi_bc.

  setobs = 0.05;//0.30; //set random obstacle density
  //double setsigma = 0.000;//1.0E-2;//3E-3;//1.5E-1;//1.5E-3;//0.25;//0.3;//0.5/10;//0.7;//0.002;//0.001;//1.0;//0.04;//0.005;//1.0;//0.01; //2.3E-3;  //set stress
  //double sigstep = 0.00;//1.0E-2;//0.0002; //set stress increment
  gamma = 0.0;
  gammalast = 0.0;
  
  //float uq1=1.0/3.0;
  uq2=1.0;
    
  /* set # of time steps between virtualstrain evolve and phase field evolve */
  t_bwvirtualpf = 1;
  checkvirtual = 0;
  border = N3*3/4;
  ppoint[0] = N1/2;//1 point penetration point on interface
  ppoint[1] = 0;
  ppoint[2] = border;
  it_checkEbarrier = 0;
  plastic_max =3000;//original:3000

  material_set(&mc);
  b = 0.256E-9;
  dslip = 1.0;//4.0;//1.0;//N1/4/sqrt(3.0); /* in units of b*/
  a_f = 0.361E-9; //for Cu
  //a_f = 0.352E-9; //for Ni
  //a_f = 0.392E-9; //for Pt
  //a_f = 0.408E-9; //for Au
  //a_f = 0.405E-9; //for Al
  //a_f = 0.409E-9;//for Ag
  //a_f = 0.380E-9;//for Rh
  //a_f = 0.389E-9;//Pd
  //a_f = 0.384E-9;//Ir
    
  a_s = 0.361E-9; //for Cu
  //a_s = 0.352E-9; //for Ni
  //a_s = 0.392E-9; //for Pt
  //a_s = 0.408E-9; //for Au
  //a_s = 0.405E-9; //for Al
  //a_s = 0.409E-9;//for Ag
  //a_s = 0.380E-9;//for Rh
  //a_s = 0.389E-9;//Pd
  //a_s = 0.384E-9;//Ir
    
  D00 = 0.0;
  D11 = 0.0;
  D10 = 0.0;
  D01 = 0.0;
  interface_n[0] = sqrt(3)/3.;//interface_n in global coordinate system
  interface_n[1] = 0.0;
  interface_n[2] = sqrt(6)/3.;
  //slipdirection is not used in simulation
  slipdirection[0] = -sqrt(2)/2.;//slipdirection in crystal coordinate system. Usually coincides with burgers vector for edge. Otherwise have both edge and screw component.
  slipdirection[1] = 0.0;
  slipdirection[2] = sqrt(2)/2.;
  checkpass = 1; //mark: check whether the dislocation has enough energy to pass interface

  if (MT == 1) {
    mc.C11 = 2. * mc.C44 + mc.C12;  
    printf("This is an isotropic material.\n");
  } else if (MT == 2) {
    printf("This is a cubic material.\n");
  } else {
    printf("Material type defined incorrectly. Please select an integer up to 2.\n");
    exit(1);
  }
    
  setMat( Cm,  Sm,  b2, dslip2, &mc, b, dslip);  //set elastic constants, b2, dslip2 for different materials
    
  mu = mc.C44;//mc.C44-(2.0*mc.C44+mc.C12-mc.C11)/5.0;
  //double ll = mc.C12;//mc.C12-(2.0*mc.C44+mc.C12-mc.C11)/5.0;
  //double mup= mc.C11-mc.C12-2*mc.C44;
  //double young = mu*(3*ll+2*mu)/(ll+mu);
  //double nu = young/2.0/mu-1.0;
    
  S11 = 1./3.*(1/(mc.C11+2*mc.C12) + 2/(mc.C11-mc.C12));
  S12 = 1./3.*(1/(mc.C11+2*mc.C12) - 1/(mc.C11-mc.C12));
  S44 = 1/mc.C44;
    
  //double L = (double)(N3);
	
  nsize = N1*N2*N3;
    
  d1 = 1;//10.0; //in unis of b so Fequencies are normalized
  d2 = 1;//10.0;
  d3 = 1;//10.0;
    
  gs = b2[0]*d1;  /*grid size*/

  //double size = L*gs;
  size3 = b2[0]*b2[0]*b2[0]*N1*N2*N3*d1*d2*d3;
  for(is=0;is<NS;is++){
    Asf2[is] = 0.0262;//the constant C in the code is C = Asf * mc.C44 *dslip*b //
  }
  Asf2[0] = 0.0064659518668288441;
  Asf2[1] = 0.0064659518668288441;
  if (NS == 4) {
    Asf2[2] = 0.013307048604153731;
    Asf2[3] = 0.013307048604153731;
  }
  // for Cu, B = 0.1519499J/m^2, for Ni, B = 0.3127156J/m^2, don't forget normalize by mc.C44
  // normalized by mc.C44_Ni, B_cu = 0.003063505420775763, B_ni = 0.0063047508507583193
  // normalized by mc.C44_Cu, B_cu = 0.0064659518668288441, B_ni = 0.013307048604153731
  // normalized by mc.C44_Au, B_au = 0.0073077793951971436, B_ag = 0.0077033035051546385
  // normalized by mc.C44_Ag, B_au = 0.006949554901960784, B_ag = 0.007325690588235293
  // normalized by mc.C44_Au, B_au = 0.0073077793951971436, B_al = 0.011566588369596448
  // normalized by mc.C44_Al, B_au = 0.0045831116465568297, B_al = 0.0072540457231736353
    
  printf ("core energy C = %lf \n", Asf2[0] * mu * dslip2[0]*b2[0]);
	
  for(is=0;is<NS;is++){
    CD2[is] = 0.1;//1.0/100/2;//0.0000001; //1.0; /* dif coefficient*/
  }
  CDv = 0.00007;//0.2/1000;//1E-2;//2E-4;//0.02;//0.00002;//0.0002;//0.002;//0.2; //0.002; //0.2;//0.00000000002; //0.1; /*kinetic coeff for virtual strain*/
  beta2=4E2;
	
  /* Set grain orientations */
  setorient(theta1);
  /* Set axis transformation matrix */
  Amatrix(AA, theta1);
  /* Set orientation for the domain */
  setDorient(xi_o,&d_f,&d_s,border,interface_n,ppoint[0],ppoint[1],ppoint[2]);
  /* Set deltaS matrix */	
  dSmat(Cm, Sm, AA,  KK, dS, ZLdC,prop,theta1);
  /*Set the slip systems frequency and matrix BB */	
    
  //set2D(xn,xb);
  // set3D2sys(xn,xb);
    
  set3D1sys(xn,xb);
  seteps(eps, epsv, xn, xb, dslip2,AA,theta1);
  //mark calculate D coefficients for interface evolve
  calculateD4int(a_f, a_s, Cm, theta1, xb, xn, interface_n,&D00, &D01, &D10, &D11,&lam1,&lam2,&stressthreshold);
  printf("D00 = %e, D11 = %e, D10 = %e, D01 = %e\n",D00,D11,D10,D01);
  
  printf("Frequencies\n");
  frec(fx, fy, fz, d1, d2, d3);
    	   
  printf("Set interaction matrix\n");
  Bmatrix(BB, fx, fy, fz, eps, epsv, d1,  d2 , d3, &mc, xi_o);
  Fmatrix(FF, DD, FFv, fx, fy, fz, eps, epsv, d1,  d2 , d3, &mc, xi_o,theta1);
  printf("HERE WE ARE AFTER CALLING THE FMATRIX BEFORE GMATRIX\n");
  //     getchar();
  Gmatrix (GG, beta2, xb, xn, fx, fy, fz);
  printf("HERE WE ARE AFTER CALLING THE GMATRIX\n");  
  
  
  /*#ifdef GPU
    #pragma acc kernels
    {
    #endif  
    for(i=1;i<2*(NSV)*(N1)*(N2)*(N3)+1;i++)
    {
    data[i]=0.;
    }
    
    #ifdef GPU
    } //#pragma acc kernels
    #endif 
  */
   
  if(choice==1){
    printf(" Generating initial data\n");
      
    /* #ifdef GPUU
       #pragma acc update host(data)
       #endif */ 
    
    initial(data, xi, xi_bc, setobs, xi_o,border,ppoint[0],ppoint[1],ppoint[2]);
      
    /*#ifdef GPUU
      #pragma acc update device(data)
      #endif*/
  }
    
  ofxi = fopen_rw("xi.dat");
  ofxi_bc = fopen_rw("xi_bc.dat");
  ofxi1 = fopen_rw("xi1.dat");
  ofxi_bc1 = fopen_rw("xi_bc1.dat");
  
  if(choice==2){
    fread(xi,sizeof(double),sizexi,ofxi);
    fread(xi_bc,sizeof(double),sizexi_bc,ofxi_bc);
    rewind(ofxi);
    rewind(ofxi_bc);
      
    for(is=0;is<NSV;is++){
      for(i=0;i<N1;i++){
	for(j=0;j<N2;j++){
	  for(k=0;k<N3;k++){
	    DATA(i,j,k, is, 0) = XI(i,j,k, is, 0);
	    DATA(i,j,k, is, 1) = XI(i,j,k, is, 1);
	  }
	}
      }
    }							
  }
    
  setq(q);
    
  printf("start time step \n");
  /* time step */
  int test_energy[NT], test_strain[NT];     // test for countenergy and countstrain in time step
  for (i = 0; i < NT; i++) {
    test_energy[i] = 0;
    test_strain[i] = 0;
  }
    
  int * testplastic;    //test for plasticevolv
  testplastic =  malloc(plastic_max*sizeof(int));
    
  pcountgamma = &countgamma;
    
  //mark setstress
  setstress(sigma,a_s,a_f,Cm,border,ppoint[0],ppoint[1],ppoint[2],interface_n);
    
  for(itp2=0;itp2<NP2;itp2++){
    /* Applied stress*/
    /*
      sigma[0][0]=0.0;
      sigma[0][1]=0.001;
      //	  if ((it+itp*NT)<20) sigma[0][2]=0.000;
      //	  else sigma[0][2]=setsigma + itp*sigstep;//0.000;
      sigma[0][2]=0.000;
      sigma[1][0]=sigma[0][1];
      sigma[1][1]=0.000;//setsigma + itp2*sigstep;//setsigma;//0.0;//setsigma + itp*sigstep;//0.000;
      sigma[1][2]=0.0;//setsigma + itp2*sigstep;//0.0;//setsigma + itp*sigstep; //0.0001;
      sigma[2][0]=sigma[0][2];
      sigma[2][1]=sigma[1][2];
      sigma[2][2]=setsigma + itp*sigstep;
    */
    resolSS(sigma,tau, eps, xi_bc, sigmal, xi_o);

    for(itp=0;itp<NP;itp++){
      countgamma=0;
      countenergy=0;
      countstrain=0;
      for(it=0;it<NT;it++){
	printf("time step, %d %d %d\n", it, itp, itp2);
	printf("xi = %e\n",xi[2*NS*N1*N2*N3]);

	//mark input Xi via file
	/* if (it == 0) { */
	/*   input_xi_from_file(xi, data, sizexi); */
	/* } */

	zero_ave(avesigma, avepsd, avepst, aveps);
	if(it!=0){
	  energy_last=energy_intotal;
	  strain_last=strain_average;
	}
	energy_intotal = 0.0;
	energy_in =0.0;
	energy_in4 = 0.0;
	strain_average = 0.0;
          
	energy_in3 = avestrain(avepsd, avepst, eps,epsv, xi, nsize, sigma, S11, S12, S44, mu,&energy_in4, &strain_average,border,interface_n,ppoint);
	energy_in2 = Imatrix(II, xi, KK, &mc, xi_o,interface_n,ppoint[0],ppoint[1],ppoint[2]);
	vflag=0;
	  
#ifdef USE_CUFFT
#pragma acc data copy(data[0:2*N1*N2*N3*NSV])
#pragma acc host_data use_device(data)
#endif
	fft_forward(data, NSV);

	calc_data2_datag(data2, datag, data, BB, GG);
          
#ifdef USE_CUFFT
#pragma acc data copy(datag[0:2*N1*N2*N3*NS])
#pragma acc host_data use_device(datag)
#endif
	fft_backward(datag, NS);
	  
	/*strain & stress calculation*/
	calc_strain_stress(databeta, dataeps, dataepsd, datasigma, data,
			   sigmav, xi, FF, FFv,
			   eps, epsv, d1, d2, d3,
			   size3, of2, of3, of4, of5, of6,
			   it, itp, avepst,
			   aveps, avesigma, theta1,
			   slipdirection, xn,
			   penetrationstress, penetrationstress2,
			   t_bwvirtualpf, border, ppoint, mu,
			   &mc);
          
	if(it >= NT-NTD){
	  //2nd calculation for rho
	  for(is=0;is<NS;is++){
	    rho2[is] = 0.0;
	  }
	  for(i=0;i<N1;i++){
	    for(j=0;j<N2;j++){
	      for(k=0;k<N3;k++){
		int xleft, xright, yleft, yright, zleft, zright,dx;
#if 0 // avoid warning
		double dxi_x, dxi_y, dxi_z;
		double e1[ND], e2[ND];
		e1[0]=-1.0/sqrt(2.0);
		e1[1]=1.0/sqrt(2.0);
		e1[2]=0.0;
		e2[0]=-1.0/sqrt(6.0);
		e2[1]=-1.0/sqrt(6.0);
		e2[2]=2.0/sqrt(6.0);
#endif
		dx = 2;
		xright = i+dx/2;
		xleft = i-dx/2;
		yright = j+dx/2;
		yleft = j-dx/2;
		zright = k+dx/2;
		zleft = k-dx/2;
		if(xleft<0){xleft += N1;}
		if(yleft<0){yleft += N2;}
		if(zleft<0){zleft += N3;}
		if(xright>=N1){xright += -N1;}
		if(yright>=N2){yright += -N2;}
		if(zright>=N2){zright += -N3;}
		for(is=0;is<NS;is++){
		  ir = (double)((i-N1/2)*(i-N1/2)+(j-N2/2)*(j-N2/2));
		  if(ir<(double)((N1/4-1)*(N1/4-1))){
#if 0 // avoid warning
		    dxi_x = (XI(xright,j,k, is, 0) - XI(xleft,j,k, is, 0))/dx;
		    dxi_y = (XI(i,yright,k, is, 0) - XI(i,yleft,k, is, 0))/dx;
		    dxi_z = (XI(i,j,zright, is, 0) - XI(i,j,zleft, is, 0))/dx;
#endif
		    rho2[is] += sqrt(sqr(XI(xright,j,k, is, 0) - XI(xleft,j,k, is, 0)) / sqr(dx) +
				     sqr(XI(i,yright,k, is, 0) - XI(i,yleft,k, is, 0)) / sqr(dx))
		      *sqr(gs)/size3/gs;
		  }
		}
	      }
	    }
	  }
	  fprintf(ofrho,"%d	",itp2);
	  for(is=0;is<NS;is++){
	    fprintf(ofrho,"%e	", rho2[is]);
	  }
	  fprintf(ofrho,"\n");
	}//end calculate Rho
          
	// #pragma acc data copyin(fx[0:N1*N2*N3],fy[0:N1*N2*N3],fz[0:N1*N2*N3]) 
	// {
	//mark  energy_in
	energy_in = Energy_calculation(fx,fy,fz,eps,epsv,mc.C11,mc.C12,mc.C44,data,interface_n,ppoint[0],ppoint[1],ppoint[2]);
	//  }
	energy_Residual = ResidualEnergy(xi,interface_n,ppoint[0],ppoint[1],ppoint[2],D00,D01,D10,D11);
	energy_intotal = energy_in+energy_in2+energy_in3+energy_in4+energy_Residual;
	if ((it%1==0)||(it==NT-NTD-1)||(it==NT-NTD)) {
	  fprintf(ofEnergy,"%d    %lf   %lf   %lf   %lf   %lf   %lf\n",it,energy_in,energy_in2,energy_in3,energy_in4,energy_Residual,energy_intotal);
	}
	virtualevolv(data, data2, sigmav, DD, xi, xi_bc,  CDv, sigmal, Rv, d1, d2, d3, size3, of7, it, itp, vflag, II, xi_o, ZLdC, prop, dS);			/*evolving the virtual strain*/
          
	//mark extract xi in final step
	if (it==NT-NTD-1) {
	  ofFinalXi_w = fopen("FinalXi_w.dat","wb");
	  for (i=0; i<2*(NSV)*(N1)*(N2)*(N3); i++) {
	    fwrite(&xi[i],sizeof(double),1,ofFinalXi_w);
	 
	  }
	  fclose(ofFinalXi_w);
	}
	  
	if(vflag == 0){
	  printf("vflag %d, time %d \n", vflag, it);
	}
          
          
	// mark where suppose to be plasticevolv
	for (isa=0; isa<NSV; isa++) {
	  for (i=0; i<N1; i++) {
	    for (j=0; j<N2; j++) {
	      for (k=0; k<N3; k++) {
		DATA(i,j,k, isa, 0) = XI(i,j,k, isa, 0);
		DATA(i,j,k, isa, 1) = XI(i,j,k, isa, 1);
	      }
	    }
	  }
	}
	if (((it!=0)&&(it%t_bwvirtualpf==0)&&(it!=NT-1)) || (checkvirtual == 1)) {
	  printf("here plastic!\n");
	  it_plastic = 0;
	  countgamma = 0;
	  checkpevolv = 1;
	  for (i = 0; i < plastic_max; i++) {
	    testplastic[i] = 0;
	  }
	  printf("evolve plastic then evolve interface\n");
	  do {
	    gammalast = gamma;
	    gamma = plasticevolv(xi_bc,xi,CD2,uq2,data2,Asf2,tau,dslip2,datag,data,gamma1,nsize,a_f,a_s,mc.C44,it_plastic);
	    checkpevolv = plasticconverge(gamma,gammalast,it_plastic,testplastic,pcountgamma);
	    printf("in evolve plastic:    %d    %d\n",it_plastic, checkpevolv);
	    it_plastic = it_plastic + 1;
	    interfacevolv(xi,penetrationstress,D00,D01,D10,D11,lam1,lam2,stressthreshold,&checkpass,border,ppoint[0],ppoint[1],ppoint[2],mc.C44,a_s,a_f,penetrationstress2,&it_checkEbarrier,ofcheckEbarrier);
	    for (isa=0; isa<NSV; isa++) {
	      for (i=0; i<N1; i++) {
		for (j=0; j<N2; j++) {
		  for (k=0; k<N3; k++) {
		    DATA(i,j,k, isa, 0) = XI(i,j,k, isa, 0);
		    DATA(i,j,k, isa, 1) = XI(i,j,k, isa, 1);
		  }
		}
	      }
	    }
	    if (checkpevolv) {
	      // FFT on data
		

#ifdef USE_CUFFT
#pragma acc data copy(data[0:2*N1*N2*N3*NSV])
#pragma acc host_data use_device(data)
#endif
	      fft_forward(data, NSV);
	  
	      //initialize data2 and datag
	      for (i = 0; i < 2*N1*N2*N3*NSV; i++) {
		data2[i] = 0.;
	      }
	      for (i = 0; i < 2*N1*N2*N3*NS; i++){
		datag[i] = 0.;
	      }
	      //calculate data2 and datag
	      for(isa=0;isa<NS;isa++){
		for(isb=0;isb<NSV;isb++){
		  for(i=0;i<N1;i++){
		    for(j=0;j<N2;j++){
		      for(k=0;k<N3;k++){
			if(isa < NS){
			  DATA2(i,j,k, isa, 0) += DATA(i,j,k, isb, 0) * BB(i,j,k, isa,isb);
			  DATA2(i,j,k, isa, 1) += DATA(i,j,k, isb, 1) * BB(i,j,k, isa,isb);
			  if(isb<NS){
			    DATAG(i,j,k, isa, 0) += DATA(i,j,k, isb, 0) * GG(i,j,k, isa,isb);
			    DATAG(i,j,k, isa, 1) += DATA(i,j,k, isb, 1) * GG(i,j,k, isa,isb);
			  }
			} else {
			  DATA2(i,j,k, isa, 0) += DATA(i,j,k, isb, 0) * DD[I5V(i, j, k, isb, isa-NS)];
			  DATA2(i,j,k, isa, 1) += DATA(i,j,k, isb, 1) * DD[I5V(i, j, k, isb, isa-NS)];
			}
		      }
		    }
		  }
		}
	      }
		
		
	      //inverse FFT on data2 and datag

#ifdef USE_CUFFT
#pragma acc data copy(data2[0:2*N1*N2*N3*NS])
#pragma acc host_data use_device(data2)
#endif
	      fft_backward(data2, NS);	/* inverse FFT*/  
#ifdef USE_CUFFT
#pragma acc data copy(datag[0:2*N1*N2*N3*NS])
#pragma acc host_data use_device(datag)
#endif
	      fft_backward(datag, NS);	/* inverse FFT*/  
  
	      if (1) {//checkpass==1
		energy_in = Energy_calculation(fx,fy,fz,eps,epsv,mc.C11,mc.C12,mc.C44,data,interface_n,ppoint[0],ppoint[1],ppoint[2]);
		energy_in3 = avestrain(avepsd, avepst, eps,epsv, xi, nsize, sigma, S11, S12, S44, mu,&energy_in4, &strain_average,border,interface_n,ppoint);
		energy_Residual = ResidualEnergy(xi,interface_n,ppoint[0],ppoint[1],ppoint[2],D00,D01,D10,D11);
		energy_intotal = energy_in+energy_in2+energy_in3+energy_in4+energy_Residual;
		fprintf(ofcheckEbarrier, "%d   %lf   %lf\n",it_checkEbarrier,fabs(energy_Residual),fabs(energy_intotal));
                  
		//mark check z direction energy density distribution
		/*   fprintf(ofzEdensity, "zone   I = 25\n");
		     for (k=ppoint[2]-12;k<ppoint[2]+13 ; k++) {
		     energy_in = Energy_calculation(fx,fy,fz,eps,epsv,mc.C11,mc.C12,mc.C44,data,interface_n,ppoint[0],ppoint[1],k);
		     energy_in3 = avestrain(avepsd, avepst, eps,epsv, xi, nsize, sigma, S11, S12, S44, mu,&energy_in4, &strain_average,border,interface_n,ppoint[0],ppoint[1],k);
		     if (k==ppoint[2]) {
		     energy_Residual = ResidualEnergy(xi,interface_n,ppoint[0],ppoint[1],ppoint[2],D00,D01,D10,D11);
		     }else{
		     energy_Residual = 0.0;
		     }
                       
		     energy_intotal_z[k+12-ppoint[2]] = energy_in+energy_in2+energy_in3+energy_in4+energy_Residual;
		     fprintf(ofzEdensity, "%d   %lf   %lf\n",k+12-ppoint[2],energy_Residual,energy_intotal_z[k+12-ppoint[2]]);
		     }*/
		  
                  
		it_checkEbarrier = it_checkEbarrier + 1;
	      }
	    }
              
	    fprintf(ofpfv, "%d       %d         %e           %e           %e\n", it,it_plastic,gamma1[0],gamma1[1],gamma);
                        
                        
	    //mark output phase field during phase field evolution
              
	    if (it_plastic%10==0||it_plastic==plastic_max || checkpevolv == 0) {
	      fprintf(ofpfout, "zone   I = %d K = %d\n",N3,N1);
	      for (i=0; i<N1; i++) {
		for (k=0; k<N3; k++) {
		  if ((interface_n[0]*(i-ppoint[0]) + interface_n[1]*(0-ppoint[1]) + interface_n[2]*(k-ppoint[2]))<=0) {
		    is = 0;
		  } else{
		    is = 2;
		  }
		  fprintf(ofpfout,"%d   %d   %lf\n",k,i,XI(i,0,k,is, 0));
		}
	      }
	    }
              
	  } while (checkpevolv&&(it_plastic<plastic_max));
	}// end if(it%10==0)
	  
	if(it == NT-1 && itp == NP-1){
	  for(is=0;is<NS;is++){
	    fprintf(of2,"	%e", gamma1[is]);
	  }
	  fprintf(of2, " %e \n", gamma);
	}
          
	if(it!=0){
	  if (it<NT-NTD-2 && it>0) {
	    //mark begin of countenergy
	    if (energy_intotal != 0) {
	      if ((((energy_intotal-energy_last)/energy_intotal)<0?-(energy_intotal-energy_last)/energy_intotal:(energy_intotal-energy_last)/energy_intotal) < 1E-5) {
		test_energy[it]=1;
		if (it>=5) {
		  if ((test_energy[it-1]+test_energy[it-2]+test_energy[it-3]+test_energy[it-4]+test_energy[it-5])==15) {
		    countenergy++;
		  }
		  else{
		    if (countenergy!=0) {
		      countenergy = 0;
		    }
		  }
		}
		printf("(energy_in-energy_last)/energy_in = %e     %d\n",(energy_in-energy_last)/energy_in,countenergy);
	      }
	    } else {
	      if (((energy_intotal-energy_last)<0?-(energy_intotal-energy_last):(energy_intotal-energy_last))<1E-7) {
		test_energy[it]=1;
		if (it>=5) {
		  if ((test_energy[it-1]+test_energy[it-2]+test_energy[it-3]+test_energy[it-4]+test_energy[it-5])==15) {
		    countenergy++;
		  }
		  else{
		    if (countenergy!=0) {
		      countenergy = 0;
		    }
		  }
		}
		printf("0  energy_in-energy_last = %e    %d\n",energy_intotal-energy_last,countenergy);
	      }
	    }
	      
	    //mark begin of countstrain
	    if (strain_average != 0) {
	      if ((((strain_average-strain_last)/strain_average)<0?-(strain_average-strain_last)/strain_average:(strain_average-strain_last)/strain_average) < 1E-5) {
		test_strain[it]=1;
		if (it>=5) {
		  if ((test_strain[it-1]+test_strain[it-2]+test_strain[it-3]+test_strain[it-4]+test_strain[it-5])==5) {
		    countstrain++;
		  }
		  else{
		    if (countstrain!=0) {
		      countstrain = 0;
		    }
		  }
		}
		printf("(strain_average-strain_last)/strain_average = %e     %d\n",(strain_average-strain_last)/strain_average,countstrain);
	      }
	    } else {
	      if (((strain_average-strain_last)<0?-(strain_average-strain_last):(strain_average-strain_last))<1E-7) {
		test_strain[it]=1;
		if (it>=5) {
		  if ((test_strain[it-1]+test_strain[it-2]+test_strain[it-3]+test_strain[it-4]+test_strain[it-5])==5) {
		    countstrain++;
		  }
		  else{
		    if (countstrain!=0) {
		      countstrain = 0;
		    }
		  }
		}
		printf("0  strain_average-strain_last = %e    %d\n",strain_average-strain_last,countstrain);
	      }
	    }
              
	    if ((countenergy==5 || countstrain==5)) {
	      it=NT-NTD-2;
	      checkvirtual = 1;
	    }
	  }//mark end of countenergy and countstrain
	}//it!=0
      }/*end it*/
	
      fwrite(xi,sizeof(double),sizexi,ofxi);
	 
      fwrite(xi_bc,sizeof(double),sizexi_bc,ofxi_bc);
	 
      rewind(ofxi);
      rewind(ofxi_bc);
      if(itp==0){
	fwrite(xi,sizeof(double),sizexi,ofxi1);
	fwrite(xi_bc,sizeof(double),sizexi_bc,ofxi_bc1);
	rewind(ofxi1);
	rewind(ofxi_bc1);
      }
    }/*end itp*/
  }/*end itp2*/
    
   
  free(data);
  free(data2);
  free(datag);
  free(databeta);
  free(dataeps);
  free(dataepsd);
  free(datasigma);
  free(xi);
  free(xi_sum);
#ifdef UGPU
#pragma acc exit data delete(fx[0:N1*N2*N3],fy[0:N1:N2:N3],fz[0:N1*N2*N3])
#endif 
  free(fx);
  free(fy);
  free(fz);
  free(f);
  free(r);
  free(BB);
  free(FF);
  free(DD);
  free(GG);
  //free(xiv);
  free(xi_bc);
  free(sigmav);
  free(FFv);
  free(sigmal);
  free(xi_o);
  free(II);
  free(testplastic);
	
  fclose(of2);
  fclose(of3);
  fclose(of4);
  fclose(of5);
  fclose(of6);
  fclose(of7);
  fclose(ofrho);
    
  fclose(ofxi);
  fclose(ofxi_bc);
    
  fclose(ofxi1);
  fclose(ofxi_bc1);
  fclose(ofEnergy);
  fclose(ofpfv);
  fclose(ofpfout);
  fclose(ofresidual);
  fclose(ofcheckEbarrier);
  fclose(ofzEdensity);

  double now = WTime();
  printf("wall time %fs\n", now - tmstart);
    
  return 0;
}


