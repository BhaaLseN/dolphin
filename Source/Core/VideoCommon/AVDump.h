// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "Common/CommonTypes.h"
#include "Common/Thread.h"

class AVDump
{
public:
	AVDump();

	bool DumpAudioEnabled();
	bool DumpVideoEnabled();

	void PushDTKAudio(const s16* data, int samples, int samplerate, u64 ticks);
	void PushAIAudio(const s16* data, int samples, int samplerate, u64 ticks);

	void PushVideo(const void* data, int width, int height, u64 ticks, bool upside_down);
	void SyncVideo();

	void Screenshot(const std::string& filename);

private:
	void EncodeVideo();
	void EncodeLoop();

	int avi_filenumber;
	bool s_avi_dump_started;
	volatile bool s_get_screenshot;
	std::string s_screenshot_path;

	const void* video_data;
	int video_width;
	int video_height;
	u64 video_ticks;
	bool video_upside_down;

	std::vector<u8> swaped_data;

	std::unique_ptr<std::thread> worker;
	bool thread_is_working;

	Common::Semaphore start_sem;
	Common::Semaphore end_sem;
};

extern AVDump* g_av_dump;
