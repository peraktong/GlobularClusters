#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "nrutil.h"
#include "cutil.h"

#define OMEGA_M 0.25
#define PI 3.14159
#define RHO_CRIT 2.775E+11
#define DELTA_HALO 200
#define SPEED_OF_LIGHT 3.0E+5
#define c_on_H0 3000.0
#define BIG_G 4.304E-9 /* BIG G in units of (km/s)^2*Mpc/M_sol */
#define G0 (1.0/sqrt(2.0*3.14159))
#define ROOT2 1.41421
#define Q0 2.0
#define Q1 -1.0
#define QZ0 0.1
#define THIRD (1.0/3.0)
#define HUBBLETIME (3.09E17/.7/31622400)

//numerical recipes
float qromo(float (*func)(float), float a, float b,
             float (*choose)(float(*)(float), float, float, int));
float midpnt(float (*func)(float), float a, float b, int n);
void spline(float x[], float y[], int n, float yp1, float ypn, float y2[]);
void splint(float xa[], float ya[], float y2a[], int n, float x, float *y);
void sort2(int n, float arr[], int id[]);
float gasdev(long *idum);
void jacobi(float **a, int n, float d[], float **v, int *nrot);
void gaussj(float **a, int n, float **b, int m);
void sort(unsigned long n, float arr[]);

/* local functions
 */
float chi2func(float *a, int n);
int prior_violation(int n, float *a, float *p1, float *p2);

/* global variables
 */
float atotal[100];
int ifree[100], ntotal;

int main(int argc, char **argv)
{
  char aa[1000];
  float chi2=0, stepfac=1, chi2prev, chi2min = 1000, amin[10];
  float scatterprev, ageprev;
  int niter=0, NSTEP_MAX=1000000,n=3, i, NSTEP_CONVERGE=10000, NSTEP_BURN=2000, OUTPUT=1, nlines;
  FILE *fp, *fp1;
  long IDUM=-555;

  int nstep=0, nacc=0, ndim, nrot, k, j;
  float **cov1,**tmp,*a,*avg1,stepfac_burn,prior1[10],prior2[10],xx[100],
    **evect,*eval,*aprev,*atemp,**tmp1,*opar,x1,fsat,**chain,*start_dev,*eval_prev;

  stepfac_burn = 0.1;
  stepfac = 0.4;

  // read in what are free parameters
  fp = openfile(argv[1]);
  nlines = filesize(fp);
  ntotal = nlines;
  n = 0;
  for(i=1;i<=nlines;++i)
    {      
      ifree[i] = 0;
      fscanf(fp,"%d",&j);fgets(aa,1000,fp);
      if(j){ n++; ifree[i] = 1; }
    }
  fprintf(stderr,"Number of free parameters: %d\n",n);
  fclose(fp);

  // declare a bunch of vectors/matrices
  a=vector(1,n);
  start_dev=vector(1,n);
  aprev=vector(1,n);
  atemp=vector(1,n);
  cov1=matrix(1,n,1,n);
  avg1=vector(1,n);

  tmp=matrix(1,n,1,n);
  tmp1=matrix(1,n,1,1);
  evect=matrix(1,n,1,n);
  eval=vector(1,n);
  eval_prev=vector(1,n);

  chain=matrix(1,NSTEP_CONVERGE,1,n);


  // now read in the starting place for the run
  fp = openfile(argv[2]);
  if(filesize(fp)!=nlines) 
    {
      fprintf(stderr,"ERROR: number of lines in [%s] != [%s]\n",argv[1],argv[2]);
      exit(0);
    }
  j = 0;
  for(i=1;i<=nlines;++i)
    {
      fscanf(fp,"%f",&x1);fgets(aa,1000,fp);
      atotal[i] = x1;
      if(ifree[i]) { a[++j]=x1;
	fprintf(stderr, "a[%d] = %e\n",j,a[j]); }
    }
  fclose(fp);

  if(argc>3)
    stepfac = atof(argv[3]);
  if(argc>4)
    stepfac_burn = atof(argv[4]);

  // open the output file for the mcmc
  if(argc>5)
    fp = fopen(argv[5],"w");
  else
    fp = fopen("mcmc.out", "w");

  for(i=1;i<=n;++i)
    start_dev[i] = a[i]*0.2;

  // priors
  prior1[1] = 0; 
  prior2[1] = 10.0;
  prior1[2] = 0.0;
  prior2[2] = 5.0;
  prior1[3] = 0.0;
  prior2[3] = 5.0;
  prior1[4] = 0;
  prior2[4] = 5.0;
      

  // is there an input file for the parameters?
  if(argc>5)
    {
      fp1 = openfile(argv[5]);
      for(i=1;i<=n+5;++i)
	fscanf(fp1,"%f",&xx[i]);
      for(i=1;i<=n;++i)
	a[i] = xx[i+5];
      fprintf(stderr,"input_model [%s]: ",argv[7]);
      for(i=1;i<=n;++i) fprintf(stderr," %e",a[i]);
      fprintf(stderr,"\n");
    }

  chi2 = chi2func(a,n);
  //exit(0);

  for(i=1;i<=n;++i)
    aprev[i] = a[i];

  chi2prev = chi2;
  ndim = n;

  while(nstep < NSTEP_MAX)
    {
      for(i=1;i<=n;++i)
	  aprev[i] = a[i];

      // BURN IN: get the new proposal
      if(nstep<NSTEP_BURN)
	{
	  if(nstep>0)
	    for(i=1;i<=n;++i) {
	      a[i] = (1+gasdev(&IDUM)*start_dev[i]*stepfac_burn)*aprev[i];
	    }
	  goto SKIP1;
	}

      //new proposal from covariance matrix
      if(nstep>=NSTEP_BURN && nstep<NSTEP_CONVERGE)
	{
	  for(j=1;j<=n;++j)
	    {
	      avg1[j]=0;
	      for(k=1;k<=n;++k)
		cov1[j][k]=0;
	    }
	  for(i=1;i<=nstep;++i)
	    {
	      for(j=1;j<=n;++j)
		{
		  avg1[j]+=chain[i][j];
		  for(k=1;k<=n;++k)
		    cov1[j][k]+=chain[i][j]*chain[i][k];
		}
	    }
	  for(i=1;i<=n;++i)
	    for(j=1;j<=n;++j)
	      tmp[i][j] = cov1[i][j]/nstep - avg1[i]*avg1[j]/(nstep*nstep);
	  
	  jacobi(tmp,n,eval,evect,&nrot);
	  gaussj(evect,n,tmp1,1);

	  for(i=1;i<=n;++i)
	    eval[i] = fabs(eval[i]);
	}
      
      
      for(i=1;i<=n;++i)
	atemp[i] = gasdev(&IDUM)*sqrt(eval[i])*stepfac;
      
      for(i=1;i<=n;++i)
	for(a[i]=0,j=1;j<=n;++j)
	  a[i] += atemp[j]*evect[j][i];
      
      for(i=1;i<=n;++i) 
	a[i] += aprev[i];
      
      
    SKIP1:

      if(i=prior_violation(n,a,prior1,prior2)) {
	//fprintf(stdout,"%d\n",i);
	for(i=1;i<=n;++i) 
	  a[i] = aprev[i];
	continue;
      }
      chi2 = chi2func(a,n);
      if(chi2<chi2min)
	{
	  chi2min = chi2;
	  for(i=1;i<=n;++i)
	    amin[i] = a[i];
	}
      
      nstep++;
      nacc++;
      //accept with probability
      if(!(chi2<chi2prev || drand48() <= exp(-(chi2-chi2prev)/2)))
	{
	  nacc--;
	  chi2 = chi2prev;
	    for(i=1;i<=ndim;++i)
	      a[i] = aprev[i];
	}
      
      // put this element in the chain if we're below convergence
      if(nstep<=NSTEP_CONVERGE)
	for(i=1;i<=ndim;++i)
	  chain[nstep][i] = a[i];
      
      
      fprintf(fp,"%d %d %e ",nstep,nacc,chi2);
      for(i=1;i<=n;++i)
	fprintf(fp," %e",a[i]);
      fprintf(fp,"\n");
      fflush(fp);
      chi2prev = chi2;
      for(i=1;i<=n;++i)
	aprev[i] = a[i];
    }

  OUTPUT = 1;
  chi2=chi2func(amin,n);

  fprintf(stdout,"%d %d %e ",niter,nacc,chi2min);
  for(i=1;i<=n;++i)
    fprintf(stdout,"%e ",amin[i]);
  fprintf(stdout,"\n");


}

