#ifndef CONTRA_PRECEDENCE_HPP
#define CONTRA_PRECEDENCE_HPP

#include "token.hpp"

#include <map>

namespace contra {

struct BinopPrecedenceResult {
  bool found = false;
  int precedence = -1;
};

class BinopPrecedence {

  std::map<char, int> Precedence_;

public:

  BinopPrecedence() {
    // Install standard binary operators.
    // 1 is lowest precedence.
    Precedence_[tok_eq] = 2;
    Precedence_[tok_lt] = 10;
    Precedence_[tok_add] = 20;
    Precedence_[tok_sub] = 20;
    Precedence_[tok_mul] = 40;
    Precedence_[tok_div] = 50;
    // highest.
  }

  BinopPrecedenceResult find( char key ) const
  {
    auto it = Precedence_.find(key);
    if ( it != Precedence_.end() )
      return {true, it->second};
    else
      return {false, -1};
  }

  int& operator[]( char key ) { return Precedence_[key]; }
  int& at( char key ) { return Precedence_.at(key); }
  const int& at( char key ) const { return Precedence_.at(key); }

  
};

} // namespace

#endif // CONTRA_PRECEDENCE_HPP
