﻿#pragma once

class fcIExrContext
{
public:
    virtual void release() = 0;
    virtual bool beginFrame(const char *path, int width, int height) = 0;
    virtual bool addLayerTexture(void *tex, fcPixelFormat fmt, int channel, const char *name) = 0;
    virtual bool addLayerPixels(const void *pixels, fcPixelFormat fmt, int channel, const char *name) = 0;
    virtual bool endFrame() = 0;
protected:
    virtual ~fcIExrContext() {}
};
fcIExrContext* fcExrCreateContextImpl(const fcExrConfig *conf, fcIGraphicsDevice *dev);
