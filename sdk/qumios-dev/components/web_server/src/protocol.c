/*
** Copyright (c) 2017  Hughes Technologies Pty Ltd.
**
** This program is qm_free software: you can redistribute it and/or modify
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

#include "qm_types.h"
#include "qm_tcp.h"
#include "httpd.h"
#include "httpd_hal.h"
#include "httpd_priv.h"
#include "qm_string.h"
#include "qm_kernel.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

int _httpd_net_read(
	int sock,
	char *buf,
	int len)
{
	return httpd_net_read(sock, buf, len);
}

int _httpd_net_write(
	int sock,
	char *buf,
	int len)
{
	int remain = len,
		sent = 0,
		offset = 0;

	while (remain)
	{
		sent = httpd_net_write(sock, buf + offset, remain);
		if (sent < 0)
		{
			return (-1);
		}
		offset = offset + sent;
		remain = remain - sent;
	}
	return (0);
}

int _httpd_readChar(
	httpd *server,
	httpReq *request,
	char *cp)
{
	if (request->readBufRemain == 0)
	{
		bzero(request->readBuf, HTTP_READ_BUF_LEN + 1);
		request->readBufRemain = _httpd_net_read(request->clientSock,
												 request->readBuf, HTTP_READ_BUF_LEN);
		if (request->readBufRemain < 1)
			return (0);
		request->readBuf[request->readBufRemain] = 0;
		request->readBufPtr = request->readBuf;
	}
	*cp = *request->readBufPtr++;
	request->readBufRemain--;
	return (1);
}

int _httpd_readLine(
	httpd *server,
	httpReq *request,
	char *destBuf,
	int len)
{
	char curChar,
		*dst;
	int count;

	count = 0;
	dst = destBuf;
	while (count < len)
	{
		if (_httpd_readChar(server, request, &curChar) < 1)
			return (0);
		if (curChar == '\n')
		{
			*dst = 0;
			return (1);
		}
		if (curChar == '\r')
		{
			continue;
		}
		else
		{
			*dst++ = curChar;
			count++;
		}
	}
	*dst = 0;
	return (1);
}

int _httpd_readBuf(
	httpd *server,
	httpReq *request,
	char *destBuf,
	int len)
{
	char curChar,
		*dst;
	int count;

	count = 0;
	dst = destBuf;
	while (count < len)
	{
		if (_httpd_readChar(server, request, &curChar) < 1)
			return (0);
		*dst++ = curChar;
		count++;
	}
	return (1);
}

int _httpd_sendExpandedText(
	httpd *server,
	httpReq *request,
	char *buf,
	int bufLen)
{
	char *textStart,
		*textEnd,
		*varStart,
		*varEnd,
		varName[HTTP_MAX_VAR_NAME_LEN + 1];
	int length,
		offset;
	httpVar *var;

	length = offset = 0;
	textStart = buf;
	while (offset < bufLen)
	{
		/*
		** Look for the start of a variable name
		*/
		textEnd = index(textStart, '$');
		if (!textEnd)
		{
			/*
			** Nope.  Write the remainder and bail
			*/
			_httpd_net_write(request->clientSock,
							 textStart, bufLen - offset);
			length += bufLen - offset;
			offset += bufLen - offset;
			break;
		}

		/*
		** Looks like there could be a variable.  Send the
		** preceeding text and check it out
		*/
		_httpd_net_write(request->clientSock, textStart,
						 textEnd - textStart);
		length += textEnd - textStart;
		offset += textEnd - textStart;
		varEnd = index(textEnd, '}');
		if (*(textEnd + 1) != '{' || varEnd == NULL)
		{
			/*
			** Nope, false alarm.
			*/
			_httpd_net_write(request->clientSock, "$", 1);
			length += 1;
			offset += 1;
			textStart = textEnd + 1;
			continue;
		}

		/*
		** OK, looks like an embedded variable
		*/
		varStart = textEnd + 2;
		varEnd = varEnd - 1;
		if (varEnd - varStart > HTTP_MAX_VAR_NAME_LEN)
		{
			/*
			** Variable name is too long
			*/
			_httpd_net_write(request->clientSock, "$", 1);
			length += 1;
			offset += 1;
			textStart = textEnd + 1;
			continue;
		}

		/*
		** Is this a known variable?
		*/
		bzero(varName, HTTP_MAX_VAR_NAME_LEN);
		strncpy(varName, varStart, varEnd - varStart + 1);
		offset += strlen(varName) + 3;
		var = httpdGetVariableByName(server, request, varName);
		if (!var)
		{
			/*
			** Nope.  It's undefined.  Ignore it
			*/
			textStart = varEnd + 2;
			continue;
		}

		/*
		** Write the variables value and continue
		*/
		_httpd_net_write(request->clientSock, var->value,
						 strlen(var->value));
		length += strlen(var->value);
		textStart = varEnd + 2;
	}
	return (length);
}

