#include "taskimpl.h"
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/types.h>

/**
 * @brief 启动监听网络
 *
 * @param istcp
 * @param server
 * @param port
 * @return int
 */
int netannounce(int istcp, char *server, int port)
{
    int fd, n, proto;
    struct sockaddr_in sa;
    socklen_t sn;
    uint32_t ip;

    taskstate("netannounce");
    proto = istcp ? SOCK_STREAM : SOCK_DGRAM;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    if (server != nil && strcmp(server, "*") != 0) {
        if (netlookup(server, &ip) < 0) {
            taskstate("netlookup failed");
            return -1;
        }
        memmove(&sa.sin_addr, &ip, 4);
    }

    sa.sin_port = htons(port);
    if ((fd = socket(AF_INET, proto, 0)) < 0) {
        taskstate("socket failed");
        return -1;
    }

    /* set reuse flag for tcp */
    if (istcp && getsockopt(fd, SOL_SOCKET, SO_TYPE, (void *)&n, &sn) >= 0) {
        n = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *)&n, sizeof n);
    }

    if (bind(fd, (struct sockaddr *)&sa, sizeof sa) < 0) {
        taskstate("bind failed");
        close(fd);
        return -1;
    }

    if (proto == SOCK_STREAM) {
        listen(fd, 16);
    }

    fdnoblock(fd);
    taskstate("netannounce succeeded");
    return fd;
}

/**
 * @brief 等待网络 accept
 *
 * @param fd 套接字对应的文件描述符
 * @param server 如不为空, 则打印对端 ip 地址到此参数
 * @param port 如不为空, 对端的端口记录在这里
 * @return int 返回新连接套接字文件描述符
 */
int netaccept(int fd, char *server, int *port)
{
    int cfd, one;
    struct sockaddr_in sa;
    uchar *ip;
    socklen_t len;

    fdwait(fd, 'r');

    taskstate("netaccept");
    len = sizeof sa;
    if ((cfd = accept(fd, (void *)&sa, &len)) < 0) {
        taskstate("accept failed");
        return -1;
    }

    if (server) {
        ip = (uchar *)&sa.sin_addr;
        snprint(server, 16, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    }

    if (port) {
        *port = ntohs(sa.sin_port);
    }

    /* cfd 设置为不阻塞, 并禁用 TCP 的 Nagle 算法 */
    fdnoblock(cfd);
    one = 1;
    setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, (char *)&one, sizeof one);
    taskstate("netaccept succeeded");
    return cfd;
}

/* IP 地址的分类:
 *
 * A类地址
 *   0.  0.  0.  0 = 00000000.00000000.00000000.00000000
 * 127.255.255.255 = 01111111.11111111.11111111.11111111
 *                   0nnnnnnn.HHHHHHHH.HHHHHHHH.HHHHHHHH
 * B类地址
 * 128.  0.  0.  0 = 10000000.00000000.00000000.00000000
 * 191.255.255.255 = 10111111.11111111.11111111.11111111
 *                   10nnnnnn.nnnnnnnn.HHHHHHHH.HHHHHHHH
 *
 * C类地址
 * 192.  0.  0.  0 = 11000000.00000000.00000000.00000000
 * 223.255.255.255 = 11011111.11111111.11111111.11111111
 *                   110nnnnn.nnnnnnnn.nnnnnnnn.HHHHHHHH
 *
 * D类地址
 * 224.  0.  0.  0 = 11100000.00000000.00000000.00000000
 * 239.255.255.255 = 11101111.11111111.11111111.11111111
 *                   1110XXXX.XXXXXXXX.XXXXXXXX.XXXXXXXX
 *
 * E类地址
 * 240.  0.  0.  0 = 11110000.00000000.00000000.00000000
 * 255.255.255.255 = 11111111.11111111.11111111.11111111
 *                   1111XXXX.XXXXXXXX.XXXXXXXX.XXXXXXXX
 *
 * 因此 IP 的分类就可以根据低一个字节的前两位来判断: 00-A 类, 10-B 类, 11-C 类
 */
#define CLASS(p) ((*(unsigned char *)(p)) >> 6)

