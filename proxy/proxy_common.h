/* Copyright (C) 2007-2008 The Android Open Source Project
**
** This software is licensed under the terms of the GNU General Public
** License version 2, as published by the Free Software Foundation, and
** may be copied, distributed, and modified under those terms.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
*/
#ifndef _PROXY_COMMON_H_
#define _PROXY_COMMON_H_

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/select.h>
#include <netinet/in.h>
#endif

/* types and definitions used by all proxy connections */

typedef enum {
    PROXY_EVENT_NONE,
    PROXY_EVENT_CONNECTED,
    PROXY_EVENT_CONNECTION_REFUSED,
    PROXY_EVENT_SERVER_ERROR
} ProxyEvent;

/* event can't be NONE when this callback is called */
typedef void (*ProxyEventFunc)( void*  opaque, ProxyEvent  event );

extern void  proxy_set_verbose(int  mode);


typedef enum {
    PROXY_OPTION_AUTH_USERNAME = 1,
    PROXY_OPTION_AUTH_PASSWORD,

    PROXY_OPTION_HTTP_NOCACHE = 100,
    PROXY_OPTION_HTTP_KEEPALIVE,
    PROXY_OPTION_HTTP_USER_AGENT,

    PROXY_OPTION_MAX

} ProxyOptionType;


typedef struct {
    ProxyOptionType  type;
    const char*      string;
    int              string_len;
} ProxyOption;


/* add a new proxified socket connection to the manager's list. the event function
 * will be called when the connection is established or refused.
 *
 * only IPv4 is supported at the moment, since our slirp code cannot handle IPv6
 *
 * returns 0 on success, or -1 if there is no proxy service for this type of connection
 */
extern int   proxy_manager_add( int  socket, struct sockaddr_in*  address, void*  ev_opaque, ProxyEventFunc  ev_func );

/* remove an on-going proxified socket connection from the manager's list.
 * this is only necessary when the socket connection must be canceled before
 * the connection accept/refusal occured
 */
extern void  proxy_manager_del( int  socket );

/* this function is called to update the select file descriptor sets
 * with those of the proxified connection sockets that are currently managed */
extern void  proxy_manager_select_fill( int  *pcount, fd_set*  read_fds, fd_set*  write_fds, fd_set*  err_fds);

/* this function is called to act on proxified connection sockets when network events arrive */
extern void  proxy_manager_poll( fd_set*  read_fds, fd_set*  write_fds, fd_set*  err_fds );

#endif /* END */
