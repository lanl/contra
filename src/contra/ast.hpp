#ifndef CONTRA_AST_HPP
#define CONTRA_AST_HPP

#include "visiter.hpp"
#include "config.hpp"
#include "errors.hpp"
#include "identifier.hpp"
#include "sourceloc.hpp"
#include "symbols.hpp"

#include <algorithm>
#include <cassert>
#include <deque>
#include <iostream>
#include <list>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace contra {

////////////////////////////////////////////////////////////////////////////////
/// NodeAST - Base class for all nodes.
////////////////////////////////////////////////////////////////////////////////
class NodeAST {
  
  LocationRange Loc_;

public:

	NodeAST() = default;
  
  NodeAST(const LocationRange & Loc) : Loc_(Loc) {}
  
  virtual ~NodeAST() = default;

  virtual void accept(AstVisiter& visiter) = 0;

  virtual std::string getClassName() const = 0;
  
  const auto & getLoc() const { return Loc_; }

};

// some useful types
using ASTBlock = std::deque< std::unique_ptr<NodeAST> >;
using ASTBlockList = std::list<ASTBlock>;

inline auto createBlock( ASTBlockList & list)
{ return list.emplace( list.end(), ASTBlock{} ); }


////////////////////////////////////////////////////////////////////////////////
/// ExprAST - Base class for all expression nodes.
////////////////////////////////////////////////////////////////////////////////
class ExprAST : public NodeAST {
protected:

  VariableType Type_;
  
public:
  
  ExprAST(const LocationRange & Loc) : NodeAST(Loc) {}
  ExprAST(const LocationRange & Loc, VariableType Type) : NodeAST(Loc),
    Type_(Type) {}

  virtual ~ExprAST() = default;
  
  void setType(const VariableType & Type) { Type_ = Type; }
  const auto & getType() const { return Type_; }
  auto & getType() { return Type_; }


  virtual void setFuture(bool IsFuture=true) { Type_.setFuture(IsFuture); }
  virtual bool isFuture() const { return Type_.isFuture(); }

};

//==============================================================================
/// ValueExprAST - Expression class for numeric literals like "1.0".
//==============================================================================
class ExprListAST : public ExprAST {

protected:

  ASTBlock Exprs_;

public:
  ExprListAST(
      const LocationRange & Loc,
      ASTBlock Exprs) :
    ExprAST(Loc),
    Exprs_(std::move(Exprs))
  {}

  void accept(AstVisiter& visiter) override;
  
  virtual std::string getClassName() const override
  { return "ExprListAST"; }
  
  const auto & getExprs() const { return Exprs_; }
  auto moveExprs() { return std::move(Exprs_); }
  
  auto size() const {return Exprs_.size();}
  
  auto getExpr(unsigned i) {return Exprs_[i].get();}
  
};


//==============================================================================
/// ValueExprAST - Expression class for numeric literals like "1.0".
//==============================================================================
class ValueExprAST : public ExprAST {

public:

  enum class ValueType {
    Int, Real, String
  };

protected:

  std::string Val_;
  ValueType ValueType_;

public:
  ValueExprAST(
      const LocationRange & Loc,
      const std::string & Val,
      ValueType Ty) :
    ExprAST(Loc),
    Val_(Val),
    ValueType_(Ty)
  {}

  template<typename T>
  T getVal() const;
  
  void accept(AstVisiter& visiter) override
  { visiter.visit(*this); }
  
  virtual std::string getClassName() const override
  { return "ValueExprAST"; }

  ValueType getValueType() const { return ValueType_; }
  
};


//==============================================================================
/// VarAccessExprAST - Expression class for referencing a variable, like "a".
//==============================================================================
class VarAccessExprAST : public ExprAST {
protected:

  Identifier Id_;
  std::unique_ptr<Identifier> TypeId_;
  VariableDef* VarDef_ = nullptr;

  // Note: Derived member VarType_ might differ from VarDef->getType().
  // This is because the accessed type might be different from the original
  // declaration.  An example is accessing a future variable as a non-future.

public:

  VarAccessExprAST(
      const LocationRange & Loc,
      const  Identifier &Id,
      std::unique_ptr<Identifier> TypeId = nullptr) :
    ExprAST(Loc),
    Id_(Id),
    TypeId_(std::move(TypeId))
  {}

