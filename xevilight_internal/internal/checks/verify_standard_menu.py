"""Headless smoke test of the actual Dear ImGui launcher and menu shell."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'jni/src/ui/xevilight/menu.hpp').read_text()

def function(name):
    start = source.index('static void ' + name + '(')
    body = source.index('{', start)
    depth, end = 1, body + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

controls = source[source.index('// Ordinary Dear ImGui controls.'):source.index('// ── FOV projection')]

cpp = r'''
#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#define XS(s) s
static bool opened = false;
static bool menuOpen() { return opened; }
static void setMenuOpen(bool v) { opened = v; }
namespace xev { namespace ui {
inline void section(const char*) {} // Ensure local controls shadow the old custom widgets.
}}
namespace xevm {
using namespace xev::ui;
''' + controls + r'''
static int pages = 0;
static void pageRage() {
    ++pages;
    static bool value = false; static float number = 1; static int choice = 0;
    static std::uint32_t mask = 0; static ImVec4 color(1, 1, 1, 1);
    const char* items[] = {"One", "Two"};
    section("Test"); toggle("toggle", value, "Toggle"); slider("slider", "Slider", number, 0, 2, "%.1f");
    selectRow("combo", "Combo", choice, items, 2, nullptr); colorRow("color", "Color", color, nullptr);
    chips("chips", mask, items, 2); buttonRow("button", "Button"); kv("Key", "Value");
}
static void pageVisuals() {}
static void pageSkins() {}
static void pageSettings() {}
''' + function('frameShell') + '\n' + function('drawWatermark') + r'''
} // namespace xevm
using xevm::frameShell; using xevm::drawWatermark; using xevm::pages;
static void frame(bool down, float x, float y) {
    auto& io = ImGui::GetIO();
    io.AddMousePosEvent(x, y); io.AddMouseButtonEvent(0, down);
    ImGui::NewFrame();
    if (menuOpen()) frameShell();
    drawWatermark();
    ImGui::Render();
}
int main() {
    ImGui::CreateContext();
    auto& io = ImGui::GetIO(); io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1000, 700); io.DeltaTime = 1.f / 60;
    unsigned char* pixels; int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    frame(false, -1, -1);
    auto* launcher = ImGui::FindWindowByName("##menu-launcher");
    assert(launcher && launcher->Active);
    float x = launcher->Pos.x + launcher->Size.x * .5f;
    float y = launcher->Pos.y + launcher->Size.y * .5f;
    frame(false, x, y); frame(true, x, y); frame(false, x, y);
    assert(opened);
    frame(false, x, y);
    assert(ImGui::FindWindowByName("Xevilight")->Active && pages > 0);
    frame(true, x, y); frame(false, x, y);
    assert(!opened);
    ImGui::DestroyContext();
}
'''
imgui = root / 'jni/third_party/imgui'
with tempfile.TemporaryDirectory() as tmp:
    test = Path(tmp) / 'test.cpp'
    test.write_text(cpp)
    exe = Path(tmp) / 'test'
    subprocess.run(['g++', '-std=c++17', '-I', str(imgui), str(test),
                    *[str(imgui / f'imgui{suffix}.cpp') for suffix in ('', '_draw', '_tables', '_widgets')],
                    '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('Standard ImGui menu smoke test passed')
