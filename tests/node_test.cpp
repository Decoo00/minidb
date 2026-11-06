#include "node.h"
#include <iostream>

using namespace std;

int main() {
    Node internal(false);
    Node leaf(true);

    internal.keys.push_back(1);
    internal.keys.push_back(2);
    internal.keys.push_back(3);
    internal.keys.push_back(4);
    internal.children.push_back(1434);
    internal.children.push_back(1435);
    internal.children.push_back(1436);
    internal.children.push_back(1437);
    internal.children.push_back(1438);

    
    leaf.keys.push_back(5);
    leaf.keys.push_back(6);
    leaf.keys.push_back(7);
    leaf.values.push_back(1005);
    leaf.values.push_back(1006);
    leaf.values.push_back(1007);
    leaf.next = 4567;

    char buf[PAGE_SIZE];
    char buf2[PAGE_SIZE];
    serialize(internal, buf);
    Node i = deserialize(buf);
    cout << i.is_leaf << " " << i.keys.size() << endl;
    for(auto i2 : i.keys) {
        cout << i2 << ", ";
    }
    cout<<endl;
    for(auto i2 : i.children) {
        cout << i2 << ", ";
    }
    cout<<endl;

    
    serialize(leaf, buf2);
    Node l = deserialize(buf2);
    cout << l.is_leaf << " " << l.keys.size() << " " << l.next << endl;
    for(auto l2 : l.keys) {
        cout << l2 << ", ";
    }
    cout<<endl;
    for(auto l2 : l.values) {
        cout << l2 << ", ";
    }
    cout<<endl;
}