  virtual void accept(AstVisiter& visiter) override;
  
  virtual std::string getClassName() const override
  { return "VarAccessExprAST"; };

  const std::string &getName() const { return Id_.getName(); }
  const Identifier & getVarId() const { return Id_; }
  
  void setVariableDef(VariableDef* VarDef) { VarDef_=VarDef; }
  VariableDef* getVariableDef() const { return VarDef_; }

  auto hasTypeId() const { return static_cast<bool>(TypeId_); }
  const auto & getTypeId() const { return *TypeId_; }
};

//==============================================================================
/// ArrayExprAST - Expression class for referencing an array.
//==============================================================================
class ArrayAccessExprAST : public VarAccessExprAST {
protected:
  
  std::unique_ptr<NodeAST> IndexExpr_;

public:
  ArrayAccessExprAST(
      const LocationRange & Loc,
      const Identifier &Id, 
      std::unique_ptr<NodeAST> IndexExpr,
      std::unique_ptr<Identifier> TypeId = nullptr) :
    VarAccessExprAST(Loc, Id, std::move(TypeId)),
    IndexExpr_(std::move(IndexExpr))
  {}
  
  virtual void accept(AstVisiter& visiter) override;
  
  virtual std::string getClassName() const override
  { return "VarAccessExprAST"; };
  
  auto getIndexExpr() const { return IndexExpr_.get(); }

};

//==============================================================================
/// ArrayExprAST - Expression class for referencing an array.
//==============================================================================
class ArrayExprAST : public ExprAST {
protected:

  ASTBlock ValExprs_;
  std::unique_ptr<NodeAST> SizeExpr_;
  std::string Name_;

public:

  ArrayExprAST(
      const LocationRange & Loc,
      std::unique_ptr<NodeAST> ValsExpr,
      std::unique_ptr<NodeAST> Size) :
    ExprAST(Loc),
    SizeExpr_(std::move(Size))
  {
    if (auto ExprList = dynamic_cast<ExprListAST*>(ValsExpr.get()))
      ValExprs_ = ExprList->moveExprs();
    else
      ValExprs_.emplace_back( std::move(ValsExpr) );
  }
  
  virtual void accept(AstVisiter& visiter) override;
  
  virtual std::string getClassName() const override
  { return "ArrayExprAST"; };

  void setName(const std::string & Name) { Name_ = Name; }
  const auto & getName() const { return Name_; }

  bool hasSize() const { return static_cast<bool>(SizeExpr_); }
  auto getSizeExpr() const { return SizeExpr_.get(); }

  auto getNumVals() const { return ValExprs_.size(); }
  auto getValExpr(int i) const { return ValExprs_[i].get(); }
  const auto & getValExprs() const { return ValExprs_; }

  auto moveValExpr(int i) { return std::move(ValExprs_[i]); }
  auto setValExpr(int i, std::unique_ptr<NodeAST> Expr) { ValExprs_[i] = std::move(Expr); }

};

//==============================================================================
/// RangeExprAST - Expression class for referencing  range.
//==============================================================================
class RangeExprAST : public ExprAST {
protected:

  std::unique_ptr<NodeAST> StartExpr_, EndExpr_, StepExpr_;
  std::string Name_;

public:

  RangeExprAST(
      const LocationRange & Loc,
      std::unique_ptr<NodeAST> Start,
      std::unique_ptr<NodeAST> End) :
    ExprAST(Loc),
    StartExpr_(std::move(Start)),
    EndExpr_(std::move(End))
  {}
  
  RangeExprAST(
      const LocationRange & Loc,
      ASTBlock Exprs) :
    ExprAST(Loc)
  {
    if (Exprs.size()>0)
      StartExpr_ = std::move(Exprs[0]);
    if (Exprs.size()>1)
      EndExpr_ = std::move(Exprs[1]);
    if (Exprs.size()>2)
      StepExpr_ = std::move(Exprs[2]);
  }
  
  virtual void accept(AstVisiter& visiter) override;
  
  virtual std::string getClassName() const override
  { return "RangeExprAST"; };
  
  void setName(const std::string & Name) { Name_ = Name; }
  const auto & getName() const { return Name_; }


  auto getStartExpr() const { return StartExpr_.get(); }
  auto moveStartExpr() { return std::move(StartExpr_); }
  auto setStartExpr(std::unique_ptr<NodeAST> Expr) { EndExpr_ = std::move(Expr); }

