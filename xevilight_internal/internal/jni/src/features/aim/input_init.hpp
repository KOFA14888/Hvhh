
// ================================================================================
// ==================================================
//  Bot:     t.me/imguisource_bot
//  Channel: t.me/ImGui_Source_FIXNEY (ImGui Source)
//  Channel: t.me/il2cpp_src (shit src 🧨)
// ==================================================
// ================================================================================

#pragma once
#include <cstdint>
#include <cmath>
namespace astra::aim::input_init {
enum Reason {NotAttempted,Ready,NoLocal,WrongThread,InvalidLocal,ComponentMissing,SignatureRejected,ClassRejected,SlotMissing,SlotReadOnly,WriteProbeFailed,ReadbackFailed,UnsupportedArchitecture,RuntimeNotReady,CallsiteRejected,RelayAllocationFailed,RelayProtectionFailed,TextProtectionFailed,RestoreProtectionFailed,PairReadbackFailed};
inline const char* reasonText(int r){switch(r){case CallsiteRejected:return XS("command-callsite-changed");case RelayAllocationFailed:return XS("command-relay-allocation-failed");case RelayProtectionFailed:return XS("command-relay-RX-failed");case TextProtectionFailed:return XS("command-text-protection-failed");case RestoreProtectionFailed:return XS("command-restore-RX-failed");case PairReadbackFailed:return XS("command-pair-verify-or-rollback-failed");case Ready:return XS("ready");case NoLocal:return XS("local-player-missing");case WrongThread:return XS("wrong-thread");case InvalidLocal:return XS("local-state-rejected");case ComponentMissing:return XS("input-component-missing");case SignatureRejected:return XS("native-signature-rejected");case ClassRejected:return XS("input-class-rejected");case SlotMissing:return XS("exact-vtable-slot-missing");case SlotReadOnly:return XS("vtable-slot-readonly");case WriteProbeFailed:return XS("slot-write-probe-failed");case ReadbackFailed:return XS("slot-readback-failed");case UnsupportedArchitecture:return XS("unsupported-architecture");case RuntimeNotReady:return XS("runtime-not-ready");default:return XS("not-attempted");}}
struct Retry {
 std::uintptr_t owner=0;double next=0,last=0;
 bool due(double now,bool enabled,bool ready,std::uintptr_t local){
  if(!std::isfinite(now))return false;
  if(!enabled){*this={};return false;}
  if(ready)return false;
  if(!local)return false;
  if(owner!=local||now<last){owner=local;next=0;}
  last=now;if(now<next)return false;next=now+1000;return true;
 }
};
struct ComponentStats {unsigned arrays=0,entries=0,objects=0,foreign=0;bool dictionary=false;};
}
