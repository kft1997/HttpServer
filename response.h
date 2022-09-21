#pragma once
#include<string>
#include <map>

using namespace std;

class HTTPResponse
{
public:
    // 已经发送成功的文件偏移量
    int _offset = 0;
    // 文件绝对路径
    string _filePath;
    // header是否被发送
    bool _first = true;
    bool _chunk = false;
    bool _range = false;
    // range范围
    map<int, int> _rangeMap;
    bool _multiRange = false;
    // 标识正在传输某一段chunk
    bool _chunking = false;
    // 某一段chunk的结束位置
    int _chunkEnd = 0;

    bool _keepalive = true;
    time_t _expire;

    HTTPResponse *_next;

    bool dead = false;

    int parseRange(string val);
    HTTPResponse();
};

class HttpHeader
{
    public:
        HttpHeader* withHeader(string key, string value);
        HttpHeader* withStatus(int code, string msg);
        char *build(int *len);

        HttpHeader();
        ~HttpHeader();

    private:
        string _head;
        string _line;
        char *_cs;
};