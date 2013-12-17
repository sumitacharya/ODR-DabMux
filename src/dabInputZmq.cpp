/*
   Copyright (C) 2009 Her Majesty the Queen in Right of Canada (Communications
   Research Center Canada)

   Copyright (C) 2013 Matthias P. Braendli
   http://mpb.li

   ZeroMQ input. see www.zeromq.org for more info

   From the ZeroMQ manpage 'zmq':

       The 0MQ lightweight messaging kernel is a library which extends the standard
       socket interfaces with features traditionally provided by specialised
       messaging middleware products. 0MQ sockets provide an abstraction of
       asynchronous message queues, multiple messaging patterns, message filtering
       (subscriptions), seamless access to multiple transport protocols and more.
   */
/*
   This file is part of CRC-DabMux.

   CRC-DabMux is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as
   published by the Free Software Foundation, either version 3 of the
   License, or (at your option) any later version.

   CRC-DabMux is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with CRC-DabMux.  If not, see <http://www.gnu.org/licenses/>.
   */

#include "dabInput.h"
#include "dabInputZmq.h"
#include "dabInputFifo.h"

#include <stdio.h>
#include <zmq.h>
#include <list>
#include <string.h>
#include <string>
#include <limits.h>

extern TcpLog etiLog;

#ifdef __MINGW32__
#   define bzero(s, n) memset(s, 0, n)
#endif

#ifdef HAVE_INPUT_ZEROMQ


struct dabInputOperations dabInputZmqOperations = {
    dabInputZmqInit,
    dabInputZmqOpen,
    dabInputSetbuf,
    NULL,
    NULL,
    NULL,
    dabInputZmqReadFrame,
    dabInputSetbitrate,
    dabInputZmqClose,
    dabInputZmqClean,
    NULL
};


int dabInputZmqInit(void** args)
{
    dabInputZmqData* input = new dabInputZmqData;
    memset(&input->stats, 0, sizeof(input->stats));
    input->stats.id = dabInputFifoData::nb++;
    input->zmq_context = zmq_ctx_new();
    if (input->zmq_context == NULL) {
        etiLog.print(TcpLog::ERR, "Failed to initialise ZeroMQ context: %s\n", zmq_strerror(errno));
        return 1;
    }

    input->zmq_sock = zmq_socket(input->zmq_context, ZMQ_SUB);
    if (input->zmq_sock == NULL) {
        etiLog.print(TcpLog::ERR, "Failed to initialise ZeroMQ socket: %s\n", zmq_strerror(errno));
        return 1;
    }

    input->prebuffering = INPUT_ZMQ_PREBUFFERING;

    *args = input;

    return 0;
}


int dabInputZmqOpen(void* args, const char* inputUri)
{
    dabInputZmqData* input = (dabInputZmqData*)args;

    std::string uri = "tcp://" + std::string(inputUri);
    int connect_error = zmq_connect(input->zmq_sock, uri.c_str());

    if (connect_error < 0) {
        etiLog.print(TcpLog::ERR,  "Failed to connect socket to uri '%s': %s\n", uri.c_str(), zmq_strerror(errno));
        return 1;
    }

    connect_error = zmq_setsockopt(input->zmq_sock, ZMQ_SUBSCRIBE, NULL, 0);
    if (connect_error < 0) {
        etiLog.print(TcpLog::ERR, "Failed to subscribe to zmq messages: %s\n", zmq_strerror(errno));
        return 1;
    }

    input->uri = uri;
    return 0;
}


int dabInputZmqReadFrame(dabInputOperations* ops, void* args, void* buffer, int size)
{
    dabInputZmqData* input = (dabInputZmqData*)args;

    // Get new ZMQ messages
    int rc = dabInputZmqReadFromSocket(input, size);
    if (rc > 0)
        etiLog.print(TcpLog::NOTICE, "inputZMQ rc %d, %d\n", rc, input->frame_buffer.size());

    if (input->prebuffering > 0) {
        if (rc > 0)
            input->prebuffering--;
        if (input->prebuffering == 0)
            etiLog.print(TcpLog::NOTICE, "inputZMQ %s input pre-buffering complete\n",
                input->uri.c_str());
        memset(buffer, 0, size);
        return size;
    }

    if (input->frame_buffer.empty()) {
        etiLog.print(TcpLog::WARNING, "inputZMQ %s input empty, re-enabling pre-buffering\n",
                input->uri.c_str());
        // reset prebuffering
        input->prebuffering = INPUT_ZMQ_PREBUFFERING;
        memset(buffer, 0, size);
        return size;
    }
    else
    {
        char* newframe = input->frame_buffer.front();
        memcpy(buffer, newframe, size);
        etiLog.print(TcpLog::NOTICE, "inputZMQ got %d, %d\n", rc, input->frame_buffer.size());
        delete[] newframe;
        input->frame_buffer.pop_front();
        return size;
    }
}

// Read a superframe from the socket, cut it into five frames, and push to list
int dabInputZmqReadFromSocket(dabInputZmqData* input, int framesize)
{
    int rc;
    zmq_msg_t msg;
    rc = zmq_msg_init(&msg);
    if (rc == -1) {
        etiLog.print(TcpLog::ERR, "Failed to init zmq message: %s\n", zmq_strerror(errno));
        return 0;
    }

    int nBytes = zmq_msg_recv(&msg, input->zmq_sock, ZMQ_DONTWAIT);
    if (nBytes == -1) {
        if (errno != EAGAIN) {
            etiLog.print(TcpLog::ERR, "Failed to receive zmq message: %s\n", zmq_strerror(errno));
        }
        return 0;
    }

    char* data = (char*)zmq_msg_data(&msg);

    if (nBytes == 5*framesize) // five frames per superframe
    {
        if (input->frame_buffer.size() > INPUT_ZMQ_MAX_BUFFER_SIZE) {
            etiLog.print(TcpLog::WARNING, "inputZMQ %s input buffer, full, dropping frame !\n",
                input->uri.c_str());
            nBytes = 0;

            // we actually drop 2 frames
            input->frame_buffer.pop_front();
        }
        else {
            // copy the input frame blockwise into the frame_buffer
            for (char* framestart = data;
                    framestart < &data[5*framesize];
                    framestart += framesize) {
                char* frame = new char[framesize];
                memcpy(frame, framestart, framesize);
                input->frame_buffer.push_back(frame);
            }
        }
    }
    else
    {
        etiLog.print(TcpLog::ERR, "ZMQ Wrong data size: recv'd %d, need %d \n",
                nBytes, 5*framesize);
        nBytes = 0;
    }

    zmq_msg_close(&msg);
    return nBytes;
}


int dabInputZmqClose(void* args)
{
    dabInputZmqData* input = (dabInputZmqData*)args;
    zmq_close(input->zmq_sock);
    return 0;
}


int dabInputZmqClean(void** args)
{
    dabInputZmqData* input = (dabInputZmqData*)(*args);
    zmq_ctx_term(input->zmq_context);
    delete input;
    return 0;
}


#endif
