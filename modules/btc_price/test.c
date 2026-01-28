#include <stdio.h>
#include <unistd.h>
#include <curl/curl.h>

#include "get_price.h"

#define UPDATE_TIME 4

int
main()
{
	curl_global_init(CURL_GLOBAL_ALL);

	price_t price;
	char    ticker[30] = "BTC-USDT-SWAP";

	while (1) {
		FILE *f = fopen("/opt/barny/modules/btc_price.tmp", "w");
		if (!f) {
			return -1;
		}

		price = get_price(ticker);
		fprintf(f, "%lg\n", price.price);
		fclose(f);
		if (rename("/opt/barny/modules/btc_price.tmp", "/opt/barny/modules/btc_price")
		    != 0) {
			return -1;
		}
		sleep(UPDATE_TIME);
	}
	curl_global_cleanup();
	return 0;
}
