// SPDX-License-Identifier: GPL-3.0-or-later
#include "nativevideo.h"
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSGTexture>
#include <QSGRendererInterface>
#include <QUrl>
#include <QDebug>
#include <atomic>
#include <memory>
#include <windows.h>
#include <d3d11.h>
#include <d3d10.h>
#include "videodecoder.h"
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
namespace {
std::atomic<int> players{0};
std::atomic<quint64> frames{0};

class VideoNode final : public QSGSimpleTextureNode {
  public:
    ComPtr<ID3D11Device> device;

    ComPtr<ID3D11VideoDevice> videoDevice;
    ComPtr<ID3D11VideoContext> videoContext;
    ComPtr<ID3D11VideoProcessorEnumerator> enumerator;
    ComPtr<ID3D11VideoProcessor> processor;
    ComPtr<ID3D11Texture2D> target;
    ComPtr<ID3D11VideoProcessorOutputView> output;
    std::unique_ptr<Decoder> decoder;
    QSize size,inputSize;
    int generation=-1;
    bool first=true,errorSent=false;
    HRESULT result=S_OK;

    VideoNode(QQuickWindow *window) {
        ++players;
        setFiltering(QSGTexture::Linear);
        QImage blank(1,1,QImage::Format_ARGB32_Premultiplied); blank.fill(Qt::black);
        setTexture(window->createTextureFromImage(blank));
        device=static_cast<ID3D11Device *>(window->rendererInterface()->getResource(window,QSGRendererInterface::DeviceResource));
        if (!device) { result=E_NOINTERFACE; return; }
        ComPtr<ID3D11DeviceContext> context; device->GetImmediateContext(&context);
        ComPtr<ID3D10Multithread> multithread;
        if (SUCCEEDED(context.As(&multithread))) multithread->SetMultithreadProtected(TRUE);
        if (FAILED(result=device.As(&videoDevice))) return;
        if (FAILED(result=context.As(&videoContext))) return;
    }
    ~VideoNode() override {
        decoder.reset(); delete texture();
        output.Reset(); target.Reset(); processor.Reset(); enumerator.Reset();
        videoContext.Reset(); videoDevice.Reset(); device.Reset();

        --players;
    }
    void load(QString path,int next) {
        decoder.reset(); generation=next; first=true; errorSent=false;
        if (FAILED(result)) return;
        if (QUrl(path).isLocalFile()) path=QUrl(path).toLocalFile();
        decoder=std::make_unique<Decoder>(path.toUtf8(),device);
    }
    bool render(QQuickWindow *window,QSize next,bool contain) {
        if (!decoder || FAILED(result)) return false;
        auto sample=decoder->take();
        if (!sample) return false;
        auto *decoded=reinterpret_cast<ID3D11Texture2D *>(sample->data[0]);
        const UINT subresource=UINT(reinterpret_cast<uintptr_t>(sample->data[1]));
        const QSize video(sample->width,sample->height);
        if (next!=size || video!=inputSize || !target) {
            output.Reset(); processor.Reset(); enumerator.Reset();
            D3D11_VIDEO_PROCESSOR_CONTENT_DESC content{};
            content.InputFrameFormat=D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
            content.InputWidth=video.width(); content.InputHeight=video.height();
            content.OutputWidth=next.width(); content.OutputHeight=next.height();
            content.InputFrameRate={60,1}; content.OutputFrameRate={60,1};
            content.Usage=D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
            if (FAILED(result=videoDevice->CreateVideoProcessorEnumerator(&content,&enumerator))) return false;
            if (FAILED(result=videoDevice->CreateVideoProcessor(enumerator.Get(),0,&processor))) return false;
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width=next.width(); desc.Height=next.height(); desc.MipLevels=1; desc.ArraySize=1;
            desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count=1; desc.Usage=D3D11_USAGE_DEFAULT;
            desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
            ComPtr<ID3D11Texture2D> nextTarget;
            if (FAILED(result=device->CreateTexture2D(&desc,nullptr,&nextTarget))) return false;
            D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC view{};
            view.ViewDimension=D3D11_VPOV_DIMENSION_TEXTURE2D;
            if (FAILED(result=videoDevice->CreateVideoProcessorOutputView(nextTarget.Get(),enumerator.Get(),&view,&output))) return false;
            auto *nextTexture=QNativeInterface::QSGD3D11Texture::fromNative(nextTarget.Get(),window,next);
            if (!nextTexture) { result=E_FAIL; return false; }
            auto *oldTexture=texture(); setTexture(nextTexture); delete oldTexture;
            target=nextTarget; size=next; inputSize=video;
        }
        D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC view{};
        view.ViewDimension=D3D11_VPIV_DIMENSION_TEXTURE2D;
        D3D11_TEXTURE2D_DESC desc{}; decoded->GetDesc(&desc);
        if (first && qEnvironmentVariableIsSet("HWS_DEBUG")) qInfo()<<"Decoder surfaces"<<desc.ArraySize<<desc.Width<<desc.Height<<desc.Format;
        view.Texture2D.MipSlice=subresource % desc.MipLevels;
        view.Texture2D.ArraySlice=subresource / desc.MipLevels;
        ComPtr<ID3D11VideoProcessorInputView> input;
        if (FAILED(result=videoDevice->CreateVideoProcessorInputView(decoded,enumerator.Get(),&view,&input))) return false;
        RECT src{0,0,video.width(),video.height()},dst{0,0,size.width(),size.height()};
        const double videoAspect=double(video.width())/video.height(),outputAspect=double(size.width())/size.height();
        if (contain) {
            if (videoAspect>outputAspect) { int height=qRound(size.width()/videoAspect); dst.top=(size.height()-height)/2; dst.bottom=dst.top+height; }
            else { int width=qRound(size.height()*videoAspect); dst.left=(size.width()-width)/2; dst.right=dst.left+width; }
        } else {
            if (videoAspect>outputAspect) { int width=qRound(video.height()*outputAspect); src.left=(video.width()-width)/2; src.right=src.left+width; }
            else { int height=qRound(video.width()/outputAspect); src.top=(video.height()-height)/2; src.bottom=src.top+height; }
        }
        videoContext->VideoProcessorSetStreamFrameFormat(processor.Get(),0,D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
        videoContext->VideoProcessorSetStreamSourceRect(processor.Get(),0,TRUE,&src);
        videoContext->VideoProcessorSetStreamDestRect(processor.Get(),0,TRUE,&dst);
        videoContext->VideoProcessorSetStreamAutoProcessingMode(processor.Get(),0,FALSE);
        D3D11_VIDEO_PROCESSOR_COLOR_SPACE space{};
        space.YCbCr_Matrix=(sample->colorspace==AVCOL_SPC_BT709 || (sample->colorspace==AVCOL_SPC_UNSPECIFIED && video.height()>576)) ? 1 : 0;
        space.Nominal_Range=(sample->color_range==AVCOL_RANGE_JPEG) ? 2 : 1;
        videoContext->VideoProcessorSetStreamColorSpace(processor.Get(),0,&space);
        D3D11_VIDEO_PROCESSOR_COLOR_SPACE rgb{};
        videoContext->VideoProcessorSetOutputColorSpace(processor.Get(),&rgb);
        D3D11_VIDEO_COLOR black{}; black.RGBA.A=1;
        videoContext->VideoProcessorSetOutputBackgroundColor(processor.Get(),FALSE,&black);
        D3D11_VIDEO_PROCESSOR_STREAM stream{}; stream.Enable=TRUE; stream.pInputSurface=input.Get();
        result=videoContext->VideoProcessorBlt(processor.Get(),output.Get(),0,1,&stream);
        if (FAILED(result)) return false;
        markDirty(QSGNode::DirtyMaterial); ++frames; return true;
    }
};
}
int NativeVideo::livePlayers() { return players.load(); }
quint64 NativeVideo::deliveredFrames() { return frames.load(); }
NativeVideo::NativeVideo(QQuickItem *parent) : QQuickItem(parent) {
    setFlag(ItemHasContents);
    m_tick.setInterval(16); m_tick.setTimerType(Qt::PreciseTimer);
    connect(&m_tick,&QTimer::timeout,this,[this] { update(); });
}
void NativeVideo::updateTimer() {
    if (!m_source.isEmpty() && (!m_ready || !m_paused)) m_tick.start(); else m_tick.stop();
}
void NativeVideo::setSource(const QString &path) {
    if (m_source==path) return;
    m_source=path; ++m_generation; m_ready=false; m_loadTime.start();
    emit sourceChanged(); emit readyChanged(); updateTimer(); update();
}
void NativeVideo::setPaused(bool value) {
    if (m_paused==value) return;
    m_paused=value; emit pausedChanged(); updateTimer(); update();
}
void NativeVideo::setContain(bool value) {
    if (m_contain==value) return;
    m_contain=value; emit containChanged(); update();
}
QSGNode *NativeVideo::updatePaintNode(QSGNode *old,UpdatePaintNodeData *) {
    auto *node=static_cast<VideoNode *>(old);
    if (m_source.isEmpty() || width()<=0 || height()<=0) { delete node; return nullptr; }
    if (!node) node=new VideoNode(window());
    if (node->generation!=m_generation) node->load(m_source,m_generation);
    const bool paused=m_ready && m_paused;
    if (node->decoder) node->decoder->setPaused(paused);
    const QSize pixels(qMax(1,qRound(width()*window()->effectiveDevicePixelRatio())),qMax(1,qRound(height()*window()->effectiveDevicePixelRatio())));
    if (!paused && node->render(window(),pixels,m_contain) && node->first) {
        node->first=false;
        QMetaObject::invokeMethod(this,[this,generation=m_generation] {
            if (generation!=m_generation) return;
            m_ready=true; emit readyChanged(); updateTimer();
            if (qEnvironmentVariableIsSet("HWS_DEBUG")) qInfo()<<"Native video first frame in"<<m_loadTime.elapsed()<<"ms"<<m_source;
        },Qt::QueuedConnection);
    }
    HRESULT error=FAILED(node->result) ? node->result : node->decoder ? node->decoder->error.load() : S_OK;
    if (FAILED(error) && !node->errorSent) {
        node->errorSent=true;
        QMetaObject::invokeMethod(this,[this,generation=m_generation,error] {
            if (generation!=m_generation) return;
            m_tick.stop(); emit failed(QString("Windows video: 0x%1").arg(quint32(error),8,16,QChar('0')));
        },Qt::QueuedConnection);
    }
    node->setRect(boundingRect()); return node;
}
