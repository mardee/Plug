#pragma once
#include <array>
#include <vector>
#include <random>
#include <numeric>
#include <cmath>
#include <algorithm>
namespace lattice {
constexpr int ppq=480, tracks=8, phraseBars=16;
inline constexpr const char* names[8]={"Kick","Snare","Low Tom","Hat","Open","Tom","Ride","Crash"};
inline constexpr int notes[8]={36,38,41,42,46,45,51,49}; // Crash = GM 49
struct Cell { int start=0, length=120, velocity=0; };
using Lane=std::vector<Cell>;
// 核心坐标以 60 tick 为单位；hits 的下标就是事件身份，力度不随位置重算。
struct CoreHit { int voice=0, step=0, velocity=127, phrase=0; };
struct CoreWindow { int begin=0, end=0; };
struct CoreMotif {
 int steps=0; unsigned grammar=0;
 std::vector<CoreWindow> windows;
 std::vector<CoreHit> hits;
};
// 纯源句构造：edges 是 generate 校验后的真实 Group 边界（含 0 和小节末）。
// 不读取 dispersion/density/ghost/accents/锁；四类语法决定单问、双问及轮换回答。
// grammar：0 单问收紧，1 双问先紧后宽，2 连问压缩，3 双问交替错开。
// member 为两小节源句中的 0/1；长组内部按两拍分句，余段仍结束在真实边界。
inline CoreMotif buildCoreMotif(const std::vector<int>& edges,unsigned key,int member,bool backbeat){
 CoreMotif core;core.steps=edges.back();core.grammar=(key+unsigned(member))%4u;
 int begin=0;
 for(size_t i=1;i<edges.size();++i){
  const int end=edges[i];
  if(end-begin<8)continue; // 相邻短组并成可容纳问答的窗口，不裁碎事件。
  while(end-begin>20){core.windows.push_back({begin,begin+16});begin+=16;}
  core.windows.push_back({begin,end});begin=end;
 }
 if(begin<core.steps){
  if(core.windows.empty())core.windows.push_back({0,core.steps});
  else core.windows.back().end=core.steps;
 }
 const int count=int(core.windows.size());
 const int selected=int(((key>>4)+unsigned(member))%unsigned(count));
 int doubles=core.grammar==0?0:core.grammar==2?2:1;
 if(core.steps>=24&&count==2)doubles=std::max(1,doubles);
 doubles=std::min(doubles,std::max(0,std::max(5,core.steps/8)-count));
 for(int i=0;i<count;++i){
  const auto& w=core.windows[i];
  const bool pair=w.end-w.begin>=8&&(i-selected+count)%count<doubles;
  const int spread=pair?2:0;
  const int call=w.begin+std::min(int(core.grammar%2u)*2,std::max(0,w.end-w.begin-spread-6));
  int reply=backbeat?w.begin+((w.end-w.begin)/4)*2:w.end-4;
  reply=std::clamp(std::max(reply,call+spread+2),w.begin,w.end-2);
  core.hits.push_back({0,call,127,i});
  if(pair)core.hits.push_back({0,call+spread,80,i});
  core.hits.push_back({1,reply,127,i});
 }
 return core;
}
// 输入为 buildCoreMotif 的结果（不是任意手工 cell）；输出不修改 source。
// 纯关系变奏：只改 step，逐下标保留 voice/velocity/phrase、数量及问句内部间距。
// 四种语法共用「起句提前/延后 + 回答间隔伸缩」，没有逐击随机或第二次 pickup 位移。
// Mid 以 1 step（60 tick）局部重述；High 保留跨拍起句及完整回答间隔变化。
// High 的 <=8-step 窄窗按语法选「问句后最早合法微位置 / 窗口末微位置」，
// 单窗长句选「紧接问句 / 窗尾长答」，不靠 Mid/High 相等检测补位置。
// 只有整句 <=8 steps 才整句恒等；其它窗口仅在起句可行域不足时原样保留。
// 分轨边界保证相邻短句不碰撞，首 Kick 限在 [0,240] tick；单窗不再直接跳过。
inline CoreMotif voiceRhythm(const CoreMotif& source,int dispersion){
 if(dispersion<=0||dispersion>2||source.steps<=8)return source;
 CoreMotif result=source;
 // 每行分别是偶/奇短句的起点位移和回答间隔增量，单位均为 60 tick。
 static constexpr int onset[4][2]={{4,-4},{-4,4},{4,-4},{-4,4}};
 static constexpr int interval[4][2]={{-2,-2},{2,2},{-2,2},{2,-2}};
 for(int i=0;i<int(source.windows.size());++i){
  const auto& w=source.windows[i];
  int first=w.end,last=w.begin;
  for(const auto& h:source.hits)if(h.phrase==i&&h.voice==0){first=std::min(first,h.step);last=std::max(last,h.step);}
  const int shift=onset[source.grammar][i%2]/(dispersion==1?4:1);
  const int gap=dispersion==1?0:interval[source.grammar][i%2];
  const int lower=i==0?0:w.begin-4;
  const int upper=std::min(w.end-(last-first)-6,i==0?4:w.end);
  if(upper<lower)continue;
  const int call=std::clamp(first+shift,lower,upper);
  for(auto& h:result.hits)if(h.phrase==i){
   if(h.voice==0)h.step+=call-first;
   else if(dispersion==2&&w.end-w.begin<=8){
    // 短答紧接整段问句（至少 1 step）；长答用最后一个合法 step。
    // 微位置属于 High 的结构规则，不将两个档位同时夹到 end-2。
    h.step=gap<0?std::max(w.begin+1,call+last-first+1):w.end-1;
   }else if(dispersion==2&&source.windows.size()==1){
    // 足够长的单窗没有相邻句可错开，直接伸缩问答距离而非平移整句。
    h.step=gap<0?call+last-first+2:w.end-2;
   }else h.step=std::clamp(h.step+shift+gap,std::max(w.begin+2,call+last-first+2),w.end-2);
  }
 }
 return result;
}
struct Pattern {
 int numerator=4, denominator=4; double bpm=100; int seed=1729; float density=0.55f; bool ghost=true, accents=true; bool backbeat=false;
 float development=.45f, fill=.5f, space=.35f; // 0..1 generative params
 int dispersion=1; // 生成器：0 贴组头、1 局部重述、2 跨拍问答；手工源仍用旧移动语义
 int gridStep=60; // 60 or 120
 std::vector<int> groups{3,3,2};
 std::array<bool,8> locked{};
 std::array<bool,16> barLocked{}; // per-bar lock
 std::array<std::array<Lane,8>,16> bars;
 int barTicks() const { return numerator*1920/denominator; }
 int eighths() const { return numerator*8/denominator; }
 bool valid() const { return numerator>=1 && numerator<=16 && (denominator==4 || denominator==8) && dispersion>=0 && dispersion<=2 && !groups.empty() && std::all_of(groups.begin(),groups.end(),[](int n){return n>0;}) && std::accumulate(groups.begin(),groups.end(),0)==eighths(); }
 // 只移动快照中的正拍主击；镲保持原位，不增删 cell，不量化手工分割。
 void syncopateBar(std::array<Lane,8>& bar,unsigned seedKey) const {
  if(dispersion<0||dispersion>2)return;
  const auto source=bar;
  const int beat=denominator==8?240:480,shift=beat/4;
  const double probability=dispersion==0?.1:dispersion==1?.35:.65;
  std::array<std::vector<int>,tracks> reserved;
  auto strongAt=[&](int r,int tick){return std::any_of(source[r].begin(),source[r].end(),[&](const Cell& c){return c.start==tick&&c.velocity>=80;});};
  auto claimed=[&](int r,int tick){return std::find(reserved[r].begin(),reserved[r].end(),tick)!=reserved[r].end();};
  for(int r:{0,1,2,5}){
   if(locked[r])continue;
   for(size_t i=0;i<source[r].size();++i){
    const auto& hit=source[r][i];
    if(hit.velocity<80||hit.start<0||hit.start>=barTicks()||hit.start%beat!=0)continue;
    std::mt19937 syncRng(seedKey^0x53594e43u^(unsigned(r)*0x9e3779b9u)^(unsigned(hit.start)*0x85ebca6bu));
    const double threshold=double(syncRng())/4294967296.0;
    const int dir=(syncRng()%2)?1:-1;
    for(int sign:{dir,-dir}){
     const int tick=hit.start+sign*shift;
     if(tick<0||tick>=barTicks()||tick%beat==0||claimed(r,tick))continue;
     if((r==0||r==1)&&(strongAt(1-r,tick)||claimed(1-r,tick)))continue;
     const auto& lane=source[r];
     auto target=std::find_if(lane.begin(),lane.end(),[&](const Cell& c){return c.start==tick&&c.velocity==0&&c.length==hit.length;});
     if(target==lane.end())continue;
     // 先预留位置再判断档位：三档共享候选、方向、阈值及冲突优先级。
     // 不同长度的空 cell 不承接位移，以同时保留音符时值和 cell 结构。
     reserved[r].push_back(tick);
     if(threshold<probability){
      bar[r][size_t(target-lane.begin())].velocity=hit.velocity;
      bar[r][i].velocity=0;
     }
     break;
    }
   }
  }
 }
 void clear() { int step=60; for(int b=0;b<phraseBars;++b) for(int r=0;r<tracks;++r) {auto& l=bars[b][r]; l.clear(); for(int t=0;t<barTicks();t+=step) l.push_back({t,step,0});} }
 Pattern(){clear();generate();}
 void meter(int n,int d) {numerator=std::clamp(n,1,16); denominator=d==8?8:4;groups={eighths()};locked.fill(false);barLocked.fill(false);clear();}
 // bar 为 0..15；返回句末禁止新触发的 60-tick 格数，不裁切采样尾音。
 // 只在第 8/16 小节断句，最长四分之一小节且不超过 8 格；锁定内容优先。
 int phraseRestSteps(int bar) const {
  if(bar<0||bar>=phraseBars||bar%8!=7||numerator<1||numerator>16||
     (denominator!=4&&denominator!=8)||!std::isfinite(development)||development<=0||
     development>1||!std::isfinite(space)||space<=0||space>1)return 0;
  const int cap=std::min(8,barTicks()/60/4);
  int rest=std::max(1,int(std::ceil(double(space)*cap)));
  // 奇数格休止使停前落点位于 120-tick 网格；长度随 Space 单调分档。
  if(rest%2==0)--rest;
  return rest;
 }
 bool generate(){
  // 先校验再计算拍号及组长，避免非法分母、组长累加溢出和 NaN 转整数。
  if(numerator<1||numerator>16||(denominator!=4&&denominator!=8)||
     dispersion<0||dispersion>2||!std::isfinite(bpm)||bpm<=0||
     !std::isfinite(density)||density<0||density>1||
     !std::isfinite(development)||development<0||development>1||
     !std::isfinite(fill)||fill<0||fill>1||!std::isfinite(space)||space<0||space>1||groups.empty())return false;
  int remaining=eighths();
  for(int g:groups){if(g<=0||g>remaining)return false;remaining-=g;}
  if(remaining!=0)return false;
  const int steps=barTicks()/60;
  using Beat=std::array<std::vector<int>,tracks>;
  std::array<Beat,2> motif;
  std::array<Beat,phraseBars> composed;
  std::vector<bool> groupHead(steps,false);
  std::vector<int> groupEdges{0};
  for(int g:groups){groupHead[groupEdges.back()]=true;groupEdges.push_back(groupEdges.back()+g*4);}
  // Group 边界构造核心问答窗口；改分组不重抽通鼓音色或过门概率。
  // 按声部/片段寻址，不共享可变随机流；Ghost、Accents 不参与节奏决策。
  auto choice=[&](unsigned domain,unsigned index){
   unsigned x=static_cast<unsigned>(seed)^domain^(index*0x9e3779b9u);
   x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;x^=x>>16;return x;
  };
  enum class Role { Repeat, Answer, Push, Close };
  struct Plan { Role role=Role::Repeat; int rest=0; bool fill=false,ride=false,crash=false; };
  std::array<Plan,phraseBars> plan;
  const unsigned shape=choice(0x434f5245u,0);
  for(int m=0;m<2;++m){auto& a=motif[m];for(auto& row:a)row.assign(steps,0);
   const auto source=buildCoreMotif(groupEdges,shape,m,backbeat);
   const auto voiced=voiceRhythm(source,dispersion);
   for(const auto& h:voiced.hits)a[h.voice][h.step]=h.velocity;
   // Density 只添组尾次要脚，不删核心；4/4 核心加密后不超过五脚。
   if(density>.72f&&std::count_if(a[0].begin(),a[0].end(),[](int v){return v>0;})<std::max(5,steps/8)){
    for(int i=int(groups.size())-1;i>=0;--i){const int s=groupEdges[i+1]-2;if(a[0][s]==0){a[0][s]=80;break;}}
   }
   for(int s=0;s<steps;s+=4)a[3][s]=groupHead[s]?127:80;
   for(int s=2;s<steps;s+=4)
    if(double(density)>.45+double(choice(0x50554c53u,unsigned(s+steps*m))%45u)/100.0)a[3][s]=80;
  }
  // 核心关系变奏及 Density 已完成；段落只消费两小节源句，不再掷骰移动核心。
  // Close 的停前合击/休止是显式段落例外，不属于 source 的数量力度契约。
  for(int b=0;b<phraseBars;++b){auto& p=plan[b];
   p.rest=phraseRestSteps(b);
   p.crash=(b==0)||(development>0&&b==8);
   if(development<=0)continue;
   if(b==5||b==13)p.role=Role::Answer;
   if(b==14||(development>.3f&&b==10))p.role=Role::Push;
   if(development>.65f&&(b==6||b==11))p.role=Role::Push;
   if(b%8==7)p.role=Role::Close;
   p.fill=p.role==Role::Close&&fill>0&&double(choice(0x46494c4cu,unsigned(b/8)))/4294967296.0<fill;
   p.ride=p.role==Role::Push;
  }
  const int tomStride=bpm>140?4:2;
  const unsigned tomStyle=choice(0x544f4d53u,0)%4u;
  auto tomVoice=[&](unsigned style,int index){
   int r=style==0?5:style==1?2:((index%2==0)==(style==2)?5:2);
   // 单锁时可由另一支完成重复动机，不以双轨全解锁为生成前提。
   if(locked[r]&&!locked[r==5?2:5])r=r==5?2:5;
   return r;
  };
  for(int b=0;b<phraseBars;++b){
   if(development==0&&b>=2){composed[b]=composed[b%2];continue;}
   Beat a=motif[b%2];const auto& p=plan[b];
   const int stop=steps-p.rest;
   const int landing=p.rest>0?stop-1:std::max(0,steps-4);
   const bool answer=p.role==Role::Answer;
   const bool push=p.role==Role::Push;
   if(answer||push){
    // 段落回答是次要层：只补空位，不重抽或移动已完成变奏的 Kick/Snare 核心。
    for(int s=steps/2+2;s<stop-2;s+=2)if(a[1][s]==0){a[1][s]=80;break;}
    if(push)for(int s=2;s<stop;s+=4)a[3][s]=std::max(a[3][s],80);
   }
   const int fillCapacity=std::min(8,steps/4)/tomStride;
   const int fillCount=p.fill?std::min(fillCapacity,1+int(fill*3)):0;
   const int fillBegin=std::max(0,landing-fillCount*tomStride);
   const int tomEnd=p.role==Role::Close?fillBegin:stop;
   // 两击是最小动机：单支重复、下行或上行；Fill=0 仍允许源句回答。
   if(steps>=16&&(answer||(b%2==1&&choice(0x544f4d53u,1)%4u!=0))){
    for(int s=steps/2+2;s+tomStride<tomEnd;s+=2){
     if(a[1][s]||a[1][s+tomStride])continue;
     a[tomVoice(tomStyle,0)][s]=80;a[tomVoice(tomStyle,1)][s+tomStride]=80;break;
    }
   }
   if(p.ride&&!locked[3]&&!locked[6])for(int s=0;s<stop;++s){a[6][s]=a[3][s];a[3][s]=0;}
   if(p.fill){
    const unsigned style=choice(0x464f524du,unsigned(b/8))%5u;
    for(int i=0;i<fillCount;++i){
     const int s=landing-(fillCount-i)*tomStride;
     if(s<0)continue;
     const int r=style==4?1:tomVoice(style,i);
     if(locked[r])continue;
     // Fill 只用军鼓句的空位，不抹去移动后的主击，也绝不拿 Kick 给过门腾位。
     if(a[1][s]>=80)continue;
     for(int hand:{1,2,3,4,5,6})if(!locked[hand])a[hand][s]=0;
     a[r][s]=(i==0?127:80);
    }
   }
   if(p.role==Role::Close){
    // 只计划实际可写的合击；手部留白待锁定实音和单锁避让结算后决定。
    if(!locked[0])a[0][landing]=127;
    if(!locked[1])a[1][landing]=127;
   }
   if(p.crash&&!locked[7]){a[7][0]=127;a[3][0]=a[4][0]=a[6][0]=0;}
   for(auto& row:a)std::fill(row.begin()+stop,row.end(),0);
   composed[b]=std::move(a);
  }
  for(int b=0;b<phraseBars;++b){
   if(barLocked[b])continue;
   auto& a=composed[b];
   Beat held;for(auto& row:held)row.assign(steps,0);
   // 只读取锁定 cell 的触发点；不重建、不量化、不修正其长度或力度。
   for(int r=0;r<tracks;++r)if(locked[r])for(const auto& c:bars[b][r])
    if(c.start>=0&&c.start%60==0&&c.start/60<steps)held[r][c.start/60]=std::max(held[r][c.start/60],c.velocity);
   auto value=[&](int r,int s){return locked[r]?held[r][s]:a[r][s];};
   const int stop=steps-phraseRestSteps(b);
   const int landing=stop<steps?stop-1:std::max(0,steps-4);
   for(int s=0;s<steps;++s){
    // 仅单锁强 Kick/Snare 避让；无锁同击有意保留，双锁完全不动。
    if(locked[0]!=locked[1]&&value(0,s)>=80&&value(1,s)>=80)a[locked[0]?1:0][s]=0;
    // 锁空 Kick/Snare 不构成虚拟落点；只为实际合击让出未锁手部。
    if(plan[b].role==Role::Close&&s==landing&&(value(0,s)>=80||value(1,s)>=80))
     for(int r:{2,3,4,5,6})if(!locked[r])a[r][s]=0;
    bool heldCymbal=false;
    for(int r:{7,4,6,3})heldCymbal|=locked[r]&&held[r][s]>0;
    bool cymbalTaken=heldCymbal;
    for(int r:{7,4,6,3})if(!locked[r]&&a[r][s]>0){
     if(cymbalTaken)a[r][s]=0;else cymbalTaken=true;
    }
    // 两只手的预算优先给锁定轨，再给 Snare/段落镲/通鼓；不限制 Kick 脚部。
    int hands=0;
    for(int r:{1,2,3,4,5,6,7})if(locked[r]&&held[r][s]>0)++hands;
    for(int r:{1,7,5,2,4,6,3})if(!locked[r]&&a[r][s]>0){if(hands>=2)a[r][s]=0;else ++hands;}
   }
   // 恢复锁定 Open 的闭镲，包括 Push 中已改配成 Ride 的位置。
   // 只在本小节休止前、下一次锁定 Open 前找合法位置；无位置或 Hat 已锁时锁优先，
   // 不改任何锁定 cell，也不向计划休止区补闭镲。
   if(locked[4]&&!locked[3])for(const auto& open:bars[b][4])if(open.velocity>0&&open.start>=0&&open.start<stop*60){
    int end=stop*60;
    for(const auto& next:bars[b][4])if(next.velocity>0&&next.start>open.start)end=std::min(end,next.start);
    for(int s=open.start/60+1;s*60<end;++s){
     if(a[3][s]>0)break;
     if(s%4!=0||(plan[b].role==Role::Close&&s==landing))continue;
     bool conflict=false;int hands=0;
     for(int r:{4,6,7})conflict|=locked[r]&&held[r][s]>0;
     for(int r:{1,2,5})if(value(r,s)>0)++hands;
     if(conflict||hands>=2)continue;
     // Ride/Crash 只是未锁配器时才让给闭镲；其它手部击打原样保留。
     for(int r:{4,6,7})if(!locked[r])a[r][s]=0;
     a[3][s]=80;break;
    }
   }
   // 锁冲突和手部预算结算后才开镲：必须已有同小节闭镲，不能生成悬空 Open。
   if(!locked[3]&&!locked[4]&&steps>=12&&density>.15f){
    const int onset=4+8*int(choice(0x4f50454eu,unsigned(b%2))%3u);
    if(onset+4<stop&&a[3][onset]>0&&a[3][onset+4]>0){
     a[4][onset]=a[3][onset];a[3][onset]=0;
    }
   }
   // Ghost 是最终纯加法：不替换正常击打，不影响随机寻址、配器、闭镲或休止。
   if(ghost&&!locked[1]&&density>.2f){
    for(int s=0;s<stop;++s)if(a[1][s]>=80)for(int offset:{-2,2}){
     const int target=s+offset;
     if(target<0||target>=stop||a[1][target]!=0||value(0,target)>=80)continue;
     int hands=0;for(int r:{1,2,3,4,5,6,7})if(value(r,target)>0)++hands;
     if(hands<2)a[1][target]=40;
    }
   }
   for(int r=0;r<tracks;++r){
    if(locked[r])continue;
    auto& lane=bars[b][r];lane.clear();lane.reserve(steps);
    for(int s=0;s<steps;++s)lane.push_back({s*60,60,a[r][s]==127&&!accents?80:a[r][s]});
   }
  }
  return true;
 }
 // 从原句发展，不调用 generate()，不量化或重建手工 cell。
 // Bar1 保持原样；仅回答小节允许按离散度移动原有正拍主击。
 bool developFromBar1(){
  if(!valid())return false;
  const auto source=bars[0];const int ticks=barTicks();bool audible=false;
  for(const auto& lane:source){
   int end=0;
   for(const auto& c:lane){
    if(c.start!=end||c.length<=0||c.length>ticks-end||c.velocity<0||c.velocity>127)return false;
    end+=c.length;audible|=c.velocity>0;
   }
   if(end!=ticks)return false;
  }
  if(!audible)return false;
  auto result=bars;
  std::mt19937 local(static_cast<unsigned>(seed)^0x42415231u);
  std::array<bool,phraseBars> answer{};
  if(development>0)for(int base:{0,8}){
   std::array<int,6> choices{{base+1,base+2,base+3,base+5,base+6,base+7}};
   std::shuffle(choices.begin(),choices.end(),local);
   for(int i=0;i<(development>.65f?2:1);++i)answer[choices[i]]=true;
  }
  auto value=[](const Lane& lane,int tick){for(const auto& c:lane)if(c.start==tick)return c.velocity;return 0;};
  for(int b=1;b<phraseBars;++b){
   if(barLocked[b])continue;
   auto& dst=result[b];
   for(int r=0;r<tracks;++r)if(!locked[r])dst[r]=source[r];
   if(development<=0)continue; // 精确复制原句；锁定轨保留目标小节原样
   auto safeHit=[&](int r,int tick,int velocity){
    if(locked[r]||velocity<=0||tick<0||tick>=ticks)return false;
    for(auto& c:dst[r])if(c.start==tick){
     if(c.velocity>0||value(source[r],tick)>0)return false; // 原句击打移走后也不回填
     if((r==0||r==1)&&velocity>=80&&value(dst[1-r],tick)>=80)return false;
     if(r==3||r==4||r==6)for(int other:{3,4,6})if(other!=r&&value(dst[other],tick)>0)return false;
     c.velocity=velocity;return true;
    }
    return false;
   };
   if(development>0){
    // Related peripheral responses: select from the source's actual voices and
    // actual cell starts, including tuplets, not a freshly generated beat grid.
    if(answer[b]){
     // 回答小节共用同一位移计划；先处理原句，再添加周边回应与过门。
     syncopateBar(dst,static_cast<unsigned>(seed)^0x42415231u);
     std::vector<int> voices;
     for(int r:{1,2,5,0})if(!locked[r]&&std::any_of(source[r].begin(),source[r].end(),[](const Cell& c){return c.velocity>0;}))voices.push_back(r);
     if(!voices.empty()){
      int r=voices[local()%voices.size()];const auto& lane=source[r];
      std::vector<int> hits;
      for(int i=0;i<int(lane.size());++i)if(lane[i].velocity>0)hits.push_back(i);
      int index=hits[local()%hits.size()];
      int dir=(local()%2)?1:-1;
      int next=index+dir;
      if(next>=0&&next<int(lane.size()))safeHit(r,lane[next].start,ghost?40:80);
      if(development>.65f){next=index-dir;if(next>=0&&next<int(lane.size()))safeHit(r,lane[next].start,80);}
     }
    }
    // 按原句位置抽样稀疏次要击打，不让位移改变后续随机流或抹去新位置主击。
    // 第 5/9/13 小节回归源句，声部最高力度不参与稀疏。
    if((b%4==2||b%4==3)&&space>0){
     for(int r:{0,1,2,3,5,6})if(!locked[r]){
      int peak=0;for(const auto& c:source[r])peak=std::max(peak,c.velocity);
      for(size_t i=0;i<dst[r].size();++i){auto& c=dst[r][i];
       const auto& original=source[r][i];
       if(original.velocity>0&&original.velocity<peak&&((i+size_t(b/4))%3==0)&&double(local())/4294967296.0<space*development)
        if(c.velocity>0&&c.velocity<peak&&!(answer[b]&&original.velocity>=80))c.velocity=0;
      }
     }
    }
    // A source-derived short fill, not a replacement factory groove. Empty
    // source voices stay empty; existing source hits remain recognisable.
    if(b%8==7&&fill>0){
     int voice=-1;for(int r:{1,5,2,0})if(!locked[r]&&std::any_of(source[r].begin(),source[r].end(),[](const Cell& c){return c.velocity>0;})){voice=r;break;}
     if(voice>=0){
      int count=1+int(fill*development*3);int begin=ticks-std::max(60,int(ticks*.25f*fill));
      for(const auto& c:source[voice])if(c.start>=begin&&count>0)
       if(safeHit(voice,c.start,ghost?40:80))--count;
     }
    }
   }
   // Locked destination material wins over inherited notes in other voices.
   // Without locks, exact source unisons are intentional and left untouched.
   for(int r=0;r<tracks;++r)if(locked[r])for(const auto& c:dst[r])if(c.velocity>0){
    for(int other=0;other<tracks;++other)if(!locked[other]){
     bool conflict=((r==0&&other==1)||(r==1&&other==0))&&c.velocity>=80;
     bool cymbal=(r==3||r==4||r==6)&&(other==3||other==4||other==6);
     if(conflict||cymbal)for(auto& n:dst[other])if(n.start==c.start&&(cymbal||n.velocity>=80))n.velocity=0;
    }
   }
  }
  bars=std::move(result);return true;
 }
 bool subdivide(int b,int r,int a,int z,int count){if(b<0||b>=phraseBars||r<0||r>=tracks)return false;auto& l=bars[b][r];if(a<0||z<a||z>=int(l.size()))return false;int begin=l[a].start,end=l[z].start+l[z].length,total=end-begin;if(count==0){if(begin%120!=0||total%120!=0)return false;count=total/120;}if(count<=0||total%count)return false;for(int k=a;k<=z;++k)if(l[k].velocity&&(l[k].start-begin)%(total/count)!=0)return false;Lane replacement;for(int i=0;i<count;++i){int t=begin+i*total/count,v=0;for(int k=a;k<=z;++k)if(l[k].start==t)v=l[k].velocity;replacement.push_back({t,total/count,v});}l.erase(l.begin()+a,l.begin()+z+1);l.insert(l.begin()+a,replacement.begin(),replacement.end());return true;}
};
}
