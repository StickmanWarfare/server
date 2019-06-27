CXX=g++
RM=rm -r -f
CPPFLAGS=-O2 -Wall -Werror
LDLIBS=-lrt

APP=stickserver
OUTDIR=runtime
SRCDIR=src
OBJDIR=obj
SRCS=$(shell find $(SRCDIR) -name "*.cpp")
OBJS=$(addprefix $(OBJDIR)/,$(subst .cpp,.o, $(notdir $(SRCS))))

all: $(APP)

$(APP): $(OBJS)
	-@mkdir -p $(OUTDIR)  
	$(CXX) $(CPPFLAGS) -o $(addprefix $(OUTDIR)/,$(APP)) $(OBJS) $(LDLIBS)
	
$(OBJS): | $(OBJDIR)

$(OBJDIR):
	-@mkdir -p $(OBJDIR)
	
$(OBJDIR)/%.o: $(SRCDIR)/%.cpp
	$(CXX) $(CPPFLAGS) -c $< -o $@

run:
	./$(OUTDIR)/$(APP)
	
clean:
	-@$(RM) $(OBJDIR)
	-@$(RM) $(OUTDIR)/$(APP)