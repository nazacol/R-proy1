#!/usr/bin/env python3
"""Pruebas de integración sobre sockets"""
import socket, subprocess, tempfile, time, pathlib, threading
BASE=pathlib.Path(__file__).resolve().parents[1]
def freeport():
    with socket.socket() as s:s.bind(('127.0.0.1',0));return s.getsockname()[1]
class Peer:
    def __init__(self,port):
        self.s=socket.create_connection(('127.0.0.1',port),timeout=5);self.buf=b''
        assert self.line()==b'200 ci4835-nodo-quetzal listo'
    def line(self):
        while b'\r\n' not in self.buf:
            b=self.s.recv(4096)
            if not b:raise AssertionError('Cierre inesperado')
            self.buf+=b
        line,self.buf=self.buf.split(b'\r\n',1);assert len(line)+2<=512;return line
    def send(self,s):self.s.sendall(s.encode()+b'\r\n')
    def expect(self,code):
        while True:
            r=self.line()
            if r.startswith(str(code).encode()+b' '):return r
            if r[:1]!=b'3':raise AssertionError((code,r))
    def multi(self,code):
        out=[]
        while True:
            r=self.line();assert r.startswith(str(code).encode());out.append(r[4:])
            if r[3:4]==b' ':return out
    def close(self):self.s.close()