  auto getEndExpr() const { return EndExpr_.get(); }
  auto moveEndExpr() { return std::move(EndExpr_); }
  auto setEndExpr(std::unique_ptr<NodeAST> Expr) { EndExpr_ = std::move(Expr); }

  auto hasStepExpr() const { return static_cast<bool>(StepExpr_); }
  auto getStepExpr() const { return StepExpr_.get(); }
  auto moveStepExpr() { return std::move(StepExpr_); }
  auto setStepExpr(std::unique_ptr<NodeAST> Expr) { StepExpr_ = std::move(Expr); }

};

//==============================================================================
/// CastExprAST - Expression class for casts
//==============================================================================
class CastExprAST : public ExprAST {
protected:

  std::unique_ptr<NodeAST> FromExpr_;
  NodeAST* FromExprPtr_ = nullptr;
  Identifier TypeId_;


public:
  CastExprAST(
      const LocationRange & Loc,
      std::unique_ptr<NodeAST> FromExpr,
      Identifier TypeId) :
    ExprAST(Loc),
    FromExpr_(std::move(FromExpr)),
    FromExprPtr_(FromExpr_.get()),
    TypeId_(TypeId)
  {}

  CastExprAST(
      const LocationRange & Loc,
      NodeAST* FromExpr,
      Identifier TypeId) :
    ExprAST(Loc),
    FromExprPtr_(FromExpr),
    TypeId_(TypeId)
  {}

  CastExprAST(
      const LocationRange & Loc,
      std::unique_ptr<NodeAST> FromExpr,
      const VariableType & Type) : 
    ExprAST(Loc, Type),
    FromExpr_(std::move(FromExpr)),
    FromExprPtr_(FromExpr_.get())
  {}

  CastExprAST(
      const LocationRange & Loc,
      NodeAST* FromExpr,
      const VariableType & Type) : 
    ExprAST(Loc, Type),
    FromExprPtr_(FromExpr)
  {}

  virtual void accept(AstVisiter& visiter) override;
  
  virtual std::string getClassName() const override
  { return "CastExprAST"; };

  const auto & getTypeId() const { return TypeId_; }
  auto getFromExpr() const { return FromExprPtr_; }
  
};


//==============================================================================
/// UnaryExprAST - Expression class for a unary operator.
//==============================================================================
class UnaryExprAST : public ExprAST {
protected:

  char OpCode_;
  std::unique_ptr<NodeAST> OpExpr_;

public:
  UnaryExprAST(
      const LocationRange & Loc,
      char Opcode,
      std::unique_ptr<NodeAST> Operand) :
    ExprAST(Loc),
    OpCode_(Opcode),
    OpExpr_(std::move(Operand))
  {}
  
  virtual void accept(AstVisiter& visiter) override;
  
  virtual std::string getClassName() const override
  { return "UnaryExprAST"; };

  auto getOperand() const { return OpCode_; }
  auto getOpExpr() const { return OpExpr_.get(); }
};

//==============================================================================
/// BinaryExprAST - Expression class for a binary operator.
//==============================================================================
class BinaryExprAST : public ExprAST {
protected:

  char OpCode_;
  std::unique_ptr<NodeAST> LeftExpr_;
  std::unique_ptr<NodeAST> RightExpr_;

public:
  BinaryExprAST(
      const LocationRange & Loc, 
      char Op, std::unique_ptr<NodeAST> lhs,
      std::unique_ptr<NodeAST> rhs) :
    ExprAST(Loc),
    OpCode_(Op),
    LeftExpr_(std::move(lhs)),
    RightExpr_(std::move(rhs))
  {}

  auto getOperand() const { return OpCode_; }
  
  virtual void accept(AstVisiter& visiter) override;
  
  virtual std::string getClassName() const override
  { return "BinaryExprAST"; };

  auto getLeftExpr() const { return LeftExpr_.get(); }
  auto moveLeftExpr() { return std::move(LeftExpr_); }
  auto setLeftExpr(std::unique_ptr<NodeAST> Expr) { LeftExpr_ = std::move(Expr); }

  auto getRightExpr() const { return RightExpr_.get(); }
  auto moveRightExpr() { return std::move(RightExpr_); }
  auto setRightExpr(std::unique_ptr<NodeAST> Expr) { RightExpr_ = std::move(Expr); }
};

