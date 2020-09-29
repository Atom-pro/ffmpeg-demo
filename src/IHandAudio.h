//
// Created by 李林超 on 2020/9/27.
//

class IHandAudio {
public:
    virtual ~IHandAudio() = default;
    virtual double getAudioClock() = 0;
};
