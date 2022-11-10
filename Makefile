target = usb_charge_control
objects = comm_layer.o control_layer.o ui_layer.o ui_locale.o main.o

prefix = .
DEBUG = 0
OPT = -O3 -flto

includedir = $(prefix)/include
libdir     = $(prefix)/lib

serialib_dir   = $(prefix)/serialib
simple_cairo_plot_dir = $(prefix)/simple-cairo-plot
serialib = $(libdir)/libserialib.a
simple_cairo_plot = $(libdir)/libsimple-cairo-plot.a

# use gcc-ar for LTO support
AR = gcc-ar

ifeq '$(findstring sh,$(SHELL))' 'sh'
# UNIX, MSYS2 or Cygwin
MKDIR = mkdir -p
CP = cp
RM = rm -f
RMDIR = rm -f -r
else
# Windows, neither MSYS2 nor Cygwin
MKDIR = mkdir
CP = copy
RM = del /Q
RMDIR = rmdir /S /Q
endif

CXXFLAGS = -I. -I$(includedir) `pkg-config gtkmm-3.0 --cflags --libs`
ifeq ($(DEBUG), 1)
CXXFLAGS += -DDEBUG -g
else
CXXFLAGS += $(OPT)
endif

LDFLAGS = -L$(libdir) -lserialib -lsimple-cairo-plot $(CXXFLAGS)
ifeq '$(OS)' 'Windows_NT'
LDFLAGS += -mwindows
endif

$(target): $(serialib) $(simple_cairo_plot) $(objects)
	$(CXX) $(objects) $(LDFLAGS) -o $@

$(serialib): $(serialib_dir)
	$(MAKE) -C $< prefix=..

$(simple_cairo_plot): $(simple_cairo_plot_dir)
	$(MAKE) -C $< prefix=..

$(serialib_dir):
	git clone https://github.com/wuwbobo2021/serialib

$(simple_cairo_plot_dir):
	git clone https://github.com/wuwbobo2021/simple-cairo-plot

.PHONY: cleanall clean
cleanall:
	-$(RMDIR) lib include $(serialib_dir) $(simple_cairo_plot_dir)
	-$(RM) *.o *.bin $(target)

clean:
	-$(RMDIR) lib include
	-$(RM) *.o $(target)
	-$(MAKE) -C $(serialib_dir) clean
	-$(MAKE) -C $(simple_cairo_plot_dir) clean
