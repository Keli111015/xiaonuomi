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
#include "httpd_hal.h"
#include "httpd.h"
#include "httpd_priv.h"
#include "qm_kernel.h"
#include "qm_string.h"
#include <sys/types.h>
#include <sys/stat.h>

char *httpdUrlEncode(
	char *str)
{
	char *new,
		*cp;

	new = (char *)_httpd_escape(str);
	if (new == NULL)
	{
		return (NULL);
	}
	cp = new;
	while (*cp)
	{
		if (*cp == ' ')
			*cp = '+';
		cp++;
	}
	return (new);
}

char *httpdRequestMethodName(
	httpReq *request)
{
	static char tmpBuf[255];

	switch (request->method)
	{
	case HTTP_GET:
		return ("GET");
	case HTTP_POST:
		return ("POST");
	default:
		qm_snprintf(tmpBuf, 255, "Invalid method '%d'",
				 request->method);
		return (tmpBuf);
	}
}

httpVar *httpdGetVariableByName(
	httpd *server,
	httpReq *request,
	char *name)
{
	httpVar *curVar;

	curVar = request->variables;
	while (curVar)
	{
		if (strcmp(curVar->name, name) == 0)
			return (curVar);
		curVar = curVar->nextVariable;
	}
	return (NULL);
}

httpVar *httpdGetVariableByPrefix(
	httpd *server,
	httpReq *request,
	char *prefix)
{
	httpVar *curVar;

	if (prefix == NULL)
		return (request->variables);
	curVar = request->variables;
	while (curVar)
	{
		if (strncmp(curVar->name, prefix, strlen(prefix)) == 0)
			return (curVar);
		curVar = curVar->nextVariable;
	}
	return (NULL);
}

httpVar *httpdGetVariableByPrefixedName(
	httpd *server,
	httpReq *request,
	char *prefix,
	char *name)
{
	httpVar *curVar;
	int prefixLen;

	if (prefix == NULL)
		return (request->variables);
	curVar = request->variables;
	prefixLen = strlen(prefix);
	while (curVar)
	{
		if (strncmp(curVar->name, prefix, prefixLen) == 0 &&
			strcmp(curVar->name + prefixLen, name) == 0)
		{
			return (curVar);
		}
		curVar = curVar->nextVariable;
	}
	return (NULL);
}

httpVar *httpdGetNextVariableByPrefix(
	httpVar *curVar,
	char *prefix)
{
	if (curVar)
		curVar = curVar->nextVariable;
	while (curVar)
	{
		if (strncmp(curVar->name, prefix, strlen(prefix)) == 0)
			return (curVar);
		curVar = curVar->nextVariable;
	}
	return (NULL);
}

int httpdAddVariable(
	httpd *server,
	httpReq *request,
	char *name,
	char *value)
{
	httpVar *curVar, *lastVar, *newVar;

	if (name == NULL || value == NULL)
		return (-1);

	while (*name == ' ' || *name == '\t')
		name++;
	newVar = qm_malloc(sizeof(httpVar));
	bzero(newVar, sizeof(httpVar));
	newVar->name = qm_strdup(name);
	newVar->value = qm_strdup(value);
	lastVar = NULL;
	curVar = request->variables;
	while (curVar)
	{
		if (strcmp(curVar->name, name) != 0)
		{
			lastVar = curVar;
			curVar = curVar->nextVariable;
			continue;
		}
		while (curVar)
		{
			lastVar = curVar;
			curVar = curVar->nextValue;
		}
		lastVar->nextValue = newVar;
		return (0);
	}
	if (lastVar)
		lastVar->nextVariable = newVar;
	else
		request->variables = newVar;
	return (0);
}

int httpdSetVariableValue(
	httpd *server,
	httpReq *request,
	char *name,
	char *value)
{
	httpVar *var;

	if (name == NULL || value == NULL)
		return (-1);

	var = httpdGetVariableByName(server, request, name);
	if (var)
	{
		if (!var->value)
		{
			var->value = qm_strdup(value);
			return (0);
		}

		/*
		** It is possible that we have been passed our existing
		** value by the caller.  If it is then just get out of here
		*/
		if (var->value == value)
			return (0);
		qm_free(var->value);
		var->value = qm_strdup(value);
		return (0);
	}
	else
	{
		return (httpdAddVariable(server, request, name, value));
	}
}

