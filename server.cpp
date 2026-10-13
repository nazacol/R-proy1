#include <sys/socket.h> 
#include <netinet/in.h> 
#include <unistd.h>     
#include <cstring>
#include <iostream>
#include <thread>
#include <mutex>

int DEFAULT_CHAT_PORT = 5000;
int DEFAULT_HTTP_PORT = 8080;
std::string DEFAULT_MAIL = "./buzones";

std::string QUIT = "quit";
std::string NICK = "nick";
std::string JOIN = "join";
std::string PART = "part";
std::string MSG = "msg";
std::string PRIV = "priv";
std::string CHANS = "chans";
std::string WHO = "who";
std::string STAT = "stat";
std::string LIST = "list";
std::string RETR = "retr";
std::string DELE = "dele";

std::string C_QUIT = "299";
std::string C_NEW_MSG = "300";
std::string C_MSG_PRIV_O = "301";
std::string C_WARNING_MOV = "302";
std::string C_MAIL_MSG = "303";
std::string C_SERVER_BUSY = "421";
std::string C_NICK_INVALID = "433";
std::string C_NOT_MEMBER = "442";
std::string C_INVALID_CMD = "500";
std::string C_INVALID_SINTAX = "501";
std::string C_CRLF_EXC = "502";
std::string C_NOT_SINGUP = "530";
std::string C_NICK_NOT_EXISTS = "550";
std::string C_CHAN_NOT_EXISTS = "551";
std::string C_INDEX_NOT_MSG = "552";
char CR = '\r';
char LF = '\n';


struct chat_http_mail
{
    int chat_port;
    int http_port;
    std::string mail;
    int fail;
};

struct reply_status
{
    std::string reply;
    bool status;
};

std::mutex policia;
// AGREGADO: poll serializa el estado; policia se conserva de la base, sin bloquear el bucle.
#include "common.hpp"
#include <poll.h>
#include <signal.h>
#include <map>
#include <set>
#include <deque>
#include <vector>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <ctime>
namespace fs = std::filesystem;

// simplemente toma el input de la llamada del programa,
// verifica si es correcto y devuelve un struct con los dos puertos
// y el directorio para el buzon
chat_http_mail CatchInput(int argc, char* argv[]){
    int c = -1;
    int h = -1;
    int b = -1;

    int chat_port = DEFAULT_CHAT_PORT;
    int http_port = DEFAULT_HTTP_PORT;
    std::string mail = DEFAULT_MAIL;
    int fail = 0;

    chat_http_mail chm;
    

    for(int i = 1; i < argc; i++){
        std::string flag = argv[i];
        // AGREGADO: revisar que exista valor antes de guardar el indice de argv.
        if(i + 1 >= argc){
            std::cerr << "ERROR: falta valor para " << flag << "\n";
            fail = 1;
            break;
        }

        if(flag == "--chat" || flag == "-c"){
            c = i + 1;
            i++;
        }
        else if(flag == "--http" || flag == "-h"){
            h = i + 1;
            i++;
        }
        else if(flag == "--buzones" || flag == "-b"){
            b = i + 1;
            i++;
        }
        else{
            std::cout << "ERROR: modo de uso: ./server \nbanderas:\n-c <puerto> | --chat <puerto>\t\t\t puerto para el chat\n"
            << "-h <puerto> | --http <puerto>\t\t\t puerto para el servicio http\n" 
            << "-b <directorio> | --buzones <directorio>\t directorio donde se coloca el buzon, se crea si no existe\n" << "\n";
            fail = 1;
        }
        
    }

    try{
    if (c > 0)
{
        chat_port = port_number(argv[c]);
    }
    else{
        chat_port = DEFAULT_CHAT_PORT;
    }
    if (h > 0)
{
        http_port = port_number(argv[h]);
    }
    else{
        http_port = DEFAULT_HTTP_PORT;
    }
    if (b > 0)
{
        mail = argv[b];
    }
    else{
        mail = DEFAULT_MAIL;
    }

    if(chat_port == http_port){
        throw std::runtime_error("Los puertos deben ser distintos");
    }
    }
    catch(const std::exception& e){
        std::cerr << "ERROR: " << e.what() << "\n";
        fail = 1;
    }

    chm.chat_port = chat_port;
    chm.http_port = http_port;
    chm.mail = mail;
    chm.fail = fail;

    return chm;
}


