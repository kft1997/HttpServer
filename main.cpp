#include <csignal>

#include"http_server.h"

using namespace std;

void signalHandler(int signum)
{
    exit(signum);
}

int main()
{
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, signalHandler);

    HttpServer server;
    int res = server.init(80);
    if (res == -1) {
        exit(-1);
    }
    server.run("./file");
}