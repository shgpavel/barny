CFLAGS := -Wall -Wextra -O3 -std=c2x ${CFLAGS}

CC = clang
LD = clang

SRC = test.c get_price.c
SRC_ = get_open_interest.c write_callback.c

get_price: $(SRC) 
	$(CC) $(SRC) -lcurl -lcjson $(CFLAGS)

get_open_interest: $(SRC_)
	$(CC) $(SRC_) -lcurl -lcjson $(CFLAGS)
clean:
	rm -rf a.out