/**
 * @brief 转换 ip 地址字符表示为相应的 32bits 整型形式
 *
 * 注意这个函数是按照网络字节序(大端序)处理的
 *
 * @param name 字符形式的 ip 地址
 * @param ip 对应的解析结果(大端序)
 * @return int
 */
static int parseip(char *name, uint32_t *ip)
{
    unsigned char addr[4];
    char *p;
    int i, x;

    p = name;
    for (i = 0; i < 4 && *p; i++) {
        x = strtoul(p, &p, 0);
        if (x < 0 || x >= 256)
            return -1;
        if (*p != '.' && *p != 0)
            return -1;
        if (*p == '.')
            p++;
        addr[i] = x;
    }

    /* A 类地址可能被表示为 10.1 或者 10.1.1 这样, 这种情况需要在中间字节里面填充一个或者两个 0
     * B 类地址可以表示为 172.16.1 形式, 这时候需要补充一个 0 */
    switch (CLASS(addr)) {
    case 0: /* A 类地址 */
    case 1: /* A 类地址 */
        if (i == 3) {
            addr[3] = addr[2];
            addr[2] = addr[1];
            addr[1] = 0;
        } else if (i == 2) {
            addr[3] = addr[1];
            addr[2] = 0;
            addr[1] = 0;
        } else if (i != 4)
            return -1;
        break;
    case 2: /* B 类地址 */
        if (i == 3) {
            addr[3] = addr[2];
            addr[2] = 0;
        } else if (i != 4)
            return -1;
        break;
    }

    *ip = *(uint32_t *)addr; /* 大端序 */
    return 0;
}

/**
 * @brief 查询 ip 或者域名的整型地址
 *
 * 如果是 ip 地址, 直接解析乘整型即可, 如果是域名, 走 DNS 查询
 *
 * @param name
 * @param ip
 * @return int
 */
int netlookup(char *name, uint32_t *ip)
{
    struct hostent *he;

    if (parseip(name, ip) >= 0)
        return 0;

    /* BUG - Name resolution blocks.  Need a non-blocking DNS. */
    taskstate("netlookup");
    if ((he = gethostbyname(name)) != 0) {
        *ip = *(uint32_t *)he->h_addr;
        taskstate("netlookup succeeded");
        return 0;
    }

    taskstate("netlookup failed");
    return -1;
}

/**
 * @brief 创建到 server:port 的网络连接
 *
 * @param istcp 传 1 创建 TCP 连接, 0 创建 UDP 连接
 * @param server 服务器地址
 * @param port 服务器端口
 * @return int
 */
int netdial(int istcp, char *server, int port)
{
    int proto, fd, n;
    uint32_t ip;
    struct sockaddr_in sa;
    socklen_t sn;

    /* 解析 server 地址 */
    if (netlookup(server, &ip) < 0)
        return -1;

    taskstate("netdial");

    /* 创建套接字对象 */
    proto = istcp ? SOCK_STREAM : SOCK_DGRAM;
    if ((fd = socket(AF_INET, proto, 0)) < 0) {
        taskstate("socket failed");
        return -1;
    }

    fdnoblock(fd);

    /* for udp */
    if (!istcp) {
        n = 1;
        /* 启用 udp 的广播功能
         * 默认情况下, UDP 不允许向广播地址发送数据包 */
        setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &n, sizeof n);
    }

    /* start connecting */
    memset(&sa, 0, sizeof sa);
    memmove(&sa.sin_addr, &ip, 4);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    if (connect(fd, (struct sockaddr *)&sa, sizeof sa) < 0 && errno != EINPROGRESS) {
        taskstate("connect failed");
        close(fd);
        return -1;
    }

    /* wait for finish */
    fdwait(fd, 'w');
    sn = sizeof sa;
    if (getpeername(fd, (struct sockaddr *)&sa, &sn) >= 0) {
        taskstate("connect succeeded");
        return fd;
    }

    /* report error */
    sn = sizeof n;
    getsockopt(fd, SOL_SOCKET, SO_ERROR, (void *)&n, &sn);
    if (n == 0)
        n = ECONNREFUSED;

    close(fd);
    taskstate("connect failed");
    errno = n;
    return -1;
}
