/*********************************************************************
 *
 * Copyright 2012 the original author or authors.
 * See the NOTICE file distributed with this work for additional
 * information regarding copyright ownership.
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *    http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 *********************************************************************/
#include "h264_server.h"

#include<ros/ros.h>
#include<microhttpd.h>
#include<sensor_msgs/CompressedImage.h>

using std::vector;
using sensor_msgs::CompressedImage;

#define PORT 8666

int static handleRequest(void *custom,
        MHD_Connection *connection,
        const char *url,
        const char *method,
        const char *version,
        const char *uploadData,
        size_t *uploadDataSize,
        void **session)
{
    H264Server *server = (H264Server*) custom;
    MHD_Response *response = server->readFrames(url);
    int ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
    MHD_destroy_response (response);
    return ret;
}

void H264Server::addFrame(sensor_msgs::CompressedImage *image, bool keyFrame)
{
    std::lock_guard<std::mutex> guard(lock);
    time_point<high_resolution_clock> currentTime = high_resolution_clock::now();
    // Purge clients that haven't accessed the stream in 10secs
    for (auto iter = clients.begin(); iter != clients.end(); iter++) {
        duration<double> duration = currentTime - iter->second.lastAccessTime;
        if (duration.count() > 10) {
            clients.erase(iter);
        }
    }
    // Add data to all the clients, so they can fetch at their own pace
    for (auto &entry : clients) {
        if (keyFrame) {
            entry.second.frameData.clear();
            entry.second.frames = 0;
        }
        if (entry.second.frames > 1) {
            continue;
        }
        entry.second.frameData.insert(
                entry.second.frameData.end(), 
                image->data.begin(),
                image->data.end());
        entry.second.frames++;
    }
}

MHD_Response* H264Server::readFrames(std::string client)
{
    std::lock_guard<std::mutex> guard(lock);
    ClientSession &session = clients[client];
    MHD_Response *response =
        MHD_create_response_from_buffer(session.frameData.size(),
                (void *) session.frameData.data(),
                MHD_RESPMEM_MUST_COPY);
    MHD_add_response_header(response, "Content-Type", "video/h264");
    // TODO: Change this to only be localhost and the local hostname
    MHD_add_response_header(response, "Access-Control-Allow-Origin", "*");
    session.frameData.clear();
    session.frames = 0;
    session.lastAccessTime = high_resolution_clock::now();
    return response;
}

void H264Server::start()
{
    if (running) {
        return;
    }
    daemon = MHD_start_daemon(MHD_USE_SELECT_INTERNALLY, PORT, nullptr, nullptr,
                             &handleRequest, this, MHD_OPTION_END);
}

void H264Server::stop()
{
    if (!running) {
        return;
    }
    running = false;
    MHD_stop_daemon(daemon);
}

H264Server::~H264Server()
{
    if (running) {
        stop();
    }
}
