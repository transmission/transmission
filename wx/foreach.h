#ifndef _Foreach_h_
#define _Foreach_h_

#define foreach(Type,var,itname) \
  for (Type::iterator itname(var.begin()), \
                            itname##end(var.end()); itname!=itname##end; \
                            ++itname)

#define foreach_const(Type,var,itname) \
  for (Type::const_iterator itname(var.begin()), \
                            itname##end(var.end()); itname!=itname##end; \
                            ++itname)

#define foreach_r(Type,var,itname) \
  for (Type::reverse_iterator itname(var.rbegin()), \
                              itname##end(var.rend()); itname!=itname##end; \
                              ++itname)

#define foreach_const_r(Type,var,itname) \
  for (Type::const_reverse_iterator itname(var.rbegin()), \
                           itname##end(var.rend()); itname!=itname##end; \
                           ++itname)

#endif
