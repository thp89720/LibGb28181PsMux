#include "DemuxFile.h"

#define  RM_HEAD_SIZE 24

typedef struct st_nalu_unit
{
	int type;
	int size;
	unsigned char *data;
}nalu_unit_t;

static int read_nalu(const unsigned char *buffer, unsigned int nBufferSize, unsigned int offSet, nalu_unit_t &nalu)
{
	int i = offSet;
	int j = 0;

	for (; i < nBufferSize; ++i)
	{
		if (0x00 == buffer[i] && 0x00 == buffer[i + 1] && 0x00 == buffer[i + 2] && 0x01 == buffer[i + 3])
		{
			for (j = i + 4; j < nBufferSize; ++j)
			{
				if (0x00 == buffer[j] && 0x00 == buffer[j + 1] && 0x00 == buffer[j + 2] && 0x01 == buffer[j + 3])
				{
					break;
				}
			}

			nalu.size = (j == nBufferSize) ? (nBufferSize - i) : (j - i);

			if (nalu.size > 4)
			{
				nalu.type = buffer[i + 4] & 0x1f;
				nalu.data = (uint8_t*)&buffer[i];

				return j;
			}
		}
	}

	return 0;
}

CMuxTest::CMuxTest()
{
	cache_buf_point_ = 0;
	frame_size_ = 0;
	frame_type_ = 0;
	head_size_ = 0;
	frame_time_ = 0;
	is_found_head_ = false;
	ps_file_ = nullptr;
	ps_time_ = 0;
	find_i_frame_ = false;

	cache_buf_ = new uint8_t[MAX_CACHE_BUF_SIZE];
	ps_buf_ = new guint8[MAX_CACHE_BUF_SIZE];

}

CMuxTest::~CMuxTest()
{
	if (ps_file_)
	{
		fclose(ps_file_);
		ps_file_ = nullptr;
	}
}

void CMuxTest::read_file(const char* path)
{
	HI_VOICE_DecReset((HI_VOID*)decodec_state_, ADPCM_DVI4);
	HI_VOICE_EncReset((HI_VOID*)encodec_state_, G711_A);

	video_idx = ps_muxer.AddStream(PSMUX_ST_VIDEO_H264);
	audio_idx = ps_muxer.AddStream(PSMUX_ST_PS_AUDIO_G711A);
	ps_file_ = fopen("RM.mpg", "wb");

	FILE* fp = fopen(path, "rb");

	if (fp)
	{
		uint8_t buf[MAX_READ_BUF_SIZE] = { 0 };

		while (true)
		{
			int bytes = fread(buf, 1, MAX_READ_BUF_SIZE, fp);

			if (bytes > 0)
			{
				input_stream(buf, bytes);
			}
			else
			{
				break;
			}
		}

		fclose(fp);
	}
}

void CMuxTest::input_stream(uint8_t* buf, int len)
{
	memcpy(cache_buf_ + cache_buf_point_, buf, len);
	cache_buf_point_ += len;

	while (cache_buf_point_ > 0)
	{
		if (!is_found_head_)
		{
			if (cache_buf_point_ <= RM_HEAD_SIZE)
			{
				break;
			}

			for (int i = 0; i + RM_HEAD_SIZE < cache_buf_point_; ++i)
			{
				if (is_header(cache_buf_ + i, cache_buf_point_ - i))
				{
					if (i > 0)
					{
						cache_buf_point_ -= i;
						memmove(cache_buf_, cache_buf_ + i, cache_buf_point_);
					}

					is_found_head_ = true;
					break;
				}
			}

			if (!is_found_head_)
			{
				memmove(cache_buf_, cache_buf_ + cache_buf_point_ - RM_HEAD_SIZE, RM_HEAD_SIZE);
				cache_buf_point_ = RM_HEAD_SIZE;
				break;
			}
		}
		else
		{
			if (cache_buf_point_ >= frame_size_)
			{
				printf("frame_type = %d, frame_time = %llu, frame_len = %d\n", frame_type_, frame_time_, frame_size_);

				if (4 == frame_type_)
				{
					process_audio_frame(frame_time_, cache_buf_ + head_size_, frame_size_ - head_size_);
				}
				else
				{
					process_video_frame(frame_type_, frame_time_, cache_buf_ + head_size_, frame_size_ - head_size_);
				}

				cache_buf_point_ -= frame_size_;
				memmove(cache_buf_, cache_buf_ + frame_size_, cache_buf_point_);
				is_found_head_ = false;
			}
			else
			{
				break;
			}
		}
	}
}

