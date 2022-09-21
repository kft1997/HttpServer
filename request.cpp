#include "request.h"

HTTPRequest::HTTPRequest()
{
    _next = nullptr;
    _last = nullptr;
}

HTTPRequest::HTTPRequest(const HTTPRequest *src)
{
    _method = src->_method;
    _url = src->_url;
    _version = src->_version;
    _protocol = src->_protocol;
    _headers = src->_headers;
    _decodeState = src->_decodeState;
    _next = nullptr;
    _last = nullptr;
}

HTTPRequest::~HTTPRequest()
{
    HTTPRequest *tmp = _next;
    if (tmp != nullptr)
    {
        delete tmp;
    }
}

void HTTPRequest::addContent(char buff[], int len)
{
    // 忽略已经读取的
    int newLen = _len + len;
    char newbuf[newLen];
    // 复制已经读取过的数据到新的数组，而不是丢弃（为了实现方便）
    for (int i = 0; i < _len; i++)
    {
        newbuf[i] = _buf[i];
    }
    for (int i = 0; i < len; i++)
    {
        newbuf[_len + i] = buff[i];
    }
    _buf = newbuf;
    _len = newLen;
    parse();
}

int HTTPRequest::parse()
{
    do_parse();
    while (_nextPos < _len)
    {
        if (_decodeState == HttpRequestDecodeState::ERROR)
        {
            return -1;
        }

        if (_decodeState == HttpRequestDecodeState::COMPLETE)
        {
            if (_nextPos < _len)
            {
                if (existHeader("Connection") && getHeaderVal("Connection") == "close")
                {
                    _decodeState = HttpRequestDecodeState::ERROR;
                    return -1;
                }
                else
                {
                    _pipeline = true;
                    if (_last == nullptr)
                    {
                        _last = this;
                    }
                    // 把当前对象复制一份放到队列中，重新再解析
                    _last->_next = new HTTPRequest(this);
                    _last = _last->_next;
                    _decodeState = HttpRequestDecodeState::START;
                    do_parse();
                }
            }
            else
            {
                break;
            }
        }
    }
    if (_pipeline)
    {
        _last->_next = new HTTPRequest(this);
        _last = _last->_next;
    }
}

bool HTTPRequest::isPipeline()
{
    return _pipeline;
}

HTTPRequest *HTTPRequest::getPipelineNext()
{
    return _next;
}

// 不检查错误，只判断是否完成。
// 调用此方法前必须调用isError是否有错
bool HTTPRequest::isFinish()
{
    if (_pipeline)
    {
        return _nextPos >= _len;
    }
    // 不是pipeline请求
    return _decodeState == HttpRequestDecodeState::COMPLETE;
}

bool HTTPRequest::isError()
{
    return _decodeState == HttpRequestDecodeState::ERROR;
}

string HTTPRequest::getHeaderVal(string key)
{
    return _headers[key];
}

bool HTTPRequest::existHeader(string key)
{
    return _headers.count(key) != 0;
}

