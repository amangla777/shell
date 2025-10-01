#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstring>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <cstdlib>

// ─── CONFIGURATION ──────────────────────────────────────────────────────────
static constexpr int   DEFAULT_PORT   = 8885;
static constexpr char  SERVER_NAME[]  = "CS252 lab5";
static const std::string ROOT_DIR     = "http-root-dir";
static const std::string HTDOCS       = ROOT_DIR + "/htdocs";
static const std::string ICONS        = HTDOCS + "/icons";
static const std::string LOGFILE      = ROOT_DIR + "/logs";
// Base64("hello:password")
static const std::string AUTH_TOKEN   = "aGVsbG86cGFzc3dvcmQ=";
static constexpr int BACKLOG = 10;

// ─── GLOBAL FD TRACKING ─────────────────────────────────────────────────────
static int listen_fd = -1;
static void close_all_fds() {
    if (listen_fd >= 0) close(listen_fd);
    long maxfd = sysconf(_SC_OPEN_MAX);
    for(int fd = 3; fd < maxfd; fd++) close(fd);
}
static void on_sigint(int) { std::exit(0); }

// ─── GLOBAL STATS ────────────────────────────────────────────────────────────
static const char* STUDENT_NAME = "Aarav Mangla";
static auto server_start       = std::chrono::steady_clock::now();
static std::mutex stats_mtx;
static int request_count       = 0;
static double min_time         = 1e9, max_time = 0;
static std::string fastest, slowest;

// ─── UTILITY TYPES & FUNCTIONS ───────────────────────────────────────────────
struct DirEntry { std::string name; bool is_dir; off_t size; time_t mtime; };

static std::string get_ext(const std::string& s) {
    auto pos = s.rfind('.');
    return (pos==std::string::npos ? "" : s.substr(pos+1));
}

static std::string mime_type(const std::string& p) {
    if(p.size()>5 && p.compare(p.size()-5,5,".html")==0) return "text/html";
    if(p.size()>4 && p.compare(p.size()-4,4,".htm")==0)  return "text/html";
    if(p.size()>4 && p.compare(p.size()-4,4,".txt")==0)  return "text/plain";
    if(p.size()>4 && p.compare(p.size()-4,4,".gif")==0)  return "image/gif";
    if(p.size()>4 && p.compare(p.size()-4,4,".png")==0)  return "image/png";
    if(p.size()>4 && p.compare(p.size()-4,4,".jpg")==0)  return "image/jpeg";
    if(p.size()>5 && p.compare(p.size()-5,5,".jpeg")==0) return "image/jpeg";
    if(p.size()>4 && p.compare(p.size()-4,4,".svg")==0)  return "image/svg+xml";
    return "text/plain";
}

static std::string human_size(off_t sz) {
    const char* suf[] = {"B","K","M","G"};
    double d = sz; int i=0;
    while(d>1024 && i<3){ d/=1024; i++; }
    std::ostringstream o;
    o<<std::fixed<<std::setprecision(i?1:0)<<d<<suf[i];
    return o.str();
}

static std::string format_time(time_t t) {
    std::tm tm = *std::localtime(&t);
    std::ostringstream o;
    o<<std::put_time(&tm,"%Y-%m-%d %H:%M");
    return o.str();
}

static std::string icon_for(const DirEntry& e) {
    if(e.name=="..") return "back.gif";
    if(e.is_dir)     return "folder.gif";
    auto ext = get_ext(e.name);
    if(ext=="gif"||ext=="png"||ext=="jpg"||ext=="jpeg") return "text.gif";
    if(ext=="txt"||ext=="c"||ext=="cpp"||ext=="h")      return "text.gif";
    return "unknown.gif";
}

static void write_all(int fd,const char*buf,size_t n){
    while(n>0){ ssize_t w=write(fd,buf,n); if(w<=0) break; buf+=w; n-=w; }
}

// ─── ERROR RESPONSES ────────────────────────────────────────────────────────
static void send_401(int c){
    std::string h =
      "HTTP/1.1 401 Unauthorized\r\n"
      "Server: "+std::string(SERVER_NAME)+"\r\n"
      "WWW-Authenticate: Basic realm=\"CS252\"\r\n"
      "Content-Length: 0\r\n\r\n";
    write_all(c,h.c_str(),h.size());
}

