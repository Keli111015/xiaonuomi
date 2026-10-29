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
**  libhttpd Header File
*/


/***********************************************************************
** Standard header preamble.  Ensure singular inclusion, setup for
** function prototypes and c++ inclusion
*/

#ifndef LIB_HTTPD_H
#define LIB_HTTPD_H 1


#define __ANSI_PROTO(x)       x


#ifdef __cplusplus
extern "C" {
#endif

#include "qm_types.h"


/***********************************************************************
** Macro Definitions
*/


#define	HTTP_PORT 		80
#define HTTP_MAX_LEN		2048
#define HTTP_MAX_URL		512
#define HTTP_MAX_HEADERS	1024
#define HTTP_MAX_AUTH		128
#define	HTTP_IP_ADDR_LEN	17
#define	HTTP_TIME_STRING_LEN	40
#define	HTTP_READ_BUF_LEN	(2048+1)
#define	HTTP_ANY_ADDR		NULL
#define HTTP_MAX_VAR_NAME_LEN	48
#define HTTP_MAX_BODY_LEN	512


#define	HTTP_GET		1
#define	HTTP_POST		2

#define	HTTP_TRUE		1
#define HTTP_FALSE		0

#define	HTTP_FILE		1
#define HTTP_C_FUNCT		2
#define HTTP_EMBER_FUNCT	3
#define HTTP_STATIC		4
#define HTTP_WILDCARD		5
#define HTTP_C_WILDCARD		6
#define HTTP_EMBER_WILDCARD	7

#define HTTP_METHOD_ERROR "\n<B>ERROR : Method Not Implemented</B>\n\n"

#define httpdRequestMethod(r) 		r->method
#define httpdRequestPath(r)		r->path
#define httpdRequestContentType(r)	r->contentType
#define httpdRequestContentLength(r)	r->contentLength

#define HTTP_ACL_PERMIT		1
#define HTTP_ACL_DENY		2



extern char 	LIBHTTPD_VERSION[],
		LIBHTTPD_VENDOR[];

/***********************************************************************
** Type Definitions
*/


typedef struct _httpd_var{
	char	*name,
		*value;
	struct	_httpd_var 	*nextValue,
				*nextVariable;
} httpVar;

typedef struct _httpd_content{
	char	*name;
	int	type,
		indexFlag;
	void	(*function)();
	char	*data,
		*path;
	int	(*preload)();
	struct	_httpd_content 	*next;
} httpContent;

typedef struct _httpd_dir{
	char	*name;
	struct	_httpd_dir *children,
			*next;
	struct	_httpd_content *entries;
} httpDir;


typedef struct {
	int		responseLength;
	httpContent	*content;
	char		headersSent,
			headers[HTTP_MAX_HEADERS],
			response[HTTP_MAX_URL],
			contentType[HTTP_MAX_URL];
} httpRes;


typedef	struct {
	int	clientSock,
		method,
		readBufRemain,
		contentLength,
		authLength;
	char	path[HTTP_MAX_URL],
		host[HTTP_MAX_URL],
		userAgent[HTTP_MAX_URL],
		referer[HTTP_MAX_URL],
		ifModified[HTTP_MAX_URL],
		contentType[HTTP_MAX_URL],
		authUser[HTTP_MAX_AUTH],
		authPassword[HTTP_MAX_AUTH],
		clientAddr[HTTP_IP_ADDR_LEN],
		readBuf[HTTP_READ_BUF_LEN + 1],
		*readBufPtr;
	char *bodyBuf;
	httpRes response;
	httpVar	*variables;
} httpReq;



typedef struct ip_acl_s{
        int     addr;
        char    len,
                action;
        struct  ip_acl_s *next;
} httpAcl;


typedef struct {
	int	port,
		serverSock;
	time_t	startTime;
	char	fileBasePath[HTTP_MAX_URL],
		*serverHostname;
	httpDir	*content;
	httpAcl	*defaultAcl;
	FILE	*accessLog,
		*errorLog;
	void	(*errorFunction304)(),
		(*errorFunction403)(),
		(*errorFunction404)();
} httpd;



/***********************************************************************
** Function Prototypes
*/


int httpdAddCContent __ANSI_PROTO((httpd*,char*,char*,int,int(*)(),void(*)()));
int httpdAddFileContent __ANSI_PROTO((httpd*,char*,char*,int,int(*)(),char*));
int httpdAddEmberContent __ANSI_PROTO((httpd*,char*,char*,int,int(*)(),char*));
int httpdAddStaticContent __ANSI_PROTO((httpd*,char*,char*,int,int(*)(),char*));
int httpdAddWildcardContent __ANSI_PROTO((httpd*,char*,int(*)(),char*));
int httpdAddCWildcardContent __ANSI_PROTO((httpd*,char*,int(*)(),void(*)()));
int httpdAddVariable __ANSI_PROTO((httpd*, httpReq*, char*, char*));
int httpdCheckAcl __ANSI_PROTO((httpd*, httpReq*, httpAcl*));
int httpdAuthenticate __ANSI_PROTO((httpd*, httpReq*, char*));
int httpdSetErrorFunction __ANSI_PROTO((httpd*,int,void(*)()));

char *httpdRequestMethodName __ANSI_PROTO((httpReq*));
char *httpdUrlEncode __ANSI_PROTO((char *));
char *httpdGetAuthUsername __ANSI_PROTO((httpd*, httpReq*));


void httpdAddHeader __ANSI_PROTO((httpd*, httpReq*, char*));
void httpdSetContentType __ANSI_PROTO((httpd*, httpReq*, char*));
void httpdSetResponse __ANSI_PROTO((httpd*, httpReq*, char*));
void httpdEndRequest __ANSI_PROTO((httpd*, httpReq*));
void httpdForceAuthenticate __ANSI_PROTO((httpd*, httpReq*, char*));
void httpdSetExternalAuthUsername __ANSI_PROTO((httpd*, httpReq*, char*));

httpd *httpdCreate __ANSI_PROTO(());
httpReq *httpdReadRequest __ANSI_PROTO((httpd*, int timeout, int *status));

void httpdFreeVariables __ANSI_PROTO((httpd*, httpReq*));
void httpdDumpVariables __ANSI_PROTO((httpd*, httpReq*));
void httpdOutput __ANSI_PROTO((httpd*, httpReq*, char*));
void httpdPrintf __ANSI_PROTO((httpd*, httpReq*, char*, ...));
void httpdProcessRequest __ANSI_PROTO((httpd*, httpReq*));
void httpdSendHeaders __ANSI_PROTO((httpd*, httpReq*));
void httpdSetFileBase __ANSI_PROTO((httpd*, char*));
void httpdSetCookie __ANSI_PROTO((httpd*, httpReq*, char*, char*));
void httpdDeleteCookie __ANSI_PROTO((httpd*, httpReq*, char*));
void httpdSendFile __ANSI_PROTO((httpd*, httpReq*, char*));

void httpdSetErrorLog __ANSI_PROTO((httpd*, FILE*));
void httpdSetAccessLog __ANSI_PROTO((httpd*, FILE*));
void httpdSetDefaultAcl __ANSI_PROTO((httpd*, httpAcl*));

httpVar	*httpdGetVariableByName __ANSI_PROTO((httpd*, httpReq*, char*));
httpVar	*httpdGetVariableByPrefix __ANSI_PROTO((httpd*, httpReq*, char*));
httpVar	*httpdGetVariableByPrefixedName __ANSI_PROTO((httpd*, httpReq*, char*, char*));
httpVar *httpdGetNextVariableByPrefix __ANSI_PROTO((httpVar*, char*));

httpAcl *httpdAddAcl __ANSI_PROTO((httpd*, httpAcl*, char*, int));
void httpdDestroy(httpd	*server);
void httpdSend(httpd* server, httpReq *request, char *buf, int len );


/***********************************************************************
** Standard header file footer.  
*/

#ifdef __cplusplus
	}
#endif /* __cplusplus */
#endif /* file inclusion */


