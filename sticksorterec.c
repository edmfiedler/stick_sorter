#include <rtai_shm.h>
#include <pthread.h>
#include <rtai_sem.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/io.h>
#include <sched.h>
#include <math.h>
#define KEEP_STATIC_INLINE
#include <rtai_lxrt.h>
#include <comedilib.h>

#include "/shome/31340/examples/comedisim12.c"
#define MILLISEC(x)(1000000*(x))/*ms->ns*/
#define PERIOD MILLISEC(5.26)
#define STICKLOG 1000000

typedef struct
{
		double u, dummy;
}statetype;

statetype p;

RT_TASK *g, *k;
pthread_t j;

char STOP_flag = 0;
//double refcon =2048+204.8*(-4.2); 	
double ref;
double refcon = 0;
double beltspeed = 0.5;
double beltspeed_c = 2048+204.8*(0.5);
double tacho = 0.3920;
double e; 
//double b0 = 1.088*3.5;
double b0 = 85.09722;
//double b0 = 3.5;
//double b1 = -0.9116*3.5;
double b1 = -72.6782;
//double b1 = -3;
//double a1 = -1;
double a1 = -0.4343;
//double a1 = -1;
double ad_in;
double pos;


void regInit(statetype *p)
{
		p -> u = 0;
		p -> dummy = 0;
		ref = refcon;
}

void regUpdate(statetype *p,double e, double b1, double a1)
{
		p -> dummy = e * b1 - (p -> u) * a1;
}

void regOut(statetype *p, double e, double b0)
{
		p -> u = e * b0 + (p -> dummy); 
}

double convert_AD(lsampl_t out){
	double v;
	v = ((double)out-2048.0)*20.0/4096.0;
	//printf("%f\n",v);
	return v;
}

lsampl_t convert_DA(double v){
	lsampl_t ci;
	/*
	ci = (v+10)*4096.0/20.0;
	if (ci<0)
	{
		ci = 0;
	}

	if (ci>4095)
	{
		ci = 4095;
	}
	printf("%d\n",ci);
	*/
	if (v<-10)
	{
		v = -9.9;
	}
	if (v>10)
	{
		v = 9.9;
	}
	ci = (v+10)*4096.0/20.0;
	//printf("%d\n",ci);
	return ci;
}

