#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "Gb28181PsMux.h"
#include "hi_voice_api.h"
#pragma comment(lib, "lib_VoiceEngine_dll.lib")

#define MAX_CACHE_BUF_SIZE 1024*1024
#define MAX_READ_BUF_SIZE 4096

class CMuxTest
{
public:
	CMuxTest();
	~CMuxTest();

public:
	void read_file(const char* path);

private:
	void input_stream(uint8_t* buf, int len);
	void process_video_frame(int frame_type, uint64_t pts, uint8_t* buf, int len);
	void process_audio_frame(uint64_t pts, uint8_t* buf, int len);
	bool is_header(uint8_t* buf, int len);

private:
	uint8_t* cache_buf_;
	int cache_buf_point_;
	
	int frame_type_;
	int frame_size_;
	int head_size_;
	uint64_t frame_time_;
	bool is_found_head_;

	Gb28181PsMux ps_muxer;
	StreamIdx video_idx;
	StreamIdx audio_idx;
	guint8* ps_buf_;

	FILE* ps_file_;
	gint64 ps_time_;
	bool find_i_frame_;


	HI_S32 decodec_state_[0x100];
	HI_S32 encodec_state_[0x100];
};