void _httpd_writeAccessLog(
	httpd *server,
	httpReq *request)
{
	char dateBuf[30];
	struct tm *timePtr;
	time_t clock;
	int responseCode;

	if (server->accessLog == NULL)
		return;
	clock = time(NULL);
	timePtr = localtime(&clock);
	strftime(dateBuf, 30, "%d/%b/%Y:%T %Z", timePtr);
	responseCode = atoi(request->response.response);
	fprintf(server->accessLog, "%s - - [%s] %s \"%s\" %d %d\n",
			request->clientAddr, dateBuf, httpdRequestMethodName(request),
			httpdRequestPath(request), responseCode,
			request->response.responseLength);
}

void _httpd_writeErrorLog(
	httpd *server,
	httpReq *request,
	char *level,
	char *message)
{
	char dateBuf[30];
	struct tm *timePtr;
	time_t clock;

	if (server->errorLog == NULL)
		return;
	clock = time(NULL);
	timePtr = localtime(&clock);
	strftime(dateBuf, 30, "%a %b %d %T %Y", timePtr);
	if (request && *request->clientAddr != 0)
	{
		fprintf(server->errorLog, "[%s] [%s] [client %s] %s\n",
				dateBuf, level, request->clientAddr, message);
	}
	else
	{
		fprintf(server->errorLog, "[%s] [%s] %s\n",
				dateBuf, level, message);
	}
}

