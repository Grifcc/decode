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
#include "rtsp_decoder.h"
#include "logger.h"

#include <chrono>

inline uint16_t transEnd(uint8_t *data)
{
    uint16_t val = static_cast<uint16_t>(*data) << 8 | *(data + 1);
    return val;
}

inline uint32_t seiNaluSize(uint32_t content)
{
    // start_code   NRI    payload_type     UUID   len   content     tail
    // 4             1      1                16     2     N         1(0x80)
    return 4 + 1 + 1 + 16 + 2 + content + 1;
}

RtspDecoder::RtspDecoder(const std::string &rtspUrl,
                         const int srcWidth,
                         const int srcHeight,
                         const int log_level) : rtspUrl_(rtspUrl),
                                                srcWidth_(srcWidth),
                                                srcHeight_(srcHeight)

{
    av_log_set_level(log_level);
    av_register_all();

    srcFmtContext_ = avformat_alloc_context();
    if (!srcFmtContext_)
    {
        MLOG_ERROR("avformat_alloc_context failed");
    }
    codecContext_ = avcodec_alloc_context3(nullptr);
    if (!codecContext_)
    {
        MLOG_ERROR("avcodec_alloc_context3 failed");
    }

    swsContext_ = sws_getContext(
        srcWidth_, srcHeight_, AV_PIX_FMT_YUV420P,
        srcWidth_, srcHeight_, AV_PIX_FMT_GRAY8,
        0, nullptr, nullptr, nullptr);
}

bool RtspDecoder::init_stream()
{

    if (avformat_open_input(&srcFmtContext_, rtspUrl_.c_str(), nullptr, nullptr) != 0)
    {
        MLOG_ERROR("avformat_open_input %s failed", rtspUrl_.c_str());
        avformat_free_context(srcFmtContext_);
        return false;
    }
    if (avformat_find_stream_info(srcFmtContext_, nullptr) < 0)
    {
        MLOG_ERROR("avformat_find_stream_info failed");
        avformat_close_input(&srcFmtContext_);
        avformat_free_context(srcFmtContext_);
        return false;
    }
    for (unsigned int i = 0; i < srcFmtContext_->nb_streams; ++i)
    {
        if (srcFmtContext_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            videoStreamIndex_ = i;
            break;
        }
    }
    MLOG_INFO("videoStreamIndex_ %d", videoStreamIndex_);
    if (videoStreamIndex_ == -1)
    {
        MLOG_ERROR("avformat_find_stream_info failed");
        avformat_close_input(&srcFmtContext_);
        avformat_free_context(srcFmtContext_);
        return false;
    }
    codecParameters_ = srcFmtContext_->streams[videoStreamIndex_]->codecpar;

    if (avcodec_parameters_to_context(codecContext_, codecParameters_) < 0)
    {
        MLOG_ERROR("avcodec_parameters_to_context failed");
        avcodec_free_context(&codecContext_);
        avformat_close_input(&srcFmtContext_);
        avformat_free_context(srcFmtContext_);
        return false;
    }
    codec_ = avcodec_find_decoder(codecParameters_->codec_id);
    MLOG_DEBUG("codec_name: %s", codec_->name);
    if (!codec_)
    {
        MLOG_ERROR("avcodec_find_decoder failed");
        avcodec_free_context(&codecContext_);
        avformat_close_input(&srcFmtContext_);
        avformat_free_context(srcFmtContext_);
        return false;
    }
    if (avcodec_open2(codecContext_, codec_, nullptr) < 0)
    {
        MLOG_ERROR("avcodec_open2 failed");
        avcodec_free_context(&codecContext_);
        avformat_close_input(&srcFmtContext_);
        avformat_free_context(srcFmtContext_);
        return false;
    }
    stopped_.store(false);
    return true;
}

void RtspDecoder::frame2mat(AVFrame *&frame, cv::Mat &mat)
{
    uint8_t *srcData[3] = {frame->data[0], frame->data[1], frame->data[2]};
    int srcLinesize[3] = {frame->linesize[0], frame->linesize[1], frame->linesize[2]};

    uint8_t *dstData[1] = {mat.data};
    int dstLinesize[1] = {mat.step};

    sws_scale(swsContext_, srcData, srcLinesize, 0, frame->height, dstData, dstLinesize);
}
void RtspDecoder::start()
{
    MLOG_INFO("start");
    AVFrame *frame = av_frame_alloc();
    if (!frame)
    {
        MLOG_ERROR("av_frame_alloc failed");
        avcodec_free_context(&codecContext_);
        avformat_close_input(&srcFmtContext_);
        avformat_free_context(srcFmtContext_);
        return;
    }

    AVPacket packet;
    while (av_read_frame(srcFmtContext_, &packet) >= 0)
    {
        if (packet.stream_index == videoStreamIndex_)
        {
            if (avcodec_send_packet(codecContext_, &packet) == 0)
            {
                while (avcodec_receive_frame(codecContext_, frame) == 0)
                {

                    Target *target = new Target();
                    target->frame = cv::Mat(frame->height, frame->width, CV_8UC1);
                    frame2mat(frame, target->frame);
                    decode_target(packet, target->labels);
                    data_queue_.push(target);
                    av_frame_unref(frame);
                }
            }
        }
        av_packet_unref(&packet);
    }
    av_frame_free(&frame);
    sws_freeContext(swsContext_);
    stop();
}

void RtspDecoder::stop()
{
    stopped_.store(true);
}

Target *RtspDecoder::get_target()
{
    return data_queue_.wait_and_pop();
}

void RtspDecoder::decode_target(const AVPacket &packet, std::vector<Label> &labels)
{
    uint8_t *data = packet.data;
    uint16_t len = transEnd(data + 22);
    auto nalusize = seiNaluSize(len);
    if (*(data + nalusize - 1) != 0x80)
    {
        MLOG_ERROR("data error");
        return;
    }
    uint8_t *target_data = data + 22 + 2;
    for (int i = 0; i < len / 12; ++i)
    {
        Label label;

        label.label = transEnd(target_data);
        label.x = transEnd(target_data + 2);
        label.y = transEnd(target_data + 4);
        label.w = transEnd(target_data + 6);
        label.h = transEnd(target_data + 8);
        label.id = transEnd(target_data + 10);
        labels.push_back(label);
        target_data += 12;
    }
}

RtspDecoder::~RtspDecoder()
{
    if (!stopped_.load())
    {
        stop();
    }
    avcodec_close(codecContext_);
    avcodec_free_context(&codecContext_);
    avformat_close_input(&srcFmtContext_);
    avformat_free_context(srcFmtContext_);
}