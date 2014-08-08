// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#pragma once

#include "Common/CommonTypes.h"
#include "Common/Thread.h"
#include "VideoBackends/Software/EfbInterface.h"

namespace SWRenderer
{
	void Init();
	void Prepare();
	void Shutdown();

	void RenderText(const char* pstr, int left, int top, u32 color);
	void DrawDebugText();

	u8* getNextColorTexture();
	u8* getCurrentColorTexture();
	void swapColorTexture();
	void UpdateColorTexture(EfbInterface::yuv422_packed *xfb, u32 fbWidth, u32 fbHeight);
	void DrawTexture(u8 *texture, int width, int height, u64 ticks);

	void Swap(u32 fbWidth, u32 fbHeight, u64 ticks);
	void SwapBuffer();
}