def run():
    cp,hp=freeport(),freeport()
    while hp==cp:hp=freeport()
    with tempfile.TemporaryDirectory() as d:
        def start():
            p=subprocess.Popen([str(BASE/'server'),'--chat',str(cp),'--http',str(hp),'--buzones',d],stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
            for _ in range(100):
                try:return p,Peer(cp)
                except OSError:time.sleep(.02)
            raise AssertionError('Servidor no inicia')
        p,a=start();peers=[a]
        try:
            a.send('STAT');a.expect(530)
            for byte in b'nIcK ana\r\n':a.s.sendall(bytes([byte]))
            a.expect(201);a.s.sendall(b'JOIN #redes\r\nWHO #redes\r\nCHANS\r\n');a.expect(210);assert b'ana' in a.multi(213);a.multi(212)
            b=Peer(cp);peers.append(b);b.send('NICK bob');b.expect(201);b.send('JOIN #redes');b.expect(210);a.expect(302)
            b.send('MSG #redes hola á');b.expect(250);assert a.expect(300).endswith('hola á'.encode())
            a.send('PRIV bob privado');a.expect(250);assert b.expect(301).endswith(b'privado')
            b.send('QUIT');b.expect(299);a.expect(302);b.close()
            a.send('PRIV bob mensaje en disco');a.expect(251);files=list(pathlib.Path(d).rglob('*.araguaney'));assert len(files)==1;raw=files[0].read_bytes();assert b'\r\n\r\n' in raw and b'Message-ID:' in raw
            a.send('PRIV nadie hola');a.expect(550)
            b=Peer(cp);peers.append(b);b.send('NICK bob');b.expect(201);b.expect(303);b.send('STAT');assert b.expect(220)==f'220 1 {len(raw)}'.encode();b.send('RETR 1');assert b'\r\n'.join(b.multi(222)[:-1])+b'\r\n'==raw
            b.send('DELE 1');b.expect(223);b.send('RETR 1');b.expect(552);b.close();time.sleep(.05)
            b=Peer(cp);peers.append(b);b.send('NICK bob');b.expect(201);b.expect(303);b.send('STAT');b.expect(220)
            # 10 MiB: el servidor debe conservar memoria acotada y recuperar la delimitación.
            a.s.sendall(b'x'*(10*1024*1024)+b'\r\nSTAT\r\n');a.expect(502);a.expect(220)
            a.s.sendall(b'PRIV bob \xff\r\n');a.expect(501);a.send('WHAT');a.expect(500)
            a.send('PART #inexistente');a.expect(551);a.send('PART #redes');a.expect(211);a.send('MSG #redes hola');a.expect(442);a.send('JOIN #redes');a.expect(210)
            def http(req,fragment=False):
                with socket.create_connection(('127.0.0.1',hp),timeout=5) as s:
                    if fragment:
                        for x in req:s.sendall(bytes([x]))
                    else:s.sendall(req)
                    r=b''
                    while True:
                        chunk=s.recv(4096)
                        if not chunk:break
                        r+=chunk
                head,body=r.split(b'\r\n\r\n',1);assert b'X-Turpial: 1' in head;assert b'Content-Length: '+str(len(body)).encode() in head;return head,body
            head,body=http(b'GET /usuarios HTTP/1.1\r\nHost: localhost\r\n\r\n',True);assert b'200 OK' in head and b'ana\n' in body
            assert b'#redes 1' in http(b'GET /canales HTTP/1.1\r\n\r\n')[1]
            assert 'hola á'.encode() in http(b'GET /historial/redes HTTP/1.1\r\n\r\n')[1]
            for req,code in [(b'GET /xxx HTTP/1.1\r\n\r\n',404),(b'POST /usuarios HTTP/1.1\r\n\r\n',405),(b'BAD\r\n\r\n',400)]:assert str(code).encode() in http(req)[0]
            idle=socket.create_connection(('127.0.0.1',hp));peers.append(idle)
            # 200 conexiones, todas registradas y atendidas.
            many=[Peer(cp) for _ in range(200)];peers+=many
            for i,s in enumerate(many):s.send(f'NICK u{i}')
            for s in many:s.expect(201)
            a.send('STAT');a.expect(220)
            for s in many:s.close()
            # El cliente muestra eventos aunque el teclado siga abierto sin escribir.
            fake=socket.socket();fake.bind(('127.0.0.1',0));fake.listen(1)
            cli=subprocess.Popen([str(BASE/'client'),'127.0.0.1',str(fake.getsockname()[1])],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
            conn,_=fake.accept();conn.sendall(b'200 otro-equipo listo\r\n301 emisor asincrono\r\n')
            import select
            assert select.select([cli.stdout],[],[],3)[0], 'Cliente bloqueado por teclado'
            assert cli.stdout.readline().startswith(b'200 ')
            assert cli.stdout.readline().startswith(b'301 ')
            conn.sendall(b'299 Adios\r\n');assert cli.wait(timeout=3)==0;conn.close();fake.close()
            # Límites exactos, conflicto de apodo, numeración con huecos y truncamiento del historial.
            a.s.sendall(b'X'*510+b'\r\n');a.expect(500)
            a.s.sendall(b'X'*511+b'\r\nSTAT\r\n');a.expect(502);a.expect(220)
            dupe=Peer(cp);peers.append(dupe);dupe.send('NICK ana');dupe.expect(433);dupe.close()
            a.send('PRIV ana yo');a.expect(301);a.expect(250)
            a.send('JOIN #hist') ;a.expect(210)
            for i in range(55):a.send(f'MSG #hist m{i}');a.expect(250)
            hist=http(b'GET /historial/hist HTTP/1.1\r\n\r\n')[1].splitlines();assert len(hist)==50 and hist[0].endswith(b'm5') and hist[-1].endswith(b'm54')
            # Un destinatario que deja de leer no detiene a un emisor activo.
            slow=Peer(cp);peers.append(slow);slow.send('NICK lento');slow.expect(201)
            slow.send('JOIN #hist');slow.expect(210)
            for batch in range(250):
                a.s.sendall((b'MSG #hist '+b'z'*430+b'\r\n')*40)
                for _ in range(40):a.expect(250)
            a.send('STAT');a.expect(220);slow.close()
            # Cliente real: stdin por pipe y respuestas sin perder el saludo.
            cli=subprocess.run([str(BASE/'client'),'localhost',str(cp)],input=b'NICK cli\nJOIN #redes\nQUIT\n',capture_output=True,timeout=10);assert cli.returncode==0 and b'201 ' in cli.stdout and b'299 ' in cli.stdout
            # Persistencia real: apagar, iniciar y volver a leer correo.
            for s in peers:
                try:s.close()
                except OSError:pass
            p.terminate();p.wait(timeout=5);p,a=start();peers=[a];a.send('NICK bob');a.expect(201);a.expect(303);a.send('RETR 1');a.multi(222);a.send('DELE 1');a.expect(223);a.send('QUIT');a.expect(299);time.sleep(.05);assert not list(pathlib.Path(d).rglob('*.araguaney'))
            print('OK: registro, canales, eventos, privados, RFC5322, DELE/QUIT, corte abrupto, reinicio, CRLF fragmentado/agrupado, 10 MiB, UTF-8, HTTP, 200 clientes, cliente real/asíncrono y destinatario lento con 10 000 mensajes.')
        finally:
            for s in peers:
                try:s.close()
                except OSError:pass
            p.terminate();p.wait(timeout=5)
            errors=p.stderr.read().decode();assert not errors,errors
if __name__=='__main__':run()
