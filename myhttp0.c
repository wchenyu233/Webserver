#include <stdio.h>
#include <string.h>
#include <libgen.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <ctype.h>
#include "epoll_server.h"

const int MAXSIZE = 2048;

void send_error(int cfd, int status, char *title, char *text)
{
    char buf[4096] = {0};

    sprintf(buf, "%s %d %s\r\n", "HTTP/1.1", status, title);
    sprintf(buf + strlen(buf), "Content-Type:%s\r\n", "text/html");
    sprintf(buf + strlen(buf), "Content-Length:%d\r\n", -1);
    sprintf(buf + strlen(buf), "Connection: close\r\n");
    send(cfd, buf, strlen(buf), 0);
    send(cfd, "\r\n", 2, 0);

    memset(buf, 0, sizeof(buf));

    sprintf(buf, "<html><head><title>%d %s</title></head>\n", status, title);
    sprintf(buf + strlen(buf), "<body bgcolor=\"#cc99cc\"><h2 align=\"center\">%d %s</h4>\n", status, title);
    sprintf(buf + strlen(buf), "%s\n", text);
    sprintf(buf + strlen(buf), "<hr>\n</body>\n</html>\n");
    send(cfd, buf, strlen(buf), 0);
    return;
}
int init_listen_fd(int port, int epfd)
{
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd == -1)
    {
        perror("socket error");
        exit(1);
    }
    struct sockaddr_in srv_addr;

    bzero(&srv_addr, sizeof(srv_addr));
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_port = htons(port);
    srv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    int opt = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    int ret = bind(lfd, (struct sockaddr *)&srv_addr, sizeof(srv_addr));
    if (ret == -1)
    {
        perror("bind error");
        exit(1);
    }
    ret = listen(lfd, 128);
    if (ret == -1)
    {
        perror("listen error");
        exit(1);
    }

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = lfd;

    ret = epoll_ctl(epfd, EPOLL_CTL_ADD, lfd, &ev);
    if (ret == -1)
    {
        perror("epoll_ctl add lfd error");
        exit(1);
    }

    return lfd;
}
void do_accept(int lfd, int epfd)
{
    struct sockaddr_in clt_addr;
    socklen_t clt_addr_len = sizeof(clt_addr);

    int cfd = accept(lfd, (struct sockaddr *)&clt_addr, &clt_addr_len);
    if (cfd == -1)
    {
        perror("accept error");
        exit(1);
    }

    char client_ip[64] = {0};
    printf("New Client IP : %s,Port %d,cfd = %d\n",
           inet_ntop(AF_INET, &clt_addr.sin_addr, client_ip, sizeof(client_ip)),
           ntohs(clt_addr.sin_port), cfd);

    int flag = fcntl(cfd, F_GETFL);
    flag |= O_NONBLOCK; // disable
    fcntl(cfd, F_SETFL, flag);

    struct epoll_event ev;
    ev.data.fd = cfd;

    ev.events = EPOLLIN | EPOLLET;

    int ret = epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &ev);
    if (ret == -1)
    {
        perror("epoll_ctl error");
        exit(1);
    }
}
int get_line(int cfd, char buf[], int size)
{
    int i = 0;
    char c = '\0';
    int n;
    while ((i < size - 1) && (c != '\n'))
    {
        n = recv(cfd, &c, 1, 0);
        if (n > 0)
        {
            if (c == '\r')
            {
                n = recv(cfd, &c, 1, MSG_PEEK);
                if (n > 0 && c == '\n')
                {
                    recv(cfd, &c, 1, 0);
                }
                else
                {
                    c = '\n';
                }
            }
            buf[i] = c;
            i++;
        }
        else
            c = '\n';
    }
    buf[i] = '\0';
    if (n == -1)
        i = n;
    return i;
}
void disconnect(int cfd, int epfd)
{
    int ret = epoll_ctl(epfd, EPOLL_CTL_DEL, cfd, NULL);
    if (ret != 0)
    {
        perror("epoll_ctl error");
        exit(1);
    }
    close(cfd);
}
void send_respond(int cfd, int no, const char *disp, const char *type, int len)
{
    char buf[1024] = {0};
    sprintf(buf, "HTTP/1.1 %d %s\r\n", no, disp);
    sprintf(buf + strlen(buf), "%s\r\n", type);
    sprintf(buf + strlen(buf), "Content-Length:%d\r\n", len);
    send(cfd, buf, strlen(buf), 0);
    send(cfd, "\r\n", 2, 0);
}
//发送文件给浏览器
void send_file(int cfd, const char *file)
{
    int n = 0;
    char buf[4096];
    int fd = open(file, O_RDONLY);
    if (fd == -1)
    {
        perror("open error");
        exit(1);
    }
    while ((n = read(fd, buf, sizeof(buf))) > 0)
    {
        int ret = send(cfd, buf, n, 0);
        if (ret == -1)
        {
            printf("error = %d\n", errno);
            if (errno == EAGAIN)
            {
                printf("---------------EAGAIN\n");
                continue;
            }
            else if (errno == EINTR)
            {
                printf("---------------EINTR\n");
                continue;
            }
            else
            {
                perror("send error");
                exit(1);
            }
        }
        if (ret < 4096)
            printf("-------send ret: %d\n", ret);
        usleep(100);
    }
    close(fd);
}
void http_request(int cfd, const char *file)
{
    struct stat sbuf;
    int ret;
    ret = stat(file, &sbuf);
    if (ret != 0) //判断文件是否存在
    {
        //回发浏览器 404 错误页面
        send_error(cfd, 404, "Not Found", "NO such file or direntry");
        return;
    }
    if (S_ISREG(sbuf.st_mode)) //是一个普通文件
    {
        //回发http协议应答
        send_respond(cfd, 200, "OK", get_file_type(file), sbuf.st_size);
        //回发给客户端请求数据内容
        send_file(cfd, file);
    }
    else if (S_ISDIR(sbuf.st_mode))
    {
        send_respond(cfd, 200, "OK", get_file_type(file), -1);
        send_dir(cfd, file);
    }
}
void send_dir(int cfd, const char *dirname)
{
    int i, ret;

    // 拼一个html页面<table></table>
    char buf[4094] = {0};

    sprintf(buf, "<html><head><title>Directory name: %s</title></head>", dirname);
    sprintf(buf+strlen(buf), "<body><h1>Current directory: %s</h1><table>", dirname);

    char enstr[1024] = {0};
    char path[1024] = {0};
    
    // 目录项二级指针
    struct dirent** ptr;
    int num = scandir(dirname, &ptr, NULL, alphasort);
    
    // 遍历
    for(i = 0; i < num; ++i) {
        char *name = ptr[i]->d_name;

        // 拼接文件的完整路径
        sprintf(path, "%s/%s", dirname, name);
        printf("path = %s ===================\n", path);
        struct stat st;
        stat(path, &st);

        // 编码生成 %E5 %A7 之类的东西
        encode_str(enstr, sizeof(enstr), name);
        printf("enstr: %s\n", enstr);
        // 如果是文件
        if (S_ISREG(st.st_mode))
        {
            sprintf(buf + strlen(buf),
                    "<tr><td><a href=\"%s\">%s</a></td><td>%ld</td></tr>",
                    enstr, name, (long)st.st_size);
        }
        else if (S_ISDIR(st.st_mode))
        { // 如果是目录
            sprintf(buf + strlen(buf),
                    "<tr><td><a href=\"%s/\">%s/</a></td><td>%ld</td></tr>",
                    enstr, name, (long)st.st_size);
        }
        ret = send(cfd, buf, strlen(buf), 0);
        if (ret == -1)
        {
            if (errno == EAGAIN)
            {
                perror("send error:");
                continue;
            }
            else if (errno == EINTR)
            {
                perror("send error:");
                continue;
            }
            else
            {
                perror("send error:");
                exit(1);
            }
        }
        memset(buf, 0, sizeof(buf));
        // 字符串拼接
    }

    sprintf(buf+strlen(buf), "</table></body></html>");
    send(cfd, buf, strlen(buf), 0);

    printf("dir message send OK!!!!\n");
}
void do_read(int cfd, int epfd)
{
    char line[1024];
    int len = get_line(cfd, line, sizeof(line));
    if (len == 0)
    {
        printf("服务器检测到客户端关闭....");
        disconnect(cfd, epfd);
    }
    else
    {
        // strtok();//拆分字符串
        char method[16], path[256], protocol[16];
        sscanf(line, "%[^ ] %[^ ] %[^ ]", method, path, protocol);
        printf("method=%s,path=%s, protocol=%s", method, path, protocol);
        while (1)
        {
            char buf[1024] = {0};
            len = get_line(cfd, buf, sizeof(buf));
            if (buf[0] == '\n' || len == -1)
                break;
            // printf("------%s",buf);
        }
        if (strncasecmp(method, "GET", 3) == 0) //忽略大小写
        {
            decode_str(path, path);
            char *file = path + 1; //取出客户端要访问的文件名
            if (strcmp(path, "/") == 0)
                file = "./";
            http_request(cfd, file);

            disconnect(cfd, epfd);
        }
    }
}
void epoll_run(int port)
{
    int i = 0;
    struct epoll_event all_events[MAXSIZE];
    int epfd = epoll_create(MAXSIZE);
    if (epfd == -1)
    {
        perror("epoll_create error");
        exit(1);
    }

    int lfd = init_listen_fd(port, epfd);

    while (1)
    {
        int ret = epoll_wait(epfd, all_events, MAXSIZE, -1);
        if (ret == -1)
        {
            perror("epoll_waite error");
            exit(1);
        }
        for (i = 0; i < ret; i++)
        {
            struct epoll_event *pev = &all_events[i];
            if (!pev->events & EPOLLIN)
                continue;
            if (pev->data.fd == lfd)
                do_accept(pev->data.fd, epfd);
            else
                do_read(pev->data.fd, epfd);
        }
    }
}
const char *get_file_type(const char *name)
{
    char *dot;
    //自右向左查找"."字符，如不存在返回NULL
    dot = strrchr(name, '.');
    if (dot == NULL)
        return "text/plain;charset=utf-8";
    if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0)
        return "text/html; charset=utf-8";
    if (strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0)
        return "image/jpeg";
    if (strcmp(dot, ".gif") == 0)
        return "image/gif";
    if (strcmp(dot, ".png") == 0)
        return "image/png";
    if (strcmp(dot, ".css") == 0)
        return "text/css";
    if (strcmp(dot, ".au") == 0)
        return "audio/basic";
    if (strcmp(dot, ".wav") == 0)
        return "audio/wav";
    if (strcmp(dot, ".avi") == 0)
        return "video/x-msvideo";
    if (strcmp(dot, ".mov") == 0 || strcmp(dot, ".qt") == 0)
        return "video/quicktime";
    if (strcmp(dot, ".mpeg") == 0 || strcmp(dot, ".mpe") == 0)
        return "video/mpeg";
    if (strcmp(dot, ".vrml") == 0 || strcmp(dot, ".wrl") == 0)
        return "model/vrml";
    if (strcmp(dot, ".midi") == 0 || strcmp(dot, ".mid") == 0)
        return "audio/midi";
    if (strcmp(dot, ".mp3") == 0)
        return "audio/mpeg";
    if (strcmp(dot, ".ogg") == 0)
        return "application/ogg";
    if (strcmp(dot, ".pac") == 0)
        return "application/x-ns-proxy-autoconfig";

    return "text/plain; charset=utf-8";
}
// 16进制数转化为10进制
int hexit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;

    return 0;
}