httpd *httpdCreate(
	char *host,
	int port)
{
	httpd *new;
	int sock;
	/*
	** Create the handle and setup it's basic config
	*/
	new = qm_malloc(sizeof(httpd));
	if (new == NULL)
		return (NULL);
	bzero(new, sizeof(httpd));
	new->port = port;
	if (host == HTTP_ANY_ADDR)
		new->serverHostname = HTTP_ANY_ADDR;
	else
		new->serverHostname = qm_strdup(host);
	new->content = (httpDir *)qm_malloc(sizeof(httpDir));
	bzero(new->content, sizeof(httpDir));
	new->content->name = qm_strdup("");

	sock = httpd_net_creat(new->port);
	if (sock < 0){
		qm_free(new);
		return (NULL);
	}
	new->serverSock = sock;
	new->startTime = time(NULL);
	return (new);
}

void httpdDestroy(
	httpd *server)
{
	if (server == NULL)
		return;
	if (server->serverHostname)
		qm_free(server->serverHostname);
	qm_free(server);
}

httpReq *httpdReadRequest(
	httpd *server,
	int timeout,
	int *status)
{
	int _httpd_decode();
	static char buf[HTTP_MAX_LEN];
	int count,
		inHeaders;
	char *cp, *cp2,
		*tmpBuf;
	httpReq *request;
	char *body = NULL;

	*status = 0;
	request = (httpReq *)qm_malloc(sizeof(httpReq));
	bzero(request, sizeof(httpReq));
	if (request == NULL)
	{
		*status = -3;
		return (NULL);
	}

	request->clientSock = httpd_net_accept(server->serverSock, request->clientAddr, timeout);
	if(request->clientSock < 0){
			/* Timeout */
		qm_free(request);
		*status = 0;
		return(NULL);
	}

	request->readBufRemain = 0;
	request->readBufPtr = NULL;

	/*
	** Check the default ACL
	*/
	if (server->defaultAcl)
	{
		if (httpdCheckAcl(server, request, server->defaultAcl) ==
			HTTP_ACL_DENY)
		{
			httpdEndRequest(server, request);
			*status = -2;
			return (NULL);
		}
	}

	/*
	** Setup for a standard response
	*/
	strcpy(request->response.headers,
		   "Server: Hughes Technologies Embedded Server\n");
	strcpy(request->response.contentType, "text/html");
	strcpy(request->response.response, "200 Output Follows\n");
	request->response.headersSent = 0;

	/*
	** Read the request
	*/
	count = 0;
	inHeaders = 1;
	
	while (_httpd_readLine(server, request, buf, HTTP_MAX_LEN) > 0)
	{
		count++;

		/*
		** Special case for the first line.  Scan the request
		** method and path etc
		*/
		if (count == 1)
		{
			/*
			** First line.  Scan the request info
			*/
			cp = cp2 = buf;
			while (isalpha((unsigned char)*cp2))
				cp2++;
			*cp2 = 0;
			if (strcasecmp(cp, "GET") == 0)
				request->method = HTTP_GET;
			if (strcasecmp(cp, "POST") == 0)
				request->method = HTTP_POST;
			if (request->method == 0)
			{
				_httpd_net_write(request->clientSock,
								 HTTP_METHOD_ERROR,
								 strlen(HTTP_METHOD_ERROR));
				_httpd_net_write(request->clientSock, cp,
								 strlen(cp));
				_httpd_writeErrorLog(server, request,
									 LEVEL_ERROR, "Invalid method received");
				httpdEndRequest(server, request);
				*status = -4;
				return (NULL);
			}
			cp = cp2 + 1;
			while (*cp == ' ')
				cp++;
			cp2 = cp;
			while (*cp2 != ' ' && *cp2 != 0)
				cp2++;
			*cp2 = 0;
			strncpy(request->path, cp, HTTP_MAX_URL);
			_httpd_sanitiseUrl(request->path);
			continue;
		}

		/*
		** Process the headers
		*/
		if (inHeaders)
		{
			if (*buf == 0)
			{
				/*
				** End of headers.  Continue if there's
				** data to read
				*/
				if (request->contentLength == 0)
					break;
				inHeaders = 0;
				break;
			}
			if (strncasecmp(buf, "Cookie: ", 7) == 0)
			{
				char *var,
					*val,
					*end;

				var = index(buf, ':');
				while (var)
				{
					var++;
					val = index(var, '=');
					*val = 0;
					val++;
					end = index(val, ';');
					if (end)
						*end = 0;
					httpdAddVariable(server, request, var,
									 val);
					var = end;
				}
			}
			if (strncasecmp(buf, "Authorization: ", 15) == 0)
			{
				cp = index(buf, ':') + 2;
				if (strncmp(cp, "Basic ", 6) != 0)
				{
					/* Unknown auth method */
				}
				else
				{
					char authBuf[100];

					cp = index(cp, ' ') + 1;
					_httpd_decode(cp, authBuf, 100);
					request->authLength = strlen(authBuf);
					cp = index(authBuf, ':');
					if (cp)
					{
						*cp = 0;
						strncpy(request->authPassword,
								cp + 1, HTTP_MAX_AUTH);
					}
					strncpy(request->authUser, authBuf,
							HTTP_MAX_AUTH);
				}
			}
			if (strncasecmp(buf, "Host: ", 6) == 0)
			{
				cp = index(buf, ':') + 2;
				if (cp)
				{
					strncpy(request->host, cp, HTTP_MAX_URL);
				}
			}
			if (strncasecmp(buf, "Referer: ", 9) == 0)
			{
				cp = index(buf, ':') + 2;
				if (cp)
				{
					strncpy(request->referer, cp,
							HTTP_MAX_URL);
				}
			}
			if (strncasecmp(buf, "If-Modified-Since: ", 19) == 0)
			{
				cp = index(buf, ':') + 2;
				if (cp)
				{
					strncpy(request->ifModified, cp,
							HTTP_MAX_URL);
					cp = index(request->ifModified, ';');
					if (cp)
						*cp = 0;
				}
			}
			if (strncasecmp(buf, "Content-Type: ", 14) == 0)
			{
				cp = index(buf, ':') + 2;
				if (cp)
				{
					strncpy(request->contentType, cp,
							HTTP_MAX_URL);
				}
			}
			if (strncasecmp(buf, "Content-Length: ", 16) == 0)
			{
				cp = index(buf, ':') + 2;
				if (cp)
				{
					request->contentLength = atoi(cp);
				}
			}
			continue;
		}
	}

	if (count == 0)
	{
		httpdEndRequest(server, request);
		*status = 0;
		return (NULL);
	}

	/*
	** Process and POST data
	*/
	if (request->contentLength > 0)
	{
		if (strcmp(request->contentType, "multipart/form-data") == 0)
		{
			/* This is a multi-part MIME encoded form submission */
		}
		else
		{
			bzero(buf, HTTP_MAX_LEN);
			_httpd_readBuf(server, request, buf,
						   request->contentLength);
			//_httpd_storeData(server, request, buf);
		}
	}

	/*
	** Process any URL data
	*/
	cp = index(request->path, '?');
	if (cp != NULL)
	{
		*cp = 0;
		cp++;
		tmpBuf = qm_strdup(cp);
		tmpBuf = _httpd_unescape(tmpBuf);
		_httpd_storeData(server, request, tmpBuf);
		qm_free(tmpBuf);
	}

	body = strstr(buf, "\r\n\r\n");
	if (body)
	{
		request->bodyBuf = body + 4;
	}

	return (request);
}

