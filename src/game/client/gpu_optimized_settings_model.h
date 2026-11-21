// Pure model. Input: user-supplied 92-desktop-GPU ranking (gpu_db/gpus.csv).
#ifndef GPU_OPTIMIZED_SETTINGS_MODEL_H
#define GPU_OPTIMIZED_SETTINGS_MODEL_H
#include <math.h>
#include <string.h>

namespace GpuModel
{
enum Vendor { NVIDIA=0x10de, AMD=0x1002, INTEL=0x8086, APPLE=0x106b };
enum Source { SOURCE_TIMESPY, SOURCE_GAMING, SOURCE_TFLOPS, SOURCE_ESTIMATE, SOURCE_INTEGRATED, SOURCE_UNKNOWN };
struct Row { unsigned int m_nVendorID; const char *m_pszName; float m_flTimeSpy, m_flGaming, m_flTflops; };
struct Result { int m_nRow; Source m_Source; float m_flScore, m_flTimeSpyEquivalent, m_flMobileFactor; };

namespace Detail
{
// Split on punctuation AND letter/digit transitions; never silently truncate a name.
// Retain unknown tokens, so a recognized base with an unlisted suffix can only estimate.
struct Words
{
	char word[32][24]; int count; bool mobile, maxQ, graphics, radeon;
	bool at(int i, const char *s) const { return i>=0 && i<count && !strcmp(word[i],s); }
	bool has(const char *s) const { for(int i=0;i<count;++i) if(at(i,s)) return true; return false; }
	int number(int i) const
	{
		if(i<0 || i>=count || strlen(word[i])>5) return -1;
		int n=0; for(const char *p=word[i];*p;++p) { if(*p<'0'||*p>'9') return -1; n=n*10+*p-'0'; } return n;
	}
};
inline int Kind(char c) { return c>='0'&&c<='9'?2:((c>='a'&&c<='z')||(c>='A'&&c<='Z')?1:0); }
inline bool Lex(const char *s, Words &w)
{
	w.count=0; w.mobile=w.maxQ=w.graphics=w.radeon=false;
	if(!s) return false;
	while(*s)
	{
		if(s[0]=='(' && (s[1]=='r'||s[1]=='R') && s[2]==')') { s+=3; continue; }
		int k=Kind(*s); if(!k) { ++s; continue; }
		if(w.count==32) return false;
		char *t=w.word[w.count]; int n=0;
		while(Kind(*s)==k) { if(n==23) return false; t[n++]=*s>='A'&&*s<='Z'?char(*s+32):*s; ++s; } t[n]=0;
		if(!strcmp(t,"graphics")) { w.graphics=true; continue; }
		if(!strcmp(t,"radeon")) { w.radeon=true; continue; }
		if(!strcmp(t,"mobile")||!strcmp(t,"laptop")||!strcmp(t,"notebook")) { w.mobile=true; continue; }
		if(!strcmp(t,"nvidia")||!strcmp(t,"geforce")||!strcmp(t,"amd")||!strcmp(t,"intel")||!strcmp(t,"apple")||!strcmp(t,"gpu")||!strcmp(t,"tm")) continue;
		++w.count;
	}
	// Strip ONLY the full Max-Q decoration (with optional 'with' and 'design').
	for(int i=0;i<w.count;++i) if(w.at(i,"maxq")||(w.at(i,"max")&&w.at(i+1,"q")))
	{
		int end=i+(w.at(i,"maxq")?1:2); if(w.at(end,"design")) ++end;
		int begin=i-(w.at(i-1,"with")?1:0); memmove(w.word[begin],w.word+end,(w.count-end)*sizeof(w.word[0]));
		w.count-=end-begin; w.mobile=w.maxQ=true; break;
	}
	return true;
}

enum Family { NONE, GTX, RTX, WORKSTATION, RX, POLARIS, ARC, M_SERIES };
struct Model
{
	Family family;
	int sku, generation, tier, gb, cores;
	bool ti, super, xt, xtx, gre, pro, exact, mobile;
	bool SameTier(const Model &other) const
	{
		return tier==other.tier && ti==other.ti && super==other.super &&
			xt==other.xt && xtx==other.xtx && gre==other.gre && pro==other.pro;
	}
};
inline Model Parse(unsigned vendor, const Words &w)
{
	Model m={}; int i=0;
	if(vendor==NVIDIA && (w.at(0,"rtx")||w.at(0,"gtx")))
	{
		bool gtx=w.at(i++,"gtx"); m.pro=w.at(i,"pro"); i+=m.pro;
		int n=w.number(i++); if(n<100||n>9999) return m;
		m.sku=n;
		if(!gtx && (w.at(i,"ada")||w.at(i,"blackwell"))) { m.family=WORKSTATION; m.generation=w.at(i++,"ada")?4:5; m.tier=n/100; }
		else if(!m.pro && n%100>=10 && n%100<=90) { m.family=gtx?GTX:RTX; m.generation=n/100; m.tier=n%100; }
		m.ti=w.at(i,"ti"); i+=m.ti; m.super=w.at(i,"super"); i+=m.super;
	}
	else if(vendor==AMD && (w.at(0,"rx") || (w.number(0)>=1000 && (w.at(1,"s")||w.at(1,"m")))))
	{
		i=w.at(0,"rx")?1:0; int n=w.number(i++); if(n<100||n>99999) return m;
		m.sku=n;
		m.family=n<1000?POLARIS:RX; m.generation=n/(n<1000?100:1000);
		int tail=n%1000; m.tier=n<1000?n%100:(tail>=100?tail/10:tail);
		m.mobile=w.at(i,"m")||w.at(i,"s"); i+=m.mobile;
		m.xt=w.at(i,"xt"); i+=m.xt; m.xtx=w.at(i,"xtx"); i+=m.xtx; m.gre=w.at(i,"gre"); i+=m.gre;
	}
	else if(vendor==INTEL && w.at(i++,"arc"))
	{
		m.pro=w.at(i,"pro"); i+=m.pro; if(i>=w.count||strlen(w.word[i])!=1) return m;
		m.generation=w.word[i++][0]-'a'+1; int n=w.number(i++);
		if(m.generation<1||m.generation>26||n<(m.pro?10:100)||n>999) return m;
		m.family=ARC; m.tier=n>=100?n/10:n;
		m.sku=n;
		m.mobile=w.at(i,"m"); i+=m.mobile;
	}
	else if(vendor==APPLE && w.at(0,"m") && w.number(1)>0 && w.at(2,"ultra") && w.number(3)>0 && w.at(4,"core"))
	{ m.family=M_SERIES; m.generation=w.number(1); m.cores=w.number(3); i=5; }
	if(w.number(i)>0 && w.at(i+1,"gb")) { m.gb=w.number(i); i+=2; }
	m.exact=i==w.count; return m;
}
inline double Position(const Model &m)
{
	return (m.tier + 4*m.ti + 2*m.super + 2*m.xt + 5*m.xtx - 2*m.gre)/10.0;
}
inline double Clamp(double x,double lo,double hi) { return x<lo?lo:(x>hi?hi:x); }
inline int Epoch(const Model &m)
{
	// GTX 9xx, 10xx, 16xx are consecutive architectures; RX 8xxx is absent.
	if(m.family==RTX) return m.generation/10;
	if(m.family==GTX && m.generation>=16) return m.generation-5;
	if(m.family==RX && m.generation>=9) return m.generation-1;
	return m.generation;
}
}

inline Result Recommend(const Row *rows,int count,unsigned vendor,const char *name,unsigned dedicatedMiB)
{
	using namespace Detail;
	Result r={-1,SOURCE_UNKNOWN,0,0,1}; Words w;
	if(!Lex(name,w)) return r;
	Model q=Parse(vendor,w);
	if(q.family==NONE)
	{
		if((vendor==INTEL && (w.has("iris") || w.has("uhd") ||
			(w.graphics && (w.has("hd") || w.at(0,"arc") || !w.count)))) ||
			(vendor==AMD && ((!w.count && w.radeon && w.graphics) ||
			(w.radeon && w.has("vega") && !w.has("rx")) ||
			(w.number(0)>=100 && w.number(0)<1000 && w.at(1,"m")))))
		{ r.m_Source=SOURCE_INTEGRATED; r.m_flScore=r.m_flTimeSpyEquivalent=1000; }
		return r;
	}
	if(w.mobile||q.mobile) r.m_flMobileFactor=(q.tier<=60?0.70f:q.tier<=75?0.76f:0.82f)-(w.maxQ?0.10f:0.0f);
	// Weighted log-linear ridge fit, centred on this query, from same-family measured rows only.
	// Accumulate sufficient statistics while looking up the exact parsed identity.
	double sw=0,sx=0,sz=0,sy=0,sxx=0,szz=0,sxz=0,sxy=0,szy=0,highest=0;
	int newest=-100, oldest=100; const int gen=Epoch(q); const double tier=Position(q);
	int peerGen=-100;
	double peerCap=0;
	for(int i=0;rows && i<count;++i)
	{
		if(rows[i].m_nVendorID!=vendor) continue;
		Words entry; if(!Lex(rows[i].m_pszName,entry)) continue;
		Model m=Parse(vendor,entry); if(m.family!=q.family||!m.exact) continue;
		int gb=q.gb; if(!gb && m.gb) gb=dedicatedMiB>=12*1024?16:8;
		if(q.exact && q.generation==m.generation && q.sku==m.sku && q.SameTier(m) &&
			q.cores==m.cores && (!m.gb || gb==m.gb))
		{
			const Row &row=rows[i]; float scale=1;
			if(row.m_flTimeSpy>0) { r.m_Source=SOURCE_TIMESPY; r.m_flScore=row.m_flTimeSpy; }
			else if(row.m_flGaming>0) { r.m_Source=SOURCE_GAMING; r.m_flScore=row.m_flGaming; scale=1739.f/7; }
			else if(row.m_flTflops>0) { r.m_Source=SOURCE_TFLOPS; r.m_flScore=row.m_flTflops; scale=1739.f/1.9f; }
			else continue;
			r.m_nRow=i; r.m_flTimeSpyEquivalent=r.m_flScore*scale*r.m_flMobileFactor; return r;
		}
		if(rows[i].m_flTimeSpy<=0||q.family==M_SERIES) continue;
		int epoch=Epoch(m); if(epoch>newest) newest=epoch; if(epoch<oldest) oldest=epoch;
		if(q.SameTier(m) && epoch<=gen && epoch>=peerGen)
		{
			if(epoch>peerGen || rows[i].m_flTimeSpy>peerCap) peerCap=rows[i].m_flTimeSpy;
			peerGen=epoch;
		}
		double x=epoch-gen,z=Position(m)-tier,y=log(rows[i].m_flTimeSpy);
		double weight=1/(1+x*x+z*z); sw+=weight; sx+=weight*x; sz+=weight*z; sy+=weight*y;
		sxx+=weight*x*x; szz+=weight*z*z; sxz+=weight*x*z; sxy+=weight*x*y; szy+=weight*z*y;
		if(rows[i].m_flTimeSpy>highest) highest=rows[i].m_flTimeSpy;
	}
	if(sw>0)
	{
		// Ridge penalty 0.25 on slopes (not intercept); cap total future uplift at 25%,
		// even for several unseen generations. Do not extrapolate tier slopes unchecked.
		double xx=sxx-sx*sx/sw+.25,zz=szz-sz*sz/sw+.25,xz=sxz-sx*sz/sw;
		double xy=sxy-sx*sy/sw,zy=szy-sz*sy/sw,det=xx*zz-xz*xz;
		double b=Clamp((xy*zz-zy*xz)/det,0,0.70), c=Clamp((zy*xx-xy*xz)/det,0,0.85);
		double pred=sy/sw+b*(Clamp(gen,oldest,newest)-gen-sx/sw)-c*sz/sw;
		if(gen>newest) pred+=Clamp(b*(gen-newest),0,log(1.25));
		double cap=1.25*((gen>newest && peerCap>0)?peerCap:highest);
		r.m_Source=SOURCE_ESTIMATE;
		r.m_flScore=(float)exp(Clamp(pred,log(100.),log(cap)));
		r.m_flTimeSpyEquivalent=r.m_flScore*r.m_flMobileFactor;
	}
	return r;
}
}
#endif