//==============================================================================
/// CallExprAST - Expression class for function calls.
//==============================================================================
class CallExprAST : public ExprAST {
protected:
  
  Identifier CalleeId_;
  ASTBlock ArgExprs_;
  bool IsTopTask_ = false;
  std::vector<VariableType> ArgTypes_;
  
  FunctionDef* FunctionDef_ = nullptr;

public:

  CallExprAST(
      const LocationRange & Loc,
      const Identifier & CalleeId,
      std::unique_ptr<NodeAST> ArgsExpr) :
    ExprAST(Loc),
    CalleeId_(CalleeId)
  {
    if (auto ExprList = dynamic_cast<ExprListAST*>(ArgsExpr.get()))
      ArgExprs_ = ExprList->moveExprs();
    else if (ArgsExpr)
      ArgExprs_.emplace_back( std::move(ArgsExpr) );
  }

  const std::string & getName() const { return CalleeId_.getName(); }

  void setTopLevelTask(bool TopTask = true) { IsTopTask_ = TopTask; }
  bool isTopLevelTask() { return IsTopTask_; }
  
  virtual void accept(AstVisiter& visiter) override;
  
  virtual std::string getClassName() const override
  { return "CallExprAST"; };

  auto getNumArgs() const { return ArgExprs_.size(); }
  const auto & getArgExprs() const { return ArgExprs_; }
  auto getArgExpr(int i) const { return ArgExprs_[i].get(); }
  
  auto moveArgExpr(int i) { return std::move(ArgExprs_[i]); }
  
  auto setArgExpr(int i, std::unique_ptr<NodeAST> Expr)
  { ArgExprs_[i] = std::move(Expr); }

  const auto & getArgType(int i) { return ArgTypes_[i]; }
  void setArgTypes(const std::vector<VariableType> & ArgTypes)
  { ArgTypes_ = ArgTypes; }

  auto getFunctionDef() const { return FunctionDef_; }
  void setFunctionDef(FunctionDef* F) { FunctionDef_ = F; }
};

////////////////////////////////////////////////////////////////////////////////
/// StmtAST - Base class for all statement nodes.
////////////////////////////////////////////////////////////////////////////////
class StmtAST : public NodeAST {
  
public:
  
  StmtAST(const LocationRange & Loc) : NodeAST(Loc) {}

  virtual ~StmtAST() = default;

};

//==============================================================================
/// Break statement
//==============================================================================
class BreakStmtAST : public StmtAST {
protected:

public:

  BreakStmtAST(const LocationRange & Loc) :
    StmtAST(Loc)
  {}
  
  virtual void accept(AstVisiter& visiter) override;
  
  virtual std::string getClassName() const override
  { return "BreakStmtAST"; };

};


//==============================================================================
/// IfExprAST - Expression class for if/then/else.
//==============================================================================
class IfStmtAST : public StmtAST {
protected:

  std::unique_ptr<NodeAST> CondExpr_;
  ASTBlock ThenExpr_, ElseExpr_;

public:

  IfStmtAST(
      const LocationRange & Loc,
      std::unique_ptr<NodeAST> Cond,
      ASTBlock Then) :
    StmtAST(Loc),
    CondExpr_(std::move(Cond)),
    ThenExpr_(std::move(Then))
  {}
  
  virtual void accept(AstVisiter& visiter) override;
  
  virtual std::string getClassName() const override
  { return "IfStmtAST"; };

  using ConditionList =
    std::list< std::pair<LocationRange, std::unique_ptr<NodeAST>> >;

  static std::unique_ptr<NodeAST> makeNested( 
    ConditionList & Conds,
    ASTBlockList & Blocks );

  auto getCondExpr() const { return CondExpr_.get(); }
  const auto & getThenExprs() const { return ThenExpr_; }
  const auto & getElseExprs() const { return ElseExpr_; }
};

//==============================================================================
// ForExprAST - Expression class for for/in.
//==============================================================================
class ForStmtAST : public StmtAST {

protected:

  Identifier VarId_;
  std::unique_ptr<NodeAST> StartExpr_;
  ASTBlock BodyExprs_;

public:

