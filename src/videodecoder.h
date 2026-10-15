// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QDebug>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <d3d11.h>
#include <wrl/client.h>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
}

struct VideoFrameDeleter { void operator()(AVFrame *frame) const { av_frame_free(&frame); } };
using VideoFrame=std::unique_ptr<AVFrame,VideoFrameDeleter>;

// Decode directly on Qt's D3D11 device. One presentation frame is pending;
// codec references remain GPU textures. No audio, software image copies or
// video transcoding are involved.
class Decoder {
    std::mutex mutex;
    std::condition_variable wake;
    std::atomic<bool> stopping{false};
    std::atomic<quint64> clockRevision{0};
    bool paused=false;
    VideoFrame pending;
    std::thread worker;
    static AVPixelFormat format(AVCodecContext *ctx,const AVPixelFormat *formats) {
        for (auto *f=formats;*f!=AV_PIX_FMT_NONE;++f) {
            if (*f!=AV_PIX_FMT_D3D11) continue;
            AVBufferRef *ref=nullptr;
            if (avcodec_get_hw_frames_parameters(ctx,ctx->hw_device_ctx,*f,&ref)<0) return AV_PIX_FMT_NONE;
            auto *pool=reinterpret_cast<AVHWFramesContext *>(ref->data);
            // H.264 reports its SPS reference count at format negotiation. Keep
            // room for reordering, our pending/presenting frames and decoder work.
            // Other codecs retain FFmpeg's conservative reference allocation.
            if (ctx->codec_id==AV_CODEC_ID_H264 && ctx->refs>0 && ctx->refs<=16)
                pool->initial_pool_size=qMin(pool->initial_pool_size+2,qMax(ctx->refs,ctx->has_b_frames)+4);
            else pool->initial_pool_size+=2;
            if (av_hwframe_ctx_init(ref)<0) { av_buffer_unref(&ref); return AV_PIX_FMT_NONE; }
            ctx->hw_frames_ctx=ref;
            if (qEnvironmentVariableIsSet("HWS_DEBUG")) qInfo()<<"D3D11 decoder pool"<<pool->initial_pool_size<<"refs"<<ctx->refs<<"reorder"<<ctx->has_b_frames;
            return *f;
        }
        return AV_PIX_FMT_NONE;
    }
    void run(const QByteArray &path,Microsoft::WRL::ComPtr<ID3D11Device> device) {
        using Clock=std::chrono::steady_clock;
        struct Resources {
            AVFormatContext *input=avformat_alloc_context();
            AVCodecContext *codec=nullptr;
            AVBufferRef *hardware=nullptr;
            AVPacket *packet=av_packet_alloc();
            ~Resources() {
                av_packet_free(&packet); avcodec_free_context(&codec);
                av_buffer_unref(&hardware); avformat_close_input(&input);
            }
        } r;
        if (!r.input || !r.packet) { error=E_OUTOFMEMORY; return; }
        r.input->interrupt_callback={[](void *self) { return static_cast<Decoder *>(self)->stopping.load() ? 1 : 0; },this};
        r.input->probesize=1024*1024;
        r.input->max_analyze_duration=500000;
        if (avformat_open_input(&r.input,path.constData(),nullptr,nullptr)<0) { error=E_FAIL; return; }
        // Indexed containers already expose codec and dimensions in their header.
        // Avoid find_stream_info's temporary software decoder and its 4K buffers.
        bool needsProbe=true;
        for (unsigned i=0;i<r.input->nb_streams;++i) {
            const auto *params=r.input->streams[i]->codecpar;
            if (params->codec_type!=AVMEDIA_TYPE_VIDEO) r.input->streams[i]->discard=AVDISCARD_ALL;
            else if (params->codec_id!=AV_CODEC_ID_NONE && params->width>0 && params->height>0) needsProbe=false;
        }
        if (needsProbe && avformat_find_stream_info(r.input,nullptr)<0) { error=E_FAIL; return; }
        const AVCodec *codec=nullptr;
        const int stream=av_find_best_stream(r.input,AVMEDIA_TYPE_VIDEO,-1,-1,&codec,0);
        if (stream<0 || !codec) { error=E_FAIL; return; }
        for (unsigned i=0;i<r.input->nb_streams;++i) if (int(i)!=stream) r.input->streams[i]->discard=AVDISCARD_ALL;
        auto *video=r.input->streams[stream];
        if (av_packet_side_data_get(video->codecpar->coded_side_data,video->codecpar->nb_coded_side_data,AV_PKT_DATA_DISPLAYMATRIX)) { error=E_NOTIMPL; return; }
        r.codec=avcodec_alloc_context3(codec);
        r.hardware=av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);
        if (!r.codec || !r.hardware) { error=E_OUTOFMEMORY; return; }
        auto *hardware=reinterpret_cast<AVHWDeviceContext *>(r.hardware->data);
        auto *d3d=reinterpret_cast<AVD3D11VADeviceContext *>(hardware->hwctx);
        d3d->device=device.Get(); d3d->device->AddRef();
        if (av_hwdevice_ctx_init(r.hardware)<0 || avcodec_parameters_to_context(r.codec,video->codecpar)<0) { error=E_FAIL; return; }
        r.codec->hw_device_ctx=av_buffer_ref(r.hardware);
        r.codec->get_format=format;
        r.codec->thread_count=1;
        if (avcodec_open2(r.codec,codec,nullptr)<0) { error=E_FAIL; return; }
        bool resetClock=true,draining=false;
        quint64 observedRevision=0;
        int64_t firstTimestamp=0,previousTimestamp=0;
        auto origin=Clock::now();
        const auto rate=av_guess_frame_rate(r.input,video,nullptr);
        const int64_t frameDuration=rate.num>0 && rate.den>0 ? av_rescale_q(1,av_inv_q(rate),AV_TIME_BASE_Q) : 16667;
        for (;;) {
            {
                std::unique_lock lock(mutex);
                const bool wasPaused=paused;
                wake.wait(lock,[this] { return stopping || (!paused && !pending); });
                if (stopping) return;
                if (wasPaused) resetClock=true;
            }
            VideoFrame frame(av_frame_alloc());
            if (!frame) { error=E_OUTOFMEMORY; return; }
            int result=avcodec_receive_frame(r.codec,frame.get());
            while (result==AVERROR(EAGAIN) && !stopping) {
                result=av_read_frame(r.input,r.packet);
                if (result<0) {
                    if (result!=AVERROR_EOF) { error=E_FAIL; return; }
                    result=avcodec_send_packet(r.codec,nullptr); draining=true;
                } else {
                    result=r.packet->stream_index==stream ? avcodec_send_packet(r.codec,r.packet) : 0;
                    av_packet_unref(r.packet);
                }
                if (result<0 && result!=AVERROR_EOF) { error=E_FAIL; return; }
                result=avcodec_receive_frame(r.codec,frame.get());
            }
            if (stopping) return;
            if (result==AVERROR_EOF && draining) {
                if (av_seek_frame(r.input,stream,video->start_time==AV_NOPTS_VALUE ? 0 : video->start_time,AVSEEK_FLAG_BACKWARD)<0) { error=E_FAIL; return; }
                avcodec_flush_buffers(r.codec); resetClock=true; draining=false; continue;
            }
            if (result<0 || frame->format!=AV_PIX_FMT_D3D11) { error=E_FAIL; return; }
            // HDR and interlaced files use the compatible backend's tone mapping/deinterlacing.
            if ((frame->flags&AV_FRAME_FLAG_INTERLACED) || frame->color_trc==AVCOL_TRC_SMPTE2084 || frame->color_trc==AVCOL_TRC_ARIB_STD_B67) { error=E_NOTIMPL; return; }
            const auto timestamp=frame->best_effort_timestamp==AV_NOPTS_VALUE ? previousTimestamp+frameDuration : av_rescale_q(frame->best_effort_timestamp,video->time_base,AV_TIME_BASE_Q);
            previousTimestamp=timestamp;
            const auto revision=clockRevision.load();
            if (revision!=observedRevision) { observedRevision=revision; resetClock=true; }
            if (resetClock) { firstTimestamp=timestamp; origin=Clock::now(); resetClock=false; }
            const auto due=origin+std::chrono::microseconds(timestamp-firstTimestamp);
            std::unique_lock lock(mutex);
            wake.wait_until(lock,due,[this] { return stopping || paused; });
            if (stopping) return;
            if (paused) {
                wake.wait(lock,[this] { return stopping || !paused; });
                if (stopping) return;
                firstTimestamp=timestamp; origin=Clock::now();
            }
            pending=std::move(frame);
        }
    }
  public:
    std::atomic<HRESULT> error{S_OK};
    Decoder(const QByteArray &path,Microsoft::WRL::ComPtr<ID3D11Device> device) {
        worker=std::thread([this,path,device] { run(path,device); });
    }
    ~Decoder() { stopping=true; wake.notify_all(); worker.join(); }
    void setPaused(bool value) {
        {
            std::lock_guard lock(mutex);
            if (paused!=value && !value) ++clockRevision;
            paused=value;
        }
        wake.notify_all();
    }
    VideoFrame take() { std::lock_guard lock(mutex); auto frame=std::move(pending); wake.notify_all(); return frame; }
};