void httpdEndRequest(
	httpd *server,
	httpReq *request)
{
	_httpd_freeVariables(request->variables);
	request->variables = NULL;
	httpd_net_destroy(request->clientSock);
	qm_free(request);
}

void httpdFreeVariables(
	httpd *server,
	httpReq *request)
{
	_httpd_freeVariables(request->variables);
}

void httpdDumpVariables(
	httpd *server,
	httpReq *request)
{
	httpVar *curVar,
		*curVal;

	curVar = request->variables;
	while (curVar)
	{
		printf("Variable '%s'\n", curVar->name);
		curVal = curVar;
		while (curVal)
		{
			printf("\t= '%s'\n", curVal->value);
			curVal = curVal->nextValue;
		}
		curVar = curVar->nextVariable;
	}
}

void httpdSetFileBase(
	httpd *server,
	char *path)
{
	strncpy(server->fileBasePath, path, HTTP_MAX_URL);
}

void httpdSetExternalAuthUsername(
	httpd *server,
	httpReq *request,
	char *username)
{
	strncpy(request->authUser, username, HTTP_MAX_AUTH);
}

char *httpdGetAuthUsername(
	httpd *server,
	httpReq *request)
{
	return (request->authUser);
}

int httpdAddFileContent(
	httpd *server,
	char *dir,
	char *name,
	int indexFlag,
	int (*preload)(),
	char *path)
{
	httpDir *dirPtr;
	httpContent *newEntry;

	dirPtr = _httpd_findContentDir(server, NULL, dir, HTTP_TRUE);
	newEntry = qm_malloc(sizeof(httpContent));
	if (newEntry == NULL)
		return (-1);
	bzero(newEntry, sizeof(httpContent));
	newEntry->name = qm_strdup(name);
	newEntry->type = HTTP_FILE;
	newEntry->indexFlag = indexFlag;
	newEntry->preload = preload;
	newEntry->next = dirPtr->entries;
	dirPtr->entries = newEntry;
	if (*path == '/')
	{
		/* Absolute path */
		newEntry->path = qm_strdup(path);
	}
	else
	{
		/* Path relative to base path */
		newEntry->path = qm_malloc(strlen(server->fileBasePath) +
								strlen(path) + 2);
		qm_snprintf(newEntry->path, HTTP_MAX_URL, "%s/%s",
				 server->fileBasePath, path);
	}
	return (0);
}

