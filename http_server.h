#include <sys/epoll.h>
// #include "epoll.h"
#include"request.h"
#include"response.h"

class HttpServer {
    public:
        int init(int port);

        void run(string file_prefix);

    private:
        int _epollfd;
        int _listenfd;
        int _timerfd;
        static const int _EVENT_SIZE = 20;
        epoll_event _events[_EVENT_SIZE];
        // 定时任务间隔时间
        static const int _INTERVAL_SEC = 10;
};

class HttpHandler {
    public:
        HttpHandler(string filePath, int fd);

        int handle_listen(epoll_event event, int listenfd);
        int handle_read(epoll_event event);
        int handle_write(epoll_event event);
        int handle_time(epoll_event event);

        void close_conn(int fd);
        int sendHeader(HttpHeader *header, int fd);
        //int responseErr(int code, string msg, int fd);
        HTTPResponse* parseReqToResp(HTTPRequest* request);
        HTTPResponse* parseSingle(HTTPRequest *request);
        void addToResponse(HTTPResponse *response, HTTPRequest *request);

        void decide_rw_len(int remain, int *readLen, int *writeLen);

        int write_range(HTTPResponse *response, int fd, ifstream* file, int fileSize);
        int write_chunk(HTTPResponse *response, int fd, ifstream* file);
        int write_simple(HTTPResponse *response, int fd, ifstream* file, int fileSize);
        int write_header(HTTPResponse *response, int fd, int fileSize);

        void changeEpollWrite(int fd);
        void changeEpollRead(int fd);
        void changeEpollR_W(int fd);
        void addEpollRead(int fd);

    private:
        int _epollfd;
        map<int, HTTPResponse *> _responseMap;
        map<int, HTTPRequest *> _requestMap;
        // 一次往tcp缓冲区写入的最小大小
        static const int _MIN_WRITE_SIZE = 1024;
        // 一次往tcp缓冲区写入的最大大小
        static const int _MAX_WRITE_SIZE = 1024 * 50;
        // 一次从文件读取的数据和写入tcp的数据的倍数关系
        static const int _READ_MULTI_WRITE = 10;
        // 一次从文件读取的最大大小
        static const int _MAX_READ_SIZE = _MAX_WRITE_SIZE * _READ_MULTI_WRITE;
        // 读缓冲
        char _buff[1024];
        // 写缓冲
        char _msg[_MAX_READ_SIZE];
        // 可下载文件的路径
        string _file_path;
        // 开启chunk传输的文件大小阈值
        static const int _CHUNK_SIZE = 1024 * 1024 * 150;
        // static const int _CHUNK_SIZE = 1024;
        // keep-alive超时时间
        static const int _KEEPALIVE_TIMEOUT = 30;
        // 一次epollout事件的最大持续处理时间
        static const int _MAX_WORK_TIME = 100;
};