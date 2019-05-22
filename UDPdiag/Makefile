.PHONY : all clean
LDFLAGS = -L/usr/local/lib
LIBS += -ldasio -lnl
#CXXFLAGS += -fdiagnostics-color=always

all : UDPdiag

UDPdiag : UDPdiag.o UDPdiagoui.o crc16modbus.o
	$(CXX) $(CXXFLAGS) -o UDPdiag UDPdiag.o UDPdiagoui.o crc16modbus.o $(LDFLAGS) $(LIBS)
UDPdiag.o : UDPdiag.cc UDP_int.h UDPdiag.h
UDPdiagoui.o : UDPdiagoui.cc UDP_int.h UDPdiag.h
UDPdiagoui.cc : UDPdiag.oui
	oui -o UDPdiagoui.cc UDPdiag.oui

clean :
	rm -f UDPdiag UDPdiagoui.cc *.o *.stackdump