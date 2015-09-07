TARGET=	cas-xmp
SRCS=	xmp.cpp xmpwrap.cpp
CXX=	g++
OPT=	-O2

PKG=		libxmp

include compiler.mk
include audacious.mk

xmp.o: xmpwrap.h
xmpwrap.o: xmpwrap.h
