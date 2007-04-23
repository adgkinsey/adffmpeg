/*
 * Copyright (c) 2007 The FFmpeg Project.
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef NETWORK_H
#define NETWORK_H

#ifdef __MINGW32__
#define GUID microsoft_issue_GUID
#include <winsock2.h>
#undef GUID
#include <ws2tcpip.h>

typedef int socklen_t;

#define O_NONBLOCK              FIONBIO
#define EINPROGRESS             WSAEWOULDBLOCK
#define EINTR                   WSAEINTR

#define fcntl(fd,b,c)           { u_long arg = 1L; \
                                  ioctlsocket(fd, c, &arg ); }

int init_winsock( void );

#else

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#endif /* __MINGW32__ */

#endif
