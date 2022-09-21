#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
// #include "epoll.h"
#include <sys/socket.h>
#include <unistd.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <sys/stat.h>

#include <memory.h>
#include <sys/timerfd.h>
#include <sys/time.h>

#include "http_server.h"
#include "io.h"
#include "log.h"

using namespace std;

int HttpServer::init(int port)
{
    _listenfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    int op = 1;
    setsockopt(_listenfd, SOL_SOCKET, SO_REUSEADDR, (char *)&op, sizeof(op));
    sockaddr_in sockAddr{};
    sockAddr.sin_port = htons(port);
    sockAddr.sin_family = AF_INET;
    sockAddr.sin_addr.s_addr = htons(INADDR_ANY);
    if (bind(_listenfd, (sockaddr *)&sockAddr, sizeof(sockAddr)) == -1)
    {
        LOG_ERROR("error when bind");
        return -1;
    }

    if (listen(_listenfd, 10) == -1)
    {
        LOG_ERROR("error when listen");
        return -1;
    }

    _epollfd = epoll_create(1);
    if (_epollfd < 0)
    {
        LOG_ERROR("cannot create epoll");
        return -1;
    }
    epoll_event event{};
    // 监听socket使用LT触发
    event.events = EPOLLIN;
    event.data.fd = _listenfd;
    epoll_ctl(_epollfd, EPOLL_CTL_ADD, _listenfd, &event);
    // 定时事件,用于定期清除过期http连接
    _timerfd = timerfd_create(CLOCK_MONOTONIC, 0);
    struct itimerspec timer;
    memset(&timer, 0, sizeof(timer));
    timer.it_interval.tv_sec = _INTERVAL_SEC;
    timer.it_value.tv_sec = _INTERVAL_SEC;
    timerfd_settime(_timerfd, 0, &timer, NULL);
    epoll_event time_event{};
    time_event.events = EPOLLIN;
    time_event.data.fd = _timerfd;
    epoll_ctl(_epollfd, EPOLL_CTL_ADD, _timerfd, &time_event);
}

void HttpServer::run(string file_prefix)
{
    HttpHandler handler(file_prefix, _epollfd);
    while (true)
    {
        int eventNum = epoll_wait(_epollfd, _events, _EVENT_SIZE, -1);
        if (eventNum == -1)
        {
            LOG_ERROR("epoll wait error");
            continue;
        }
        for (int i = 0; i < eventNum; i++)
        {
            epoll_event e = _events[i];
            int fd = e.data.fd;
            if (fd == _listenfd){
                handler.handle_listen(e, _listenfd);
            }
            else if (e.events & EPOLLERR || e.events & EPOLLHUP){
                LOG_ERROR("got epoll err/hup");
                handler.close_conn(fd);
            }
            else if (e.events & EPOLLIN){
                if (fd == _timerfd){
                    handler.handle_time(e);
                }else{
                    handler.handle_read(e);
                }
            }
            else if (e.events & EPOLLOUT){
                handler.handle_write(e);
            }
            else{
                LOG_ERROR("unexpected event");
                handler.close_conn(fd);
            }
        }
    }
}

HttpHandler::HttpHandler(string filePath, int epfd)
{
    _file_path = filePath;
    _epollfd = epfd;
}

int HttpHandler::handle_time(epoll_event event)
{
    uint64_t exp = 0;
    read(event.data.fd, &exp, sizeof(uint64_t));
    time_t now = time(0);
    // 清除已经超时的长连接，并不能准确的在timeout指定的时间内关闭连接，但也足够了
    map<int, HTTPResponse *>::iterator iter = _responseMap.begin();
    while (iter != _responseMap.end())
    {
        int fd = iter->first;
        HTTPResponse *response = iter->second;
        iter++;
        if (response->_keepalive && response->dead)
        {
            if (now > response->_expire)
            {
                close_conn(fd);
            }
        }
    }
}

