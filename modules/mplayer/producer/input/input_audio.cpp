#include "../../stdafx.h"
#include "input_audio.h"
#include "input_video.h"
#include "../../ffmpeg/producer/audio/audio_resampler.h"
#include "AudioDroper.h"
#include "mywavfile.h"


namespace caspar { namespace mplayer {
using namespace std;

struct input_audio::implementation : boost::noncopyable
{
	executor							executor_;
	mplayer_process*					mplayer_;
	shared_ptr<mplayer_pipe>			pipe_;
	HANDLE								mutex_;

	unsigned char*						avio_buffer_;
	tbb::atomic<bool>					initialized_;
	tbb::atomic<bool>					doread_;
	shared_ptr<AVFormatContext>			audio_format_;
	shared_ptr<AVCodecContext>			audio_codec_;
	rawdata_audio_queue					audio_raw_buffer_;
	shared_ptr<ffmpeg::audio_resampler>	swr_;
	core::audio_buffer					tmpAudioBuf_;

	std::shared_ptr<AVPacket>			packet;
	std::shared_ptr<AVFrame>			frame;
	int									packet_offset;
	
	int									read_state;	
	common_input_data*					cmn_;
	tbb::atomic<bool>					active_;

	tbb::atomic<bool>					empty_frames_;
	
	std::shared_ptr<AudioDroper>		audioDroper;
	bool								useAudioDroper;
	MyWavFile							debugWav;


	explicit implementation(long unique_num, mplayer_process* mplayer, common_input_data* cmn, AudioDroperCtorParams audioDroperParams, bool empty_mode)
		: executor_(L"mplayer-audio-read-" + toWStr(cmn->resource_name_) + L"-" + std::to_wstring((long long)unique_num))
		, mplayer_(mplayer)
		, pipe_(mplayer->audio_pipe())
		, avio_buffer_(0)
		, tmpAudioBuf_(384000)
		, read_state(1)
		, cmn_(cmn)
		, useAudioDroper(audioDroperParams.enable)
	{
		active_ = true;
		initialized_ = false;
		doread_ = false;
		audio_raw_buffer_.push( core::audio_buffer() );
		mutex_ = CreateMutex(NULL, FALSE, NULL);

		empty_frames_ = empty_mode;
		if (empty_frames_) mlg("no audio mode");

		mlg("out audio cadence = ");
		for (unsigned int i = 0; i < cmn_->out_audio_cadence_.size(); i++) mlg(cmn_->out_audio_cadence_[i]);								
		
		if (useAudioDroper) audioDroper.reset(new AudioDroper(audioDroperParams));
	}

	~implementation()
	{
		mdb("~input_audio");
		CloseHandle(mutex_);
		mdb("~input_audio OK");
	}

	void deinit()
	{
		mlg("deinit audio input...");
		initialized_ = false;
		clear_data();

		mdb("packet.reset();");
		packet.reset();

		mdb("frame.reset();");
		frame.reset();

		//mdb("sws_.reset();");
		//swr_.reset();

		mdb("video_codec_.reset();");
		audio_codec_.reset();

		//avio_buffer will get free in _format_.reset()
		//mdb("avio_buffer_.reset();");
		//avio_buffer_.reset();

		mdb("video_format_.reset();");
		audio_format_.reset();

	}



