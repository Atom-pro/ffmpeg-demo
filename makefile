NAME := $(shell uname)

ifeq ($(NAME), Linux)
    $(warning "Linux")
    MYPATH = -I/home/newuser/gaoding/FFmpeg -L/home/newuser/gaoding/FFmpeg \
    -L/home/newuser/gaoding/FFmpeg/libswresample -L/home/newuser/gaoding/FFmpeg/libavutil -L/home/newuser/gaoding/FFmpeg/libavformat \
    -L/home/newuser/gaoding/FFmpeg/libavcodec
else ifeq ($(NAME), Darwin)
    $(warning "Mac")
    MYPATH = -I/usr/local/Cellar/ffmpeg/4.2.1_2/include/libavcodec -I/usr/local/Cellar/ffmpeg/4.2.1_2/include/libswscale \
    -I/usr/local/Cellar/ffmpeg/4.2.1_2/include/libavformat -I/usr/local/Cellar/ffmpeg/4.2.1_2/include/libswresample \
    -I/usr/local/Cellar/ffmpeg/4.2.1_2/include/libavutil -I//usr/local/Cellar/ffmpeg/4.2.1_2/include/libavcodec \
    -I/usr/local/Cellar/sdl2/2.0.10/include/SDL2 -L/usr/local/Cellar/ffmpeg/4.2.1_2/lib -L/usr/local/Cellar/sdl2/2.0.10/lib
endif

# SRC = ./src/ffmpeg_SDL.c
#SRC = ./src/videoplay.include
# SRC = ./src/video.c
# SRC = ./src/player.c
SRC = ./src/play.cpp ./src/videocode.cpp ./src/framequeue.cpp ./src/packetqueue.cpp
BIN = fm_$(NAME)
CC = g++

$(BIN):$(SRC)
	$(CC) -o $@ $^ $(MYPATH) -lswscale -lavformat -lavutil -lswresample -lavcodec -lSDL2 -lSDL2main -lx264 -lpthread -std=c++11

.PHONY:clean
clean:
	rm -rf fp $(BIN)