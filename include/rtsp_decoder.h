/*************************************************************************************************************************
 * Copyright 2023 Xidian114
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the “Software”), to deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
 * WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *************************************************************************************************************************/
#pragma once

#include <opencv2/opencv.hpp>
#include <opencv2/core.hpp>

#include <string>
#include <atomic>
#include <vector>
#include <cstdint>

#include "threadsafe_queue.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
#include <libavutil/time.h>
#include <libavutil/imgutils.h>
#include <libavutil/log.h>
}

struct Label
{
    uint16_t label;
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;
    uint16_t id;
};

struct Target
{
    cv::Mat frame;
    std::vector<Label> labels;
};

class RtspDecoder
{
public:
    RtspDecoder(const std::string &rtspUrl, const int srcWidth, const int srcHeight, const int log_level = AV_LOG_QUIET);
    ~RtspDecoder();

    bool init_stream();
    void start();
    void stop();

    bool is_stop() const { return status_flag_.load() == 2; }
    bool is_ready() const { return status_flag_.load() == 0; }
    bool is_running() const { return status_flag_.load() == 1; }
    bool is_empty() const { return data_queue_.empty(); }
    Target *get_target();

private:
    void frame2mat(AVFrame* &frame, cv::Mat &mat);
    void decode_target(const AVPacket &packet, std::vector<Label> &labels);

    std::string rtspUrl_;
    int srcWidth_;
    int srcHeight_;
    const AVPixelFormat srcFormat_ = AV_PIX_FMT_YUV420P;
    int log_level_;
    
    int videoStreamIndex_ = -1;
    AVFormatContext *srcFmtContext_ = nullptr;
    AVCodecParameters *codecParameters_ = nullptr;
    AVCodecContext *codecContext_ = nullptr;
    AVCodec *codec_ = nullptr;
    SwsContext *swsContext_ = nullptr;

    threadsafe_queue<Target *> data_queue_;
    std::atomic<int> status_flag_;
};
