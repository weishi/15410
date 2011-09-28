#include "httpResponder.h"

responseObj *createResponseObj()
{
    responseObj *newObj = malloc(sizeof(responseObj));
    newObj->header = malloc(sizeof(DLL));
    initList(newObj->header, NULL, NULL, NULL);
    newObj->statusLine = NULL;
    newObj->fileMeta = NULL;
    newObj->headerBuffer = NULL;
    newObj->fileBuffer = NULL;
    newObj->headerPtr = 0;
    newObj->filePtr = 0;
    newObj->maxHeaderPtr = 0;
    newObj->maxFilePtr = 0;
    newObj->close = 0;
    return newObj;
}

void freeResponseObj(responseObj *res)
{
    if(res != NULL) {
        freeList(res->header);
        free(res->statusLine);
        free(res->fileMeta);
        free(res->headerBuffer);
        free(res->fileBuffer);
        free(res);
    }
}

void buildResponseObj(responseObj *res, requestObj *req)
{
    int errorFlag = addStatusLine(res, req);
    DLL *header = res->header;
    /* Add general headers */
    logger(LogDebug, "to add date\n");
    char *dateStr = getHTTPDate(time(0));
    insertNode(header, newHeaderEntry("date", dateStr));
    free(dateStr);

    logger(LogDebug, "to add server\n");
    insertNode(header, newHeaderEntry("server", "Liso/1.0"));

    if(errorFlag == 1) {
        /* Serve Error request */
        logger(LogDebug, "to add connection close\n");
        insertNode(header, newHeaderEntry("connection", "close"));
        res->close = 1;
    } else {
        switch(req->method) {
        case POST:
        case GET:
            logger(LogDebug, "Load file for GET\n");
            res->fileBuffer = loadFile(res->fileMeta);
            res->maxFilePtr = res->fileMeta->length;
        case HEAD: {
            logger(LogDebug, "Add header for HEAD\n");
            char *valBuf;
            char *valPtr;
            //Connection
            valPtr = getValueByKey(req->header, "connection");
            if(valPtr != NULL && strcmp(valPtr, "close") == 0) {
                insertNode(header, newHeaderEntry("connection", "close"));
                res->close = 1;
            }
            //Content-length
            valBuf = getContentLength(res->fileMeta);
            insertNode(header, newHeaderEntry("content-length", valBuf));
            free(valBuf);
            //Content-type
            insertNode(header, newHeaderEntry("content-type",
                                              getContentType(res->fileMeta)));
            //Last-modified
            insertNode(header, newHeaderEntry("last-modified",
                                              getHTTPDate(getLastMod(res->fileMeta))));
        }
        break;
        default:
            break;
        }
    }
    logger(LogDebug, "Fill header...\n");
    fillHeader(res);

}
void fillHeader(responseObj *res)
{
    int headerSize = res->header->size;
    size_t bufSize = 0;
    size_t lineSize;
    int i;
    bufSize += strlen(res->statusLine);
    res->headerBuffer = malloc(bufSize + 1);
    strcpy(res->headerBuffer, res->statusLine);
    for(i = 0; i < headerSize; i++) {
        headerEntry *hd = getNodeDataAt(res->header, i);
        lineSize = strlen(hd->key) + strlen(": ") + strlen(hd->value) + strlen("\r\n");
        res->headerBuffer = realloc(res->headerBuffer, bufSize + lineSize + 1);
        sprintf(res->headerBuffer + bufSize, "%s: %s\r\n", hd->key, hd->value);
        bufSize += lineSize;
    }
    lineSize = strlen("\r\n");
    res->headerBuffer = realloc(res->headerBuffer, bufSize + lineSize + 1);
    sprintf(res->headerBuffer + bufSize, "\r\n");
    bufSize += lineSize;
    res->maxHeaderPtr = bufSize;
    logger(LogDebug, "Complete Header: ---------\n%s\n---------\n", res->headerBuffer);
}

size_t writeResponse(responseObj *res, char *buf, ssize_t maxSize)
{
    size_t hdPart = 0;
    size_t fdPart = 0;
    size_t headerPtr = res->headerPtr;
    size_t maxHeaderPtr = res->maxHeaderPtr;
    size_t filePtr = res->filePtr;
    size_t maxFilePtr = res->maxFilePtr;

    if(maxSize <= 0) {
        return 0;
    }
    logger(LogDebug, "header=%d, file=%d\n", maxHeaderPtr - headerPtr, maxFilePtr - filePtr);
    if(headerPtr + maxSize <= maxHeaderPtr) {
        hdPart = maxSize;
    } else {
        hdPart = maxHeaderPtr - headerPtr;
        fdPart = maxSize - hdPart;
        if(filePtr + fdPart > maxFilePtr) {
            fdPart = maxFilePtr - filePtr;
        }
    }
    logger(LogDebug, "hdPart=%d, fdPart=%d\n", hdPart, fdPart);
    memcpy(buf, res->headerBuffer, hdPart);
    memcpy(buf + hdPart, res->fileBuffer, fdPart);
    res->headerPtr += hdPart;
    res->filePtr += fdPart;

    return hdPart + fdPart;
}

int toClose(responseObj *res)
{
    return res->close;
}

char *getHTTPDate(time_t tmraw)
{
    char *dateStr = malloc(256);
    struct tm ctm = *gmtime(&tmraw);
    strftime(dateStr, 256, "%a, %d %b %Y %H:%M:%S %Z", &ctm);
    return dateStr;
}

int addStatusLine(responseObj *res, requestObj *req)
{
    int errorFlag = 0;
    fileMetadata *fm;
    // 1.request error
    char **sl = &res->statusLine;
    *sl = "HTTP/1.1 200 OK\r\n";
    if(req->curState == requestError) {
        errorFlag = 1;
        switch((enum StatusCode)req->statusCode) {
        case BAD_REQUEST:
            *sl = "HTTP/1.1 400 BAD REQUEST\r\n";
            break;
        case NOT_FOUND:
            *sl = "HTTP/1.1 404 NOT FOUND\r\n";
            break;
        case LENGTH_REQUIRED:
            *sl = "HTTP/1.1 411 LENGTH REQUIRED\r\n";
            break;
        case INTERNAL_SERVER_ERROR:
            *sl = "HTTP/1.1 500 INTERNAL SERVER ERROR\r\n";
            break;
        case NOT_IMPLEMENTED:
            *sl = "HTTP/1.1 501 NOT IMPLEMENTED\r\n";
            break;
        case SERVICE_UNAVAILABLE:
            *sl = "HTTP/1.1 503 SERVICE UNAVAILABLE\r\n";
            break;
        case HTTP_VERSION_NOT_SUPPORTED:
            *sl = "HTTP/1.1 505 HTTP VERSION NOT SUPPORTED\r\n";
            break;
        default:
            *sl = "HTTP/1.1 500 INTERNAL SERVER ERROR\r\n";
            break;
        }
    }
    // 2.file error
    fm = prepareFile(req->uri, "r");
    if(fm == NULL) {
        errorFlag = 1;
        *sl = "HTTP/1.1 404 FILE NOT FOUND\r\n";
    } else {
        res->fileMeta = fm;
    }
    return errorFlag;
}