int HttpHandler::handle_listen(epoll_event event, int listenfd)
{
    if (event.events & EPOLLIN)
    {
        sockaddr_in cli_addr{};
        socklen_t len = sizeof(cli_addr);
        int fd = accept(listenfd, (sockaddr *)&cli_addr, &len);
        if (fd > 0)
        {
            // set non-block
            int flag = fcntl(fd, F_GETFL, 0);
            if (flag < 0)
            {
                LOG_ERROR("error when set non blocked");
                return -1;
            }
            if (fcntl(fd, F_SETFL, flag | O_NONBLOCK) < 0)
            {
                LOG_ERROR("error when really set non blocked");
                return -1;
            }
            // add to epoll
            addEpollRead(fd);
        }
        else
        {
            LOG_ERROR("accept error");
            return -1;
        }
    }
    else
    {
        LOG_ERROR("unexpected event");
        return -1;
    }
    return 0;
}

int HttpHandler::handle_read(epoll_event event)
{
    int fd = event.data.fd;
    // 是否是第一次收到这个请求的事件
    if (_requestMap.count(fd) == 0)
    {
        _requestMap[fd] = new HTTPRequest();
    }

    HTTPRequest *request = _requestMap[fd];
    int res = readHttp(fd, _buff, sizeof(_buff), request);
    if (res == READ_FAIL || request->isError())
    {
        close_conn(fd);
        return -1;
    }
    // 已经接收完整个请求，开始往epoll注册写
    if (request->isFinish())
    {
        changeEpollR_W(fd);
        if (_responseMap.count(fd) > 0)
        {
            HTTPResponse *pre = _responseMap[fd];
            if (pre->dead) {
                // 长连接的下一个请求
                delete pre;
                _responseMap[fd] = parseReqToResp(request);
            }
            else
            {
                // 边界情况处理。当还有请求没处理完时，客户端又发送了新的请求，此时当作pipeline处理
                addToResponse(pre, request);
            }
        } else {
            _responseMap[fd] = parseReqToResp(request);
        }
        // 保存好套接字和response的映射后，直接丢掉request
        delete _requestMap[fd];
        _requestMap.erase(fd);
    }
    // 否则等待下次再读取
}

long getMillSec() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

