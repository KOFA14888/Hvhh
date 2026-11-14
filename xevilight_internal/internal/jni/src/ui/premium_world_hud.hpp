
// ================================================================================
// Powered by @imguisource_bot | Channels: @ImGui_Source_FIXNEY, @il2cpp_src
// ================================================================================

#pragma once
// GPU Night only. World/Fog/HUD controls remain absent.
static void worldHudControls(){
 if(!astra_sections::header(XS("GPU Night mode"),width))return;
 ImGui::PushID(XS("gpu-night-recovery"));
 astra_night_diag::controls();
 ImGui::PopID();
}
