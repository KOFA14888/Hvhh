
// ================================================================================
// t.me/imguisource_bot | t.me/ImGui_Source_FIXNEY (ImGui Source) | t.me/il2cpp_src (shit src 🧨)
// ================================================================================

#pragma once
namespace agent_catalog {
struct entry {int id;int team;const char* name;};
// Serialized AgentContentEntry IDs from builtincollections_definitions.bundle.
inline constexpr entry entries[]={
 {10001,2,"CT / Lucas"},{10002,1,"T / Scrap"},
 {10003,2,"CT / Warden"},{10004,2,"CT / Hammer"},
 {10005,1,"T / Adam"},{10006,1,"T / Marco"},
 {10007,2,"CT / Lincoln"},{10008,1,"T / Reis"},
 {10009,2,"CT / Michael"},{10010,1,"T / Rafael"},
{10011,2,"CT / TJ"},
{10012,1,"T / Scratch"},
{10013,1,"T / Scratch"},
};
inline bool valid(int id){if(!id)return true;for(const auto& e:entries)if(e.id==id)return true;return false;}
}
