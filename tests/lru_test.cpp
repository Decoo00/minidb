#include "lru.h"
#include <iostream>

using namespace std;

int main(){
    LRU l(0);
    cout << "popped out " << static_cast<int>(l.touch(1).value_or(-1))<<endl;
   
}