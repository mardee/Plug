#pragma once
#include <array>
#include <vector>
#include <random>
#include <numeric>
#include <cmath>
#include <algorithm>
namespace lattice {
constexpr int ppq=480, tracks=8, phraseBars=16;
inline constexpr const char* names[8]={"Kick","Snare","Rim","Hat","Open","Tom","Ride","Crash"};
inline constexpr int notes[8]={36,38,37,42,46,45,51,49}; // Crash = GM 49
struct Cell { int start=0, length=120, velocity=0; };
using Lane=std::vector<Cell>;
struct Pattern {
 int numerator=4, denominator=4; double bpm=100; int seed=1729; float density=0.55f; bool ghost=true, accents=true;
 // new interface fields
 float development=.45f, fill=.5f, space=.35f; // 0..1 generative params
 int gridStep=60; // 60 or 120
 std::vector<int> groups{3,3,2};
 std::array<bool,8> locked{};
 std::array<bool,16> barLocked{}; // per-bar lock
 std::array<std::array<Lane,8>,16> bars;
 int barTicks() const { return numerator*1920/denominator; }
 int eighths() const { return numerator*8/denominator; }
 bool valid() const { return numerator>=1 && numerator<=16 && (denominator==4 || denominator==8) && !groups.empty() && std::all_of(groups.begin(),groups.end(),[](int n){return n>0;}) && std::accumulate(groups.begin(),groups.end(),0)==eighths(); }
 void clear() { int step=60; for(int b=0;b<phraseBars;++b) for(int r=0;r<tracks;++r) {auto& l=bars[b][r]; l.clear(); for(int t=0;t<barTicks();t+=step) l.push_back({t,step,0});} }
 Pattern(){clear();generate();}
 void meter(int n,int d) {numerator=std::clamp(n,1,16); denominator=d==8?8:4;groups={eighths()};locked.fill(false);barLocked.fill(false);clear();}
 bool generate(){
  if(!valid())return false;
  const int steps=barTicks()/60;
  using Beat=std::array<std::vector<int>,tracks>;
  std::array<Beat,2> motif;
  std::mt19937 rng(static_cast<unsigned>(seed));
  auto chance=[&](double probability){return double(rng())/4294967296.0<std::clamp(probability,0.0,1.0);};
  const int accentVelocity=accents?127:80;
  std::vector<int> boundaries;int sum=0;for(int g:groups){boundaries.push_back(sum*4);sum+=g;}
  // Compose a two-bar question/answer once; every later bar inherits this material.
  for(int m=0;m<2;++m){auto& a=motif[m];for(auto& row:a)row.assign(steps,0);
   a[0][0]=accentVelocity;
   for(int s:boundaries)if(s>0&&s<steps&&chance(.45+.4*density))a[0][s]=80;
   int answer=steps>=16?steps/2:std::max(1,steps/2);answer=(answer/2)*2;
   if(m==1&&steps>=24)answer=std::min(steps-2,answer+2);
   a[1][answer]=accentVelocity;a[0][answer]=0;
   for(int s=2;s<steps;s+=2){
    if(a[0][s]==0&&a[1][s]==0&&chance(density*.12))a[0][s]=80;
    if(s%4==0&&chance(.65+density*.3))a[3][s]=(s%8==0?80:40);
   }
   // Ghost pairs belong to the main snare, never independent random empty-slot fill.
   if(ghost&&density>.2){if(answer>=2&&a[0][answer-2]==0)a[1][answer-2]=40;
    if(m==1&&answer+2<steps&&a[0][answer+2]==0)a[1][answer+2]=40;}
   // A recognisable gap is part of the source motif and therefore repeats with it.
   int gap=std::min(steps-1,int(space*6)*2);
   if(m==1)for(auto& row:a)for(int s=steps-gap;s<steps;++s)row[s]=0;
  }
  for(int b=0;b<phraseBars;++b){
   Beat a=motif[b%2];const int section=b/4;const bool boundary=(b%4==3);
   // Development modifies peripheral positions, never re-rolls the main sentence.
   if(section>0&&development>0){
    for(int s=2;s<steps-2;s+=2){
     if(a[1][s]>=80||s==steps/2)continue;
     if(chance(development*(section==2?.23:.10))){
      if(a[0][s])a[0][s]=0;else if(a[1][s]==0&&chance(density))a[0][s]=80;
     }
    }
    if(section==2&&development>.3)for(int s=0;s<steps;++s){a[6][s]=a[3][s];a[3][s]=0;}
   }
   // Plan breath first. Ornaments and fills below cannot enter this reserved region.
   int rest=0;
   if(b%2==1)rest=std::min(steps-1,int(space*(boundary?12:6))*2);
   const int stop=steps-rest;
   if(boundary&&fill>0&&chance(fill)){
    const int type=int(rng()%4);const int length=std::min(stop,std::max(2,int((b==15?16:8)*fill)));
    const int begin=std::max(0,stop-length);
    for(auto& row:a)for(int s=begin;s<steps;++s)row[s]=0;
    if(type!=3){ // type 3 deliberately ends early instead of a roll
     const int stride=(type==2&&fill>.65&&bpm<=130)?1:2;
     for(int s=begin;s<stop;s+=stride){
      int r=type==0?1:type==1?((s-begin)%4==0?1:5):((s-begin)%4<2?1:5);
      a[r][s]=(s==begin?accentVelocity:((s-begin)%4==0?80:40));
     }
    }
   }
   // Section punctuation, not a crash on every bar. Incoming beat remains recognisable.
   if(b==0||b==8||(b==12&&development>.6)){a[7][0]=accentVelocity;a[3][0]=a[6][0]=0;}
   if(!boundary&&b%2==1&&stop>=4&&density>.6){a[4][stop-2]=80;a[3][stop-2]=0;}
   for(auto& row:a)for(int s=stop;s<steps;++s)row[s]=0;
   if(!ghost)for(auto& row:a)for(int& v:row)if(v==40)v=80;
   if(barLocked[b])continue;
   for(int r=0;r<tracks;++r){if(locked[r])continue;auto& lane=bars[b][r];lane.clear();
    for(int s=0;s<steps;++s)lane.push_back({s*60,60,a[r][s]});}
  }
  return true;
 }
 bool subdivide(int b,int r,int a,int z,int count){if(b<0||b>=phraseBars||r<0||r>=tracks)return false;auto& l=bars[b][r];if(a<0||z<a||z>=int(l.size()))return false;int begin=l[a].start,end=l[z].start+l[z].length,total=end-begin;if(count==0){if(begin%120!=0||total%120!=0)return false;count=total/120;}if(count<=0||total%count)return false;for(int k=a;k<=z;++k)if(l[k].velocity&&(l[k].start-begin)%(total/count)!=0)return false;Lane replacement;for(int i=0;i<count;++i){int t=begin+i*total/count,v=0;for(int k=a;k<=z;++k)if(l[k].start==t)v=l[k].velocity;replacement.push_back({t,total/count,v});}l.erase(l.begin()+a,l.begin()+z+1);l.insert(l.begin()+a,replacement.begin(),replacement.end());return true;}
};
}
