tmcbase = base.tmc
tmcbase = /usr/local/share/linkeng/flttime.tmc
colbase = UDPdiag_col.tmc
# cmdbase = root.cmd
# genuibase = udp.genui

Module UDPdiag

TGTDIR = /home/UDPdiag
IGNORE = "*.o" "*.exe" "*.stackdump" Makefile
SCRIPT = cyg_nc.sh
IDISTRIB = interact_local.sh interact_remote.sh

UDPcol :
# UDPsrvr :
# UDPclt :
UDPdisp : display.tbl
%%
CXXFLAGS=-g
