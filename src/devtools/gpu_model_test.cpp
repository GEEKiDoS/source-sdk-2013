// Standalone C++ test. gpu_model_fit_test.py supplies the CSV fixture include.
#include "gpu_optimized_settings_model.h"
#include <stdio.h>
#include <stdlib.h>
using namespace GpuModel;
static Row rows[]={
#include "gpu_model_rows.inc"
};
static const int count=sizeof(rows)/sizeof(rows[0]);
static void check(bool ok,const char *name,int line)
{
	if(!ok) { fprintf(stderr,"line %d: %s\n",line,name); exit(1); }
}
#define CHECK(ok,name) check((ok),(name),__LINE__)
static Result query(unsigned v,const char *s,unsigned mib=0) { return Recommend(rows,count,v,s,mib); }
static Result match(unsigned v,const char *s,unsigned mib,const char *want)
{
	Result r=query(v,s,mib);
	CHECK(r.m_nRow>=0 && !strcmp(rows[r.m_nRow].m_pszName,want),s);
	return r;
}
static Result estimate(unsigned v,const char *s,float lo,float hi)
{
	Result r=query(v,s); CHECK(r.m_Source==SOURCE_ESTIMATE && r.m_nRow==-1,s);
	CHECK(r.m_flScore>=lo && r.m_flScore<=hi,s); return r;
}
static void integrated(unsigned v,const char *s)
{
	Result r=query(v,s); CHECK(r.m_Source==SOURCE_INTEGRATED && r.m_nRow==-1 && r.m_flTimeSpyEquivalent<1739,s);
}
int main()
{
	CHECK(count==92,"source row count");
	for(int i=0;i<count;++i)
	{
		const Row &row=rows[i]; Result r=query(row.m_nVendorID,row.m_pszName);
		CHECK(r.m_nRow==i && r.m_flMobileFactor==1,row.m_pszName);
		CHECK(r.m_Source==(row.m_flTimeSpy?SOURCE_TIMESPY:SOURCE_GAMING),row.m_pszName);
		CHECK(r.m_flScore==(row.m_flTimeSpy?row.m_flTimeSpy:row.m_flGaming),row.m_pszName);
	}
	match(NVIDIA,"nViDia GeForce GTX1050-ti",0,"GTX 1050 Ti");
	match(NVIDIA,"GeForce RTX 4070 Ti SUPER",0,"RTX 4070 Ti SUPER");
	match(NVIDIA,"NVIDIA GeForce RTX 4060 Ti",12287,"RTX 4060 Ti 8GB");
	match(NVIDIA,"NVIDIA GeForce RTX 4060 Ti",12288,"RTX 4060 Ti 16GB");
	match(NVIDIA,"RTX 5060 Ti",0,"RTX 5060 Ti (8GB)");
	match(NVIDIA,"RTX 5060 Ti",16384,"RTX 5060 Ti (16GB)");
	match(NVIDIA,"RTX 5060 Ti (8GB)",16384,"RTX 5060 Ti (8GB)");
	match(APPLE,"Apple M5 Ultra GPU (64-Core)",0,"M5 Ultra GPU (64-core)");
	match(INTEL,"Intel(R) Arc(TM) A770 Graphics",0,"Arc A770");
	match(AMD,"AMD Radeon RX 6600",0,"RX 6600");
	match(AMD,"AMD Radeon RX6600XT",0,"RX 6600 XT");
	Result low=match(NVIDIA,"RTX 3060 Laptop GPU",0,"RTX 3060");
	CHECK(low.m_flScore==8754 && low.m_flTimeSpyEquivalent==8754*low.m_flMobileFactor,"mobile raw vs adjusted");
	CHECK(match(NVIDIA,"NVIDIA GeForce RTX 4060 Laptop GPU",8192,"RTX 4060").m_flMobileFactor==low.m_flMobileFactor,"RTX mobile tier");
	CHECK(match(NVIDIA,"GTX 1650 with Max-Q Design",0,"GTX 1650").m_flMobileFactor<low.m_flMobileFactor,"Max-Q penalty");
	CHECK(match(NVIDIA,"RTX 4080 Mobile",0,"RTX 4080").m_flMobileFactor>low.m_flMobileFactor,"mobile tier scaling");
	CHECK(match(AMD,"AMD Radeon RX 6600M",0,"RX 6600").m_flMobileFactor<1,"AMD M");
	CHECK(match(AMD,"Radeon 7600S",0,"RX 7600").m_flMobileFactor<1,"AMD S");
	CHECK(match(INTEL,"Intel(R) Arc(TM) A770M Graphics",16384,"Arc A770").m_flMobileFactor<1,"Intel M");
	integrated(INTEL,"Intel(R) Iris(R) Xe Graphics"); integrated(INTEL,"Intel UHD Graphics 630");
	integrated(INTEL,"Intel Arc Graphics"); integrated(AMD,"AMD Radeon 780M Graphics");
	integrated(AMD,"AMD Radeon 890M"); integrated(AMD,"AMD Radeon(TM) Graphics");
	estimate(NVIDIA,"NVIDIA GeForce RTX 6050",5000,10200*1.25f);
	estimate(INTEL,"Intel(R) Arc(TM) C770",6000,13554*1.25f);
	Result xt=estimate(AMD,"AMD Radeon RX 10070 XT",8000,29556*1.25f);
	CHECK(estimate(AMD,"RX 10060 XT",6000,16348*1.25f).m_flScore<xt.m_flScore,"future tier order");
	estimate(NVIDIA,"GeForce RTX 4060 Ti Fury GPU",2000,40000);
	estimate(INTEL,"Arc A771",2000,25000);
	CHECK(query(AMD,"GeForce RTX 4090").m_Source==SOURCE_UNKNOWN,"vendor mismatch");
	CHECK(query(NVIDIA,"RTX 12345678901234567890").m_Source==SOURCE_UNKNOWN,"oversize number");
	CHECK(query(0,"Mystery Graphics").m_Source==SOURCE_UNKNOWN,"unknown vendor");
	CHECK(query(INTEL,0).m_Source==SOURCE_UNKNOWN,"null name");
	Row ref={NVIDIA,"GTX 1050",0,7,1.9f};
	Result r=Recommend(&ref,1,NVIDIA,ref.m_pszName,0);
	CHECK(r.m_Source==SOURCE_GAMING && r.m_flScore==7 && fabs(r.m_flTimeSpyEquivalent-1739)<0.01,"gaming anchor");
	ref.m_flGaming=0; r=Recommend(&ref,1,NVIDIA,ref.m_pszName,0);
	CHECK(r.m_Source==SOURCE_TFLOPS && r.m_flScore==1.9f && fabs(r.m_flTimeSpyEquivalent-1739)<0.01,"TFLOPS anchor");
	// True leave-one-row-out: exclude all fields of held row, predict using its name only.
	unsigned vendors[]={NVIDIA,AMD,INTEL,APPLE}; const char *names[]={"Nvidia","AMD","Intel","Apple"};
	for(int v=0;v<4;++v)
	{
		double ae=0,ape=0; int n=0;
		for(int i=0;i<count;++i) if(rows[i].m_nVendorID==vendors[v] && rows[i].m_flTimeSpy>0)
		{
			rows[i].m_nVendorID=0; Result pred=query(vendors[v],rows[i].m_pszName); rows[i].m_nVendorID=vendors[v];
			CHECK(pred.m_Source==SOURCE_ESTIMATE,rows[i].m_pszName);
			double error=fabs(pred.m_flScore-rows[i].m_flTimeSpy); ae+=error; ape+=error/rows[i].m_flTimeSpy; ++n;
		}
		if(n) printf("%s: %d measured rows; LOO MAE %.1f Time Spy, MAPE %.2f%%\n",names[v],n,ae/n,100*ape/n);
		else printf("%s: no measured Time Spy; no fit or LOO metric\n",names[v]);
	}
	puts("92 exact rows and adapter contract cases passed");
}