int httpdAddWildcardContent(
	httpd *server,
	char *dir,
	int (*preload)(),
	char *path)
{
	httpDir *dirPtr;
	httpContent *newEntry;

	dirPtr = _httpd_findContentDir(server, NULL, dir, HTTP_TRUE);
	newEntry = qm_malloc(sizeof(httpContent));
	if (newEntry == NULL)
		return (-1);
	bzero(newEntry, sizeof(httpContent));
	newEntry->name = NULL;
	newEntry->type = HTTP_WILDCARD;
	newEntry->indexFlag = HTTP_FALSE;
	newEntry->preload = preload;
	newEntry->next = dirPtr->entries;
	dirPtr->entries = newEntry;
	if (*path == '/')
	{
		/* Absolute path */
		newEntry->path = qm_strdup(path);
	}
	else
	{
		/* Path relative to base path */
		newEntry->path = qm_malloc(strlen(server->fileBasePath) +
								strlen(path) + 2);
		qm_snprintf(newEntry->path, HTTP_MAX_URL, "%s/%s",
				 server->fileBasePath, path);
	}
	return (0);
}

int httpdAddCContent(
	httpd *server,
	char *dir,
	char *name,
	int indexFlag,
	int (*preload)(),
	void (*function)())
{
	httpDir *dirPtr;
	httpContent *newEntry;

	dirPtr = _httpd_findContentDir(server, NULL, dir, HTTP_TRUE);
	newEntry = qm_malloc(sizeof(httpContent));
	if (newEntry == NULL)
		return (-1);
	bzero(newEntry, sizeof(httpContent));
	newEntry->name = qm_strdup(name);
	newEntry->type = HTTP_C_FUNCT;
	newEntry->indexFlag = indexFlag;
	newEntry->function = function;
	newEntry->preload = preload;
	newEntry->next = dirPtr->entries;
	dirPtr->entries = newEntry;
	return (0);
}

int httpdAddCWildcardContent(
	httpd *server,
	char *dir,
	int (*preload)(),
	void (*function)())
{
	httpDir *dirPtr;
	httpContent *newEntry;

	dirPtr = _httpd_findContentDir(server, NULL, dir, HTTP_TRUE);
	newEntry = qm_malloc(sizeof(httpContent));
	if (newEntry == NULL)
		return (-1);
	bzero(newEntry, sizeof(httpContent));
	newEntry->name = NULL;
	newEntry->type = HTTP_C_WILDCARD;
	newEntry->indexFlag = HTTP_FALSE;
	newEntry->function = function;
	newEntry->preload = preload;
	newEntry->next = dirPtr->entries;
	dirPtr->entries = newEntry;
	return (0);
}

