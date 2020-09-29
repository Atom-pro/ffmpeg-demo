//
// Created by 李林超 on 2020/9/23.
//

#include "framequeue.h"

FrameQueue::FrameQueue()
        :nb_frames(0)
        ,size(0)
{
}

FrameQueue::~FrameQueue() {}

bool FrameQueue::put(AVFrame *frame) {
    AVFrame *fm = av_frame_alloc();
    if (av_frame_ref(fm, frame) < 0) {
        return false;
    }
    fm->opaque = (void*)new double(*(double*)fm->opaque);
    std::mutex mutex;
    std::unique_lock<std::mutex> lock(mutex);
    queue.push(fm);

    nb_frames++;
    fprintf(stdout, "FrameQueue nb_frames : %d， size : %d, FrameQueueSize : %d, queue.size() : %ld\n", nb_frames, size, FrameQueueSize, queue.size());
    if (nb_frames >= FrameQueueSize) {
        cv.wait(lock);
    }
    return true;
}

bool FrameQueue::get(AVFrame *frame) {
    bool ret = false;
    std::mutex mutex;
    std::unique_lock<std::mutex> lock(mutex);
    while (1) {
        if (!queue.empty()) {
            if (av_frame_ref(frame, queue.front()) < 0) {
                ret = false;
                break;
            }
            AVFrame *fm = queue.front();
            queue.pop();

            nb_frames--;
            fprintf(stdout, "FrameQueue get frame : %d\n", nb_frames);
//            size -= packet->size;
            cv.notify_one();
            ret = true;
            break;
        }
    }
    return ret;
}