int _httpd_decode(
	char *bufcoded,
	char *bufplain,
	int outbufsize)
{
	static char six2pr[64] = {
		'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
		'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
		'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
		'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
		'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '+', '/'};

	static unsigned char pr2six[256];

	/* single character decode */
#define DEC(c) pr2six[(int)c]
#define _DECODE_MAXVAL 63

	static int first = 1;

	int nbytesdecoded, j;
	register char *bufin = bufcoded;
	register unsigned char *bufout = (unsigned char *)bufplain;
	register int nprbytes;

	/*
	** If this is the first call, initialize the mapping table.
	** This code should work even on non-ASCII machines.
	*/
	if (first)
	{
		first = 0;
		for (j = 0; j < 256; j++)
			pr2six[j] = _DECODE_MAXVAL + 1;
		for (j = 0; j < 64; j++)
			pr2six[(int)six2pr[j]] = (unsigned char)j;
	}

	/* Strip leading whitespace. */

	while (*bufcoded == ' ' || *bufcoded == '\t')
		bufcoded++;

	/*
	** Figure out how many characters are in the input buffer.
	** If this would decode into more bytes than would fit into
	** the output buffer, adjust the number of input bytes downwards.
	*/
	bufin = bufcoded;
	while (pr2six[(int)*(bufin++)] <= _DECODE_MAXVAL)
		;
	nprbytes = bufin - bufcoded - 1;
	nbytesdecoded = ((nprbytes + 3) / 4) * 3;
	if (nbytesdecoded > outbufsize)
	{
		nprbytes = (outbufsize * 4) / 3;
	}
	bufin = bufcoded;

	while (nprbytes > 0)
	{
		*(bufout++) = (unsigned char)(DEC(*bufin) << 2 | DEC(bufin[1]) >> 4);
		*(bufout++) = (unsigned char)(DEC(bufin[1]) << 4 | DEC(bufin[2]) >> 2);
		*(bufout++) = (unsigned char)(DEC(bufin[2]) << 6 | DEC(bufin[3]));
		bufin += 4;
		nprbytes -= 4;
	}
	if (nprbytes & 03)
	{
		if (pr2six[(int)bufin[-2]] > _DECODE_MAXVAL)
		{
			nbytesdecoded -= 2;
		}
		else
		{
			nbytesdecoded -= 1;
		}
	}
	bufplain[nbytesdecoded] = 0;
	return (nbytesdecoded);
}

char _httpd_from_hex(char c)
{
	return c >= '0' && c <= '9'	  ? c - '0'
		   : c >= 'A' && c <= 'F' ? c - 'A' + 10
								  : c - 'a' + 10; /* accept small letters just in case */
}

char *_httpd_unescape(
	char *str)
{
	char *p = str;
	char *q = str;
	static char blank[] = "";

	if (!str)
		return (blank);
	while (*p)
	{
		if (*p == '%')
		{
			p++;
			if (*p)
				*q = _httpd_from_hex(*p++) * 16;
			if (*p)
				*q = (*q + _httpd_from_hex(*p++));
			q++;
		}
		else
		{
			if (*p == '+')
			{
				*q++ = ' ';
				p++;
			}
			else
			{
				*q++ = *p++;
			}
		}
	}

	*q++ = 0;
	return str;
}

void _httpd_freeVariables(
	httpVar *var)
{
	httpVar *curVar, *lastVar;

	if (var == NULL)
		return;
	_httpd_freeVariables(var->nextVariable);
	var->nextVariable = NULL;
	curVar = var;
	while (curVar)
	{
		lastVar = curVar;
		curVar = curVar->nextValue;
		qm_free(lastVar->name);
		qm_free(lastVar->value);
		qm_free(lastVar);
	}
	return;
}

void _httpd_storeData(
	httpd *server,
	httpReq *request,
	char *query)
{
	char *cp,
		*cp2,
		var[50],
		*val,
		*tmpVal;

	if (!query)
		return;

	cp = query;
	cp2 = var;
	bzero(var, sizeof(var));
	val = NULL;
	while (*cp)
	{
		if (*cp == '=')
		{
			cp++;
			*cp2 = 0;
			val = cp;
			continue;
		}
		if (*cp == '&')
		{
			*cp = 0;
			tmpVal = _httpd_unescape(val);
			httpdAddVariable(server, request, var, tmpVal);
			cp++;
			cp2 = var;
			val = NULL;
			continue;
		}
		if (val)
		{
			cp++;
		}
		else
		{
			*cp2 = *cp++;
			if (*cp2 == '.')
			{
				strcpy(cp2, "_dot_");
				cp2 += 5;
			}
			else
			{
				cp2++;
			}
		}
	}
	*cp = 0;
	tmpVal = _httpd_unescape(val);
	httpdAddVariable(server, request, var, tmpVal);
}

void _httpd_formatTimeString(
	char *ptr,
	time_t clock)
{
	struct tm *timePtr;
	time_t localClock;

	if (clock == 0)
	{
		time(&localClock);
	}
	else
	{
		localClock = clock;
	}
	timePtr = gmtime(&localClock);
	if (!timePtr)
	{
		perror("gmtime");
	}
	strftime(ptr, (size_t)HTTP_TIME_STRING_LEN, "%a, %d %b %Y %T GMT",
			 timePtr);
}

void _httpd_sendHeaders(
	httpd *server,
	httpReq *request,
	int contentLength,
	time_t modTime)
{
	char tmpBuf[80],
		timeBuf[HTTP_TIME_STRING_LEN];
	time_t tmpTime;

	if (request->response.headersSent)
		return;

	request->response.headersSent = 1;
	_httpd_net_write(request->clientSock, "HTTP/1.0 ", 9);
	_httpd_net_write(request->clientSock, request->response.response,
					 strlen(request->response.response));
	_httpd_net_write(request->clientSock, request->response.headers,
					 strlen(request->response.headers));

	time(&tmpTime);
	_httpd_formatTimeString(timeBuf, tmpTime);
	_httpd_net_write(request->clientSock, "Date: ", 6);
	_httpd_net_write(request->clientSock, timeBuf, strlen(timeBuf));
	_httpd_net_write(request->clientSock, "\n", 1);

	_httpd_net_write(request->clientSock, "Connection: close\n", 18);
	_httpd_net_write(request->clientSock, "Content-Type: ", 14);
	_httpd_net_write(request->clientSock, request->response.contentType,
					 strlen(request->response.contentType));
	_httpd_net_write(request->clientSock, "\n", 1);

	if (contentLength > 0)
	{
		_httpd_net_write(request->clientSock, "Content-Length: ", 16);
		qm_snprintf(tmpBuf, sizeof(tmpBuf), "%d", contentLength);
		_httpd_net_write(request->clientSock, tmpBuf, strlen(tmpBuf));
		_httpd_net_write(request->clientSock, "\n", 1);

		_httpd_formatTimeString(timeBuf, modTime);
		_httpd_net_write(request->clientSock, "Last-Modified: ", 15);
		_httpd_net_write(request->clientSock, timeBuf, strlen(timeBuf));
		_httpd_net_write(request->clientSock, "\n", 1);
	}
	_httpd_net_write(request->clientSock, "\n", 1);
}

httpDir *_httpd_findContentDir(
	httpd *server,
	httpReq *request,
	char *dir,
	int createFlag)
{
	char buffer[HTTP_MAX_URL],
		*curDir;
	httpDir *curItem,
		*curChild;

	strncpy(buffer, dir, HTTP_MAX_URL);
	curItem = server->content;
	curDir = strtok(buffer, "/");
	while (curDir)
	{
		curChild = curItem->children;
		while (curChild)
		{
			if (strcmp(curChild->name, curDir) == 0)
				break;
			curChild = curChild->next;
		}
		if (curChild == NULL)
		{
			if (createFlag == HTTP_TRUE)
			{
				curChild = qm_malloc(sizeof(httpDir));
				bzero(curChild, sizeof(httpDir));
				curChild->name = qm_strdup(curDir);
				curChild->next = curItem->children;
				curItem->children = curChild;
			}
			else
			{
				return (NULL);
			}
		}
		curItem = curChild;
		curDir = strtok(NULL, "/");
	}
	return (curItem);
}

httpContent *_httpd_findContentEntry(
	httpd *server,
	httpReq *request,
	httpDir *dir,
	char *entryName)
{
	httpContent *curEntry;

	curEntry = dir->entries;
	while (curEntry)
	{
		if (curEntry->type == HTTP_WILDCARD ||
			curEntry->type == HTTP_C_WILDCARD)
			break;
		if (*entryName == 0 && curEntry->indexFlag)
			break;
		if (strcmp(curEntry->name, entryName) == 0)
			break;
		curEntry = curEntry->next;
	}
	if (curEntry)
		request->response.content = curEntry;
	return (curEntry);
}

void _httpd_send304(
	httpd *server,
	httpReq *request)
{
	httpdSetResponse(server, request, "304 Not Modified\n");
	if (server->errorFunction304)
	{
		(server->errorFunction304)(server, request, 304);
	}
	else
	{
		_httpd_sendHeaders(server, request, 0, 0);
	}
}

void _httpd_send403(
	httpd *server,
	httpReq *request)
{
	httpdSetResponse(server, request, "403 Permission Denied\n");
	if (server->errorFunction403)
	{
		(server->errorFunction403)(server, request, 403);
	}
	else
	{
		_httpd_sendHeaders(server, request, 0, 0);
		_httpd_sendText(server, request,
						"<HTML><HEAD><TITLE>403 Permission Denied</TITLE></HEAD>\n");
		_httpd_sendText(server, request,
						"<BODY><H1>Access to the request URL was denied!</H1>\n");
	}
}

void _httpd_send404(
	httpd *server,
	httpReq *request)
{
	char msg[HTTP_MAX_URL];

	qm_snprintf(msg, HTTP_MAX_URL,
			 "File does not exist: %s", request->path);
	_httpd_writeErrorLog(server, request, LEVEL_ERROR, msg);
	httpdSetResponse(server, request, "404 Not Found\n");
	if (server->errorFunction404)
	{
		(server->errorFunction404)(server, request, 404);
	}
	else
	{
		_httpd_sendHeaders(server, request, 0, 0);
		_httpd_sendText(server, request,
						"<HTML><HEAD><TITLE>404 Not Found</TITLE></HEAD>\n");
		_httpd_sendText(server, request,
						"<BODY><H1>The request URL was not found!</H1>\n");
		_httpd_sendText(server, request, "</BODY></HTML>\n");
	}
}

void _httpd_catFile(
	httpd *server,
	httpReq *request,
	char *path,
	int mode)
{
	int fd,
		readLen,
		writeLen;
	char buf[HTTP_MAX_LEN];

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return;
	readLen = _httpd_net_read(fd, buf, HTTP_MAX_LEN - 1);
	while (readLen > 0)
	{
		if (mode == HTTP_RAW_DATA)
		{
			request->response.responseLength += readLen;
			_httpd_net_write(request->clientSock, buf, readLen);
		}
		else
		{
			buf[readLen] = 0;
			writeLen = _httpd_sendExpandedText(server, request, buf,
											   readLen);
			request->response.responseLength += writeLen;
		}
		readLen = _httpd_net_read(fd, buf, HTTP_MAX_LEN - 1);
	}
	httpd_net_destroy(fd);
}

void _httpd_sendStatic(
	httpd *server,
	httpReq *request,
	char *data)
{
	if (_httpd_checkLastModified(server, request, server->startTime) == 0)
	{
		_httpd_send304(server, request);
	}
	_httpd_sendHeaders(server, request, strlen(data), server->startTime);
	httpdOutput(server, request, data);
}

void _httpd_sendFile(
	httpd *server,
	httpReq *request,
	char *path)
{
	char *suffix;
	struct stat sbuf;

	suffix = rindex(path, '.');
	if (suffix != NULL)
	{
		if (strcasecmp(suffix, ".gif") == 0)
			strcpy(request->response.contentType, "image/gif");
		if (strcasecmp(suffix, ".jpg") == 0)
			strcpy(request->response.contentType, "image/jpeg");
		if (strcasecmp(suffix, ".xbm") == 0)
			strcpy(request->response.contentType, "image/xbm");
		if (strcasecmp(suffix, ".png") == 0)
			strcpy(request->response.contentType, "image/png");
		if (strcasecmp(suffix, ".css") == 0)
			strcpy(request->response.contentType, "text/css");
	}
	if (stat(path, &sbuf) < 0)
	{
		_httpd_send404(server, request);
		return;
	}
	if (_httpd_checkLastModified(server, request, sbuf.st_mtime) == 0)
	{
		_httpd_send304(server, request);
	}
	else
	{
		_httpd_sendHeaders(server, request, sbuf.st_size, sbuf.st_mtime);
		if (strncmp(request->response.contentType, "text/", 5) == 0)
			_httpd_catFile(server, request, path, HTTP_EXPAND_TEXT);
		else
			_httpd_catFile(server, request, path, HTTP_RAW_DATA);
	}
}

int _httpd_sendDirectoryEntry(
	httpd *server,
	httpReq *request,
	httpContent *entry,
	char *entryName)
{
	char path[HTTP_MAX_URL];

	qm_snprintf(path, HTTP_MAX_URL, "%s/%s", entry->path, entryName);
	_httpd_sendFile(server, request, path);
	return (0);
}

void _httpd_sendText(
	httpd *server,
	httpReq *request,
	char *msg)
{
	request->response.responseLength += strlen(msg);
	_httpd_net_write(request->clientSock, msg, strlen(msg));
}

int _httpd_checkLastModified(
	httpd *server,
	httpReq *request,
	int modTime)
{
	char timeBuf[HTTP_TIME_STRING_LEN];

	_httpd_formatTimeString(timeBuf, modTime);
	if (strcmp(timeBuf, request->ifModified) == 0)
		return (0);
	return (1);
}

static unsigned char isAcceptable[96] =

/* Overencodes */
#define URL_XALPHAS (unsigned char)1
#define URL_XPALPHAS (unsigned char)2

	/*      Bit 0           xalpha          -- see HTFile.h
	**      Bit 1           xpalpha         -- as xalpha but with plus.
	**      Bit 2 ...       path            -- as xpalpha but with /
	*/
	/*   0 1 2 3 4 5 6 7 8 9 A B C D E F */
	{7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7, 0, 0, 7, 7, 7,  /* 2x   !"#$%&'()*+,-./ */
	 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 0, 0, 0, 0, 0, 0,  /* 3x  0123456789:;<=>?  */
	 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,  /* 4x  @ABCDEFGHIJKLMNO */
	 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 0, 0, 0, 0, 7,  /* 5X  PQRSTUVWXYZ[\]^_ */
	 0, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,  /* 6x  `abcdefghijklmno */
	 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 0, 0, 0, 0, 0}; /* 7X  pqrstuvwxyz{\}~ DEL */

#define ACCEPTABLE(a) (a >= 32 && a < 128 && ((isAcceptable[a - 32]) & mask))

static char *hex = "0123456789ABCDEF";

char *_httpd_escape(
	char *str)
{
	unsigned char mask = URL_XPALPHAS;
	char *p;
	char *q;
	char *result;
	int unacceptable = 0;
	for (p = str; *p; p++)
		if (!ACCEPTABLE((unsigned char)*p))
			unacceptable += 2;
	result = (char *)qm_malloc(p - str + unacceptable + 1);
	bzero(result, (p - str + unacceptable + 1));

	if (result == NULL)
	{
		return (NULL);
	}
	for (q = result, p = str; *p; p++)
	{
		unsigned char a = *p;
		if (!ACCEPTABLE(a))
		{
			*q++ = '%'; /* Means hex commming */
			*q++ = hex[a >> 4];
			*q++ = hex[a & 15];
		}
		else
			*q++ = *p;
	}
	*q++ = 0; /* Terminate */
	return result;
}

void _httpd_sanitiseUrl(
	char *url)
{
	char *from,
		*to,
		*last;

	/*
	** Remove multiple slashes
	*/
	from = to = url;
	while (*from)
	{
		if (*from == '/' && *(from + 1) == '/')
		{
			from++;
			continue;
		}
		*to = *from;
		to++;
		from++;
	}
	*to = 0;

	/*
	** Get rid of ./ sequences
	*/
	from = to = url;
	while (*from)
	{
		if (*from == '/' && *(from + 1) == '.' && *(from + 2) == '/')
		{
			from += 2;
			continue;
		}
		*to = *from;
		to++;
		from++;
	}
	*to = 0;

	/*
	** Catch use of /../ sequences and remove them.  Must track the
	** path structure and remove the previous path element.
	*/
	from = to = last = url;
	while (*from)
	{
		if (*from == '/' && *(from + 1) == '.' &&
			*(from + 2) == '.' && *(from + 3) == '/')
		{
			to = last;
			from += 3;
			continue;
		}
		if (*from == '/')
		{
			last = to;
		}
		*to = *from;
		to++;
		from++;
	}
	*to = 0;
}