  ForStmtAST(const LocationRange & Loc,
      const Identifier &VarId,
      std::unique_ptr<NodeAST> Start,
      ASTBlock Body) :
    StmtAST(Loc),
    VarId_(VarId),
    StartExpr_(std::move(Start)),
    BodyExprs_(std::move(Body))
  {}
  
  virtual void accept(AstVisiter& visiter) override;
  
  virtual std::string getClassName() const override
  { return "ForStmtAST"; };

  const std::string & getVarName() const { return VarId_.getName(); }

  const auto & getVarId() const { return VarId_; }

  const auto & getBodyExprs() const { return BodyExprs_; }

  auto getStartExpr() const { return StartExpr_.get(); }
};

//==============================================================================
// ForEachExprAST - Expression class for for/in.
//==============================================================================
class ForeachStmtAST : public ForStmtAST {

  std::vector<VariableDef*> AccessedVariables_;
  std::string Name_;
  bool IsLifted_ = false;
  unsigned NumQualifiers_ = 0;
  
  bool HasReduce_ = false;
  std::vector<ReductionDef> ReduceVariables_;



public:
  
  ForeachStmtAST(
      const LocationRange & Loc,
      const Identifier &VarId,
      std::unique_ptr<NodeAST> Start,
      ASTBlock Body) :
    ForStmtAST(
        Loc,
        VarId,
        std::move(Start),
        std::move(Body))
  {}
  
  virtual void accept(AstVisiter& visiter) override;

  void setAccessedVariables(const std::vector<VariableDef*> & VarDefs)
  { AccessedVariables_ = VarDefs; }

  const auto & getAccessedVariables()
  { return AccessedVariables_; }

  auto getBodyExpr(unsigned i) const { return BodyExprs_[i].get(); }

  auto moveBodyExprs() {
    ASTBlock NewBody;
    auto NumBody = BodyExprs_.size();
    for (unsigned i=NumQualifiers_; i<NumBody; ++i) {
      NewBody.emplace_front( std::move(BodyExprs_.back()) );
      BodyExprs_.pop_back();
    }
    return NewBody;
  }
  
  const std::string &getName() const { return Name_; }
  void setName(const std::string& Name) { Name_ = Name; }
  
  bool isLifted() const { return IsLifted_; }
  void setLifted(bool IsLifted=true) { IsLifted_ = IsLifted; }
  
  void setNumQualifiers(unsigned NumQualifiers) { NumQualifiers_=NumQualifiers; }
  auto getNumQualifiers() const { return NumQualifiers_; }

  void setHasReduction(bool HasReduce=true) { HasReduce_=HasReduce; }
  bool hasReduction() const { return HasReduce_; }

  const auto & getReductionVars() const { return ReduceVariables_; }
  void setReductionVars(const std::vector<ReductionDef> & ReduceVars)
  { ReduceVariables_ = ReduceVars; }
};

//==============================================================================
/// Partition statement
//==============================================================================
class PartitionStmtAST : public StmtAST {
protected:

  std::vector<Identifier> RangeIds_;
  std::unique_ptr<NodeAST> PartExpr_;

  std::vector<VariableDef*> VarDefs_;

public:
  PartitionStmtAST(
      const LocationRange & Loc,
      const std::vector<Identifier> & RangeIds,
      std::unique_ptr<NodeAST> PartExpr) :
    StmtAST(Loc),
    RangeIds_(RangeIds),
    PartExpr_(std::move(PartExpr)),
    VarDefs_(RangeIds.size(), nullptr)
  {}

  virtual void accept(AstVisiter& visiter) override;
  
  virtual std::string getClassName() const override
  { return "PartitionStmtAST"; };
  
  auto getPartExpr() const { return PartExpr_.get(); }

  auto getNumVars() const { return RangeIds_.size(); }
  const auto & getVarName(unsigned i) const { return RangeIds_[i].getName(); }
  const auto & getVarId(unsigned i) const { return RangeIds_[i]; }

  auto getVarDef(unsigned i) const { return VarDefs_[i]; }
  void setVarDef(unsigned i, VariableDef* Var) { VarDefs_[i] = Var; }
};


//==============================================================================
/// Reduction statement
//==============================================================================
class ReductionStmtAST : public StmtAST {
protected:

  std::vector<Identifier> VarIds_;
  std::string OperatorName_;
  char OperatorCode_ = 0;
  LocationRange OperatorLoc_;
  bool IsOperator_ = false;