int httpdAddStaticContent(
	httpd *server,
	char *dir,
	char *name,
	int indexFlag,
	int (*preload)(),
	char *data)
{
	httpDir *dirPtr;
	httpContent *newEntry;

	dirPtr = _httpd_findContentDir(server, NULL, dir, HTTP_TRUE);
	newEntry = qm_malloc(sizeof(httpContent));
	if (newEntry == NULL)
		return (-1);
	bzero(newEntry, sizeof(httpContent));
	newEntry->name = qm_strdup(name);
	newEntry->type = HTTP_STATIC;
	newEntry->indexFlag = indexFlag;
	newEntry->data = data;
	newEntry->preload = preload;
	newEntry->next = dirPtr->entries;
	dirPtr->entries = newEntry;
	return (0);
}

void httpdSendHeaders(
	httpd *server,
	httpReq *request)
{
	_httpd_sendHeaders(server, request, request->response.responseLength, 0);
}

void httpdSetResponse(
	httpd *server,
	httpReq *request,
	char *msg)
{
	strncpy(request->response.response, msg, HTTP_MAX_URL);
}

void httpdSetContentType(
	httpd *server,
	httpReq *request,
	char *type)
{
	strcpy(request->response.contentType, type);
}

void httpdAddHeader(
	httpd *server,
	httpReq *request,
	char *msg)
{
	strcat(request->response.headers, msg);
	if (msg[strlen(msg) - 1] != '\n')
		strcat(request->response.headers, "\n");
}

void httpdSetCookie(
	httpd *server,
	httpReq *request,
	char *name,
	char *value)
{
	char buf[HTTP_MAX_URL];

	qm_snprintf(buf, HTTP_MAX_URL, "Set-Cookie: %s=%s; path=/;", name, value);
	httpdAddHeader(server, request, buf);
}

void httpdDeleteCookie(
	httpd *server,
	httpReq *request,
	char *name)
{
	char buf[HTTP_MAX_URL];

	qm_snprintf(buf, HTTP_MAX_URL, "Set-Cookie: %s=; path=/; expires=%s;",
			 name, "Thu, 01 Jan 1970 00:00:01 GMT");
	httpdAddHeader(server, request, buf);
}

void httpdOutput(
	httpd *server,
	httpReq *request,
	char *msg)
{
	char buf[HTTP_MAX_LEN],
		varName[80],
		*src,
		*dest;
	int count;

	src = msg;
	dest = buf;
	count = 0;
	while (*src && count < HTTP_MAX_LEN)
	{
		if (*src == '$' && *(src + 1) == '{')
		{
			char *cp,
				*tmp;
			int count2;
			httpVar *curVar;

			tmp = src + 2;
			cp = varName;
			count2 = 0;
			while (*tmp && (isalnum((unsigned char)*tmp) || *tmp == '_') && count2 < 80)
			{
				*cp++ = *tmp++;
				count2++;
			}
			if (*tmp == '}')
			{
				*cp = 0;
				curVar = httpdGetVariableByName(server,
												request, varName);
				if (curVar)
				{
					strcpy(dest, curVar->value);
					dest = dest + strlen(dest);
					count += strlen(dest);
				}
				else
				{
					*dest++ = '$';
					*dest++ = '{';
					strcpy(dest, varName);
					dest += strlen(varName);
					*dest++ = '}';
					count += 3 + strlen(varName);
				}
				src = src + strlen(varName) + 3;
			}
			else
			{
				*dest++ = '$';
				*dest++ = '{';
				strcpy(dest, varName);
				dest += strlen(varName);
				count += 2 + strlen(varName);
				src = src + strlen(varName) + 2;
			}
			continue;
		}
		*dest++ = *src++;
		count++;
	}
	*dest = 0;
	request->response.responseLength += strlen(buf);
	if (request->response.headersSent == 0)
		httpdSendHeaders(server, request);
	_httpd_net_write(request->clientSock, buf, strlen(buf));
}

void httpdPrintf(httpd *server, httpReq *request, char *fmt, ...)
{

	va_list args;
	char buf[HTTP_MAX_LEN];
	va_start(args, fmt);
	if (request->response.headersSent == 0)
		httpdSendHeaders(server, request);
	vsnprintf(buf, HTTP_MAX_LEN, fmt, args);
	request->response.responseLength += strlen(buf);
	_httpd_net_write(request->clientSock, buf, strlen(buf));
}

