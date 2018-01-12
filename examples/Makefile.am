AM_CPPFLAGS = -I$(top_srcdir)/include 

bin_PROGRAMS = testctrl hookguest

hookguest_SOURCES = hookguest.cpp
hookguest_LDADD = $(top_srcdir)/src/libbdvmi.la

testctrl_SOURCES = testctrl.cpp
testctrl_LDADD = $(top_srcdir)/src/libbdvmi.la -lxenctrl
