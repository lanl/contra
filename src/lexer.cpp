#include "errors.hpp"
#include "lexer.hpp"
#include "string_utils.hpp"
#include "token.hpp"

#include <cstdio>
#include <iostream>

namespace contra {

//==============================================================================
// Get the next char
//==============================================================================
int Lexer::advance() {
  int LastChar = readchar();

  if (LastChar == '\n' || LastChar == '\r') {
    LexLoc.Line++;
    LexLoc.Col = 0;
  }
  else {
    LexLoc.Col++;
  }
  return LastChar;
}


//==============================================================================
/// gettok - Return the next token from standard input.
//==============================================================================
int Lexer::gettok() {

  // Skip any whitespace.
  while (isspace(LastChar))
    LastChar = advance();
  
  CurLoc = LexLoc;

  if (isalpha(LastChar)) { // identifier: [a-zA-Z][a-zA-Z0-9]*
    IdentifierStr = LastChar;
    while (isalnum((LastChar = advance())))
      IdentifierStr += LastChar;

    for ( int i=0; i<num_keywords; ++i )
      if (IdentifierStr == getTokName(tok_keywords[i]))
        return tok_keywords[i];
    return tok_identifier;
  }

  // Number: [0-9.]+
  if (isdigit(LastChar) || LastChar == '.' || LastChar == '+' || LastChar == '-') {

    IdentifierStr.clear();

    // peak if this is a unary or number
    if (LastChar == '+' || LastChar == '-') {
      auto NextChar = peek();
      if ( !isdigit(NextChar) && NextChar != '.' ) {
        int ThisChar = LastChar;
        LastChar = advance();
        return ThisChar;
      }
    }

    // read first part of number
    bool is_float = (LastChar == '.');
    do {
      IdentifierStr += LastChar;
      LastChar = advance();
      if (LastChar == '.') {
        if (is_float)
          THROW_SYNTAX_ERROR( "Multiple '.' encountered in real", LexLoc.Line );
        is_float = true;
        // eat '.'
        IdentifierStr += LastChar;
        LastChar = advance();
      }
    } while (isdigit(LastChar));

    if (LastChar == 'e' || LastChar == 'E') {
      is_float = true;
      // eat e/E
      IdentifierStr += LastChar;
      LastChar = advance();
      // make sure next character is sign or number
      if (LastChar != '+' && LastChar != '-' && !isdigit(LastChar))
        THROW_SYNTAX_ERROR( "Digit or +/- must follow exponent", LexLoc.Line );
      // eat sign or number
      IdentifierStr += LastChar;
      LastChar = advance();
      // only numbers should follow
      do {
        IdentifierStr += LastChar;
        LastChar = advance();
      } while (isdigit(LastChar) );
    }

    if (is_float)
      return tok_real_number;
    else
      return tok_int_number;
  }

  if (LastChar == '#') {
    // Comment until end of line.
    do
      LastChar = advance();
    while (LastChar != eof() && LastChar != '\n' && LastChar != '\r');

    if (LastChar != eof())
      return gettok();
  }

  if (LastChar == '\"') {
    std::string quoted;
    while ((LastChar = advance()) != '\"')
      quoted += LastChar;
    IdentifierStr = unescape(quoted);
    LastChar = advance();
    // string literal
    return tok_string;
  }

  // Check for end of file.  Don't eat the EOF.
  if (LastChar == eof())
    return tok_eof;

  // Otherwise, just return the character as its ascii value.
  int ThisChar = LastChar;
  LastChar = advance();
  return ThisChar;
}

} // namespace
