#opt: OPTFLAGS = -O2
#export OPTFLAGS

.PHONY: all clean opt

all:
		cd src/ && $(MAKE)
		cd test && $(MAKE)
clean:
		cd src/ && $(MAKE) clean
		cd test && $(MAKE) clean
		rm lib/*
opt:
		$(MAKE) all