static void send_404(int c){
    const char* body="404 File Not Found\n";
    std::ostringstream h;
    h<<"HTTP/1.1 404 Not Found\r\n"
     <<"Server: "<<SERVER_NAME<<"\r\n"
     <<"Content-Type: text/plain\r\n"
     <<"Content-Length: "<<strlen(body)<<"\r\n\r\n"
     <<body;
    auto s=h.str();
    write_all(c,s.c_str(),s.size());
}

// ─── SERVE STATIC FILE ─────────────────────────────────────────────────────
static void serve_file(int c,const std::string&path){
    int fd=open(path.c_str(),O_RDONLY);
    if(fd<0){ send_404(c); return; }
    struct stat st; fstat(fd,&st);
    off_t        sz = st.st_size;
    std::string  mt = mime_type(path);
    std::ostringstream h;
    h<<"HTTP/1.1 200 OK\r\n"
     <<"Server: "<<SERVER_NAME<<"\r\n"
     <<"Content-Type: "<<mt<<"\r\n"
     <<"Content-Length: "<<sz<<"\r\n\r\n";
    write_all(c,h.str().c_str(),h.str().size());
    off_t off=0;
    while(off<sz){ if(sendfile(c,fd,&off,sz-off)<=0) break; }
    close(fd);
}

// ─── SERVE DIRECTORY ───────────────────────────────────────────────────────
static void serve_directory(int c,const std::string&url,const std::string&fs,const std::string&qs){
    char C='N',O='A';
    if(!qs.empty()){
        std::istringstream iss(qs);
        for(std::string tok; std::getline(iss,tok,';'); ){
            if(tok.size()>2&&tok[1]=='='){
                if(tok[0]=='C') C=tok[2];
                if(tok[0]=='O') O=tok[2];
            }
        }
    }
    std::vector<DirEntry> v;
    if(DIR*dp=opendir(fs.c_str())){
        struct dirent* e;
        while((e=readdir(dp))){
            std::string n(e->d_name);
            if(n==".") continue;
            std::string p=fs+"/"+n; struct stat st;
            if(stat(p.c_str(),&st)==0)
                v.push_back({n,S_ISDIR(st.st_mode),st.st_size,st.st_mtime});
        }
        closedir(dp);
    }
    auto cmp=[&](auto&a,auto&b){
        bool r=false;
        if(C=='N') r=a.name<b.name;
        if(C=='M') r=a.mtime<b.mtime;
        if(C=='S') r=a.size<b.size;
        return (O=='A')?r:!r;
    };
    std::sort(v.begin(),v.end(),cmp);

    std::string parent="/";
    if(url.size()>1){
        std::string u=url; if(u.back()=='/') u.pop_back();
        auto pos=u.rfind('/');
        if(pos!=std::string::npos) parent=u.substr(0,pos+1);
    }

    std::ostringstream h;
    h<<"HTTP/1.1 200 OK\r\n"
     <<"Server: "<<SERVER_NAME<<"\r\n"
     <<"Content-Type: text/html\r\n\r\n"
     <<"<html><head><title>Index of "<<url<<"</title></head><body>\n"
     <<"<h1>Index of "<<url<<"</h1><hr>\n"
     <<"<table>\n<tr>"
     <<"<th><a href=\""<<url<<"?C=N;O="<<(O=='A'?'D':'A')<<"\">Name</a></th>"
     <<"<th><a href=\""<<url<<"?C=M;O="<<(O=='A'?'D':'A')<<"\">Last modified</a></th>"
     <<"<th><a href=\""<<url<<"?C=S;O="<<(O=='A'?'D':'A')<<"\">Size</a></th>"
     <<"<th>Description</th></tr>\n";

    for(auto&e:v){
        std::string ic="/icons/"+icon_for(e);
        std::string name=(e.name==".."? "Parent Directory": e.name);
        std::string href=(e.name==".."? parent: url+(e.is_dir?e.name+"/":e.name));
        h<<"<tr><td><img src=\""<<ic<<"\" alt=\"[I]\"/> "
         <<"<a href=\""<<href<<"\">"<<name<<"</a></td>"
         <<"<td>"<<format_time(e.mtime)<<"</td>"
         <<"<td>"<<(e.is_dir?"-":human_size(e.size))<<"</td>"
         <<"<td></td></tr>\n";
    }
    h<<"</table><hr></body></html>\n";
    write_all(c,h.str().c_str(),h.str().size());
}

