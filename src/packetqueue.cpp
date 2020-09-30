//
// Created by 李林超 on 2020/9/23.
//

#include "packetqueue.h"

PacketQueue::PacketQueue()
    :nb_packet(0)
    ,size(0)
{
}

PacketQueue::~PacketQueue() {}

bool PacketQueue::put(AVPacket *packet) {
    AVPacket *pkt = av_packet_alloc();
    if (av_packet_ref(pkt, packet) < 0) {
        fprintf(stderr, "av_packet_ref failed\n");
        return false;
    }

    fprintf(stdout, "unique_lock\n");
    std::mutex tmpmutex;
    std::unique_lock<std::mutex> lock(tmpmutex);
    queue.push(*pkt);
    size += pkt->size;
    nb_packet++;
    fprintf(stdout, "%d nb_packet : %d， size : %d\n", packet->stream_index, nb_packet, size);
    if (nb_packet >= PacketQueueSize) {
        cv.wait(lock);
    }
    return true;
}

bool PacketQueue::get(AVPacket *packet) {
    bool ret = false;
    std::unique_lock<std::mutex> lock(mutex);
    while (1) {
        if (!queue.empty()) {
            if (av_packet_ref(packet, &queue.front()) < 0) {
                ret = false;
                break;
            }
            AVPacket pkt = queue.front();
            queue.pop();
            av_packet_unref(&pkt);
            nb_packet--;
            size -= packet->size;
            cv.notify_one();
            ret = true;
            break;
        }
    }
    return ret;
}