int HttpHandler::handle_write(epoll_event event)
{
    int fd = event.data.fd;
    HTTPResponse *response = _responseMap[fd];
    long expire = getMillSec() + _MAX_WORK_TIME;
    while (response)
    {
        if (getMillSec() > expire) {
            changeEpollR_W(fd);
            return WRITE_CONTINUE;
        }
        struct stat file_stat;
        if (stat(response->_filePath.c_str(), &file_stat) != 0)
        {
            if (response->_first)
            {
                HttpHeader header;
                header.withStatus(302, "FOUND")->withHeader("Location", "http://192.168.33.10/index.html");
                sendHeader(&header, fd);
            }
            close_conn(fd);
            return 0;
        }
        int fileSize = file_stat.st_size;
        // 判断是否要进行chunk编码，暂不支持chunk和range兼容
        if (fileSize > _CHUNK_SIZE && !response->_range)
        {
            response->_chunk = true;
        }
        // 发送响应头
        if (response->_first)
        {
            int res = write_header(response, fd, fileSize);
            // 响应头发送不成功不重试
            if (res != WRITE_SUCCESS)
            {
                close_conn(fd);
                return -1;
            }
            response->_first = false;
        }
        // 发送body内容
        ifstream file;
        file.open(response->_filePath, ios::out);
        int res = 0;
        if (response->_chunk)
        {
            res = write_chunk(response, fd, &file);
        }
        else if (response->_range)
        {
            res = write_range(response, fd, &file, fileSize);
        }
        else
        {
            // 发送整个文件内容
            res = write_simple(response, fd, &file, fileSize);
        }
        file.close();
        // 结果处理
        if (res == WRITE_FAIL)
        {
            close_conn(fd);
            return -1;
        }
        else if (res == WRITE_SUCCESS)
        {
            if (!response->_keepalive)
            {
                // connection: close
                close_conn(fd);
                return 0;
            }
            else
            {
                if (response->_next)
                {
                    // pipeline请求，切换到下一个
                    _responseMap[fd] = response->_next;
                    delete response;
                    response = _responseMap[fd];
                }
                else
                {
                    // 最后一个待处理请求
                    response->dead = true;
                    response->_expire = time(0) + _KEEPALIVE_TIMEOUT;
                    changeEpollRead(fd);
                    return 0;
                }
            }
        } else {
            // 非阻塞错误，等待下一次事件
            return 0;
        }
    }
    return 1;
}
// 发送http响应头
int HttpHandler::write_header(HTTPResponse *response, int fd, int fileSize)
{
    HttpHeader header;
    // 客户端不想长连接，或是最后一个长连接中的请求
    if (!response->_keepalive)
    {
        header.withHeader("Connection", "close");
    }
    else
    {
        header.withHeader("Connection", "keep-alive")->withHeader("Keep-Alive", "timeout=" + to_string(_KEEPALIVE_TIMEOUT));
    }
    if (response->_chunk)
    {
        header.withStatus(200, "OK")->withHeader("Transfer-Encoding", "chunked");
    }
    else if (response->_range)
    {
        header.withStatus(206, "Partial Content");
        if (response->_multiRange)
        {
            // 多重range
            header.withHeader("Content-Type", "multipart/byteranges; boundary=3d6b6a416f9b5");
        }
        else
        {
            // 单个range
            map<int, int>::iterator iter = response->_rangeMap.begin();
            int start = iter->first;
            int end = iter->second;
            header.withHeader("Content-Range", "bytes " + to_string(start) + "-" + to_string(end) + "/" + to_string(fileSize))
                ->withHeader("Content-Length", to_string(end - start + 1));
        }
    }
    else
    {
        header.withStatus(200, "OK")
            ->withHeader("Content-Length", to_string(fileSize));
    }
    return sendHeader(&header, fd);
}

// 一次性发送整个文件
int HttpHandler::write_simple(HTTPResponse *response, int fd, ifstream *file, int fileSize)
{
    long expire = getMillSec() + _MAX_WORK_TIME;
    int readLen = 0;
    int writeLen = 0;
    if (response->_offset >= fileSize)
    {
        return WRITE_SUCCESS;
    }
    decide_rw_len(fileSize - response->_offset, &readLen, &writeLen);
    while (!file->eof())
    {
        if (getMillSec() > expire) {
            changeEpollR_W(fd);
            return WRITE_CONTINUE;
        }
        file->seekg(response->_offset, ios::beg);
        file->read(_msg, readLen);
        int real = file->gcount();
        int res = writeHttp(fd, _msg, real, writeLen, response);
        if (res != WRITE_SUCCESS)
        {
            return res;
        }
    }
    return WRITE_SUCCESS;
}

int HttpHandler::write_chunk(HTTPResponse *response, int fd, ifstream *file)
{
    long expire = getMillSec() + _MAX_WORK_TIME;
    int writeLen = _MAX_WRITE_SIZE;
    while (!file->eof())
    {
        if (getMillSec() > expire) {
            changeEpollR_W(fd);
            return WRITE_CONTINUE;
        }

        int readLen = _MAX_READ_SIZE;
        // 上次的chunk没发送完，从上次结束位置开始
        if (response->_chunking)
        {
            readLen = response->_chunkEnd - response->_offset;
        }
        file->seekg(response->_offset, ios::beg);
        file->read(_msg, readLen);
        int real = file->gcount();
        if (!response->_chunking)
        {
            // 第一次发chunk，记录chunk结束位置
            response->_chunkEnd = response->_offset + real;
            // 第一次写这个chunk，先写长度信息
            string chunkLen;
            stringstream ss;
            ss << std::hex << real;
            ss >> chunkLen;
            chunkLen += "\r\n";
            char *lenChar = const_cast<char *>(chunkLen.c_str());
            // response参数传入null，不将长度信息发送结果计入response.offset
            int res = writeHttp(fd, lenChar, chunkLen.length(), writeLen, nullptr);
            if (res != WRITE_SUCCESS)
            {
                return res;
            }
        }
        // 写文件实际内容
        response->_chunking = true;
        int res = writeHttp(fd, _msg, real, writeLen, response);
        if (res != WRITE_SUCCESS)
        {
            return res;
        }
        // 写一个chunk结束标志
        char cs[] = "\r\n";
        res = writeHttp(fd, cs, sizeof(cs) - 1, writeLen, nullptr);
        if (res != WRITE_SUCCESS)
        {
            // 边界情况需处理，chunk结束标志（不会真的两个字符都发不出去吧)
            return res;
        }
        response->_chunking = false;
    }
    // 所有chunk发送完
    char endMsg[] = "0\r\n\r\n";
    int res = writeHttp(fd, endMsg, sizeof(endMsg) - 1, writeLen, nullptr);
    if (res != WRITE_SUCCESS)
    {
        return WRITE_FAIL;
    }
    return WRITE_SUCCESS;
}

