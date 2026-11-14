
// ================================================================================
// Uploaded to @imguisource_bot
// Sub: t.me/ImGui_Source_FIXNEY (ImGui Source)
// Sub: t.me/il2cpp_src (shit src 🧨)
// ================================================================================

// эыкстрааоляция

#pragma once
#include "features/aim/core.hpp"
#include <array>
namespace astra::aim::prediction {
struct Motion {
 static constexpr unsigned kHistory=8;
 struct Sample{Vec3 position{};double at=0;};
 std::array<Sample,kHistory> history{};
 unsigned count=0,next=0;
 Vec3 position{},velocity{},acceleration{};
 double sampled=0;std::uintptr_t world=0;int team=0;
 bool moving=false;unsigned samples=0;

 void reset(){*this={};}
 void push(Vec3 p,double now){history[next]={p,now};next=(next+1)%kHistory;if(count<kHistory)++count;position=p;sampled=now;}

 // Weighted slope of position over time; recent samples weigh 2.5x more.
 bool fitVelocity(Vec3& out)const{
  if(count<3)return false;
  const double newest=sampled;
  float sw=0,sxx=0,sx=0,sy[3]{},sxy[3]{};
  for(unsigned i=0;i<count;++i){
   const unsigned idx=(next+kHistory-count+i)%kHistory;
   const auto& s=history[idx];
   const double age=newest-s.at;
   if(age<0||age>.22)continue;
   const float x=float(-age);
   const float w=1.f+1.5f*float(i)/float(count-1);
   const float y[3]={s.position.x-position.x,s.position.y-position.y,s.position.z-position.z};
   sw+=w;sxx+=w*x*x;sx+=w*x;
   for(int c=0;c<3;++c){sy[c]+=w*y[c];sxy[c]+=w*x*y[c];}
  }
  const float den=sw*sxx-sx*sx;
  if(!(sw>0.f)||std::fabs(den)<1e-6f)return false;
  out=Vec3{(sw*sxy[0]-sx*sy[0])/den,(sw*sxy[1]-sx*sy[1])/den,(sw*sxy[2]-sx*sy[2])/den};
  return finite(out);
 }

 bool update(Vec3 p,double now,std::uintptr_t w,int t){
  if(!finite(p)||!std::isfinite(now)||!w){reset();return false;}
  const double dt=now-sampled;
  if(samples&&world==w&&team==t&&dt==0)return samples>=2;
  if(!samples||world!=w||team!=t||dt<=0||dt>.25){reset();world=w;team=t;push(p,now);samples=1;return false;}
  if(dt<.002)return samples>=2;
  const Vec3 delta=sub(p,position);
  if(length(delta)>3.f){reset();world=w;team=t;push(p,now);samples=1;return false;}
  const Vec3 old=velocity;
  push(p,now);
  Vec3 measured{};
  if(!fitVelocity(measured))measured=Vec3{delta.x/float(dt),delta.y/float(dt),delta.z/float(dt)};
  const float speed=length(measured);
  if(!finite(measured)||speed>14.f){reset();world=w;team=t;push(p,now);samples=1;return false;}
  Vec3 smoothed=moving?Vec3{velocity.x+(measured.x-velocity.x)*.65f,
                            velocity.y+(measured.y-velocity.y)*.65f,
                            velocity.z+(measured.z-velocity.z)*.65f}:measured;
  const float dt_s=std::max(.001f,float(dt));
  Vec3 accel{(smoothed.x-old.x)/dt_s,(smoothed.y-old.y)/dt_s,(smoothed.z-old.z)/dt_s};
  const float am=length(accel);
  if(!std::isfinite(am))accel={};
  else if(am>25.f)accel={accel.x/am*25.f,accel.y/am*25.f,accel.z/am*25.f};
  acceleration=moving?Vec3{acceleration.x+(accel.x-acceleration.x)*.40f,
                           acceleration.y+(accel.y-acceleration.y)*.40f,
                           acceleration.z+(accel.z-acceleration.z)*.40f}:accel;
  velocity=smoothed;
  moving=length(smoothed)>.30f;
  samples=std::min(samples+1,1000u);
  return samples>=2;
 }

 // ticks = 0..10 at the server tick rate (64 Hz for this build). extraMs keeps
 // the manual lead/rewind trim, smoothing adds the aim smoothing window.
 Vec3 leadTicks(Vec3 point,double now,float ticks,float tickrate,float extraMs,float smoothing,bool enabled=true)const{
  if(!enabled||samples<2||!moving||!finite(point)||!finite(velocity)||!std::isfinite(now))return point;
  if(!std::isfinite(ticks)||!std::isfinite(tickrate)||!std::isfinite(extraMs)||!std::isfinite(smoothing))return point;
  const double age=now-sampled;
  if(age<-.001||age>.35)return point;
  const float rate=std::clamp(tickrate,16.f,128.f);
  const float tickLead=std::clamp(ticks,0.f,10.f)/rate;
  const float horizon=std::clamp(float(age)+tickLead+std::clamp(extraMs,-150.f,150.f)*.001f+
                                 std::clamp(smoothing,0.f,.15f),-.20f,.35f);
  float freshness=1.f;
  if(age>.12)freshness=std::max(0.f,1.f-float(age-.12)/.20f);
  if(freshness<=0.f)return point;
  Vec3 lead{velocity.x*horizon,velocity.y*horizon,velocity.z*horizon};
  Vec3 accel{acceleration.x*(.5f*horizon*horizon),acceleration.y*(.5f*horizon*horizon),
             acceleration.z*(.5f*horizon*horizon)};
  const float am=length(accel);
  if(!std::isfinite(am))accel={};
  else if(am>.6f)accel={accel.x/am*.6f,accel.y/am*.6f,accel.z/am*.6f};
  lead={lead.x+accel.x,lead.y+accel.y,lead.z+accel.z};
  lead={lead.x*freshness,lead.y*freshness,lead.z*freshness};
  const float n=length(lead);
  if(!std::isfinite(n)||n>2.5f)return point;
  Vec3 next{point.x+lead.x,point.y+lead.y,point.z+lead.z};
  return finite(next)?next:point;
 }

 Vec3 lead(Vec3 point,double now,float extraMs,float smoothing,bool enabled=true)const{
  return leadTicks(point,now,0.f,64.f,extraMs,smoothing,enabled);
 }
};
inline bool cameraLease(bool enabled,bool back,bool ready,bool paused,bool activeOrRestored,double now,double until,std::uintptr_t owner,std::uintptr_t world,std::uintptr_t currentOwner,std::uintptr_t currentWorld){return enabled&&back&&ready&&!paused&&activeOrRestored&&std::isfinite(now)&&std::isfinite(until)&&until>now&&owner&&world&&owner==currentOwner&&world==currentWorld;}
}
