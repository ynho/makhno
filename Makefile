OBJS = main.o
PROG = quotebot

all: $(PROG)
$(PROG): $(OBJS) ; $(CC) $(LDFLAGS) $(OBJS) -o $@
$(OBJS): main.c
	$(CC) -c $^ -g -o $@
clean: ; rm -f $(OBJS) $(PROG)