	void init()
	{
		try
		{
			mlg("init audio input...");
			deinit();


			mdb("mplayer working wait...");
			while (!mplayer_->working());
	
			//avio_buffer_.reset((unsigned char*)av_malloc(PIPE_BUF_SIZE), &av_free);
			avio_buffer_ = (unsigned char*)av_malloc(PIPE_BUF_SIZE);

			AVFormatContext *ctx = 0;
			
			ctx = avformat_alloc_context();
			ctx->probesize = 2048;

			AVIOContext *c = avio_alloc_context(avio_buffer_, PIPE_BUF_SIZE, 0, (void*)pipe_.get(), &readPipe, nullptr, nullptr);
			ctx->pb = c;	

			pipe_->use_counter = 0;

			mdb("open input");
			int ret = avformat_open_input(&ctx, "", nullptr, nullptr);
			if (ret != 0)
			{
				char desc[4096];
				memset(desc, 0, 4096);
				av_strerror(ret, desc, 4096);
				merr("Error avformat_open_input (audio), error code " << ret << " desc = " << desc);
				avformat_free_context(ctx);

				mplayer_fatal_error("");
			}
			
			audio_format_.reset(ctx, &free_av_context);
			if (avformat_find_stream_info(audio_format_.get(), nullptr) < 0) mplayer_fatal_error("Error avformat_find_stream_info (audio)");

			AVCodec *audio_codec = nullptr;

			AVStream *audio_stream = audio_format_->streams[0];		
			auto codecID = audio_stream->codec->codec_id;		

			mdb("init audio input - find decoder...");
			audio_codec = avcodec_find_decoder(codecID);


			if (audio_codec == nullptr) mplayer_fatal_error("Codec required by audio not available");


	
			audio_codec_.reset(avcodec_alloc_context3(audio_codec), [](AVCodecContext* c) { avcodec_close(c); av_free(c); });

			std::vector<uint8_t> extraData(audio_stream->codec->extradata, audio_stream->codec->extradata + audio_stream->codec->extradata_size);

			audio_codec_->extradata = reinterpret_cast<uint8_t*>(extraData.data());
			audio_codec_->extradata_size = extraData.size();
			audio_codec_->channels = SAudioChannels;

			audio_codec_->sample_rate = SAudioSamplingRate;
			audio_codec_->sample_fmt = SAudioSampleFmt;

		
			mdb("init audio input - open decoder...");
			if (avcodec_open2(audio_codec_.get(), audio_codec, nullptr) < 0) mplayer_fatal_error("Could not open audio codec");


			if (!swr_)
			{
				mdb("init audio input - make swr...");
				swr_.reset( new ffmpeg::audio_resampler(SAudioChannels, SAudioChannels, SAudioSamplingRate, SAudioSamplingRate, SAudioSampleFmt, audio_codec_->sample_fmt) );
			}
	
			packet.reset(new AVPacket, [](AVPacket* p)
											{
												av_free_packet(p);
												delete p;
											});


			frame.reset(avcodec_alloc_frame(), &av_free);
			memset(packet.get(), 0, sizeof(AVPacket));
			packet_offset = 0;

			initialized_ = true;

			mlg("init audio input OK");

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
							mwarn("error read audio input. Try restart all");
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
				if ((!input_full()) && (active_))
				{
					this->thread();
					this->start_thread();
				}
			});
		}
	}	




	bool read()
	{
		mtr("audio read...");

		if (!mplayer_->working()) return false;

		if (packet_offset >= packet->size)
		{
			if (packet->data) av_free_packet(packet.get());

			if (av_read_frame(audio_format_.get(), packet.get()) < 0) 
			{
				mwarn("audio read error (av_read_frame < 0)");
				if (active_) mplayer_->problem();
				return false;
			}

			if (pipe_->cant_read_)
			{
				mwarn("audio read error (can't read pipe)");
				if (active_) mplayer_->problem();
				return false;
			}

			packet_offset = 0;
		}

		packet->data = packet->data + packet_offset;
		packet->size = packet->size - packet_offset;

		unsigned int written_bytes = tmpAudioBuf_.size() - FF_INPUT_BUFFER_PADDING_SIZE;

		mtr("avcodec_decode_audio3...");
		const auto processedLength = avcodec_decode_audio3(audio_codec_.get(), reinterpret_cast<int16_t*>(tmpAudioBuf_.data()), (int*)&written_bytes, packet.get());
		unsigned int samples_count = written_bytes / 4;
		bool isFrameAvailable = (samples_count > 0);

		mtr("processedLength = " << processedLength << "; samples_count = " << samples_count);
		if (processedLength < 0) 
		{
			mplayer_fatal_error("error decode raw audio?");
			return false;
		}
		packet_offset += processedLength;

		if (isFrameAvailable) 
		{	
			mtr("audio data available");

			try
			{	
				float S = 0;
				if (enable_fps_log)
				{
					S = output_size();
				}

				WaitForSingleObject(mutex_, INFINITE);
				{
					boost::range::push_back(audio_raw_buffer_.back(), getAudio(tmpAudioBuf_, samples_count));
				}
				ReleaseMutex(mutex_);

				if (enable_fps_log)
				{
					mdebug.iaCtr += (output_size() - S);
					mdebug.iarCtr++;
					if (TIMER_GET(AUDIO_IN_TMR) >= 1000)
					{
						mdebug.iaFps = mdebug.iaCtr;
						mdebug.iarFps = mdebug.iarCtr;
						mdebug.iaCtr = 0;
						mdebug.iarCtr = 0;
						TIMER_CLEAR(AUDIO_IN_TMR);

						mfps("input audio fps = " << mdebug.iaFps);
						mfps("input audio raw buffers fps = " << mdebug.iarFps);
					}
				}
			}
			catch (...)
			{
				ReleaseMutex(mutex_);
				throw;
			}	
		}

		return true;
	}

	typedef std::vector<int8_t, tbb::cache_aligned_allocator<int8_t>> vector8;

