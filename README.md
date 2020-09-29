# ffmpeg-demo
该仓库主要记录了学习 ffmpeg 的过程，分别包括音频编码和解码，视频编码和解码。编解码接口均采用 ffmpeg4.2 编写

### player.c
使用 C 语言编写的音频解码 demo，mp3/wav/aac 解码后通过 SDL 进行播放，其中 SDL 播放采用回调的方式进行播放

### videoplay.c
使用 C 语言编写的视频解码过程和使用 SDL 播放

### video.c
使用 C 语言编写的音频和视频解码的 demo，视频解码 YUV420 格式，通过 SDL 进行播放

### play.cpp videocode.cpp
使用 C++ 语言编写的音频和视频的解码，并分别解码保存成 pcm 和 yuv 文件，分别编码成 mp3 和 h264 文件

### packetqueue.cpp framequeue.cpp 
解码进行保存的 packet 和 frame 队列，采用 std::queue 进行存放

### 编译方式
* Linux 上采用 makefile 或者 cmake 的方式进行编译，使用的时候替换路径即可
* Mac 上采用 cmake 的方式编译