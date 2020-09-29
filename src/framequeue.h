//
// Created by 李林超 on 2020/9/24.
//

#include "ffmpeg_header.h"
#include <iostream>
#include <queue>
#include <mutex>
#include <condition_variable>

using namespace std;

class FrameQueue {
public:
    FrameQueue();
    ~FrameQueue();
//
    bool put(AVFrame *frame);
    bool get(AVFrame *frame);

public:
    std::queue<AVFrame*> queue;
    std::condition_variable cv;
    uint32_t nb_frames;
    uint32_t size;

    const int FrameQueueSize = 64;
};