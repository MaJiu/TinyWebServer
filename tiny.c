/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the 
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh 
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 *
 * Updated 2020/03/07
 *   - 优化子进程的回收, 在 SIG_ERR 信号处理程序中对子进程进行回收
 *    MarkJ
 */
#include "csapp.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, 
         char *shortmsg, char *longmsg);
void reap_handler(int sig);
void do_get(int fd, int is_static, char *filename, char *cgiargs);
void do_head(int fd, int is_static, char *filename, char *cgiargs);


int main(int argc, char **argv) 
{
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
   
    /* Check command line args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }
    if (signal(SIGCHLD, reap_handler) == SIG_ERR)
        unix_error("signal error");
    listenfd = Open_listenfd(argv[1]);
    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); //line:netp:tiny:accept
        Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, 
                    port, MAXLINE, NI_NUMERICHOST | NI_NUMERICSERV);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        doit(connfd);                                             //line:netp:tiny:doit
        Close(connfd);                                            //line:netp:tiny:close
    }
}
/* $end tinymain */

/*
 * doit - handle one HTTP request/response transaction
 */
/* $begin doit */
void doit(int fd) 
{
    int is_static;
    char version[MAXLINE], uri[MAXLINE], method[MAXLINE];
    char buf[MAXLINE];
    rio_t rio;

    /* Read request line and headers */
    Rio_readinitb(&rio, fd);
    if (!Rio_readlineb(&rio, buf, MAXLINE))  //line:netp:doit:readrequest
        return;
    printf("%s", buf);
    sscanf(buf, "%s %s %s", method, uri, version);

    char filename[MAXLINE], cgiargs[MAXLINE];
    is_static = parse_uri(uri, filename, cgiargs);
    if (strcasecmp(method, "GET") == 0)
        do_get(fd, is_static, filename, cgiargs);
    else if(strcasecmp(method, "HEAD") == 0)
        do_head(fd, is_static, filename, cgiargs);
    else {
        clienterror(fd, method, "501", "Not Implemented",
                    "Tiny does not implement this method");
        return;
    }
    read_requesthdrs(&rio);                              //line:netp:doit:readrequesthdrs
    }
/* $end doit */

/*
 * read_requesthdrs - read HTTP request headers
 */
/* $begin read_requesthdrs */
void read_requesthdrs(rio_t *rp) 
{
    char buf[MAXLINE];

    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
    while(strcmp(buf, "\r\n")) {          //line:netp:readhdrs:checkterm
        Rio_readlineb(rp, buf, MAXLINE);
        printf("%s", buf);
    }
    return;
}
/* $end read_requesthdrs */

/*
 * parse_uri - parse URI into filename and CGI args
 *             return 0 if dynamic content, 1 if static
 */
/* $begin parse_uri */
int parse_uri(char *uri, char *filename, char *cgiargs) 
{
    char *ptr;

    if (!strstr(uri, "cgi-bin")) {  /* Static content */ //line:netp:parseuri:isstatic
    strcpy(cgiargs, "");                             //line:netp:parseuri:clearcgi
    strcpy(filename, ".");                           //line:netp:parseuri:beginconvert1
    strcat(filename, uri);                           //line:netp:parseuri:endconvert1
    if (uri[strlen(uri)-1] == '/')                   //line:netp:parseuri:slashcheck
        strcat(filename, "home.html");               //line:netp:parseuri:appenddefault
    return 1;
    }
    else {  /* Dynamic content */                        //line:netp:parseuri:isdynamic
    ptr = index(uri, '?');                           //line:netp:parseuri:beginextract
    if (ptr) {
        strcpy(cgiargs, ptr+1);
        *ptr = '\0';
    }
    else 
        strcpy(cgiargs, "");                         //line:netp:parseuri:endextract
        strcpy(filename, ".");                           //line:netp:parseuri:beginconvert2
        strcat(filename, uri);                           //line:netp:parseuri:endconvert2
        return 0;
    }
}
/* $end parse_uri */

/*
 * serve_static - copy a file back to the client 
 */
/* $begin serve_static */
void serve_static(int fd, char *filename, int filesize)
{
    int srcfd;
    char *srcp, filetype[MAXLINE], buf[MAXBUF];

    /* Send response headers to client */
    get_filetype(filename, filetype);    //line:netp:servestatic:getfiletype
    sprintf(buf, "HTTP/1.0 200 OK\r\n"); //line:netp:servestatic:beginserve
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Server: Tiny Web Server\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n", filesize);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: %s\r\n\r\n", filetype);
    Rio_writen(fd, buf, strlen(buf));    //line:netp:servestatic:endserve

    /* Send response body to client */
    srcfd = Open(filename, O_RDONLY, 0); //line:netp:servestatic:open
    srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0); //line:netp:servestatic:mmap
    Close(srcfd);                       //line:netp:servestatic:close
    Rio_writen(fd, srcp, filesize);     //line:netp:servestatic:write
    Munmap(srcp, filesize);             //line:netp:servestatic:munmap
}

