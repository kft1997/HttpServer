#include"request.h"
#include"response.h"

#define WRITE_CONTINUE 0
#define WRITE_FAIL -1
#define WRITE_SUCCESS 1

#define READ_CONTINUE 0
#define READ_FAIL -1
#define READ_SUCCESS 1

int readHttp(int fd, char* buff, int len, HTTPRequest* request);
int writeHttp(int fd, char* buff, int total, int single, HTTPResponse* response);