int HttpHandler::write_range(HTTPResponse *response, int fd, ifstream *file, int fileSize)
{
    long expire = getMillSec() + _MAX_WORK_TIME;
    string range_split = "3d6b6a416f9b5";
    map<int, int>::iterator iter;
    iter = response->_rangeMap.begin();
    while (iter != response->_rangeMap.end())
    {
        int start = iter->first;
        int end = iter->second;
        if (end == -1 || end > fileSize)
        {
            end = fileSize;
        }
        // 是否第一次发送某个range的信息
        if (response->_offset == 0)
        {
            // 多重range需要插入分割信息
            if (response->_multiRange)
            {
                string split = "\n--" + range_split + "\r\nContent-Range: bytes " + to_string(start) + "-" + to_string(end) + "/" + to_string(fileSize) + "\r\n\n";
                char *cs = const_cast<char *>(split.c_str());
                int res = writeHttp(fd, cs, split.length(), split.length(), nullptr);
                if (res != WRITE_SUCCESS)
                {
                    return WRITE_FAIL;
                }
            }
            response->_offset = start;
        }
        else
        {
            start = response->_offset;
        }

        int readLen = 0;
        int writeLen = 0;
        decide_rw_len(end - start + 1, &readLen, &writeLen);
        int res = 0;
        while (start < end)
        {
            if (getMillSec() > expire) {
                changeEpollR_W(fd);
                return WRITE_CONTINUE;
            }
            file->seekg(start, ios::beg);
            file->read(_msg, readLen);
            int real = file->gcount();
            res = writeHttp(fd, _msg, real, writeLen, response);
            if (res != WRITE_SUCCESS)
            {
                return res;
            }
            start += real;
        }
        // 完成一个范围的发送
        response->_offset = 0;
        response->_rangeMap.erase(iter->first);
        iter++;
    }
    if (response->_multiRange)
    {
        // 所有都发送完了
        string split = "\n--" + range_split + "--\n";
        char *cs = const_cast<char *>(split.c_str());
        int res = writeHttp(fd, cs, split.length(), split.length(), nullptr);
        if (res != WRITE_SUCCESS) {
            return WRITE_FAIL;
        }
    }
    return WRITE_SUCCESS;
}

void HttpHandler::decide_rw_len(int remain, int *readLen, int *writeLen)
{
    // 一次调用write，写入tcp缓冲区长度
    int once = _MIN_WRITE_SIZE;
    if (remain > once)
    {
        int multi = remain / once;
        // TODO，溢出处理
        once = multi * once;
        if (once > _MAX_WRITE_SIZE)
        {
            once = _MAX_WRITE_SIZE;
        }
    }
    // 一次从文件读取内容长度
    int flashSize = once * _READ_MULTI_WRITE;
    if (flashSize > _MAX_READ_SIZE)
    {
        flashSize = _MAX_READ_SIZE;
    }
    if (flashSize > remain)
    {
        flashSize = remain;
    }

    *readLen = flashSize;
    *writeLen = once;
}

