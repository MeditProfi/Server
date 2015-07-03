#include "../../stdafx.h"
#include "input_video.h"
#include "input_audio.h"
#include <boost/math/special_functions/round.hpp>


namespace caspar { namespace mplayer {
using namespace std;

struct input_video::implementation : boost::noncopyable
{
	executor							executor_;
	mplayer_process*					mplayer_;
	shared_ptr<mplayer_pipe>			pipe_;
	tbb::atomic<bool>					initialized_;
	tbb::atomic<bool>					doread_;

	unsigned char*						avio_buffer_;
	shared_ptr<AVFormatContext>			video_format_;
	shared_ptr<AVCodecContext>			video_codec_;
	rawdata_video_queue					video_raw_buffer_;
	AVStream*							video_stream_;
	shared_ptr<SwsContext>				sws_;

	
	std::shared_ptr<AVPacket>			packet;
	std::shared_ptr<AVFrame>			frame;
	int									packet_offset;

	int									read_state;
	common_input_data*					cmn_;
	tbb::atomic<bool>					active_;

#ifndef NO_VIDEO
	tbb::atomic<bool>					empty_frames_;
#endif


	implementation(long unique_num, mplayer_process* mplayer, common_input_data* cmn, bool empty_mode)
		: executor_(L"mplayer-video-read-" + toWStr(cmn->resource_name_) + L"-" + std::to_wstring((long long)unique_num))
		, mplayer_(mplayer)
		, pipe_(mplayer->video_pipe())
		, avio_buffer_(0)
		, read_state(1)
		, cmn_(cmn)
	{
		active_ = true;
		initialized_ = false;
		doread_ = false;

		empty_frames_ = empty_mode;
		if (empty_frames_) mlg("no video mode");

		mlg("out fps = " << cmn_->out_fps_);
	}


	~implementation()
	{
		mdb("~input_video");
		mdb("~input_video OK");
	}



	void deinit()
	{
		mlg("deinit video input...");
		initialized_ = false;
		clear_data();

		mdb("packet.reset();");
		packet.reset();

		mdb("frame.reset();");
		frame.reset();

		mdb("sws_.reset();");
		sws_.reset();

		mdb("video_codec_.reset();");
		video_codec_.reset();

		//mdb("video_stream_.reset();");
		//video_stream_.reset();

		//avio_buffer will get free in _format_.reset()
		//mdb("avio_buffer_.reset();");
		//avio_buffer_.reset();

		mdb("video_format_.reset();");
		video_format_.reset();
	}


