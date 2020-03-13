#ifndef CONTRA_VIZUALIZER_HPP
#define CONTRA_VIZUALIZER_HPP

#include "config.hpp"
#include "dispatcher.hpp"

#include <iostream>
#include <fstream>

namespace contra {

class Vizualizer : public AstDispatcher {

  std::ofstream OutputStream_;
  std::ostream * out_ = nullptr;
  int_t ind_ = 0;

public:

  Vizualizer(std::ostream & out = std::cout) : out_(&out)
  {}

  Vizualizer(const std::string & FileName)
  {
    OutputStream_.open(FileName.c_str());
    out_ = &OutputStream_;
  }

  virtual ~Vizualizer()
  { if (OutputStream_) OutputStream_.close(); }

  std::ostream & out() { return *out_; }
  
  // Codegen function
  template<typename T>
  void runVisitor(T&e)
  { e.accept(*this); }
  
  void dispatch(ValueExprAST<int_t>&) override;
  void dispatch(ValueExprAST<real_t>&) override;
  void dispatch(ValueExprAST<std::string>&) override;
  void dispatch(VariableExprAST&) override;
  void dispatch(ArrayExprAST&) override;
  void dispatch(CastExprAST&) override;
  void dispatch(UnaryExprAST&) override;
  void dispatch(BinaryExprAST&) override;
  void dispatch(CallExprAST&) override;
  void dispatch(ForStmtAST&) override;
  void dispatch(IfStmtAST&) override;
  void dispatch(VarDeclAST&) override;
  void dispatch(ArrayDeclAST&) override;
  void dispatch(PrototypeAST&) override;
  void dispatch(FunctionAST&) override;

};



}

#endif // CONTRA_VIZUALIZER_HPP