void *Regulator(void *arg) {

		g = rt_task_init(nam2num("h101"),0,0,0);
		rt_task_make_periodic_relative_ns(g,0,10000000);

		lsampl_t out;
		lsampl_t dat_DA;
		lsampl_t st_d;
		lsampl_t beltData;
		double belt_ref = 0, e_b = 0, b0_b = 1.088*3.5, b1_b = -0.9116*3.5, a1_b = -1, dummy_b = 0, u_b = 0;
		double sticklength = 0.075, st_upper =0, st_lower =0;
		double ref_b = 0.1;
		double st_b=0,st_e=0,st_diff=0,st_lat=0,t_current=0,reset_diff=0;
		double st_bf1 = 0, st_bf2 = 0, st_bf3 = 0, st_bf4 = 0;
		double ti_bf1 = 0, ti_bf2 = 0, ti_bf3 = 0, ti_bf4 = 0;
		float sticklogbuf[STICKLOG][4];
		int logcount = 0;
		double real_bs = 0;
		FILE *f;

		int i_s;

		comedi_t * dev0 = comedi_open("/dev/comedi2");

		regInit(&p);

		while (STOP_flag == 0)
		{
			comedi_data_read_delayed(dev0, 0, 0, 0, AREF_DIFF, &out, 50000);
			comedi_data_read_delayed(dev0, 0, 2, 0, AREF_DIFF, &st_d, 50000);
			comedi_data_read_delayed(dev0, 0, 1, 0, AREF_DIFF, &beltData, 50000);

			belt_ref = beltspeed_c;
			if (beltspeed == 0)
			{
				comedi_data_write(dev0, 1, 1, 0, AREF_GROUND, 2048);
			}
			else
			{ 
				e_b = (double)(belt_ref-(int)beltData);

				//Getting real belt speed
				real_bs = ((int)beltData - 2048)/204.8;

				u_b = e_b*b0_b+dummy_b;
				int o_b = round(u_b);

				if (o_b > 4095)
				{
					o_b = 4095;
				} else if (o_b < 0){
					o_b = 0;
				}

				comedi_data_write(dev0, 1, 1, 0, AREF_GROUND, o_b);
				dummy_b = b1_b*e_b - a1_b*u_b;

			}

			//printf("%d\n",st_d);


			if (st_d>3500)
			{
				st_b = rt_get_time_ns();
			}

			if (st_d<3500)
			{
				st_diff = (st_b-st_e)*0.000000001;
				st_e = rt_get_time_ns();
				
			}


			if (st_diff>0.0001)
			{
				st_lat = st_diff;
				
				st_bf4 = st_bf3;
				st_bf3 = st_bf2;
				st_bf2 = st_bf1;
				

				ti_bf4 = ti_bf3;
				ti_bf3 = ti_bf2;
				ti_bf2 = ti_bf1;
			}

			st_bf1 = ref_b;
			ti_bf1 = st_b*0.000000001+0.5;

			t_current = rt_get_time_ns();
			//printf("%f\n",time_delayed-t_current*0.000000001);
			if(ti_bf1>t_current*0.000000001)
			{
				ref = st_bf1;
			}

			if(ti_bf2>t_current*0.000000001)
			{
				ref = st_bf2;
			}

			if(ti_bf3>t_current*0.000000001)
			{
				ref = st_bf3;
			}

			if(ti_bf4>t_current*0.000000001)
			{
				ref = st_bf4;
			}
			
			st_upper = (sticklength/beltspeed)*1.25;
			st_lower = (sticklength/beltspeed)*0.75;
			if (st_lat<st_lower)
			{
				ref_b = -0.2;
			}

			if (st_lat<st_upper && st_lat>st_lower)
			{
				ref_b = 0;
			}

			if (st_lat>st_upper)
			{
				ref_b = 0.2;
			}
			
			/*
			t_current = rt_get_time_ns();
			reset_diff = (t_current-st_b)*0.000000001;
			if (reset_diff>st_upper)
			{
				st_lat = 0.13;
			}
			*/

			//Convert Bits to Voltage
			ad_in = convert_AD(out)-5.3;
			//printf("%f\n",out);
			//Convert Voltage to rad using Kpot
			pos = ad_in/1.628;

			//Build data array
			sticklogbuf[logcount][0]=pos;
			sticklogbuf[logcount][1]=ref;
			sticklogbuf[logcount][2]=real_bs;
			sticklogbuf[logcount][3]=st_d;
			logcount++;
    			if (logcount>= SIMLOGBUFSIZE)
			{
       				logcount=SIMLOGBUFSIZE-1;
			}
			
			e = (ref-pos);
			regOut(&p, e, b0);
									
			dat_DA = convert_DA(p.u);
			
			/*
			ci = p.u;

			//Convert Voltage to bit
			ci = (ci+10)*4096.0/20.0;
			//printf("%f\n",ci);
			if (ci > 4095)
			{
				ci = 4095;
			}
			if(ci < 0)
			{
				ci = 0;
			}
			*/

			comedi_data_write(dev0, 1, 0, 0, AREF_GROUND, dat_DA);
			
			regUpdate(&p, e, b1, a1);

			rt_task_wait_period();
		}

		rt_task_delete(g);
		comedi_data_write(dev0, 1, 1, 0, AREF_GROUND, 2048);
		comedi_close(dev0);

		f=fopen("sticklog","w");
  		if (f){
    			for (i=0;i<logcount;i++)
      				fprintf(f,"%f %f %f %f\n",sticklogbuf[i][0],sticklogbuf[i][1],sticklogbuf[i][2],sticklogbuf[i][3]);
    		fclose(f);
  		}

		
		return 0;
}


int main()
{
		rt_allow_nonroot_hrt();
		mlockall(MCL_CURRENT|MCL_FUTURE);

		k = rt_task_init(nam2num("h102"), 0, 0, 0);
		printf("Press any key to exit. \n");
		pthread_create(&j, NULL, Regulator, NULL);
					
	 	getchar();
 		STOP_flag = 1;
		pthread_join(j, NULL);

		rt_task_delete(k);
		return 0;
}