  std::vector<VariableDef*> VarDefs_;
  FunctionDef* OperatorDef_ = nullptr;

public:
  ReductionStmtAST(
      const LocationRange & Loc,
      const std::vector<Identifier> & VarIds,
      const std::string & OperatorName,
      const LocationRange & OperatorLoc) :
    StmtAST(Loc),
    VarIds_(VarIds),
    OperatorName_(OperatorName),
    OperatorLoc_(OperatorLoc),
    IsOperator_(false),
    VarDefs_(VarIds.size(), nullptr)
  {}
  
  ReductionStmtAST(
      const LocationRange & Loc,
      const std::vector<Identifier> & VarIds,
      char OperatorCode,
      const std::string & OperatorName,
      const LocationRange & OperatorLoc) :
    StmtAST(Loc),
    VarIds_(VarIds),
    OperatorName_(OperatorName),
    OperatorCode_(OperatorCode),
    OperatorLoc_(OperatorLoc),
    IsOperator_(true),
    VarDefs_(VarIds.size(), nullptr)
  {}

  virtual void accept(AstVisiter& visiter) override;
  
  virtual std::string getClassName() const override
  { return "ReductionStmtAST"; };
  
  auto getNumVars() const { return VarIds_.size(); }
  const auto & getVarName(unsigned i) const { return VarIds_[i].getName(); }
  const auto & getVarId(unsigned i) const { return VarIds_[i]; }
  const auto & getVarIds() const { return VarIds_; }

  auto isOperator() const { return IsOperator_; }

  auto getOperatorId() const { return Identifier{OperatorName_, OperatorLoc_}; }
  auto getOperatorCode() const { return OperatorCode_; }
  const auto & getOperatorLoc() const { return OperatorLoc_; }
  const auto & getOperatorName() const { return OperatorName_; }

  auto getVarDef(unsigned i) const { return VarDefs_[i]; }
  void setVarDef(unsigned i, VariableDef* Var) { VarDefs_[i] = Var; }
  
  void setOperatorDef(FunctionDef* Fun) { OperatorDef_ = Fun; }
  auto getOperatorDef() const { return OperatorDef_; }
};



//==============================================================================
/// Assignment statement
//==============================================================================
class AssignStmtAST : public StmtAST {
protected:

  ASTBlock LeftExprs_;
  ASTBlock RightExprs_;

  std::map<unsigned, VariableType> CastTypes_;

public:
  AssignStmtAST(
      const LocationRange & Loc,
      std::unique_ptr<NodeAST> LeftExprs,
      std::unique_ptr<NodeAST> RightExprs) :
    StmtAST(Loc)
  {
    if (auto ExprList = dynamic_cast<ExprListAST*>(LeftExprs.get()))
      LeftExprs_ = ExprList->moveExprs();
    else
      LeftExprs_.emplace_back( std::move(LeftExprs) );

    if (auto ExprList = dynamic_cast<ExprListAST*>(RightExprs.get()))
      RightExprs_ = ExprList->moveExprs();
    else
      RightExprs_.emplace_back( std::move(RightExprs) );
  }

  virtual void accept(AstVisiter& visiter) override;
  
  virtual std::string getClassName() const override
  { return "AssignStmtAST"; };

  auto getNumLeftExprs() const { return LeftExprs_.size(); }
  const auto & getLeftExprs() const { return LeftExprs_; }
  auto getLeftExpr(unsigned i) const { return LeftExprs_[i].get(); }
  auto moveLeftExpr(unsigned i) { return std::move(LeftExprs_[i]); }
  auto setLeftExpr(unsigned i, std::unique_ptr<NodeAST> Expr)
  { LeftExprs_[i] = std::move(Expr); }

  auto getNumRightExprs() const { return RightExprs_.size(); }
  const auto & getRightExprs() const { return RightExprs_; }
  auto getRightExpr(unsigned i) const { return RightExprs_[i].get(); }
  auto moveRightExpr(unsigned i) { return std::move(RightExprs_[i]); }
  auto setRightExpr(unsigned i, std::unique_ptr<NodeAST> Expr)
  { RightExprs_[i] = std::move(Expr); }

  void addCast(unsigned i, const VariableType & ToType)
  { CastTypes_[i] = ToType; }

  const VariableType* getCast(unsigned i) const 
  {
    auto it = CastTypes_.find(i);
    if (it == CastTypes_.end()) return nullptr;
    return &it->second;
  }
};

