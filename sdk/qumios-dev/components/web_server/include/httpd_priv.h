/*
** Copyright (c) 2017  Hughes Technologies Pty Ltd. 
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
** 
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
** 
** You should have received a copy of the GNU General Public License
** along with this program.  If not, see <http://www.gnu.org/licenses/>.
**
*/



/*
**  libhttpd Private Header File
*/


/***********************************************************************
** Standard header preamble.  Ensure singular inclusion, setup for
** function prototypes and c++ inclusion
*/

#ifndef LIB_HTTPD_PRIV_H

#define LIB_HTTPD_H_PRIV 1

#define __ANSI_PROTO(x)       x


#ifdef __cplusplus
extern "C" {
#endif


#define	LEVEL_NOTICE	"notice"
#define LEVEL_ERROR	"error"

#define HTTP_EXPAND_TEXT	1
#define HTTP_RAW_DATA		2

char * _httpd_unescape __ANSI_PROTO((char*));
char *_httpd_escape __ANSI_PROTO((char*));
char _httpd_from_hex(char);


void _httpd_catFile __ANSI_PROTO((httpd*, httpReq*, char*, int));
void _httpd_send403 __ANSI_PROTO((httpd*, httpReq*));
void _httpd_send304 __ANSI_PROTO((httpd*, httpReq*));
void _httpd_send404 __ANSI_PROTO((httpd*, httpReq*));
void _httpd_sendText __ANSI_PROTO((httpd*, httpReq*, char*));
void _httpd_sendFile __ANSI_PROTO((httpd*, httpReq*, char*));
void _httpd_sendStatic __ANSI_PROTO((httpd*, httpReq*, char*));
void _httpd_sendHeaders __ANSI_PROTO((httpd*, httpReq*, int, time_t);)
void _httpd_sanitiseUrl __ANSI_PROTO((char*));
void _httpd_freeVariables __ANSI_PROTO((httpVar*));
void _httpd_formatTimeString __ANSI_PROTO((char*, time_t));
void _httpd_storeData __ANSI_PROTO((httpd*, httpReq*, char*));
void _httpd_writeAccessLog __ANSI_PROTO((httpd*, httpReq*));
void _httpd_writeErrorLog __ANSI_PROTO((httpd*, httpReq*, char*, char*));


int _httpd_net_read __ANSI_PROTO((int, char*, int));
int _httpd_net_write __ANSI_PROTO((int, char*, int));
int _httpd_readBuf __ANSI_PROTO((httpd*, httpReq*, char*, int));
int _httpd_readChar __ANSI_PROTO((httpd*, httpReq*, char*));
int _httpd_readLine __ANSI_PROTO((httpd*, httpReq*, char*, int));
int _httpd_checkLastModified __ANSI_PROTO((httpd*, httpReq*, int));
int _httpd_sendDirectoryEntry __ANSI_PROTO((httpd*, httpReq*, httpContent*, char*));
int _httpd_executeEmber __ANSI_PROTO((httpd*, httpReq*, char*));

httpContent *_httpd_findContentEntry __ANSI_PROTO((httpd*, httpReq*, httpDir*, char*));
httpDir *_httpd_findContentDir __ANSI_PROTO((httpd*, httpReq*, char*, int));

#endif  /* LIB_HTTPD_PRIV_H */
