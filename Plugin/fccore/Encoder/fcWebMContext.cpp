#include "pch.h"
#include "fcInternal.h"
#include "Foundation/fcFoundation.h"
#include "GraphicsDevice/fcGraphicsDevice.h"
#include "fcWebMContext.h"
#include "fcVorbisEncoder.h"
#include "fcVPXEncoder.h"
#include "fcWebMWriter.h"

#ifdef fcSupportWebM

class fcWebMContext : public fcIWebMContext
{
public:
    using VideoEncoderPtr   = std::unique_ptr<fcIWebMVideoEncoder>;
    using AudioEncoderPtr   = std::unique_ptr<fcIWebMAudioEncoder>;
    using WriterPtr         = std::unique_ptr<fcIWebMWriter>;
    using WriterPtrs        = std::vector<WriterPtr>;

    using VideoBuffer       = Buffer;
    using VideoBufferPtr    = std::shared_ptr<VideoBuffer>;
    using VideoBufferQueue  = ResourceQueue<VideoBufferPtr>;

    using AudioBuffer       = RawVector<float>;
    using AudioBufferPtr    = std::shared_ptr<AudioBuffer>;
    using AudioBufferQueue  = ResourceQueue<AudioBufferPtr>;


    fcWebMContext(fcWebMConfig &conf, fcIGraphicsDevice *gd);
    ~fcWebMContext() override;
    void release() override;

    void addOutputStream(fcStream *s) override;

    bool addVideoFrameTexture(void *tex, fcPixelFormat fmt, fcTime timestamp) override;
    bool addVideoFramePixels(const void *pixels, fcPixelFormat fmt, fcTime timestamp) override;
    bool addVideoFramePixelsImpl(const void *pixels, fcPixelFormat fmt, fcTime timestamp);
    void flushVideo();

    bool addAudioFrame(const float *samples, int num_samples, fcTime timestamp) override;
    void flushAudio();


    // Body: [](fcIWebMWriter& writer) {}
    template<class Body>
    void eachStreams(const Body &b)
    {
        for (auto& s : m_writers) { b(*s); }
    }

private:
    fcWebMConfig        m_conf;
    fcIGraphicsDevice   *m_gdev = nullptr;

    WriterPtrs          m_writers;

    TaskQueue           m_video_tasks;
    VideoEncoderPtr     m_video_encoder;
    VideoBufferQueue    m_video_buffers;
    fcWebMVideoFrame    m_video_frame;

    TaskQueue           m_audio_tasks;
    AudioEncoderPtr     m_audio_encoder;
    AudioBufferQueue    m_audio_buffers;
    fcWebMAudioFrame    m_audio_frame;
};


fcWebMContext::fcWebMContext(fcWebMConfig &conf, fcIGraphicsDevice *gd)
    : m_conf(conf)
    , m_gdev(gd)
{
    if (conf.video) {
        fcVPXEncoderConfig econf;
        econf.width = conf.video_width;
        econf.height = conf.video_height;
        econf.target_framerate = conf.video_target_framerate;
        econf.bitrate_mode = conf.video_bitrate_mode;
        econf.target_bitrate = conf.video_target_bitrate;

        switch (conf.video_encoder) {
        case fcWebMVideoEncoder::VP8:
            m_video_encoder.reset(fcCreateVP8EncoderLibVPX(econf));
            break;
        case fcWebMVideoEncoder::VP9:
            m_video_encoder.reset(fcCreateVP9EncoderLibVPX(econf));
            break;
        case fcWebMVideoEncoder::VP9LossLess:
            m_video_encoder.reset(fcCreateVP9LossLessEncoderLibVPX(econf));
            break;
        }

        for (int i = 0; i < 4; ++i) {
            m_video_buffers.push(VideoBufferPtr(new VideoBuffer()));
        }
    }

    if (conf.audio) {
        fcVorbisEncoderConfig econf;
        econf.sample_rate = conf.audio_sample_rate;
        econf.num_channels = conf.audio_num_channels;
        econf.bitrate_mode = conf.audio_bitrate_mode;
        econf.target_bitrate = conf.audio_target_bitrate;

        switch (conf.audio_encoder) {
        case fcWebMAudioEncoder::Vorbis:
            m_audio_encoder.reset(fcCreateVorbisEncoder(econf));
            break;
        case fcWebMAudioEncoder::Opus:
            m_audio_encoder.reset(fcCreateOpusEncoder(econf));
            break;
        }

        for (int i = 0; i < 4; ++i) {
            m_audio_buffers.push(AudioBufferPtr(new AudioBuffer()));
        }
    }
}