//==============================================================================
/// PrototypeAST - This class represents the "prototype" for a function,
/// which captures its name, and its argument names (thus implicitly the number
/// of arguments the function takes).
//==============================================================================
class PrototypeAST : public NodeAST {
protected:

  Identifier Id_;
  std::vector<Identifier> ReturnTypeIds_;
  bool IsOperator_ = false;
  unsigned Precedence_ = 0;  // Precedence if a binary op.
  
  std::vector<Identifier> ArgIds_;
  std::vector<Identifier> ArgTypeIds_;
  std::vector<bool> ArgIsArray_;
  
  std::vector<VariableType> ArgTypes_;
  VariableType ReturnType_;

  bool IsAnonExpr_ = false;

public:
  
  PrototypeAST(const Identifier & Id) :
    NodeAST(Id.getLoc()), Id_(Id), IsAnonExpr_(true)
  {}

  PrototypeAST(
    const Identifier & Id,
    std::vector<Identifier> && Args,
    std::vector<Identifier> && ArgTypes,
    std::vector<bool> && ArgIsArray,
    std::vector<Identifier> ReturnIds = {},
    bool IsOperator = false,
    unsigned Prec = 0) :
      NodeAST(Id.getLoc()),
      Id_(Id),
      ReturnTypeIds_(std::move(ReturnIds)),
      IsOperator_(IsOperator),
      Precedence_(Prec),
      ArgIds_(std::move(Args)),
      ArgTypeIds_(std::move(ArgTypes)),
      ArgIsArray_(std::move(ArgIsArray))
  {}

  
  virtual void accept(AstVisiter& visiter) override;
  
  virtual std::string getClassName() const override
  { return "PrototypeAST"; };
  
  const std::string &getName() const { return Id_.getName(); }
  const auto & getId() const { return Id_; }

  bool isUnaryOp() const { return IsOperator_ && ArgIds_.size() == 1; }
  bool isBinaryOp() const { return IsOperator_ && ArgIds_.size() == 2; }
  bool isAnonExpr() const { return IsAnonExpr_; } 

  char getOperatorName() const {
    assert(isUnaryOp() || isBinaryOp());
    auto Name = Id_.getName();
    return Name[Name.size() - 1];
  }

  unsigned getBinaryPrecedence() const { return Precedence_; }
  auto getLoc() const { return Id_.getLoc(); }

  const auto & getReturnType() const { return ReturnType_; }
  void setReturnType(const VariableType & ReturnType)
  { ReturnType_ = ReturnType; }

  auto hasReturn() const { return !ReturnTypeIds_.empty(); }
  auto hasMultipleReturn() const { return (ReturnTypeIds_.size()>1); }
  const auto & getReturnTypeIds() const { return ReturnTypeIds_; }

  auto getNumArgs() const { return ArgIds_.size(); } 
  const auto & getArgTypeId(int i) const { return ArgTypeIds_[i]; }
  const auto & getArgId(int i) const { return ArgIds_[i]; }
  const auto & getArgName(int i) const { return ArgIds_[i].getName(); }
  
  auto isArgArray(int i) const { return ArgIsArray_[i]; } 

  const auto & getArgType(int i) { return ArgTypes_[i]; }

  void setArgTypes(const std::vector<VariableType> & ArgTypes)
  { ArgTypes_ = ArgTypes; }
  
};

////////////////////////////////////////////////////////////////////////////////
/// FunctionAST - This class represents a function definition itself.
////////////////////////////////////////////////////////////////////////////////
class FunctionAST : public NodeAST {
protected:

  std::unique_ptr<PrototypeAST> ProtoExpr_;
  ASTBlock BodyExprs_;
  std::unique_ptr<NodeAST> ReturnExpr_;
  bool IsTopExpression_ = false;
  bool IsTask_ = false;
  std::string Name_;
  bool IsLeaf_ = false;

  FunctionDef* FunctionDef_ = nullptr;

public:
  
  FunctionAST(
      const std::string & Name,
      ASTBlock Body,
      bool IsTask = false,
      bool MoveReturn = true) :
    BodyExprs_(std::move(Body)),
    IsTask_(IsTask),
    Name_(Name)
  { if (MoveReturn) checkReturn(); }