// ─── SERVE CGI-BIN ─────────────────────────────────────────────────────────
static void serve_cgi(int c,const std::string&url,const std::string&qs){
    std::string prog=ROOT_DIR+url;
    if(auto i=prog.find('?');i!=std::string::npos) prog.resize(i);
    struct stat st;
    if(stat(prog.c_str(),&st)<0||!(st.st_mode&S_IXUSR)){ send_404(c); return; }
    if(fork()==0){
        dup2(c,1); dup2(c,2);
        setenv("REQUEST_METHOD","GET",1);
        setenv("QUERY_STRING",qs.c_str(),1);
        std::string h="HTTP/1.1 200 OK\r\nServer:"+std::string(SERVER_NAME)+"\r\n";
        write_all(c,h.c_str(),h.size());
        execl(prog.c_str(),prog.c_str(),(char*)0);
        _exit(1);
    }
    waitpid(-1,NULL,WNOHANG);
}

// ─── SERVE /stats & /logs ───────────────────────────────────────────────────
static void serve_stats(int c){
    using clk=std::chrono::steady_clock;
    auto now=clk::now();
    double up=std::chrono::duration<double>(now-server_start).count();
    std::ostringstream b;
    b<<"Name: "<<STUDENT_NAME<<"\n"
     <<"Uptime: "<<up<<"s\n"
     <<"Requests: "<<request_count<<"\n"
     <<"Min service: "<<min_time<<"s ("<<fastest<<")\n"
     <<"Max service: "<<max_time<<"s ("<<slowest<<")\n";
    auto body=b.str();
    std::ostringstream h;
    h<<"HTTP/1.1 200 OK\r\n"
     <<"Server: "<<SERVER_NAME<<"\r\n"
     <<"Content-Type: text/plain\r\n"
     <<"Content-Length: "<<body.size()<<"\r\n\r\n";
    write_all(c,h.str().c_str(),h.str().size());
    write_all(c,body.c_str(),body.size());
}

static void serve_logs(int c){
    std::ifstream f(LOGFILE);
    std::ostringstream b; b<<f.rdbuf();
    auto body=b.str();
    std::ostringstream h;
    h<<"HTTP/1.1 200 OK\r\n"
     <<"Server: "<<SERVER_NAME<<"\r\n"
     <<"Content-Type: text/plain\r\n"
     <<"Content-Length: "<<body.size()<<"\r\n\r\n";
    write_all(c,h.str().c_str(),h.str().size());
    write_all(c,body.c_str(),body.size());
}