void CMuxTest::process_video_frame(int frame_type, uint64_t pts, uint8_t* buf, int len)
{
	printf("video: %02x %02x %02x %02x %02x\n", (uint8_t)buf[0], (uint8_t)buf[1], (uint8_t)buf[2], (uint8_t)buf[3], (uint8_t)buf[4]);

	if (1 == frame_type)
	{
		find_i_frame_ = true;
	}

	if (find_i_frame_)
	{
		int	pos = 0;
		int size = 0;
		int left = size;
		nalu_unit_t	h264Nalu;

		int out_size = 0;
		int ret = 0;

		while (size = read_nalu(buf, len, pos, h264Nalu))
		{
			ret = ps_muxer.MuxH264SingleFrame(h264Nalu.data, h264Nalu.size, ps_time_, ps_time_, video_idx, ps_buf_, &out_size, MAX_CACHE_BUF_SIZE);

			if (0 == ret && out_size > 0)
			{
				fwrite(ps_buf_, 1, out_size, ps_file_);
				fflush(ps_file_);
			}
			else
			{
				printf("===================================> \n");
			}

			pos = size;
		}

		ps_time_ += 3600;
	}
	
}

void CMuxTest::process_audio_frame(uint64_t pts, uint8_t* buf, int len)
{
	printf("audio: %02x %02x %02x %02x\n", (uint8_t)buf[0], (uint8_t)buf[1], (uint8_t)buf[2], (uint8_t)buf[3]);

	HI_S16 in_buf[HI_VOICE_MAX_FRAME_SIZE] = {0};
	HI_S16 out_buf[HI_VOICE_MAX_FRAME_SIZE] = { 0 };
	HI_S16 out_len = 0;

	memcpy((uint8_t*)in_buf, buf, len);

	int ret = HI_VOICE_DecodeFrame(decodec_state_, in_buf, out_buf, &out_len);

	if (0 == ret)
	{
		ret = HI_VOICE_EncodeFrame(encodec_state_, out_buf, in_buf, out_len);

		if (0 == ret)
		{
			static FILE* s_fp = fopen("audio.g711", "wb");
			fwrite(in_buf, 2, out_len, s_fp);
			fflush(s_fp);

			int out_size = 0;
			ret = ps_muxer.MuxAudioFrame((guint8*)in_buf, 2 * out_len, ps_time_, ps_time_, audio_idx, ps_buf_, &out_size, MAX_CACHE_BUF_SIZE);

			if (ret != 0)
			{
				printf("===================================>2 \n");
			}
		}
	}
	else
	{
		if (HI_ERR_VOICE_DEC_TYPE == ret)
		{
			printf("HiSi Failed: Audio Type Invalid!\n");
		}
		else if (HI_ERR_VOICE_DEC_FRAMETYPE == ret)
		{
			printf("HiSi Failed: Audio Frame Invalid!\n");
		}
		else if (HI_ERR_VOICE_DEC_FRAMESIZE == ret)
		{
			printf("HiSi Failed: Audio Size Invalid!\n");
		}
		else
		{
			printf("HiSi Failed: Unknown Error!\n");
		}
	}
}

bool CMuxTest::is_header(uint8_t* buf, int len)
{
	//< 视频帧
	if ('d' == buf[2] && 'c' == buf[3] && 'H' == buf[4] && '2' == buf[5])
	{
		int nChannel = buf[0] - 0x30 + 1;

		if ('0' == buf[1])
		{
			frame_type_ = 1;
		}
		else if ('1' == buf[1])
		{
			frame_type_ = 2;
		}
		else
		{
			frame_type_ = 3;
		}

		//< 计算帧头长度、帧数据长度
		memcpy(&frame_size_, &buf[8], 4);
		memcpy(&head_size_, &buf[12], 2);
		head_size_ += 24;

		//< 解析时间戳(单位: 微妙)
		memcpy(&frame_time_, &buf[16], 8);

		is_found_head_ = true;
	}
	//< 音频帧
	else if ('3' == buf[1] && 'w' == buf[2] && 'b' == buf[3])
	{
		frame_type_ = 4;
		head_size_ = 8;
		memcpy(&frame_size_, buf + 6, 2);

		is_found_head_ = true;
	}

	if (is_found_head_)
	{
		int padding = frame_size_ % 8;

		frame_size_ = (0 == padding) ? frame_size_ : (frame_size_ + 8 - padding);
		frame_size_ += head_size_;
	}

	return is_found_head_;
}

