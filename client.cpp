#include <sys/socket.h> 
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>     
#include <cstring>      // Para memset
#include <iostream>

#include <arpa/inet.h>

int DEFAULT_SERVER_PORT = 5000;
const char* DEFAULT_SERVER_IP = "localhost";


const int QUIT = 299;
const int NEW_MSG = 300;
const int MSG_PRIV_O = 301;
const int WARNING_MOV = 302;
const int MAIL_MSG = 303;
const int SERVER_BUSY = 421;
const int NICK_INVALID = 433;
const int NOT_MEMBER = 442;
const int INVALID_CMD = 500;
const int INVALID_SINTAX = 501;
const int LINE_OVERFLOW = 502;
const int NOT_SINGUP = 530;
const int NICK_NOT_EXISTS = 550;
const int CHAN_NOT_EXISTS = 551;
const int INDEX_NOT_MSG = 552;

char CR = '\r';
char LF = '\n';

struct ip_port
{
    int server_port;
    const char* server_ip;
    int fail;
};

#include "common.hpp"  // AGREGADO: limites y validadores compartidos
#include <poll.h>
#include <cerrno>

// simplemente toma el input de la llamada del programa,
// verifica si es correcto y devuelve un struct con el ip y el puerto
// especificados para el server
ip_port CatchInput(int argc, char* argv[]){

    int server_port = DEFAULT_SERVER_PORT;
    const char* server_ip = DEFAULT_SERVER_IP;
    int fail = 0;

    ip_port result;

    // para elegir el puerto podemos hacer que los argumentos sean <ip> <puerto> pero opcional
    if(argc == 1){
        //iniciamos el cliente por defecto en localhost en el puerto 5000
        server_port = DEFAULT_SERVER_PORT;
        server_ip = DEFAULT_SERVER_IP;
    }
    else if(argc == 3){
        // este si se especifica otro ip puerto
        
        // CORRECCION: atoi no detecta letras ni puertos fuera de rango.
        try{
            server_port = port_number(argv[2]);
        }
        catch(const std::exception& e){
            std::cerr << "ERROR: " << e.what() << "\n";
            fail = 1;
        }
        server_ip = argv[1];
    }
    else{
        // y por si a caso se pone algo mas
        std::cout << "ERROR: Modo de uso: ./client [<ip> <port>] \n--- lo que esta entre [] es opcional" << "\n";
        fail = 1;
    }

    result.server_port = server_port;
    result.server_ip = server_ip;
    result.fail = fail;

    return result;
}


int Connect_C2S(const char* ip, int port){

    // AF_INET es el protocolo IPv4
    // SOCK_STREAM es que vamos a usar TCP para comunicarnos
    // y el 0 no se que es pero segun es que lo que sea que se 
    // deba escojer se hace automatico, y es el estandar tonces asi se quedo
    // ACLARACION: protocolo 0 pide el predeterminado para el tipo elegido (TCP).
    // AGREGADO: getaddrinfo permite probar IPv4 o IPv6 sin cambiar el protocolo.
    int fd_socket = -1;

    addrinfo* server = nullptr;
    addrinfo serv_addr{};
    // bzero((char *) &serv_addr, sizeof(serv_addr));
    // CORRECCION: {} ya inicializa toda la estructura a cero.

    // esta parte es lo mismo que en server
    serv_addr.ai_family = AF_UNSPEC;
    serv_addr.ai_socktype = SOCK_STREAM;

    // pero aqui viene lo del host:
    // queremos poder usar nombres ed host y no solo una ip cruda
    // tonces pormos usar este struct "hostent" para guarada alli la ip de un host
    // y esa ip la sacamos de usa esta funcion gethostname
    // CORRECCION: gethostname devuelve el nombre local; getaddrinfo resuelve el destino.
    int result = getaddrinfo(ip, std::to_string(port).c_str(), &serv_addr, &server);
    if(result != 0){
        std::cerr << "ERROR: no se pudo obtener el host: " << gai_strerror(result) << "\n";
        return -1;
    }


    // esta funcion bcopy copia como strcpy, creo :P
    // CORRECCION: bcopy copia bytes, no cadenas; aqui usamos directamente ai_addr.

    // a partir de aqui ya tenemos el serv_addr listo pa usarse
    for(addrinfo* address = server; address != nullptr; address = address->ai_next){
        fd_socket = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if(fd_socket < 0){
            // algo salio mal
            continue;
        }

        if(connect(fd_socket, address->ai_addr, address->ai_addrlen) == 0){
            break;
        }
        close(fd_socket);
        fd_socket = -1;
    }
    freeaddrinfo(server);

    if(fd_socket < 0){
        std::cerr << "ERROR: fallo al conectar: " << strerror(errno) << "\n";
        return -1;
    }
    try{
        nonblocking(fd_socket);
    }
    catch(const std::exception& e){
        std::cerr << "ERROR: " << e.what() << "\n";
        close(fd_socket);
        return -1;
    }

    return fd_socket;
}

