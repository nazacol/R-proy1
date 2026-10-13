#!/usr/bin/env python3
import socket,struct,time,threading,pathlib,subprocess,tempfile,sys
from test_project import Peer,freeport
BASE=pathlib.Path(__file__).resolve().parents[1]
def block(kind,data):
    data+=b'\0'*((-len(data))%4);n=len(data)+12
    return struct.pack('<II',kind,n)+data+struct.pack('<I',n)
def capture(output):
    cp,hp=freeport(),freeport()
    raw=socket.socket(socket.AF_PACKET,socket.SOCK_RAW,socket.htons(3));raw.bind(('lo',0));raw.settimeout(.1)
    done=threading.Event();packets=[]
    def collect():
        while not done.is_set():
            try:
                frame,addr=raw.recvfrom(65535)
                if addr[2]==socket.PACKET_OUTGOING:continue
                if len(frame)<54 or frame[12:14]!=b'\x08\x00' or frame[23]!=6:continue
                offset=14+(frame[14]&15)*4
                src,dst=struct.unpack('!HH',frame[offset:offset+4])
                if cp not in (src,dst) and hp not in (src,dst):continue
                packets.append((time.time_ns()//1000,frame))
            except socket.timeout:pass
    t=threading.Thread(target=collect);t.start()
    with tempfile.TemporaryDirectory() as d:
        p=subprocess.Popen([str(BASE/'server'),'--chat',str(cp),'--http',str(hp),'--buzones',d],stdout=subprocess.DEVNULL)
        try:
            for _ in range(100):
                try:a=Peer(cp);break
                except OSError:time.sleep(.02)
            a.send('NICK ana');a.expect(201);a.send('JOIN #redes');a.expect(210)
            b=Peer(cp);b.send('NICK bob');b.expect(201);b.send('JOIN #redes');b.expect(210);a.expect(302)
            a.send('MSG #redes hola desde la captura');a.expect(250);b.expect(300)
            b.send('QUIT');b.expect(299);b.close();a.expect(302)
            a.send('PRIV bob revisa el laboratorio');a.expect(251)
            b=Peer(cp);b.send('NICK bob');b.expect(201);b.expect(303);b.send('RETR 1');b.multi(222)
            with socket.create_connection(('127.0.0.1',hp)) as h:
                h.sendall(b'GET /historial/redes HTTP/1.1\r\nHost: localhost\r\n\r\n')
                while h.recv(4096):pass
            a.send('QUIT');a.expect(299);a.close();b.send('QUIT');b.expect(299);b.close();time.sleep(.1)
        finally:p.terminate();p.wait();done.set();t.join();raw.close()
    with open(output,'wb') as f:
        f.write(block(0x0a0d0d0a,struct.pack('<IHHq',0x1a2b3c4d,1,0,-1)))
        f.write(block(1,struct.pack('<HHI',1,0,65535)))
        for stamp,frame in packets:f.write(block(6,struct.pack('<IIIII',0,stamp>>32,stamp&0xffffffff,len(frame),len(frame))+frame))
    assert packets,'No se capturaron paquetes'
    print(f'Captura real: {len(packets)} paquetes; chat={cp}, HTTP={hp}; {output}')
if __name__=='__main__':capture(sys.argv[1] if len(sys.argv)>1 else str(BASE/'sesion.pcapng'))