void HTTPRequest::do_parse()
{
    StringBuffer method;
    StringBuffer url;

    StringBuffer protocol;
    StringBuffer version;

    StringBuffer headerKey;
    StringBuffer headerValue;

    int size = _len;
    // 从上一次读取结束的位置开始读取
    char *p0 = _buf + _nextPos;

    while (_decodeState != HttpRequestDecodeState::ERROR && _decodeState != HttpRequestDecodeState::COMPLETE && _nextPos < size)
    {
        // 当前字符
        char ch = *p0;
        // 当前指针
        char *p = p0++;
        _nextPos++;

        switch (_decodeState)
        {
        // 开始读取
        case HttpRequestDecodeState::START:
        {
            if (ch == CR || ch == LF || isblank(ch))
            {
            }
            else if (isupper(ch))
            {
                method.begin = p;
                _decodeState = HttpRequestDecodeState::METHOD;
            }
            else
            {
                _decodeState = HttpRequestDecodeState::ERROR;
            }
            break;
        }
        // 读取方法
        case HttpRequestDecodeState::METHOD:
        {
            if (isupper(ch))
            {
            }
            else if (isblank(ch))
            {
                method.end = p;
                _method = method;
                _decodeState = HttpRequestDecodeState::BEFORE_URI;
            }
            else
            {
                _decodeState = HttpRequestDecodeState::ERROR;
            }
            break;
        }
        // URI读取前
        case HttpRequestDecodeState::BEFORE_URI:
        {
            if (ch == '/')
            {
                url.begin = p;
                _decodeState = HttpRequestDecodeState::URI;
            }
            else if (isblank(ch))
            {
            }
            else
            {
                _decodeState = HttpRequestDecodeState::ERROR;
            }
            break;
        }
        // URI读取
        case HttpRequestDecodeState::URI:
        {
            if (isblank(ch))
            {
                url.end = p;
                _url = url;
                _decodeState = HttpRequestDecodeState::BEFORE_PROTOCOL;
            }
            else
            {
            }
            break;
        }
        // 协议读取前
        case HttpRequestDecodeState::BEFORE_PROTOCOL:
        {
            if (isblank(ch))
            {
            }
            else if (isalpha(ch))
            {
                protocol.begin = p;
                _decodeState = HttpRequestDecodeState::PROTOCOL;
            } else {
                _decodeState = HttpRequestDecodeState::ERROR;
            }
            break;
        }
        // 协议读取
        case HttpRequestDecodeState::PROTOCOL:
        {
            if (ch == '/')
            {
                protocol.end = p;
                _protocol = protocol;
                _decodeState = HttpRequestDecodeState::BEFORE_VERSION;
            }
            else if (!isalpha(ch))
            {
                _decodeState = HttpRequestDecodeState::ERROR;
            }
            break;
        }
        // 版本号读取前
        case HttpRequestDecodeState::BEFORE_VERSION:
        {
            if (isdigit(ch))
            {
                version.begin = p;
                _decodeState = HttpRequestDecodeState::VERSION;
            }
            else
            {
                _decodeState = HttpRequestDecodeState::ERROR;
            }
            break;
        }
        // 版本号读取
        case HttpRequestDecodeState::VERSION:
        {
            if (ch == CR)
            {
                version.end = p;
                _version = version;
                _decodeState = HttpRequestDecodeState::CR_ING;
            }
            else if (ch == '.')
            {
                // 可能有多个小数点，不做校验
                _decodeState = HttpRequestDecodeState::VERSION_SPLIT;
            }
            else if (isdigit(ch))
            {
            }
            else
            {
                _decodeState = HttpRequestDecodeState::ERROR;
            }
            break;
        }
        // 版本号中的小数点
        case HttpRequestDecodeState::VERSION_SPLIT:
        {
            if (isdigit(ch))
            {
                _decodeState = HttpRequestDecodeState::VERSION;
            }
            else
            {
                _decodeState = HttpRequestDecodeState::ERROR;
            }
            break;
        }
        // Header key读取
        case HttpRequestDecodeState::HEADER_KEY:
        {
            if (isblank(ch))
            {
                headerKey.end = p;
                _decodeState = HttpRequestDecodeState::HEADER_BEFORE_COLON;
            }
            else if (ch == ':')
            {
                headerKey.end = p;
                _decodeState = HttpRequestDecodeState::HEADER_AFTER_COLON;
            }
            else
            {
            }
            break;
        }
        // 冒号前的空格读取
        case HttpRequestDecodeState::HEADER_BEFORE_COLON:
        {
            if (isblank(ch))
            {
            }
            else if (ch == ':')
            {
                _decodeState = HttpRequestDecodeState::HEADER_AFTER_COLON;
            }
            else
            {
                _decodeState = HttpRequestDecodeState::ERROR;
            }
            break;
        }
        // 冒号后的空格读取
        case HttpRequestDecodeState::HEADER_AFTER_COLON:
        {
            if (isblank(ch))
            {
            }
            else
            {
                headerValue.begin = p;
                _decodeState = HttpRequestDecodeState::HEADER_VALUE;
            }
            break;
        }
        // Header value 读取
        case HttpRequestDecodeState::HEADER_VALUE:
        {
            if (ch == CR)
            {
                headerValue.end = p;
                _headers.insert({headerKey, headerValue});
                _decodeState = HttpRequestDecodeState::CR_ING;
            }
            break;
        }
        // 读取到'\r'
        case HttpRequestDecodeState::CR_ING:
        {
            if (ch == LF)
            {
                _decodeState = HttpRequestDecodeState::CR_LF;
            }
            else
            {
                _decodeState = HttpRequestDecodeState::ERROR;
            }
            break;
        }
        // 读取到'\n'
        case HttpRequestDecodeState::CR_LF:
        {
            if (ch == CR)
            {
                // 只支持Get方法，不用读取body，所以直接结束
                _decodeState = HttpRequestDecodeState::COMPLETE;
                // 完成解析，读取位置应该加1
                _nextPos++;
            }
            else if (!isalpha(ch))
            {
                _decodeState = HttpRequestDecodeState::ERROR;
            }
            else
            {
                // 下一个Header读取开始
                headerKey.begin = p;
                _decodeState = HttpRequestDecodeState::HEADER_KEY;
            }
            break;
        }
        default:
            break;
        }
    }
}

string HTTPRequest::getUrl()
{
    return _url;
}