fcWebMContext::~fcWebMContext()
{
    flushVideo();
    flushAudio();
    m_video_tasks.wait();
    m_audio_tasks.wait();

    m_video_encoder.reset();
    m_audio_encoder.reset();
    m_writers.clear();
}

void fcWebMContext::release()
{
    delete this;
}

void fcWebMContext::addOutputStream(fcStream *s)
{
    auto *writer = fcCreateWebMWriter(*s, m_conf);
    if (m_video_encoder) { writer->setVideoEncoderInfo(*m_video_encoder); }
    if (m_audio_encoder) { writer->setAudioEncoderInfo(*m_audio_encoder); }
    m_writers.emplace_back(writer);
}


bool fcWebMContext::addVideoFrameTexture(void *tex, fcPixelFormat fmt, fcTime timestamp)
{
    if (!tex || !m_video_encoder || !m_gdev) { return false; }

    auto buf = m_video_buffers.pop();
    size_t psize = fcGetPixelSize(fmt);
    size_t size = m_conf.video_width * m_conf.video_height * psize;
    buf->resize(size);
    if (m_gdev->readTexture(buf->data(), buf->size(), tex, m_conf.video_width, m_conf.video_height, fmt)) {
        m_video_tasks.run([this, buf, fmt, timestamp]() {
            addVideoFramePixelsImpl(buf->data(), fmt, timestamp);
            m_video_buffers.push(buf);
        });
    }
    else {
        m_video_buffers.push(buf);
        return false;
    }
    return true;
}

bool fcWebMContext::addVideoFramePixels(const void *pixels, fcPixelFormat fmt, fcTime timestamp)
{
    if (!pixels || !m_video_encoder) { return false; }

    auto buf = m_video_buffers.pop();
    size_t psize = fcGetPixelSize(fmt);
    size_t size = m_conf.video_width * m_conf.video_height * psize;
    buf->resize(size);
    memcpy(buf->data(), pixels, size);

    m_video_tasks.run([this, buf, fmt, timestamp]() {
        addVideoFramePixelsImpl(buf->data(), fmt, timestamp);
        m_video_buffers.push(buf);
    });
    return true;
}

bool fcWebMContext::addVideoFramePixelsImpl(const void *pixels, fcPixelFormat fmt, fcTime timestamp)
{
    // encode!
    if (m_video_encoder->encode(m_video_frame, pixels, fmt, timestamp)) {
        eachStreams([&](fcIWebMWriter& writer) {
            writer.addVideoFrame(m_video_frame);
        });
        m_video_frame.clear();
    }

    return true;
}

void fcWebMContext::flushVideo()
{
    if (!m_video_encoder) { return; }

    m_video_tasks.run([this]() {
        if (m_video_encoder->flush(m_video_frame)) {
            eachStreams([&](fcIWebMWriter& writer) {
                writer.addVideoFrame(m_video_frame);
            });
            m_video_frame.clear();
        }
    });
}


bool fcWebMContext::addAudioFrame(const float *samples, int num_samples, fcTime timestamp)
{
    if (!samples || !m_audio_encoder) { return false; }

    auto buf = m_audio_buffers.pop();
    buf->assign(samples, num_samples);

    m_audio_tasks.run([this, buf, timestamp]() {
        if (m_audio_encoder->encode(m_audio_frame, buf->data(), buf->size(), timestamp)) {
            eachStreams([&](fcIWebMWriter& writer) {
                writer.addAudioFrame(m_audio_frame);
            });
            m_audio_frame.clear();
        }
        m_audio_buffers.push(buf);
    });
    return true;
}

void fcWebMContext::flushAudio()
{
    if (!m_audio_encoder) { return; }

    m_audio_tasks.run([this]() {
        if (m_audio_encoder->flush(m_audio_frame)) {
            eachStreams([&](fcIWebMWriter& writer) {
                writer.addAudioFrame(m_audio_frame);
            });
            m_audio_frame.clear();
        }
    });
}


fcIWebMContext* fcWebMCreateContextImpl(fcWebMConfig &conf, fcIGraphicsDevice *gd) { return new fcWebMContext(conf, gd); }

#else  // fcSupportWebM

fcIWebMContext* fcWebMCreateContextImpl(fcWebMConfig &conf, fcIGraphicsDevice *gd) { return nullptr; }

#endif // fcSupportWebM