/*
 *  这里的内容是处理%20之类的东西！是"解码"过程。
 *  %20 URL编码中的‘ ’(space)
 *  %21 '!' %22 '"' %23 '#' %24 '$'
 *  %25 '%' %26 '&' %27 ''' %28 '('......
 *  相关知识html中的‘ ’(space)是&nbsp
 */
void encode_str(char *to, int tosize, const char *from)
{
    int tolen;

    for (tolen = 0; *from != '\0' && tolen + 4 < tosize; ++from)
    {
        if (isalnum(*from) || strchr("/_.-~", *from) != (char *)0)
        {
            *to = *from;
            ++to;
            ++tolen;
        }
        else
        {
            sprintf(to, "%%%02x", (int)*from & 0xff);
            to += 3;
            tolen += 3;
        }
    }
    *to = '\0';
}

void decode_str(char *to, char *from)
{
    for (; *from != '\0'; ++to, ++from)
    {
        if (from[0] == '%' && isxdigit(from[1]) && isxdigit(from[2]))
        {
            *to = hexit(from[1]) * 16 + hexit(from[2]);
            from += 2;
        }
        else
        {
            *to = *from;
        }
    }
    *to = '\0';
}
int main(int argc, char **argv)
{
    if (argc < 3)
        printf("Usage: %s port path\n", basename(argv[0]));
    int port = atoi(argv[1]);
    int ret = chdir(argv[2]); //切换工作目录为给定目录
    if (ret != 0)
    {
        perror("chdir error");
        exit(1);
    }

    epoll_run(port);
    return 0;
}