	core::audio_buffer getAudio(core::audio_buffer &buf, int samples_count)
	{	
		//It's pretty dirty way: copy-resample-copy. TODO: find way to avoid copying
		int8_t *beg8 = reinterpret_cast<int8_t*>(buf.data());
		vector8 V(beg8, beg8 + samples_count *4);

		V = swr_->resample(std::move(V));

		int32_t *beg32 = reinterpret_cast<int32_t*>(V.data());
		int res_cnt = samples_count;

		if (useAudioDroper)
		{		
			int c = V.size();
			audioDroper->feedSamples(beg32, c / 8);
			//mdb("Input_Audio feed " << c / 8);
			V.reserve(c * 100);
			beg32 = reinterpret_cast<int32_t*>(V.data());
			auto getres = audioDroper->getSamples( beg32, (c * 100) / 8 );
			//mdb("Input_Audio get " << getres);
			res_cnt = getres * SAudioChannels;
		}

		return core::audio_buffer(beg32, beg32 + res_cnt);
	}

	void drop_frame()
	{
		if (useAudioDroper)
		{
			try
			{
				WaitForSingleObject(mutex_, INFINITE);
				{
					size_t samples = cmn_->out_audio_cadence_.front();
					audioDroper->dropSamples(samples);
					boost::range::rotate(cmn_->out_audio_cadence_, std::begin(cmn_->out_audio_cadence_)+1);
				}
				ReleaseMutex(mutex_);
			}
			catch (...)
			{
				ReleaseMutex(mutex_);
				throw;
			}
		}
		else
		{
			core::audio_buffer dummy;
			try_pop(dummy);
		}
	}

	bool try_pop(core::audio_buffer &audio)
	{
		try
		{
			float S = 0;
			if (enable_fps_log)
			{
				S = output_size();
			}

			bool res = false;
			WaitForSingleObject(mutex_, INFINITE);
			{		
				size_t samples_per_frame = cmn_->out_audio_cadence_.front() * SAudioChannels;

				if (!empty_frames_)
				{
					if (audio_raw_buffer_.front().size() >= samples_per_frame)
					{
						auto begin = audio_raw_buffer_.front().begin();
						auto end   = begin + samples_per_frame;

						audio = core::audio_buffer(begin, end);

						audio_raw_buffer_.front().erase(begin, end);
						res = true;
					} else res = false;
				}
				else
				{
					audio = core::audio_buffer(samples_per_frame, 0);
					res = true;
				}

				boost::range::rotate(cmn_->out_audio_cadence_, std::begin(cmn_->out_audio_cadence_)+1);
			}
			ReleaseMutex(mutex_);

			if (!res) {mtr("can't pop audio from input");}


			if (enable_fps_log)
			{
				mdebug.oaCtr += (S - output_size());
				if (TIMER_GET(AUDIO_OUT_TMR) >= 1000)
				{
					mdebug.oaFps = mdebug.oaCtr;
					mdebug.oaCtr = 0;
					TIMER_CLEAR(AUDIO_OUT_TMR);

					mfps("output audio fps = " << mdebug.oaFps);
				}
			}

			return res;
		}
		catch (...)
		{
			ReleaseMutex(mutex_);
			throw;
		}
	}	

	float output_size()
	{
		float res = 0;
		try
		{
			WaitForSingleObject(mutex_, INFINITE);
			{	
				res =  (float)audio_raw_buffer_.front().size() / ((float)cmn_->out_audio_cadence_.front() * SAudioChannels);
			}
			ReleaseMutex(mutex_);
		}
		catch (...)
		{
			ReleaseMutex(mutex_);
			throw;
		}

		return res;
	}

	void clear_data()
	{
		try
		{
			WaitForSingleObject(mutex_, INFINITE);
			{	
				audio_raw_buffer_.front().clear();
			}
			ReleaseMutex(mutex_);
		}
		catch (...)
		{
			ReleaseMutex(mutex_);
			throw;
		}
	}


	void drop()
	{
		mdb("audio drop...");
		executor_.clear();
		clear_data();
		read_state = 1;
		while (doread_ == true); //executor.join does not work. Why?
		deinit();
		mdb("audio drop OK");
	}

	void stop()
	{
		mdb("input audio stop....");
		active_ = false; 
		pipe_->stop();
		drop();
		mdb("input audio stop OK");
	}

};


input_audio::input_audio(long unique_num, mplayer_process* mplayer, common_input_data* cmn, AudioDroperCtorParams audioDroperParams, bool empty_mode) : impl_(new implementation(unique_num, mplayer, cmn, audioDroperParams, empty_mode)) {}
void input_audio::start_thread() {impl_->start_thread();}
bool input_audio::try_pop(core::audio_buffer &audio) {return impl_->try_pop(audio);}
bool input_audio::initialized() {return impl_->initialized_;}
float input_audio::output_size() {return impl_->output_size();}
void input_audio::stop() {impl_->stop();}
bool input_audio::empty_frames() {return impl_->empty_frames_;}
void input_audio::drop() { impl_->drop(); }
void input_audio::drop_frame() { impl_->drop_frame(); }
}}



