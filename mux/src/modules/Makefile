CXX = g++ -std=c++17
# Note: this Makefile is overwritten by ./configure (run from src/).
# Edit modules/Makefile.in for persistent changes.

ALLSRC     = funcs.cpp sample.cpp sum.cpp sqlproxy.cpp sqlslave.cpp execscript.cpp jsonparse.cpp httpclient.cpp

all: funcs.so sample.so sum.so sqlproxy.so sqlslave.so execscript.so jsonparse.so httpclient.so

funcs.so: funcs.cpp funcs.h ../autoconf.h ../config.h ../libmux.h ../modules.h
	( if [ -f funcs.so ]; then mv -f funcs.so funcs.so~ ; fi )
	$(CXX) -bundle -fPIC -o funcs.so funcs.cpp -L.. -lmux
	( if [ -f funcs.so ]; then cd ../../game/bin ; rm -f funcs.so ; ln -s ../../src/modules/funcs.so funcs.so ; fi )

sample.so: sample.cpp sample.h ../autoconf.h ../config.h ../libmux.h ../modules.h
	( if [ -f sample.so ]; then mv -f sample.so sample.so~ ; fi )
	$(CXX) -bundle -fPIC -o sample.so sample.cpp -L.. -lmux
	( if [ -f sample.so ]; then cd ../../game/bin ; rm -f sample.so ; ln -s ../../src/modules/sample.so sample.so ; fi )

sum.so: sum.cpp sum.h ../autoconf.h ../config.h ../libmux.h ../modules.h
	( if [ -f sum.so ]; then mv -f sum.so sum.so~ ; fi )
	$(CXX) -bundle -fPIC -o sum.so sum.cpp -L.. -lmux
	( if [ -f sum.so ]; then cd ../../game/bin ; rm -f sum.so ; ln -s ../../src/modules/sum.so sum.so ; fi )

sqlproxy.so: sqlproxy.cpp sql.h ../autoconf.h ../config.h ../libmux.h ../modules.h
	( if [ -f sqlproxy.so ]; then mv -f sqlproxy.so sqlproxy.so~ ; fi )
	$(CXX) -bundle -fPIC -o sqlproxy.so sqlproxy.cpp -L.. -lmux
	( if [ -f sqlproxy.so ]; then cd ../../game/bin ; rm -f sqlproxy.so ; ln -s ../../src/modules/sqlproxy.so sqlproxy.so ; fi )

sqlslave.so: sqlslave.cpp sql.h ../autoconf.h ../config.h ../libmux.h ../modules.h
	( if [ -f sqlslave.so ]; then mv -f sqlslave.so sqlslave.so~ ; fi )
	$(CXX) -bundle -fPIC  -o sqlslave.so sqlslave.cpp -L..  -lmux 
	( if [ -f sqlslave.so ]; then cd ../../game/bin ; rm -f sqlslave.so ; ln -s ../../src/modules/sqlslave.so sqlslave.so ; fi )

execscript.so: execscript.cpp execscript.h ../autoconf.h ../config.h ../libmux.h ../modules.h ../command.h
	( if [ -f execscript.so ]; then mv -f execscript.so execscript.so~ ; fi )
	$(CXX) -bundle -fPIC -o execscript.so execscript.cpp -L.. -lmux
	( if [ -f execscript.so ]; then cd ../../game/bin ; rm -f execscript.so ; ln -s ../../src/modules/execscript.so execscript.so ; fi )

jsonparse.so: jsonparse.cpp jsonparse.h ../autoconf.h ../config.h ../libmux.h ../modules.h
	( if [ -f jsonparse.so ]; then mv -f jsonparse.so jsonparse.so~ ; fi )
	$(CXX) -bundle -fPIC -o jsonparse.so jsonparse.cpp -L.. -lmux
	( if [ -f jsonparse.so ]; then cd ../../game/bin ; rm -f jsonparse.so ; ln -s ../../src/modules/jsonparse.so jsonparse.so ; fi )

httpclient.so: httpclient.cpp httpclient.h ../autoconf.h ../config.h ../libmux.h ../modules.h
	( if [ -f httpclient.so ]; then mv -f httpclient.so httpclient.so~ ; fi )
	$(CXX) -bundle -fPIC -o httpclient.so httpclient.cpp -L.. -lmux -lcurl
	( if [ -f httpclient.so ]; then cd ../../game/bin ; rm -f httpclient.so ; ln -s ../../src/modules/httpclient.so httpclient.so ; fi )

depend: $(ALLSRC) ../unsplit
	for i in $(ALLSRC) ; do $(CXX) $(CXXFLAGS) -bundle -fPIC  -M $$i; done | ../unsplit > .depend~
	mv .depend~ .depend

clean:
	-rm -f *.o a.out core gmon.out warn.log config.status config.log funcs.so~ sample.so~ sum.so~ sqlproxy.so~ sqlslave.so~ execscript.so~ jsonparse.so~ httpclient.so~

realclean:
	-rm -f *.o a.out core gmon.out warn.log config.status config.log funcs.so funcs.so~ sample.so sample.so~ sum.so sum.so~ sqlproxy.so sqlproxy.so~ sqlslave.so sqlslave.so~ execscript.so execscript.so~ jsonparse.so jsonparse.so~ httpclient.so httpclient.so~ autoconf.h Makefile

include .depend
