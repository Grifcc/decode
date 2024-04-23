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

#include <opencv2/opencv.hpp>
#include <unistd.h>

#include "logger.h"
#include <string>
#include <atomic>
#include <thread>

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        std::cout << "Usage: main <rtsp_url>  <save_path> \n"
                  << "\t retsp_url: rtsp url\n"
                  << "\t save_path: save path\n"
                  << "Examples:\n"
                  << "\t ./main rtsp://127.0.0.1:8554/0 output\n"
                  << std::endl;
        return -1;
    }
    std::string rtsp_url = argv[1];
    std::string save_path = argv[2];

    RtspDecoder *rtsp_decoder = new RtspDecoder(rtsp_url, 512, 512, AV_LOG_QUIET);
    while (!rtsp_decoder->init_stream())
    {
        MLOG_ERROR("init_stream failed");
        sleep(1);
    }
    MLOG_INFO("init_stream success");

    // 启动解码线程
    auto pull_thread = new std::thread(&RtspDecoder::start, rtsp_decoder);
    pull_thread->detach();

    MLOG_INFO("Start Read Img");
    uint16_t i = 0;
    while (true)
    {
        if (rtsp_decoder->is_empty())
        {
            if (rtsp_decoder->is_stop())
            {
                MLOG_INFO("rtsp_decoder is stopped");
                break;
            }
            else
            {
                usleep(1000);
                continue;
            }
        }
        MLOG_INFO("Get Img %d", i);
        Target *target = rtsp_decoder->get_target();
        std::string img_name = save_path + "/img_" + std::to_string(i) + ".jpg";
        cv::imwrite(img_name, target->frame);
        for (auto label : target->labels)
        {
            MLOG_INFO("label: %d, x: %d, y: %d, w: %d, h: %d, id: %d", label.label, label.x, label.y, label.w, label.h, label.id);
        }
        delete target;
        i++;
        /* code */
    }
    while (rtsp_decoder->is_ready())
    {
        /* code */
        usleep(1000);
    }
    delete rtsp_decoder;
    delete pull_thread;
    return 0;
}
