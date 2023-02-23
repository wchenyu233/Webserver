#include "http_conn.h"

//定义http相应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400 "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently
    impossible to satisfy\n ";
    const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file from this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file does not exist.\n";
const char *error_500_title = "Internal Server Error";
const char *error_500_form = "There was an unusual problem seveing the requested file.\n";
const char *doc_root = "/var/www/html";

int setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}
void addfd(int epollfd, int fd, bool one_shot)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    if (one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}
void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_, fd, 0);
    close(fd);
}
void modfd(int epollfd, int fd, int ev)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

void http_conn::close_conn(bool read_close)
{
    if (read && m_sockfd != -1)
    {
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

void http_conn::init(int sockfd, const sockaddr_in &addr)
{
    m_sockfd = sockfd;
    m_;
    addfd(m_epollfd, sockfd, true);
    m_user_count++;

    init();
}

void http_conn::init()
{
    m_check_state = CHECK_STATE_REQUES
        m_linger = false;

    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_read_buf, '\0', FILE_BUFFER_SIZE);
}

http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;
    for (; m_check_idx < m_read_idx; ++m_check_idx)
    {
        temp = m_read_buf[m_checked_idx];
        if (temp == '\r')
        {
            if ((m_checked_idx + 1) == m_read_idx)
                return LINE_OPEN;
            else if (m_read_buf[m_checked_idx + 1] == '\n')
            {
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if (temp == '\n')
        {
            if ((m_check_idx > 1) && (m_read_buf[m_checked_idx - 1] == '\r'))
            {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
}

bool http_conn::read()
{
    if (m_read_idx >= READ_BUFFER_SIZE)
        return false;
    int bytes_read = 0;
    while (true)
    {
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        if (bytes_read == -1)
        {
            if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
            {
                break;
            }
            else if (bytes_read == 0)
                return false;
        }
        m_read_idx += bytes_read;
    }
    return true;
}

http_conn::HTTP_CODE htttp_conn::parse_request_line(char *text)
{
    m_url = strpbrk(text, "\t");
    if (!m_url)
        return BAD_REQUEST;
    *m_url++ = '\0';

    char *method = text;
    if (strcasecmp(method, "GET") == 0)
    {
        m_method = GET;
    }
    else
        return BAD_REQUEST;
    m_url += strspn(m_url, "\t");
    m_version = strpbrk(m_url, "\t");
    if (strcasecmp(m_version, "HTTP/1.1") != 0)
        return BAD_REQUEST;
    if (strncasecmp(m_url, "http://", 7) == 0)
    {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }
    if (!m_url || m_url[0] != '/')
        return BAD_REQUEST;
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}
//解析HTTP请求的一个头部信息
http_conn::HTTP_CODE http_conn::parse_headers(char *text)
{
    if (text[0] == '\0')
    {
        if (m_content_length != 0)
        {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }
    else if (strncasecmp(text, "Connection:", 11) == 0)
    {
        text += 11;
        text += strspn(text, "\t");
        if (strcasecmp(text, "keep-alive") == 0)
            m_linger = true;
    }
    else if (strncasecmp(text, "Content-Length:", 15) == 0)
    {
        text += 15;
        text += strspn(text,"\t);
        m_content_length = atoi(text);
    }
    else if (strncasecmp(text, "HOST:", 5) == 0)
    {
        text += 5;
        text += strspn(text, "\t");
        m_host = text;
    }
    else
        printf("oop!,unkown header %s\n", text);
    return NO_REQUEST
}