int Bind_Serv(int chat_port){

    // socket de escucha
    int fd_socket = socket(AF_INET, SOCK_STREAM, 0);
    if(fd_socket < 0){ 
        // algo salio mal
        std::cerr << "ERROR: no se pudo crear el socket: " << errno << "\n";
        return -1;
    }

    // AGREGADO: facilitar reinicios del servidor usando el mismo puerto.
    int yes = 1;
    setsockopt(fd_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in serv_addr;

    // lo hacemos todo cero pa que este limpio
    // bzero((char*) &serv_addr, sizeof(serv_addr) );
    memset(&serv_addr, 0x00, sizeof(serv_addr));

    // y aqui le metemos todo lo que necesita
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(chat_port); // casteamos al endianosmo correcto, nojoda

    // reclamamos el puerto escogido como el puerto donde escucha el server
    if (bind(fd_socket, (sockaddr*)&serv_addr, sizeof(serv_addr)) < 0){
        
        std::cerr << "no se puedo hacer el bind: " << errno << "\n";
        close(fd_socket);
        return -1;
    }

    nonblocking(fd_socket);  // AGREGADO: accept no debe detener a los demas clientes.
    return fd_socket;
}

// 3. Todo el estado de una conexion; los indices de correo son estables.
struct Client{
    int fd;
    bool http=false,closing=false,discard=false,previous_cr=false;
    std::string nick,in,out;
    size_t sent=0;
    std::vector<fs::path> mail;
    std::set<size_t> deleted;
    std::chrono::steady_clock::time_point activity=std::chrono::steady_clock::now();
};
struct Channel{
    std::set<int> members;
    std::deque<std::string> history;
};
std::map<int,Client> clients;
std::map<std::string,int> online;
std::set<std::string> known;
std::map<std::string,Channel> channels;
fs::path root;
unsigned long serial=0;
volatile sig_atomic_t running=1;
void stop(int){
    running=0;
}
std::string timestamp(bool mail=false){
    time_t t=time(nullptr);
    tm g{
    };
    gmtime_r(&t,&g);
    char b[80];
    strftime(b,sizeof b,mail?"%a, %d %b %Y %H:%M:%S +0000":"%Y-%m-%dT%H:%M:%SZ",&g);
    return b;
}
// 4. Encolar bytes: nunca esperar a que un destinatario lento lea.
void queue(Client& c,const std::string& s){
    if(c.closing)return;
    if(c.out.size()-c.sent+s.size()>1024*1024){
        c.out="421 Cliente lento\r\n";
        c.sent=0;
        c.closing=true;
        return;
    }
    c.out+=s;
}
// Prefijo numerico y CRLF; guion para continuacion, espacio para final.
void reply(Client& c,int code,std::string s,bool more=false){
    std::string prefix=std::to_string(code)+(more?"-":" ");
    // Todas las respuestas respetan 512 bytes; las extensiones de texto largas se fragmentan en UTF-8.
    size_t cap=510-prefix.size();
    while(s.size()>cap){
        size_t n=cap;
        while(n && (static_cast<unsigned char>(s[n])&192)==128)--n;
        queue(c,std::to_string(code)+"-"+s.substr(0,n)+"\r\n");
        s.erase(0,n);
    }
    queue(c,prefix+s+"\r\n");
}
// Solo archivos publicados con la extension requerida; ignorar temporales.
std::vector<fs::path> mailbox(const std::string& nick){
    std::vector<fs::path> v;
    fs::path d=root/nick;
    if(fs::exists(d))for(auto& e:fs::directory_iterator(d))if(e.is_regular_file()&&e.path().extension()==".araguaney")v.push_back(e.path());
    std::sort(v.begin(),v.end());
    return v;
}
// Agregar nuevos mensajes al final sin renumerar los anteriores.
void refresh(Client& c){
    auto v=mailbox(c.nick);
    for(auto& p:v)if(std::find(c.mail.begin(),c.mail.end(),p)==c.mail.end())c.mail.push_back(p);
}
// Difundir mediante colas, excepto al emisor.
void broadcast(const std::string& channel,int except,const std::string& line){
    for(int fd:channels.at(channel).members)if(fd!=except)queue(clients.at(fd),line);
}
// Quitar TODAS las membresias antes de destruir la conexion.
void leave(Client& c,const std::string& event){
    for(auto& [name,ch]:channels)if(ch.members.erase(c.fd))broadcast(name,c.fd,"302 "+name+" "+c.nick+" "+event+"\r\n");
    if(!c.nick.empty())online.erase(c.nick);
}
// 5. Publicar correo offline en disco con cabeceras y CRLF.
void save_mail(const std::string& from,const std::string& to,const std::string& body){
    fs::create_directories(root/to);
    auto id=std::to_string(std::chrono::system_clock::now().time_since_epoch().count())+"."+std::to_string(++serial);
    fs::path p=root/to/(id+".araguaney"),tmp=p.string()+".tmp";
    std::ofstream f(tmp,std::ios::binary);
    f<<"From: "<<from<<"@chat.ci4835\r\nTo: "<<to<<"@chat.ci4835\r\nDate: "<<timestamp(true)<<"\r\nSubject: Mensaje privado de "<<from<<"\r\nMessage-ID: <"<<id<<"@chat.ci4835>\r\n\r\n"<<body<<"\r\n";
    f.close();
    if(!f)throw std::runtime_error("No se pudo escribir buzon");
    fs::rename(tmp,p);
}
bool Valid_MSG(const std::string& buffer){

    bool valid_msg = false;
    // AGREGADO: se valida una linea acumulada completa, no un recv aislado.
    if(buffer.size() >= 2 && buffer.size() <= 512){
        size_t i = buffer.size() - 2;
        // si i es 514 (el ultimo indice) no se cumple
        // CORRECCION: el ultimo indice depende del largo real, no de un arreglo de 515B.
        if(buffer[i] == CR){
            if(buffer[i + 1] == LF){
                // mensaje valido
                valid_msg = true;
            }
            else{
                // mensaje invalido
                valid_msg = false;
            }
        }
        else{
            // mensaje invalido
            valid_msg = false;
        }
    }

    return valid_msg;
}

void Command_Handle(Client& c,const std::string& line){
    if(!utf8(line)){
        reply(c,501,"Texto o UTF-8 invalido");
        return;
    }
    auto split=line.find(' ');
    std::string cmd=line.substr(0,split),args=split==std::string::npos?"":line.substr(split+1);
    for(char& ch:cmd)if(ch>='a'&&ch<='z')ch-=32;
    if(c.nick.empty()&&cmd!="NICK"&&cmd!="QUIT"){
        reply(c,530,"Primero NICK");
        return;
    }
    auto noargs=[&]{
        if(split!=std::string::npos){
            reply(c,501,"No acepta argumentos");
            return false;
        }
        return true;
    };
    if(cmd=="QUIT"){
        if(!noargs())return;
        for(size_t i:c.deleted)fs::remove(c.mail.at(i));
        reply(c,299,"Adios");
        leave(c,"sale");
        c.nick.clear();
        c.closing=true;
        return;
    }
    if(cmd=="NICK"){
        if(!name_ok(args)||!c.nick.empty()){
            reply(c,501,"Apodo invalido o ya registrado en esta conexion");
            return;
        }
        if(online.count(args)){
            reply(c,433,"Apodo en uso");
            return;
        }
        fs::create_directories(root/args);
        c.nick=args;
        known.insert(args);
        online[args]=c.fd;
        c.mail=mailbox(args);
        reply(c,201,"Bienvenido, "+args);
        if(!c.mail.empty())reply(c,303,std::to_string(c.mail.size()));
        return;
    }
    if(cmd=="JOIN"||cmd=="PART"||cmd=="WHO"){
        if(!name_ok(args,true)){
            reply(c,501,"Canal invalido");
            return;
        }
        if(cmd=="JOIN"){
            auto& ch=channels[args];
            bool added=ch.members.insert(c.fd).second;
            reply(c,210,"Unido a "+args);
            if(added)broadcast(args,c.fd,"302 "+args+" "+c.nick+" entra\r\n");
            return;
        }
        if(!channels.count(args)){
            reply(c,551,"Canal inexistente");
            return;
        }
        auto& ch=channels.at(args);
        if(cmd=="PART"){
            if(!ch.members.erase(c.fd)){
                reply(c,442,"No eres miembro");
                return;
            }
            reply(c,211,"Saliste de "+args);
            broadcast(args,c.fd,"302 "+args+" "+c.nick+" sale\r\n");
        }
        else{
            for(int fd:ch.members)reply(c,213,clients.at(fd).nick,true);
            reply(c,213,std::to_string(ch.members.size())+" miembros");
        }
        return;
    }
    if(cmd=="MSG"||cmd=="PRIV"){
        size_t p=args.find(' ');
        std::string target=args.substr(0,p),body=p==std::string::npos?"":args.substr(p+1);
        if(body.empty()||!name_ok(target,cmd=="MSG")){
            reply(c,501,"Destino o texto invalido");
            return;
        }
        if(cmd=="MSG"){
            if(!channels.count(target)||!channels.at(target).members.count(c.fd)){
                reply(c,442,"No eres miembro");
                return;
            }
            std::string ev="300 "+target+" "+c.nick+" "+body+"\r\n";
            if(ev.size()>512){
                reply(c,501,"Texto excede limite del evento (incluye apodo)");
                return;
            }
            broadcast(target,c.fd,ev);
            auto& h=channels.at(target).history;
            h.push_back(timestamp()+" "+c.nick+" "+body);
            if(h.size()>50)h.pop_front();
            reply(c,250,"OK");
        }
        else{
            if(!known.count(target)){
                reply(c,550,"Usuario desconocido");
                return;
            }
            if(online.count(target)){
                std::string ev="301 "+c.nick+" "+body+"\r\n";
                if(ev.size()>512){
                    reply(c,501,"Texto excede limite del evento");
                    return;
                }
                queue(clients.at(online.at(target)),ev);
                reply(c,250,"OK");
            }
            else{
                save_mail(c.nick,target,body);
                reply(c,251,"Mensaje guardado");
            }
        }
        return;
    }
    if(cmd=="CHANS"){
        if(!noargs())return;
        for(auto& [name,ch]:channels)reply(c,212,name+" "+std::to_string(ch.members.size()),true);
        reply(c,212,"fin");
        return;
    }
    if(cmd=="STAT"||cmd=="LIST"){
        if(!noargs())return;
        refresh(c);
        uintmax_t total=0;
        size_t count=0;
        for(size_t i=0;i<c.mail.size();++i)if(!c.deleted.count(i)){
            auto bytes=fs::file_size(c.mail[i]);
            total+=bytes;
            ++count;
            if(cmd=="LIST")reply(c,221,std::to_string(i+1)+" "+std::to_string(bytes),true);
        }
        if(cmd=="STAT")reply(c,220,std::to_string(count)+" "+std::to_string(total));
        else reply(c,221,"fin");
        return;
    }
    if(cmd=="RETR"||cmd=="DELE"){
        refresh(c);
        size_t n=0;
        try{
            if(args.empty()||args.find_first_not_of("0123456789")!=std::string::npos)throw std::runtime_error("numero");
            n=std::stoull(args);
        }
        catch(...){
            reply(c,552,"Numero inexistente");
            return;
        }
        if(n==0||n>c.mail.size()||c.deleted.count(n-1)){
            reply(c,552,"Numero inexistente");
            return;
        }
        if(cmd=="DELE"){
            c.deleted.insert(n-1);
            reply(c,223,"Mensaje marcado");
        }
        else{
            std::ifstream f(c.mail[n-1],std::ios::binary);
            if(!f)throw std::runtime_error("No se pudo leer mensaje");
            std::string data((std::istreambuf_iterator<char>(f)),{
            });
            size_t start=0,p;
            while((p=data.find("\r\n",start))!=std::string::npos){
                reply(c,222,data.substr(start,p-start),true);
                start=p+2;
            }
            reply(c,222,"fin del mensaje");
        }
        return;
    }
    reply(c,500,"Comando desconocido");
}
// 7. Subconjunto HTTP manual: una peticion y cierre tras la respuesta.
void HTTP_Handle(Client& c){
    std::string first=c.in.substr(0,c.in.find("\r\n"));
    std::istringstream ss(first);
    std::string method,path,version,extra;
    ss>>method>>path>>version;
    int status=200;
    std::string body;
    if(method.empty()||path.empty()||path[0]!='/'||version!="HTTP/1.1"||(ss>>extra)||first.find('\t')!=std::string::npos)status=400;
    else if(method!="GET")status=405;
    else if(path=="/usuarios"){
        for(auto& [nick,fd]:online){
            (void)fd;
            body+=nick+"\n";
        }
    }
    else if(path=="/canales"){
        for(auto& [name,ch]:channels)body+=name+" "+std::to_string(ch.members.size())+"\n";
    }
    else if(path.rfind("/historial/",0)==0){
        std::string name="#"+path.substr(11);
        if(!name_ok(name,true)||!channels.count(name))status=404;
        else for(auto& s:channels.at(name).history)body+=s+"\n";
    }
    else status=404;
    std::string reason=status==200?"OK":status==400?"Bad Request":status==404?"Not Found":"Method Not Allowed";
    if(status!=200)body=reason+"\n";
    queue(c,"HTTP/1.1 "+std::to_string(status)+" "+reason+"\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: "+std::to_string(body.size())+"\r\nConnection: close\r\nX-Turpial: 1\r\n"+(status==405?"Allow: GET\r\n":"")+"\r\n"+body);
    c.closing=true;
}
void Manage_Client(Client& c,const char* bytes,size_t n){
    if(c.http){
        c.in.append(bytes,n);
        if(c.in.size()>16384){
            c.in="INVALID\r\n\r\n";
            HTTP_Handle(c);
        }
        else if(c.in.find("\r\n\r\n")!=std::string::npos)HTTP_Handle(c);
        return;
    }
    for(size_t i=0;i<n&&!c.closing;++i){
        char b=bytes[i];
        if(c.discard){
            if(c.previous_cr&&b=='\n'){
                c.discard=false;
                c.previous_cr=false;
            }
            else c.previous_cr=b=='\r';
            continue;
        }
        c.in+=b;
        if(c.in.size()>=2&&c.in.compare(c.in.size()-2,2,"\r\n")==0){
            bool valid_msg = Valid_MSG(c.in);
            // antes de entrar aqui necesariamente debemos haber cumplido que el mensaje tenga CRLF
            if(!valid_msg){
                reply(c, 502, "Linea demasiado larga");
                c.in.clear();
                continue;
            }
            std::string line=c.in.substr(0,c.in.size()-2);
            c.in.clear();
            Command_Handle(c,line);
            // aqui suponemos que ya hice lo que tenia que hacer con el mensaje del cliente
            // AGREGADO: la respuesta queda en c.out y poll la envia por partes sin bloquear.
        }
        else if(c.in.size()>510 && !(c.in.size()==511 && b=='\r')){
            reply(c,502,"Linea demasiado larga");
            c.discard=true;
            c.previous_cr=b=='\r';
            c.in.clear();
        }
    }
}
int main(int argc,char** argv){
    try{
        // ----- capturamos input ---------
        chat_http_mail input = CatchInput(argc, argv);

        int chat_port = input.chat_port;
        int http_port = input.http_port;
        std::string mail = input.mail;
        int fail = input.fail;

        if(fail == 1){
            return 1;
        }
        // ----- capturamos input ---------


        //______________________________________________
        //-----------BINDEO DEL SERVIDOR----------------
        //----------------------------------------------
        int fd_socket = Bind_Serv(chat_port);
        if(fd_socket < 0){
            return 1;
        }
        // AGREGADO: el segundo servicio se ejecuta dentro del mismo proceso.
        int fd_http_socket = Bind_Serv(http_port);
        if(fd_http_socket < 0){
            close(fd_socket);
            return 1;
        }
        //----------------------------------------------
        //-----------BINDEO DEL SERVIDOR----------------
        //______________________________________________

        // el estandar dice que el tamano de la cola debe ser 5, y que si ponemos
        // un socket valido, el codigo no puede fallar, tonces no hace falta verificar
        // CORRECCION: listen puede fallar; backlog 512 admite mas conexiones pendientes.
        if(listen(fd_socket, 512) < 0 || listen(fd_http_socket, 512) < 0){
            close(fd_socket);
            close(fd_http_socket);
            throw std::runtime_error("listen");
        }

        root = mail;
        fs::create_directories(root);
        for(auto& e:fs::directory_iterator(root)){
            if(e.is_directory() && name_ok(e.path().filename().string())){
                known.insert(e.path().filename().string());
            }
        }
        signal(SIGPIPE,SIG_IGN);
        signal(SIGINT,stop);
        signal(SIGTERM,stop);
        std::cout<<"Chat "<<chat_port<<", HTTP "<<http_port<<", buzones "<<root<<std::endl;
        while(running){
            std::vector<pollfd> fds{
{
                    fd_socket,POLLIN,0
                },{
                    fd_http_socket,POLLIN,0
                }
            };
            for(auto& [fd,c]:clients)fds.push_back({
                fd,short((c.closing?0:POLLIN)|(c.sent<c.out.size()?POLLOUT:0)),0
            });
            int ready=poll(fds.data(),fds.size(),100);
            if(ready<0){
                if(errno==EINTR)continue;
                throw std::runtime_error("poll");
            }
            for(size_t i=0;i<2;++i)if(fds[i].revents&POLLIN){
                for(int k=0;k<32;++k){
                    sockaddr_in client_addr{};
                    // el tamaño en bytes del addr del cliente , en el estandar lo definen siempre
                    // como un int, pero no fnciona asi, hay otros sitios que directamente le ponen el tipo de
                    // socklen_t pero el mimo dice que es un unsigned int, y hay sitios que ponen el u_int, tal
                    // parece que son intercambiables
                    // ACLARACION: usar socklen_t es la forma exigida por la API POSIX.
                    socklen_t client_len = sizeof(client_addr);

                    int fd_cli_socket = accept(fds[i].fd, (sockaddr*)&client_addr, &client_len);
                    int fd = fd_cli_socket;
                    if(fd<0)break;
                    nonblocking(fd);
                    if(clients.size()>=1024){
                        const char* busy="421 Servidor saturado\r\n";
                        send(fd,busy,strlen(busy),MSG_NOSIGNAL);
                        close(fd);
                        continue;
                    }
                    Client c{
                    };
                    c.fd=fd;
                    c.http=i==1;
                    clients.emplace(fd,std::move(c));
                    if(i==0)reply(clients.at(fd),200,"ci4835-nodo-quetzal listo");
                }
            }
            std::set<int> dead;
            for(size_t i=2;i<fds.size();++i){
                auto& c=clients.at(fds[i].fd);
                short ev=fds[i].revents;
                if(ev&POLLIN){
                    char morrocoyBuffer[MORROCOY_BUFFER];
                    ssize_t n=recv(c.fd,morrocoyBuffer,sizeof morrocoyBuffer,0);
                    if(n>0){
                        c.activity=std::chrono::steady_clock::now();
                        try{
                            Manage_Client(c,morrocoyBuffer,size_t(n));
                        }
                        catch(const std::exception& e){
                            std::cerr<<e.what()<<'\n';
                            reply(c,451,"Error temporal de almacenamiento");
                        }
                    }
                    // el cliente cerro la coneccion TCP sin hacer el quit
                    // esta nomenclatura cheverisima nojoda
                    else if(n==0||(errno!=EAGAIN&&errno!=EWOULDBLOCK&&errno!=EINTR))dead.insert(c.fd);
                }
                if(ev&POLLOUT && c.sent<c.out.size()){
                    ssize_t n=send(c.fd,c.out.data()+c.sent,c.out.size()-c.sent,MSG_NOSIGNAL);
                    if(n>0){
                        c.sent+=size_t(n);
                        c.activity=std::chrono::steady_clock::now();
                        if(c.sent==c.out.size()){
                            c.out.clear();
                            c.sent=0;
                        }
                        else if(c.sent>65536){
                            c.out.erase(0,c.sent);
                            c.sent=0;
                        }
                    }
                    else if(n<0&&errno!=EAGAIN&&errno!=EWOULDBLOCK&&errno!=EINTR)dead.insert(c.fd);
                }
                if(ev&(POLLERR|POLLNVAL))dead.insert(c.fd);
                if((ev&POLLHUP)&&!(ev&POLLIN))dead.insert(c.fd);
                if(c.closing&&c.out.empty())dead.insert(c.fd);
                if((c.http||c.closing)&&std::chrono::steady_clock::now()-c.activity>std::chrono::seconds(15))dead.insert(c.fd);
            }
            for(int fd:dead){
                leave(clients.at(fd),"desconecta");
                // AGREGADO: equivale al cierre de Manage_Client de la base, sin un hilo por cliente.
                // El mensaje original era: terminando conexion exitosamente con: fd_cli_socket.
                close(fd);
                clients.erase(fd);
            }
        }
        for(auto& [fd,c]:clients){
            (void)c;
            close(fd);
        }
        close(fd_socket);
        close(fd_http_socket);
    }
    catch(const std::exception& e){
        std::cerr<<"ERROR: "<<e.what()<<"\nUso: ./server --chat 5000 --http 8080 --buzones ./buzones\n";
        return 1;
    }
    return 0;
}
