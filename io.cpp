#include"request.h"
#include"response.h"
#include"log.h"
#include"io.h"

#include <unistd.h>

int readHttp(int fd, char* buff, int len, HTTPRequest* request) {
    while (!request->isError())
    {
        int res = read(fd, buff, len);
        if (res < 0)
        {
            if (errno == EINTR)
            {
                // 被中断，应该继续
                continue;
            }
            else if (errno == EWOULDBLOCK || errno == EAGAIN)
            {
                // 没有数据可再读取，此次事件结束
                return READ_CONTINUE;
            }
            else
            {
                // 不知道啥错误，直接关闭连接
                LOG_ERROR("unknow error");
                return READ_FAIL;
            }
        }
        else if (res == 0)
        {
            // 接收到FIN，请求结束了，取消此次读取
            LOG_ERROR("conn close by peer");
            return READ_FAIL;
        }
        else
        {
            request->addContent(buff, res);
        }
    }
    return READ_SUCCESS;
}

int writeHttp(int fd, char* buff, int total, int single, HTTPResponse* response) {
    int remain = total;
    int res = 0;
    while (remain > 0)
    {
        // 剩余内容已经比一次的缓冲写入长度更小
        if (single > remain)
        {
            single = remain;
        }
        buff += res;
        while (true)
        {
            res = write(fd, buff, single);
            if (res < 0)
            {
                if (errno == EINTR)
                {
                    // 被中断
                    continue;
                }
                else if (errno == EWOULDBLOCK || errno == EAGAIN)
                {
                    // 等待下次唤醒再写
                    return WRITE_CONTINUE;
                }
                else
                {
                    // 不明错误, 如EPIPE
                    LOG_ERROR("un know error");
                    return WRITE_FAIL;
                }
            }
            else if (res == 0)
            {
                // 客户端可能收到了content-length指定的数据，主动关闭连接
                LOG_ERROR("peer got all");
                return WRITE_FAIL;
            }
            else
            {
                // 成功发送，记录文件偏移量
                if (response != nullptr) {
                    response->_offset += res;
                }
                remain -= res;
                break;
            }
        }
    }
    return WRITE_SUCCESS;
}