using System;
using UnityEngine;


namespace UTJ.FrameCapturer
{
    public class WebMEncoder : MovieEncoder
    {
        fcAPI.fcWebMContext m_ctx;
        fcAPI.fcWebMConfig m_config;
        fcAPI.fcStream m_ostream;

        public override Type type { get { return Type.WebM; } }

        public override void Initialize(object config, string outPath)
        {
            m_config = (fcAPI.fcWebMConfig)config;
            m_config.videoTargetFramerate = 60;
            m_config.audioSampleRate = AudioSettings.outputSampleRate;
            m_config.audioNumChannels = fcAPI.fcGetNumAudioChannels();
            m_ctx = fcAPI.fcWebMCreateContext(ref m_config);

            var path = outPath + ".webm";
            m_ostream = fcAPI.fcCreateFileStream(path);
            fcAPI.fcWebMAddOutputStream(m_ctx, m_ostream);
        }

        public override void Release()
        {
            fcAPI.fcGuard(() =>
            {
                m_ctx.Release();
                m_ostream.Release();
            });
        }

        public override void AddVideoFrame(byte[] frame, fcAPI.fcPixelFormat format, double timestamp)
        {
            if (m_config.video)
            {
                fcAPI.fcWebMAddVideoFramePixels(m_ctx, frame, format, timestamp);
            }
        }

        public override void AddAudioFrame(float[] samples, double timestamp)
        {
            if (m_config.audio)
            {
                fcAPI.fcWebMAddAudioFrame(m_ctx, samples, samples.Length);
            }
        }
    }
}
