#pragma once
#include<string>
#include <map>

using namespace std;

#define CR '\r'
#define LF '\n'

enum class HttpRequestDecodeState {
    ERROR,

    START,
    METHOD,

    BEFORE_URI,
    URI,

    BEFORE_PROTOCOL,
    PROTOCOL,

    BEFORE_VERSION,
    VERSION_SPLIT,
    VERSION,

    HEADER_KEY,

    HEADER_BEFORE_COLON,
    HEADER_AFTER_COLON,
    HEADER_VALUE,

    CR_ING,

    CR_LF,

    COMPLETE,
};

struct StringBuffer {
    char *begin = NULL;
    char *end = NULL;

    operator std::string() const {
        return std::string(begin, end);
    }
};

class HTTPRequest
{
public:
    bool isFinish();
    bool isError();
    int parse();
    void do_parse();
    void addContent(char buff[], int len);

    string getHeaderVal(string key);
    string getUrl();
    bool existHeader(string key);

    HTTPRequest();
    HTTPRequest(const HTTPRequest *src);
    ~HTTPRequest();

    bool isPipeline();
    HTTPRequest *getPipelineNext();

private:
    string _method;
    string _url;
    string _version;
    string _protocol;
    map<string, string> _headers;
    HttpRequestDecodeState _decodeState = HttpRequestDecodeState::START;
    int _nextPos = 0;
    char *_buf;
    int _len = 0;
    
    bool _pipeline = false;
    HTTPRequest * _next;
    HTTPRequest *_last;
};