void HttpHandler::close_conn(int fd)
{
    epoll_ctl(_epollfd, EPOLL_CTL_DEL, fd, nullptr);
    close(fd);
    if (_requestMap.count(fd) != 0)
    {
        delete _requestMap[fd];
        _requestMap.erase(fd);
    }    
    if (_responseMap.count(fd) != 0)
    {
        HTTPResponse *resp = _responseMap[fd];
        while (resp)
        {
            HTTPResponse *tmp = resp->_next;
            delete resp;
            resp = tmp;
        }
        _responseMap.erase(fd);
    }
}

int HttpHandler::sendHeader(HttpHeader *header, int fd)
{
    int len = 0;
    char *hs = header->build(&len);
    return writeHttp(fd, hs, len, _MIN_WRITE_SIZE, nullptr);
}

// int HttpHandler::responseErr(int code, string msg, int fd)
// {
//     HttpHeader header;
//     int len = 0;
//     char *hs = header.withStatus(code, msg)
//                 ->build(&len);
//     return writeHttp(fd, hs, len, _MIN_WRITE_SIZE, nullptr);
// }

void HttpHandler::addToResponse(HTTPResponse *head, HTTPRequest *request) {
    HTTPResponse *response = parseReqToResp(request);
    HTTPResponse *last = head;
    if (last->_next) {
        last = last->_next;
    }
    last->_next = response;
}

HTTPResponse *HttpHandler::parseReqToResp(HTTPRequest *request)
{
    if (!request->isPipeline())
    {
        return parseSingle(request);
    }
    else
    {
        HTTPRequest *next = request->getPipelineNext();
        HTTPResponse *head = nullptr, *pre = nullptr;
        while (next != nullptr)
        {
            HTTPResponse *response = parseSingle(next);
            next = next->getPipelineNext();
            if (pre != nullptr)
                pre->_next = response;
            else
                head = response;
            pre = response;
        }
        return head;
    }
}

HTTPResponse *HttpHandler::parseSingle(HTTPRequest *request)
{
    HTTPResponse *response = new HTTPResponse();
    string filePath = _file_path + request->getUrl();
    response->_filePath = filePath;
    if (request->existHeader("Range"))
    {
        response->_range = true;
        response->parseRange(request->getHeaderVal("Range"));
        if (response->_rangeMap.size() > 1)
        {
            response->_multiRange = true;
        }
    }
    if (request->existHeader("Connection"))
    {
        // http1.1默认开启长连接
        if (request->getHeaderVal("Connection") == "close")
        {
            response->_keepalive = false;
        }
    }
    if (response->_keepalive)
    {
        response->_expire = time(0) + _KEEPALIVE_TIMEOUT;
    }
    return response;
}

void HttpHandler::changeEpollWrite(int fd) {
    epoll_event new_event;
    new_event.events = EPOLLOUT | EPOLLET;
    new_event.data.fd = fd;
    epoll_ctl(_epollfd, EPOLL_CTL_MOD, fd, &new_event);
}

void HttpHandler::addEpollRead(int fd) {
    epoll_event new_event;
    new_event.events = EPOLLIN | EPOLLET;
    new_event.data.fd = fd;
    epoll_ctl(_epollfd, EPOLL_CTL_ADD, fd, &new_event);
}

void HttpHandler::changeEpollRead(int fd) {
    epoll_event new_event;
    new_event.events = EPOLLIN | EPOLLET;
    new_event.data.fd = fd;
    epoll_ctl(_epollfd, EPOLL_CTL_MOD, fd, &new_event);
}

void HttpHandler::changeEpollR_W(int fd) {
    epoll_event new_event;
    new_event.events = EPOLLOUT | EPOLLIN | EPOLLET;
    new_event.data.fd = fd;
    epoll_ctl(_epollfd, EPOLL_CTL_MOD, fd, &new_event);
}