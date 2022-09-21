#include"response.h"
#include<iostream>

HTTPResponse::HTTPResponse() {
    _next = nullptr;
}

HTTPResponse::~HTTPResponse() {
    HTTPResponse *tmp = _next;
    if (tmp != nullptr) {
        delete tmp;
    }
}

HttpHeader* HttpHeader::withHeader(string key, string value) {
    _line += key + ": " + value + "\r\n";
    return this;
}

HttpHeader* HttpHeader::withStatus(int code, string msg) {
    _head = "HTTP/1.1 " + to_string(code) + " " + msg + "\r\n";
    return this;
}

char* HttpHeader::build(int* len) {
    string fin = _head + _line + "\r\n";
    _cs = new char[fin.length()];
    for (int i = 0; i < fin.length(); i++)
    {
        _cs[i] = fin[i];
    }
    *len = fin.length();
    return _cs;
}

HttpHeader::HttpHeader() {
    _cs = nullptr;
}

HttpHeader::~HttpHeader() {
    delete[] _cs;
}

int HTTPResponse::parseRange(string val) {
    // 只支持bytes
    if (val.find("bytes", 0) == string::npos) {
        return -1;
    }

    int start = 0;
    string left, right;
    int lr, rr;
    int p1, p2;
    while (start != -1)
    {
        p2 = p1 = -1;
        int split = val.find("-", start);
        if (split == string::npos) {
            break;
        }
        for (p1 = split - 1; p1 >= start; p1--) {
            if (!isdigit(val[p1])) {
                break;
            }
        }
        for (p2 = split + 1; p2 < val.length(); p2++) {
            if (!isdigit(val[p2])) {
                break;
            }
        }
        p1++;
        left = val.substr(p1, split - p1);
        split++;
        right = val.substr(split, p2 - split);
        lr = left.empty() ? 0 : stoi(left);
        rr = right.empty() ? -1 : stoi(right);
        _rangeMap[lr] = rr;
        // 如果p2=-1,结束循环
        start = p2;
    }
    return start == -1 ? -1 : 1;
}