void httpdSend(httpd *server, httpReq *request, char *buf, int len)
{

	if (request->response.headersSent == 0)
		httpdSendHeaders(server, request);
	request->response.responseLength += len;
	_httpd_net_write(request->clientSock, buf, len);
}

void httpdProcessRequest(
	httpd *server,
	httpReq *request)
{
	char dirName[HTTP_MAX_URL],
		entryName[HTTP_MAX_URL],
		*cp;
	httpDir *dir;
	httpContent *entry;

	request->response.responseLength = 0;
	strncpy(dirName, httpdRequestPath(request), HTTP_MAX_URL);
	cp = rindex(dirName, '/');
	if (cp == NULL)
	{
		printf("Invalid request path '%s'\n", dirName);
		return;
	}
	strncpy(entryName, cp + 1, HTTP_MAX_URL);
	if (cp != dirName)
		*cp = 0;
	else
		*(cp + 1) = 0;
	dir = _httpd_findContentDir(server, request, dirName, HTTP_FALSE);
	if (dir == NULL)
	{
		_httpd_send404(server, request);
		_httpd_writeAccessLog(server, request);
		return;
	}
	entry = _httpd_findContentEntry(server, request, dir, entryName);
	if (entry == NULL)
	{
		_httpd_send404(server, request);
		_httpd_writeAccessLog(server, request);
		return;
	}
	if (entry->preload)
	{
		if ((entry->preload)(server, request) < 0)
		{
			_httpd_writeAccessLog(server, request);
			return;
		}
	}
	switch (entry->type)
	{
	case HTTP_C_FUNCT:
	case HTTP_C_WILDCARD:
		(entry->function)(server, request);
		break;

#ifdef HAVE_EMBER
	case HTTP_EMBER_FUNCT:
	case HTTP_EMBER_WILDCARD:
		_httpd_executeEmber(server, request, entry->data);
		break;
#endif

	case HTTP_STATIC:
		_httpd_sendStatic(server, request, entry->data);
		break;

	case HTTP_FILE:
		httpdSendFile(server, request, entry->path);
		break;

	case HTTP_WILDCARD:
		if (_httpd_sendDirectoryEntry(server, request, entry,
									  entryName) < 0)
		{
			_httpd_send404(server, request);
		}
		break;
	}
	_httpd_writeAccessLog(server, request);
}

void httpdSetAccessLog(
	httpd *server,
	FILE *fp)
{
	server->accessLog = fp;
}

void httpdSetErrorLog(
	httpd *server,
	FILE *fp)
{
	server->errorLog = fp;
}

int httpdAuthenticate(
	httpd *server,
	httpReq *request,
	char *realm)
{
	char buffer[255];

	if (request->authLength == 0)
	{
		httpdSetResponse(server, request, "401 Please Authenticate");
		qm_snprintf(buffer, sizeof(buffer),
				 "WWW-Authenticate: Basic realm=\"%s\"\n", realm);
		httpdAddHeader(server, request, buffer);
		httpdOutput(server, request, "\n");
		return (0);
	}
	return (1);
}

void httpdForceAuthenticate(
	httpd *server,
	httpReq *request,
	char *realm)
{
	char buffer[255];

	httpdSetResponse(server, request, "401 Please Authenticate");
	qm_snprintf(buffer, sizeof(buffer),
			 "WWW-Authenticate: Basic realm=\"%s\"\n", realm);
	httpdAddHeader(server, request, buffer);
	httpdOutput(server, request, "\n");
}

int httpdSetErrorFunction(
	httpd *server,
	int error,
	void (*function)())
{
	static char errBuf[80];

	switch (error)
	{
	case 304:
		server->errorFunction304 = function;
		break;
	case 403:
		server->errorFunction403 = function;
		break;
	case 404:
		server->errorFunction404 = function;
		break;
	default:
		qm_snprintf(errBuf, 80,
				 "Invalid error code (%d) for custom callback",
				 error);
		_httpd_writeErrorLog(server, NULL, LEVEL_ERROR, errBuf);
		return (-1);
		break;
	}
	return (0);
}

void httpdSendFile(
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
		if (strcasecmp(suffix, ".json") == 0)
			strcpy(request->response.contentType,
				   "application/json");
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