float chi2func(float *a, int n)
{
  char aa[1000];
  float mh[10000], gc[10000];
  FILE *fp;
  int i,j,nbin=100,n1;
  double x, y, e, chi2=0;

  // first, need to make a batch file
  fp = fopen("gc.bat","w");
  for(j=0,i=1;i<=ntotal;++i)
    {
      if(ifree[i])fprintf(fp,"%e\n",a[++j]);
      else fprintf(fp,"%e\n",atotal[i]);
    }
  fclose(fp);
  
  // now run the script
  system("./sh.tree_mcmc");

  // now read in the result and get a chi^2
  fp = openfile("gcmass.dat");
  n1 = filesize(fp);
  for(i=1;i<=n1;++i)
    {
      fscanf(fp,"%f %f",&mh[i], &gc[i]);
      fgets(aa,1000,fp);
    }
  fclose(fp);

  // bin the relation to get the mean relation
  j =0;
  for(i=1;i<=n1;++i)
    {
      if(j==nbin) 
	{ 
	  x = x/nbin;
	  y = y/nbin;
	  e = (e/nbin-y*y)/(nbin-1); // error in the mean
	  chi2 += ((y-x) + 4.4)*((y-x) + 4.4)/e;
	  //printf("%e %e %e %e\n",x,y,e,y-x);
	  x = y = e = j = 0;
	}
      x += log10(mh[i]);
      y += log10(gc[i]);
      e += log10(gc[i])*log10(gc[i]);
      j++;
    }
  //fprintf(stderr,"CHI2 %e\n",chi2);
  return chi2;
}

/* check the priors
 */
int prior_violation(int n, float *a, float *p1, float *p2)
{
  int i;
  return 0;
  for(i=1;i<=n;++i)
    {
      if(a[i]<=p1[i])return -i;
      if(a[i]>=p2[i])return i;
    }
  return 0;
}