  FunctionAST(
      std::unique_ptr<PrototypeAST> Proto,
      ASTBlock Body, 
      std::unique_ptr<NodeAST> Return,
      bool IsTask = false) :
    NodeAST(Proto->getLoc()),
    ProtoExpr_(std::move(Proto)),
    BodyExprs_(std::move(Body)),
    ReturnExpr_(std::move(Return)),
    IsTask_(IsTask),
    Name_(ProtoExpr_->getName())
  { checkReturn(); }

  FunctionAST(
      std::unique_ptr<PrototypeAST> Proto,
      std::unique_ptr<NodeAST> Return) :
    NodeAST(Proto->getLoc()),
    ProtoExpr_(std::move(Proto)),
    ReturnExpr_(std::move(Return)),
    IsTopExpression_(true),
    Name_(ProtoExpr_->getName())
  { checkReturn(); }

  void checkReturn() {
    if (!ReturnExpr_ && BodyExprs_.size()) {
      if (dynamic_cast<ExprAST*>(BodyExprs_.back().get())) {
        ReturnExpr_ = std::move(BodyExprs_.back());
        BodyExprs_.pop_back();
      }
    }
  }

  auto isTopLevelExpression() const { return IsTopExpression_; }
  auto isTask() const { return IsTask_; }
  const std::string &getName() const { return Name_; }
  
  virtual void accept(AstVisiter& visiter) override;

  virtual std::string getClassName() const override
  { return "FunctionAST"; };

  bool hasReturn() const { return static_cast<bool>(ReturnExpr_); }
  auto getReturnExpr() const { return ReturnExpr_.get(); }
  
  auto moveReturnExpr() { return std::move(ReturnExpr_); }
  auto setReturnExpr(std::unique_ptr<NodeAST> Expr) { ReturnExpr_ = std::move(Expr); }
  
  auto getProtoExpr() const { return ProtoExpr_.get(); }
  auto moveProtoExpr() { return std::move(ProtoExpr_); }
  
  auto getNumBodyExprs() const { return BodyExprs_.size(); }
  const auto & getBodyExprs() const { return BodyExprs_; }

  auto getFunctionDef() const { return FunctionDef_; }
  void setFunctionDef(FunctionDef* F) { FunctionDef_ = F; }

  void setLeaf(bool IsLeaf = true) { IsLeaf_ = IsLeaf; }
  bool isLeaf() const { return IsLeaf_; }
};

////////////////////////////////////////////////////////////////////////////////
/// TaskAST - This class represents a function definition itself.
////////////////////////////////////////////////////////////////////////////////
class TaskAST : public FunctionAST {

public:

  TaskAST(
      std::unique_ptr<PrototypeAST> Proto,
      ASTBlock Body, 
      std::unique_ptr<NodeAST> Return) :
    FunctionAST(
        std::move(Proto),
        std::move(Body),
        std::move(Return),
        true)
  {}

  virtual void accept(AstVisiter& visiter) override;

  virtual std::string getClassName() const override
  { return "TaskAST"; };

};

////////////////////////////////////////////////////////////////////////////////
/// TaskAST - This class represents a function definition itself.
////////////////////////////////////////////////////////////////////////////////
class IndexTaskAST : public FunctionAST {

  std::string LoopVarName_;
  std::vector<VariableDef*> Vars_;
  std::vector<ReductionDef> ReductionVars_;

public:

  IndexTaskAST(
      const std::string & Name,
      ASTBlock Body,
      const std::string & LoopVar,
      const std::vector<VariableDef*>& Vars,
      const std::vector<ReductionDef>& ReduceVars = {}) :
    FunctionAST(Name, std::move(Body), true, false),
    LoopVarName_(LoopVar),
    Vars_(Vars),
    ReductionVars_(ReduceVars)
  {}

  virtual void accept(AstVisiter& visiter) override;

  virtual std::string getClassName() const override
  { return "IndexTaskAST"; };
 
  const auto & getVariableDefs() { return Vars_; }
  auto getVariableDef(unsigned i) const { return Vars_[i]; }
 
  bool hasReduction() { return !ReductionVars_.empty(); }
  const auto & getReductionDefs() { return ReductionVars_; }

  const auto & getLoopVariableName() const { return LoopVarName_; }
  const auto & getName() const { return Name_; }
};

} // namespace

#endif // CONTRA_AST_HPP