int main(int argc, char* argv[]){

    // ----- capturamos input ---------
    int server_port;
    const char* server_ip;
    int fail;

    ip_port input = CatchInput(argc, argv);

    server_port = input.server_port;
    server_ip = input.server_ip;
    fail = input.fail;

    if(fail == 1){
        return 1;
    }
    // ----- capturamos input ---------
    
    
    //______________________________________________
    //-----------CONEXION CLIENTE SERVIDOR----------
    //----------------------------------------------
    int fd_socket = Connect_C2S(server_ip, server_port);
    if(fd_socket < 0){
        return 1;
    }
    //----------------------------------------------
    //-----------CONEXION CLIENTE SERVIDOR----------
    //______________________________________________

    // 512B de tamaño total de la line + 2B CRLF + 1B para null termination porsia las moscas
    // CORRECCION: el maximo son 512B INCLUYENDO CRLF; quedan 510B de contenido.
    // AGREGADO: el buffer de lectura no limita una linea; MORROCOY_BUFFER lee bloques.
    try{
        bool keyboard=true,too_long=false;
        std::string g,in,out;
        size_t sent=0;
        int op = 0;
        while(op != QUIT){
            pollfd fds[2]{
{
                    fd_socket,short(POLLIN|(sent<out.size()?POLLOUT:0)),0
                },{
                    keyboard?STDIN_FILENO:-1,POLLIN,0
                }
            };
            if(poll(fds,2,-1)<0){
                if(errno==EINTR)continue;
                throw std::runtime_error("poll");
            }
            // si se usa .at() para recorrer el string no se toma en cuenta el null termination
            // osea, da un error, pero si se usa [] si se toma en cuenta, es decir, si puedo mostrar ese 0x00
            // lo otro es que si aqui ocurre un error, se cierra la conexion repentinamente con el server
            // y, gracias a dios, el server ya estaba preparado para eso
            // ACLARACION: at verifica limites; no dependemos de ningun byte fuera del contenido.
            // AGREGADO: poll y read reemplazan getline para no frenar los eventos del socket.
            if(fds[1].revents&(POLLIN|POLLHUP)){
                char buffer[MORROCOY_BUFFER];
                ssize_t n=read(STDIN_FILENO,buffer,sizeof buffer);
                if(n==0){
                    keyboard=false;
                    if(!g.empty())std::cerr<<"Linea sin Enter descartada\n";
                    out+="QUIT\r\n";
                }
                else if(n>0)for(ssize_t i=0;i<n;++i){
                    char b=buffer[i];
                    if(b=='\n'){
                        if(!g.empty()&&g.back()=='\r')g.pop_back();
                        // lenght() esta contando hasta null termination sin incluirlo, y empieza desde 1
                        // (no como indice)
                        // ACLARACION: size/length cuenta bytes almacenados, sin terminador NUL.

                        // entonces la linea va a tener:
                        //  max 4B    |    disponible 508B
                        // el comando + sus argumentos (incluido texto) 
                        // necesariamente nosotros debemos colocar el CRLF
                        // CORRECCION: todos los comandos mas sus argumentos disponen de 510B antes del CRLF.

                        // esto no deberia dar core dumped primero porque el buffer es mas grande que g
                        // y porque al usar [] podriamos gozar del colchon de null termination, po lo que sea
                        // CORRECCION: string administra su memoria; no leemos fuera de sus limites.

                        // ---------------- CRLF ----------------------
                        // i queda en la ultima posicion disponible
                        // AGREGADO: concatenamos CRLF sin copiar bytes sobrantes del buffer.
                        if(!too_long&&g.size()<=510&&utf8(g))out+=g+"\r\n";
                        else std::cerr<<"Linea invalida: UTF-8 y maximo 510 bytes antes de CRLF\n";
                        // ---------------- CRLF ----------------------
                        g.clear();
                        too_long=false;
                    }
                    else if(g.size()<511)g+=b;
                    else too_long=true;
                }
                else if(errno!=EINTR)throw std::runtime_error("stdin");
                if(out.size()-sent>1024*1024)throw std::runtime_error("Demasiados comandos pendientes");
            }
            if(fds[0].revents&POLLOUT&&sent<out.size()){
                ssize_t n=send(fd_socket,out.data()+sent,out.size()-sent,MSG_NOSIGNAL);
                if(n>0){
                    sent+=size_t(n);
                    if(sent==out.size()){
                        out.clear();
                        sent=0;
                    }
                }
                else if(n<0&&errno!=EINTR&&errno!=EAGAIN)throw std::runtime_error("send");
            }
            if(fds[0].revents&POLLIN){
                char buffer[MORROCOY_BUFFER];
                ssize_t n=recv(fd_socket,buffer,sizeof buffer,0);
                if(n==0)break;
                if(n<0){
                    if(errno!=EAGAIN&&errno!=EINTR)throw std::runtime_error("recv");
                }
                else{
                    in.append(buffer,size_t(n));
                    size_t p;
                    while((p=in.find("\r\n"))!=std::string::npos){
                        auto line=in.substr(0,p);
                        in.erase(0,p+2);
                        std::cout<<line<<std::endl;
                        // el server siempre responde con un codigo de clase, da igual si es multi linea o no
                        // AGREGADO: se reconoce el codigo solo despues de reunir la linea completa.
                        if(line.size() >= 4 && line[3] == ' ' &&
                           line[0] >= '0' && line[0] <= '9' &&
                           line[1] >= '0' && line[1] <= '9' &&
                           line[2] >= '0' && line[2] <= '9'){
                            op = 0;
                            op += (line[0] - 48) * 100;
                            op += (line[1] - 48) * 10;
                            op += (line[2] - 48);
                        }
                        if(op == QUIT){
                            close(fd_socket);
                            return 0;
                        }
                    }
                    if(in.size()>65536)throw std::runtime_error("Servidor envia linea sin delimitador");
                }
            }
            if(fds[0].revents&(POLLERR|POLLNVAL))throw std::runtime_error("Socket cerrado");
            if((fds[0].revents&POLLHUP)&&!(fds[0].revents&POLLIN))break;
        }
        close(fd_socket);
    }
    catch(const std::exception& e){
        std::cerr << "ERROR: " << e.what() << "\n";
        close(fd_socket);
        return 1;
    }


    return 0;

}