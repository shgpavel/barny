#ifndef STRUCTS_H
#define STRUCTS_H

typedef struct price {
  double  price;
  long    time;
  char    ticker[30];
} price_t;

#endif
