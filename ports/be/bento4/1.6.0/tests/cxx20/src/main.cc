// Compiles the library headers as C++20, where template-id constructors
// (as in Ap4Array.h) are ill-formed.
#include <bento4/Ap4.h>

int main() {
  AP4_Array<int> a;
  a.Append(3);
  AP4_List<int> l;
  int v = 4;
  l.Add(&v);
  AP4_Array<int> b(a);
  return b.ItemCount() == 1 && l.ItemCount() == 1 && b[0] == 3 ? 0 : 1;
}
