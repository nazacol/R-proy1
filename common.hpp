#pragma once
#include <string>
#include <stdexcept>
#include <fcntl.h>
#include <cstdint>
constexpr size_t MORROCOY_BUFFER = 4096;
inline int port_number(const std::string& s)  {
    if(s.empty() || s.find_first_not_of("0123456789") != std::string::npos) throw std::runtime_error("Puerto invalido");
    unsigned long n=std::stoul(s);
    if(n<1 || n>65535) throw std::runtime_error("Puerto fuera de rango");
    return int(n);
}
inline void nonblocking(int fd)  {
    int f=fcntl(fd,F_GETFL);
    if(f<0 || fcntl(fd,F_SETFL,f|O_NONBLOCK)<0) throw std::runtime_error("fcntl");
}
inline bool utf8(const std::string& s)  {
    for(size_t i=0;i<s.size();)  {
        unsigned char c=s[i++];
        if(c<128)  {
            if(c<32 || c==127) return false;
            continue;
        }
        int n;
        uint32_t v,min;
        if(c>=0xc2 && c<=0xdf) {
            n=1;
            v=c&31;
            min=128;
        }
        else if(c>=0xe0 && c<=0xef) {
            n=2;
            v=c&15;
            min=2048;
        }
        else if(c>=0xf0 && c<=0xf4) {
            n=3;
            v=c&7;
            min=65536;
        }
        else return false;
        if(i+n>s.size()) return false;
        while(n--) {
            unsigned char b=s[i++];
            if((b&192)!=128)return false;
            v=(v<<6)|(b&63);
        }
        if(v<min || v>0x10ffff || (v>=0xd800 && v<=0xdfff)) return false;
    }
    return true;
}
inline bool name_ok(const std::string& s,bool channel=false)  {
    size_t start=channel?1:0;
    if(s.size()<=start || s.size()>start+(channel?32:16) || (channel && s[0]!='#'))return false;
    for(size_t i=start;i<s.size();++i) {
        char c=s[i];
        if(!((c>='a'&&c<='z')||(!channel&&c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'))return false;
    }
    return true;
}
