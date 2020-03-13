#include "ast.hpp"
#include "vizualizer.hpp"

namespace contra {

//==============================================================================
void Vizualizer::dispatch(ExprAST& e)
{ e.accept(*this); }

//==============================================================================
void Vizualizer::dispatch(ValueExprAST<int_t>& e)
{
  out() << "node" << ind_ << "[label=\"IntegerExprAST\"];" << std::endl;
}

//==============================================================================
void Vizualizer::dispatch(ValueExprAST<real_t>& e)
{
  out() << "node" << ind_ << "[label=\"RealExprAST\"];" << std::endl;
}

//==============================================================================
void Vizualizer::dispatch(ValueExprAST<std::string>& e)
{
  out() << "node" << ind_ << "[label=\"StringExprAST\"];" << std::endl;
}

//==============================================================================
void Vizualizer::dispatch(VariableExprAST& e)
{
  out() << "node" << ind_ << "[label=\"VariableExprAST\"];" << std::endl;
}

//==============================================================================
void Vizualizer::dispatch(ArrayExprAST& e)
{
  out() << "node" << ind_ << "[label=\"ArrayExprAST\"];" << std::endl;
}

//==============================================================================
void Vizualizer::dispatch(CastExprAST& e)
{
  out() << "node" << ind_ << "[label=\"CastExprAST\"];" << std::endl;
}

//==============================================================================
void Vizualizer::dispatch(UnaryExprAST& e)
{
  auto my_ind = ind_;
  out() << "node" << my_ind << "[label=\"UnaryExprAST\"];" << std::endl;
  out() << "node" << my_ind << " -> node" << ++ind_ << ";" << std::endl;
  dispatch(*e.OpExpr_);
}

//==============================================================================
void Vizualizer::dispatch(BinaryExprAST& e)
{
  auto my_ind = ind_;
  out() << "node" << my_ind << "[label=\"BinaryExprAST\"];" << std::endl;
  out() << "node" << my_ind << " -> node" << ++ind_ << ";" << std::endl;
  dispatch(*e.LeftExpr_);
  out() << "node" << my_ind << " -> node" << ++ind_ << ";" << std::endl;
  dispatch(*e.RightExpr_);
}

//==============================================================================
void Vizualizer::dispatch(CallExprAST& e)
{
  auto my_ind = ind_;
  out() << "node" << my_ind << "[label=\"CallExprAST\"];" << std::endl;
  for (unsigned i=0; i<e.ArgExprs_.size(); ++i) {
    out() << "node" << my_ind << " -> node" << ++ind_ << ";" << std::endl;
    dispatch(*e.ArgExprs_[i]);
  }
}

//==============================================================================
void Vizualizer::dispatch(ForExprAST& e)
{
  auto my_ind = ind_;
  out() << "node" << my_ind << "[label=\"ForExprAST\"];" << std::endl;
  out() << "node" << my_ind << " -> node" << ++ind_ << ";" << std::endl;
  dispatch(*e.StartExpr_);
  out() << "node" << my_ind << " -> node" << ++ind_ << ";" << std::endl;
  dispatch(*e.EndExpr_);
  out() << "node" << my_ind << " -> node" << ++ind_ << ";" << std::endl;
  dispatch(*e.StepExpr_);
  for (unsigned i=0; i<e.BodyExprs_.size(); ++i) {
    out() << "node" << my_ind << " -> node" << ++ind_ << ";" << std::endl;
    dispatch(*e.BodyExprs_[i]);
  }
}

//==============================================================================
void Vizualizer::dispatch(IfExprAST& e)
{
  auto my_ind = ind_;
  out() << "node" << my_ind << "[label=\"IfExprAST\"];" << std::endl;
  out() << "node" << my_ind << " -> node" << ++ind_ << ";" << std::endl;
  dispatch(*e.CondExpr_);
  for (unsigned i=0; i<e.ThenExpr_.size(); ++i) {
    out() << "node" << my_ind << " -> node" << ++ind_ << ";" << std::endl;
    dispatch(*e.ThenExpr_[i]);
  }
  for (unsigned i=0; i<e.ElseExpr_.size(); ++i) {
    out() << "node" << my_ind << " -> node" << ++ind_ << ";" << std::endl;
    dispatch(*e.ElseExpr_[i]);
  }
}

//==============================================================================
void Vizualizer::dispatch(VarDefExprAST& e)
{
  out() << "node" << ind_ << "[label=\"VarDefExprAST\"];" << std::endl;
  out() << "node" << ind_ << " -> node" << ind_+1 << ";" << std::endl;
  ind_++;
  dispatch(*e.InitExpr_);
}

//==============================================================================
void Vizualizer::dispatch(ArrayDefExprAST& e)
{
  auto my_ind = ind_;
  out() << "node" << my_ind << "[label=\"ArrayDefExprAST\"];" << std::endl;
  out() << "node" << my_ind << " -> node" << ++ind_ << ";" << std::endl;
  dispatch(*e.InitExpr_);
  out() << "node" << my_ind << " -> node" << ++ind_ << ";" << std::endl;
  dispatch(*e.SizeExpr_);
}

//==============================================================================
void Vizualizer::dispatch(PrototypeAST& e)
{ std::cout << "PrototypeAST" << std::endl; }

//==============================================================================
void Vizualizer::dispatch(FunctionAST& e)
{
  auto my_ind = ind_;
  out() << "digraph {" << std::endl;
  out() << "node" << my_ind << "[label=\"FunctionAST\"];" << std::endl;
  for ( unsigned i=0; i<e.BodyExprs_.size(); ++i ) {
    out() << "node" << my_ind << " -> node" << ++ind_ << ";" << std::endl;
    dispatch(*e.BodyExprs_[i]);
  }
  out() << "}" << std::endl;

}

}