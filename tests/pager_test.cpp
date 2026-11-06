#include "pager.h"
#include <cstring>
#include <cstdio>

int main() {
    Pager p("bad.db");
    printf("%u\n", p.page_count());

    //p.allocate();
    //printf("%u\n", p.page_count());

    
    //char wbuf[PAGE_SIZE] = {};
    //strcpy(wbuf,"Hello World!");
    //p.write(0, wbuf);

    char rbuf[PAGE_SIZE];
    p.read(1,rbuf);

    printf("%s\n", rbuf);
    
}