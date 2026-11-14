
// ================================================================================
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Telegram Bot: t.me/imguisource_bot
// ImGui Source: t.me/ImGui_Source_FIXNEY
// shit src 🧨: t.me/il2cpp_src
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// ================================================================================

// 67
#pragma once

#ifndef ASTRA_DEBUG
#define ASTRA_DEBUG 0
#endif

#include <android/log.h>

#if ASTRA_DEBUG
#define ASTRA_LOG(...) ::astra::aim::debug::log(__VA_ARGS__)
#else
#define __android_log_print(...) ((void)0)
#define ASTRA_LOG(...) ((void)0)
#endif