	void init()
	{
		try
		{
			mlg("init video input...");
			deinit();


			mdb("mplayer working wait...");
			while (!mplayer_->working());

			//avio_buffer_.reset((unsigned char*)av_malloc(PIPE_BUF_SIZE), &av_free);  //there is no memory leak here, because avformat_close_input cleans all memory assotiated with av context.
			avio_buffer_ = (unsigned char*)av_malloc(PIPE_BUF_SIZE);

			AVFormatContext *ctx = 0;

			mdb("avformat_alloc_context...");
			ctx = avformat_alloc_context();
			ctx->probesize = 2048; 

			mdb("avio_alloc_context...");
			AVIOContext *c = avio_alloc_context(avio_buffer_, PIPE_BUF_SIZE, 0, (void*)pipe_.get(), &readPipe, nullptr, nullptr);
			ctx->pb = c;

			pipe_->use_counter = 0;
	

			mdb("open input...");
			int ret = avformat_open_input(&ctx, "", nullptr, nullptr);
			if (ret != 0)
			{
				char desc[4096];
				memset(desc, 0, 4096);
				av_strerror(ret, desc, 4096);
				merr("Error avformat_open_input (video), error code " << ret << " desc = " << desc);
				avformat_free_context(ctx);

				mplayer_fatal_error("");
			}
			video_format_.reset(ctx, &free_av_context);

			mdb("find stream info...");
			if (avformat_find_stream_info(video_format_.get(), nullptr) < 0) mplayer_fatal_error("Error avformat_find_stream_info (video)");

			AVCodec *video_codec = nullptr;


			video_stream_ = video_format_->streams[0];		
			auto codecID = video_stream_->codec->codec_id;	

			video_codec = avcodec_find_decoder(codecID);
			if (video_codec == nullptr) mplayer_fatal_error("Codec required by video not available");

			video_codec_.reset(avcodec_alloc_context3(video_codec), [](AVCodecContext* c) { mdb2("delete codec"); avcodec_close(c); av_free(c); });
	
			std::vector<uint8_t> extraData(video_stream_->codec->extradata, video_stream_->codec->extradata + video_stream_->codec->extradata_size);
			video_codec_->extradata = reinterpret_cast<uint8_t*>(extraData.data());
			video_codec_->extradata_size = extraData.size();

			video_codec_->pix_fmt = SPixFmt;
			video_codec_->width = cmn_->width;
			video_codec_->height = cmn_->height;

			mdb("open codec...");
			if (avcodec_open2(video_codec_.get(), video_codec, nullptr) < 0) mplayer_fatal_error("Could not open video codec");

			if (!sws_)
			{
				sws_.reset(sws_getContext(cmn_->width, cmn_->height, SPixFmt, cmn_->width, cmn_->height, PIX_FMT_RGB32, SWS_BILINEAR, nullptr, nullptr, nullptr), &sws_freeContext);
			}

			frame.reset(avcodec_alloc_frame(), [](void *res) {mdb2("release frame"); av_free(res);});
			packet.reset(new AVPacket, [](AVPacket* p)
								{
									av_free_packet(p);
									delete p;
								});
			packet_offset = 0;
			memset(packet.get(), 0, sizeof(AVPacket));


			cmn_->in_fps_ = getFpsFromHeader();
			mlg("input fps from header -> " << cmn_->in_fps_);

			initialized_ = true;
		
			mlg("init video input OK");
		}
		catch (...)
		{
			merr("mplayer producer fatal error");
			if (active_) mplayer_->problem();
			mplayer_->stop();
			cmn_->stopAll();
			throw;		
		}
	}

	bool input_full()
	{
		//to avoid memory leak if receive is not called
		return (output_size() >= cmn_->frames_max + 5);
	}

	void start_thread()
	{
		if ((active_ ) && (executor_.empty()) && (!empty_frames_))
		{
			executor_.begin_invoke([this]
			{
				if ((!input_full()) && (this->active_))
				{
					this->thread();
					this->start_thread();
				}
			});
		}
	}	

	void thread()
	{
		try
		{
			if (!active_ || empty_frames_) return;

			switch (read_state)
			{
				case 1:
					if (mplayer_->working()) read_state = 2;
					break;

				case 2:
					init();
					read_state = 3;
					break;

				case 3:
					if (cmn_->input_initialized() && active_)
					{
						doread_ = true;
						pipe_->use_counter = 0;
						if (!read())
						{
							mwarn("error read video input. Try restart all");
							initialized_ = false;
							clear_data();
							read_state = 1; 
						}

						doread_ = false;
					}
					break;
			}
		}
		catch (...)
		{
			CASPAR_LOG_CURRENT_EXCEPTION();
		}
	}


	float getFpsFromHeader()
	{
		if (video_stream_->r_frame_rate.num == 0) return 0.0f;
		if (video_stream_->r_frame_rate.den == 0) return 0.0f;
		return (float)video_stream_->r_frame_rate.num / (float)video_stream_->r_frame_rate.den;
	}