// ─── HANDLE ONE CONNECTION ─────────────────────────────────────────────────
static void handle_connection(int c) {
    using clk = std::chrono::steady_clock;
    auto t0 = clk::now();

    // ─── 1) grab client IP ────────────────────────────────────────────────
    char ipstr[INET6_ADDRSTRLEN] = "unknown";
    {
        struct sockaddr_storage addr;
        socklen_t addrlen = sizeof(addr);
        if (getpeername(c, (struct sockaddr*)&addr, &addrlen) == 0) {
            if (addr.ss_family == AF_INET) {
                auto *s = (struct sockaddr_in*)&addr;
                inet_ntop(AF_INET, &s->sin_addr, ipstr, sizeof(ipstr));
            } else {
                auto *s = (struct sockaddr_in6*)&addr;
                inet_ntop(AF_INET6, &s->sin6_addr, ipstr, sizeof(ipstr));
            }
        }
    }

    // ─── 2) read headers ──────────────────────────────────────────────────
    std::string req;
    char buf[2048];
    while (req.find("\r\n\r\n") == std::string::npos) {
        ssize_t r = read(c, buf, sizeof(buf));
        if (r <= 0) break;
        req.append(buf, r);
    }

    // ─── 3) auth ─────────────────────────────────────────────────────────
    if (req.find("Authorization: Basic " + AUTH_TOKEN) == std::string::npos) {
        send_401(c);
        // log & stats before closing
    }
    else {
        // ─── 4) parse method + URL ───────────────────────────────────
        std::istringstream ls(req);
        std::string method, fullurl, ver;
        ls >> method >> fullurl >> ver;

        if (method == "GET") {
            // split path vs query
            std::string url = fullurl, qs;
            if (auto i = fullurl.find('?'); i != std::string::npos) {
                url = fullurl.substr(0, i);
                qs  = fullurl.substr(i+1);
            }
            if (url == "/") url = "/index.html";

            // ─── 5) special endpoints ───────────────────────────────
            if (url == "/stats") {
                serve_stats(c);
            }
            else if (url == "/logs") {
                serve_logs(c);
            }
            else if (url.rfind("/cgi-bin/", 0) == 0) {
                serve_cgi(c, url, qs);
            }
            else {
                // ─── 6) static file / directory ────────────────────
                std::string fs = (url.rfind("/icons/",0)==0)
                                 ? ICONS + url.substr(6)
                                 : HTDOCS + url;
                struct stat st;
                if (stat(fs.c_str(), &st)==0 && S_ISDIR(st.st_mode)) {
                    // slash-redirect
                    if (url.back() != '/') {
                        std::ostringstream rr;
                        rr << "HTTP/1.1 301 Moved Permanently\r\n"
                           << "Location: " << url << "/\r\n\r\n";
                        write_all(c, rr.str().c_str(), rr.str().size());
                    } else {
                        serve_directory(c, url, fs, qs);
                    }
                }
                else if (stat(fs.c_str(), &st)==0) {
                    serve_file(c, fs);
                }
                else {
                    send_404(c);
                }
            }
        }
        else {
            // method != GET
            close(c);  // no response for other methods
            c = -1;    // mark it closed so we don't double-close below
        }
    }

    // ─── 7) log & stats ───────────────────────────────────────────────
    auto t1 = clk::now();
    double dt = std::chrono::duration<double>(t1 - t0).count();
    {
        std::lock_guard<std::mutex> lk(stats_mtx);
        request_count++;
        if (dt < min_time)  { min_time = dt; fastest = ipstr; }
        if (dt > max_time)  { max_time = dt; slowest = ipstr; }
    }
    // append client IP & URL to log file
    {
        // extract path part for logging
        auto pos = req.find(' ');
        std::string path = pos == std::string::npos ? "/" 
                          : req.substr(pos+1, req.find(' ', pos+1)-pos-1);
        std::ofstream lf(LOGFILE, std::ios::app);
        lf << ipstr << ":" << path << "\n";
    }

    // ─── 8) close socket if still open ────────────────────────────────
    if (c >= 0) close(c);
}

// ─── LISTENER CREATION & MAIN ───────────────────────────────────────────────
static int make_listener(int port){
    signal(SIGPIPE,SIG_IGN);
    signal(SIGINT,on_sigint);
    listen_fd=socket(AF_INET,SOCK_STREAM,0);
    int opt=1; setsockopt(listen_fd,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
    sockaddr_in a{}; a.sin_family=AF_INET; a.sin_addr.s_addr=INADDR_ANY; a.sin_port=htons(port);
    bind(listen_fd,(sockaddr*)&a,sizeof(a));
    listen(listen_fd,BACKLOG);
    return listen_fd;
}

int main(int argc,char**argv){
    atexit(close_all_fds);
    int mode=0, port=DEFAULT_PORT;
    if(argc>=2){
        std::string f=argv[1];
        if(f=="-f") mode=-1;
        else if(f=="-t") mode=1;
        else if(f=="-p") mode=2;
        else port=std::stoi(f);
    }
    if(argc==3) port=std::stoi(argv[2]);
    int ms=make_listener(port);
    std::cout<<"Listening on port "<<port<<"\n";
    if(mode==2){
        std::vector<std::thread> pool;
        for(int i=0;i<5;i++) pool.emplace_back([&](){
            while(true){
                sockaddr_in cli; socklen_t L=sizeof(cli);
                int c=accept(ms,(sockaddr*)&cli,&L);
                if(c<0) continue;
                handle_connection(c);
            }
        });
        pool.front().join();
    } else {
        while(true){
            sockaddr_in cli; socklen_t L=sizeof(cli);
            int c=accept(ms,(sockaddr*)&cli,&L);
            if(c<0) continue;
            if(mode<0){
                if(fork()==0){ handle_connection(c); _exit(0); }
                close(c); waitpid(-1,NULL,WNOHANG);
            }
            else if(mode>0){ std::thread t(handle_connection,c); t.detach(); }
            else { handle_connection(c); }
        }
    }
    return 0;
}