/*
 * get_filetype - derive file type from file name
 */
void get_filetype(char *filename, char *filetype) 
{
    if (strstr(filename, ".html"))
        strcpy(filetype, "text/html");
    else if (strstr(filename, ".gif"))
        strcpy(filetype, "image/gif");
    else if (strstr(filename, ".png"))
        strcpy(filetype, "image/png");
    else if (strstr(filename, ".jpg"))
        strcpy(filetype, "image/jpeg");
    else
        strcpy(filetype, "text/plain");
}  
/* $end serve_static */

/*
 * serve_dynamic - run a CGI program on behalf of the client
 */
/* $begin serve_dynamic */
void serve_dynamic(int fd, char *filename, char *cgiargs) 
{
    char buf[MAXLINE], *emptylist[] = { NULL };

    /* Return first part of HTTP response */
    sprintf(buf, "HTTP/1.0 200 OK\r\n"); 
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Server: Tiny Web Server\r\n");
    Rio_writen(fd, buf, strlen(buf));
  
    if (Fork() == 0) { /* Child */ //line:netp:servedynamic:fork
    /* Real server would set all CGI vars here */
    setenv("QUERY_STRING", cgiargs, 1); //line:netp:servedynamic:setenv
    Dup2(fd, STDOUT_FILENO);         /* Redirect stdout to client */ //line:netp:servedynamic:dup2
    Execve(filename, emptylist, environ); /* Run CGI program */ //line:netp:servedynamic:execve
    }
}
/* $end serve_dynamic */

/*
 * clienterror - returns an error message to the client
 */
/* $begin clienterror */
void clienterror(int fd, char *cause, char *errnum, 
         char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE];

    /* Print the HTTP response headers */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n\r\n");
    Rio_writen(fd, buf, strlen(buf));

    /* Print the HTTP response body */
    sprintf(buf, "<html><title>Tiny Error</title>");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<body bgcolor=""ffffff"">\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "%s: %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<p>%s: %s\r\n", longmsg, cause);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<hr><em>The Tiny Web server</em>\r\n");
    Rio_writen(fd, buf, strlen(buf));
}
/* $end clienterror */

void reap_handler(int sig)
{
    int olderrno = errno; //保存旧的的errno
    while (waitpid(-1, NULL, 0) > 0) {
        Sio_puts("reaped a chiled!\n");
    }
    if (errno != ECHILD)
        sio_error("waitpid error!\n");
    errno = olderrno;
}

void do_get(int fd, int is_static, char *filename, char* cgiargs)
{
    struct stat sbuf;
    /* Parse URI from GET request */
    if (stat(filename, &sbuf) < 0) {                     //line:netp:doit:beginnotfound
        clienterror(fd, filename, "404", "Not found",
            "Tiny couldn't find this file");
        return;
    }                                                    
    if (is_static) { /* Serve static content */          
        if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) { //line:netp:doit:readable
            clienterror(fd, filename, "403", "Forbidden",
                "Tiny couldn't read the file");
            return;
        }
        serve_static(fd, filename, sbuf.st_size);        //line:netp:doit:servestatic
    }
    else { /* Serve dynamic content */
        if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) { //line:netp:doit:executable
            clienterror(fd, filename, "403", "Forbidden",
                "Tiny couldn't run the CGI program");
            return;
        }
        serve_dynamic(fd, filename, cgiargs);            //line:netp:doit:servedynamic
    }
}

void do_head(int fd, int is_static, char *filename, char *cgiargs)
{
    char buf[MAXLINE];
    struct stat sbuf;
    char *server = "Server:Tiny Wbe Server\r\n";
    char *http = "HTTP/1.0";
    if (stat(filename, &sbuf) < 0) {
        Sio_puts(filename);
        Sio_puts("\n");
        sprintf(buf, "%s 404 Not Found\r\n%s\r\n", http, server);
        Sio_puts("----------\n");
    }
    else if (is_static) {
        if ( !(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
             sprintf(buf, "%s 403 Forbidden\r\n%s\r\n", http, server);
        }
        else {
            char filetype[MAXLINE];
            get_filetype(filename, filetype);
            sprintf(buf, "%s 200 OK\r\n"
                     "%sContent-type:%s\r\n"
                     "Content-length:%ld\r\n\r\n", http, server, filetype, sbuf.st_size);
        }
    }
    else if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
        sprintf(buf, "%s 403 Forbidden\r\n%s\r\n", http, server);
    }
    else 
        sprintf(buf, "%s 200 OK\r\n%sContent-type:text/html\r\n\r\n", http, server);
    Rio_writen(fd, buf, strlen(buf));
}

