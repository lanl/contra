#include "token.hpp"

namespace contra {

// Initializers
Tokens::map_type Tokens::TokenMap = {};
Tokens::reverse_map_type Tokens::KeywordMap = {};

//==============================================================================
// Install the tokens
//==============================================================================
void Tokens::setup() {

  // add non-keywords here
  TokenMap = {
    { tok_eq, "==" },
    { tok_ne, "!=" },
    { tok_le, "<=" },
    { tok_ge, ">=" },
    { tok_asgmt_add, "+=" },
    { tok_asgmt_sub, "-=" },
    { tok_asgmt_mul, "*=" },
    { tok_asgmt_div, "/=" },
    { tok_eof, "eof" },
    { tok_identifier, "identifier" },
    { tok_char_literal, "char_literal" },
    { tok_int_literal, "integer_literal" },
    { tok_real_literal, "real_literal" },
    { tok_string_literal, "string_literal" },
  };

  // add keywords here
  std::map<int, std::string> Keywords = {
    { tok_binary, "binary" },
    { tok_break, "break" },
    { tok_elif, "elif" },
    { tok_else, "else" },
    { tok_false, "false" },
    { tok_for, "for" },
    { tok_foreach, "foreach" },
    { tok_function, "fn" },
    { tok_if, "if" },
    { tok_reduce, "reduce" },
    { tok_return, "return" },
    { tok_task, "tsk" },
    { tok_true, "true" },
    { tok_unary, "unary" },
    { tok_use, "use" },
  };

  // create keyword list
  KeywordMap.clear();
  for ( const auto & key_pair : Keywords )
    KeywordMap.emplace( key_pair.second, key_pair.first );

  // insert keywords into full token map
  TokenMap.insert( Keywords.begin(), Keywords.end() );
}
  
//==============================================================================
// Get a tokens name
//==============================================================================
std::string Tokens::getName(int Tok) {
  auto it = TokenMap.find(Tok);
  if (it != TokenMap.end()) return it->second;
  return std::string(1, (char)Tok);
}
  
//==============================================================================
// get a token from its name
//==============================================================================
TokenResult Tokens::getTok(const std::string & Name)
{
  auto it = KeywordMap.find(Name);
  if (it != KeywordMap.end())
    return {true, it->second };
  else 
    return {false, 0};
}

} // namespace
