//
// Created by 李林超 on 2020/9/23.
//
#include "ffmpeg_header.h"
#include <iostream>
#include <queue>
#include <mutex>
#include <condition_variable>

class PacketQueue {
public:
    PacketQueue();
    ~PacketQueue();

    bool put(AVPacket *packet);
    bool get(AVPacket *packet);

public:
    std::queue<AVPacket> queue;
    std::mutex mutex;
    std::condition_variable cv;
    Uint32 nb_packet;
    Uint32 size;

    const int PacketQueueSize = 64;
};