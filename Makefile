OBJS_PROD = main_prod.o
OBJS_TEST = main_test.o
PROD = quotebot
TEST = test_quotebot

all: $(PROD) $(TEST)
$(PROD): $(OBJS_PROD)
	$(CC) $(LDFLAGS) $(OBJS_PROD) -o $@
$(TEST): $(OBJS_TEST)
	$(CC) $(LDFLAGS) $(OBJS_TEST) -o $@
$(OBJS_PROD): main.c
	$(CC) -DPROD -g -c $^ -o $@
$(OBJS_TEST): main.c
	$(CC) -g -c $^ -o $@

clean: ; rm -f $(OBJS_PROD) $(OBJS_TEST) $(PROD) $(TEST)