	bool read()
	{
		mtr("video read...");

		if (!mplayer_->working()) return false;
		

		if (packet_offset >= packet->size)
		{
			if (packet->data) av_free_packet(packet.get());

			if (av_read_frame(video_format_.get(), packet.get()) < 0) 
			{
				mwarn("video read error (av_read_frame < 0)");
				if (active_) mplayer_->problem();
				return false;
			}

			if (pipe_->cant_read_)
			{
				mwarn("video read error (can't read pipe)");
				if (active_) mplayer_->problem();
				return false;
			}

			packet_offset = 0;
		}

		packet->data = packet->data + packet_offset;
		packet->size = packet->size - packet_offset;

		int isFrameAvailable = 0;
		const auto processedLength = avcodec_decode_video2(video_codec_.get(), frame.get(), &isFrameAvailable, packet.get());

		if (processedLength < 0) 
		{
			mplayer_fatal_error("error decoding raw video?");
			return false; 
		}
		packet_offset += processedLength;


		if (isFrameAvailable) 
		{
			mtr("video data available");
			float S = 0;
			if (enable_fps_log)
			{
				S = output_size();
			}

			shared_ptr<AVPicture> pic(new AVPicture, free_picture);
			avpicture_alloc(pic.get(), PIX_FMT_RGB32, cmn_->width, cmn_->height);
	
			sws_scale(sws_.get(), frame->data, frame->linesize, 0, cmn_->height, pic->data, pic->linesize);
				
			video_raw_buffer_.push(pic);

			if (enable_fps_log)
			{
				mdebug.ivCtr += (output_size() - S);
				if (TIMER_GET(VIDEO_IN_TMR) >= 1000)
				{
					mdebug.ivFps = mdebug.ivCtr;
					mdebug.ivCtr = 0;
					TIMER_CLEAR(VIDEO_IN_TMR);

					mtr("input video fps = " << mdebug.ivFps);
				}
			}
		}

		return true;
	}

	bool try_pop(video_buffer &video)
	{
		float S = 0;
		if (enable_fps_log)
		{
			S = output_size();
		}
		
		bool res;
			
		if (!empty_frames_)
		{
			res = video_raw_buffer_.try_pop(video); 
			if (!res) {mtr("can't pop video from input");}
		}
		else
		{
			shared_ptr<AVPicture> pic(new AVPicture, free_picture);
			avpicture_alloc(pic.get(), PIX_FMT_RGB32, cmn_->width, cmn_->height);
			video = pic;
			res = true;
		}

		if (enable_fps_log)
		{
			mdebug.ovCtr += (S - output_size());
			if (TIMER_GET(VIDEO_OUT_TMR) >= 1000)
			{
				mdebug.ovFps = mdebug.ovCtr;
				mdebug.ovCtr = 0;
				TIMER_CLEAR(VIDEO_OUT_TMR);

				mfps("output video fps = " << mdebug.ovFps);
			}
		}

		return res;
	}

	float output_size()
	{
		if (cmn_->in_fps_ != 0.0f)
			return ((float)video_raw_buffer_.size() * (float)cmn_->out_fps_) / (float)cmn_->in_fps_;
		else
			return 0;
	}

	void clear_data()
	{
		video_raw_buffer_.clear();
	}

	void drop()
	{
		mdb("video drop...");
		executor_.clear();
		clear_data();
		read_state = 1;
		while (doread_ == true); //executor.join does not work. Why?
		deinit();
		mdb("video drop OK");
	}

	void stop()
	{
		mdb("input video stop....");
		active_ = false; 
		pipe_->stop();
		drop();
		mdb("input video stop OK");
	}
};


input_video::input_video(long unique_num, mplayer_process* mplayer, common_input_data* cmn, bool empty_mode) : impl_(new implementation(unique_num, mplayer, cmn, empty_mode)) {}
void input_video::start_thread() {impl_->start_thread();}
bool input_video::try_pop(video_buffer &video) {return impl_->try_pop(video);}
float input_video::fps() {return (float)impl_->cmn_->in_fps_;}
bool input_video::initialized() { return impl_->initialized_;}
float input_video::output_size() { return impl_->output_size(); }
void input_video::stop() {impl_->stop();}
bool input_video::empty_frames() {return impl_->empty_frames_;}
void input_video::drop() { impl_->drop(); }


}}