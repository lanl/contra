#include "ast.hpp"
#include "analysis.hpp"
#include "token.hpp"

#include "librt/librt.hpp"

#include <memory>
#include <string>

namespace contra {

////////////////////////////////////////////////////////////////////////////////
// Base type interface
////////////////////////////////////////////////////////////////////////////////

//==============================================================================
TypeDef* Analyzer::getType(const std::string & Name, const SourceLocation & Loc)
{
  auto res = Context::instance().getType(Name);
  if (!res)
    THROW_NAME_ERROR("Unknown type specifier '" << Name << "'.", Loc);
  return res.get();
}

//==============================================================================
TypeDef* Analyzer::getType(const Identifier & Id)
{ return getType(Id.getName(), Id.getLoc()); }

  
////////////////////////////////////////////////////////////////////////////////
// Function routines
////////////////////////////////////////////////////////////////////////////////

//==============================================================================
void Analyzer::removeFunction(const std::string & Name)
{ Context::instance().eraseFunction(Name); }

//==============================================================================
FunctionDef* Analyzer::getFunction(
    const std::string & Name,
    const SourceLocation & Loc)
{
  
  // If not, check whether we can codegen the declaration from some existing
  // prototype.
  auto FP = Context::instance().getFunction(Name);
  if (FP) return FP.get();
  
  // see if this is an available intrinsic, try installing it first
  if (auto F = librt::RunTimeLib::tryInstall(Name)) {
    auto res = Context::instance().insertFunction(std::move(F));
    return res.get();
  }
  
  THROW_NAME_ERROR("No valid prototype for '" << Name << "'.", Loc);

  // if found it, make sure its not a variable in scope
  return nullptr;
}

//==============================================================================
FunctionDef* Analyzer::getFunction(const Identifier & Id)
{ return getFunction(Id.getName(), Id.getLoc()); }
  
//==============================================================================
FunctionDef* Analyzer::insertFunction(
    const Identifier & Id,
    const VariableTypeList & ArgTypes,
    const VariableType & RetType)
{ 
  const auto & Name = Id.getName();
  auto Sy = std::make_unique<UserFunction>(Name, Id.getLoc(), RetType, ArgTypes);
  auto res = Context::instance().insertFunction( std::move(Sy) );
  if (!res.isInserted())
    THROW_NAME_ERROR("Prototype already exists for '" << Name << "'.",
      Id.getLoc());
  return res.get();
}

////////////////////////////////////////////////////////////////////////////////
// Variable interface
////////////////////////////////////////////////////////////////////////////////


//==============================================================================
VariableDef* Analyzer::getVariable(const std::string & Name, const SourceLocation & Loc)
{
  auto res = Context::instance().getVariable(Name);
  if (!res)
    THROW_NAME_ERROR("Variable '" << Name << "' has not been"
       << " previously defined", Loc);
  return res.get();
}

//==============================================================================
VariableDef* Analyzer::getVariable(const Identifier & Id)
{ return getVariable(Id.getName(), Id.getLoc()); }

//==============================================================================
VariableDef*
Analyzer::insertVariable(const Identifier & Id, const VariableType & VarType)
{
  const auto & Name = Id.getName();
  const auto & Loc = Id.getLoc();
  auto S = std::make_unique<VariableDef>(Name, Loc, VarType);
  auto res = Context::instance().insertVariable( std::move(S) );
  if (!res.isInserted())
    THROW_NAME_ERROR("Variable '" << Name << "' has been"
        << " previously defined", Loc);
  return res.get();
}

////////////////////////////////////////////////////////////////////////////////
// type checking interface
////////////////////////////////////////////////////////////////////////////////

//==============================================================================
void Analyzer::checkIsCastable(
    const VariableType & FromType,
    const VariableType & ToType,
    const SourceLocation & Loc)
{
  auto IsCastable = FromType.isCastableTo(ToType);
  if (!IsCastable) {
    abort();
    THROW_NAME_ERROR("Cannot cast from type '" << FromType << "' to type '"
        << ToType << "'.", Loc);
  }
}
  
//==============================================================================
void Analyzer::checkIsAssignable(
    const VariableType & LeftType,
    const VariableType & RightType,
    const SourceLocation & Loc)
{
  auto IsAssignable = RightType.isAssignableTo(LeftType);
  if (!IsAssignable)
    THROW_NAME_ERROR("A variable of type '" << RightType << "' cannot be"
         << " assigned to a variable of type '" << LeftType << "'." , Loc);
}

//==============================================================================
std::unique_ptr<CastExprAST>
Analyzer::insertCastOp(
    std::unique_ptr<NodeAST> FromExpr,
    const VariableType & ToType )
{
  auto Loc = FromExpr->getLoc();
  auto E = std::make_unique<CastExprAST>(Loc, std::move(FromExpr), ToType);
  return E;
}

//==============================================================================
VariableType
Analyzer::promote(
    const VariableType & LeftType,
    const VariableType & RightType,
    const SourceLocation & Loc)
{
  if (LeftType == RightType) return LeftType;

  if (LeftType.isNumber() && RightType.isNumber()) {
    if (LeftType == F64Type_ || RightType == F64Type_)
      return F64Type_;
    else
      return LeftType;
  }
  
  THROW_NAME_ERROR("No promotion rules between the type '" << LeftType
       << " and the type '" << RightType << "'." , Loc);

  return {};
}

////////////////////////////////////////////////////////////////////////////////
// Visitors
////////////////////////////////////////////////////////////////////////////////

//==============================================================================
void Analyzer::visit(ValueExprAST& e)
{
  switch (e.getValueType()) {
  case ValueExprAST::ValueType::Int:
    TypeResult_ = I64Type_;
    break;
  case ValueExprAST::ValueType::Real:
    TypeResult_ = F64Type_;
    break;
  case ValueExprAST::ValueType::String:
    TypeResult_ = StrType_;
    break;
  }
  e.setType(TypeResult_);
  e.setParentFunctionDef(ParentFunction_);
}


//==============================================================================
void Analyzer::visit(VarAccessExprAST& e)
{
  const auto & Name = e.getName();
  auto VarDef = getVariable(Name, e.getLoc());
  auto VarType = VarDef->getType();

  // result
  TypeResult_ = VarType;
  e.setType(TypeResult_);
  e.setParentFunctionDef(ParentFunction_);
  e.setVariableDef(VarDef);
}

//==============================================================================
void Analyzer::visit(ArrayAccessExprAST& e)
{
  const auto & Name = e.getName();
  auto VarDef = getVariable(Name, e.getLoc());
  auto VarType = VarDef->getType();

  // array index
  auto Loc = e.getIndexExpr()->getLoc();
  
  if (!VarType.isArray())
    THROW_NAME_ERROR( "Cannot index scalar using '[]' operator", Loc);
  
  auto IndexType = runExprVisitor(*e.getIndexExpr());
  if (IndexType != I64Type_)
    THROW_NAME_ERROR( "Array index for variable '" << Name << "' must "
        << "evaluate to an integer.", Loc );

  VarType.setArray(false); // revert to scalar

  // result
  TypeResult_ = VarType;
  e.setType(TypeResult_);
  e.setVariableDef(VarDef);
  e.setParentFunctionDef(ParentFunction_);
}

//==============================================================================
void Analyzer::visit(ArrayExprAST& e)
{
  if (e.hasSize()) {
    auto SizeType = runExprVisitor(*e.getSizeExpr());
    if (SizeType != I64Type_)
      THROW_NAME_ERROR( "Size expression for arrays must be an integer.",
          e.getSizeExpr()->getLoc());
  }

  int NumVals = e.getNumVals();
  
  VariableTypeList ValTypes;
  ValTypes.reserve(NumVals);
  
  VariableType CommonType;

  for (int i=0; i<NumVals; ++i) {
    auto & ValExpr = *e.getValExpr(i);
    auto ValType = runExprVisitor(ValExpr);
    if (i==0) CommonType = ValType;
    else      CommonType = promote(ValType, CommonType, ValExpr.getLoc());
    ValTypes.emplace_back(ValType);
  }

  if (DestinationType_) {
    CommonType = DestinationType_;
    CommonType.setArray(false);
  }

  for (int i=0; i<NumVals; ++i) {
    const auto & ValType = ValTypes[i];
    if (CommonType != ValType) {
      auto Loc = e.getValExpr(i)->getLoc();
      checkIsCastable(ValType, CommonType, Loc);
      e.setValExpr(i, insertCastOp(std::move(e.moveValExpr(i)), CommonType) );
    }
  }

  CommonType.setArray();
  TypeResult_ = CommonType;
  e.setType(TypeResult_);
  e.setParentFunctionDef(ParentFunction_);
}

//==============================================================================
void Analyzer::visit(CastExprAST& e)
{
  auto FromType = runExprVisitor(*e.getFromExpr());
  auto TypeId = e.getTypeId();
  auto ToType = VariableType(getType(TypeId));
  checkIsCastable(FromType, ToType, e.getLoc());
  TypeResult_ = VariableType(ToType);
  e.setType(TypeResult_);
  e.setParentFunctionDef(ParentFunction_);
}

//==============================================================================
void Analyzer::visit(UnaryExprAST& e)
{
  auto OpCode = e.getOperand();
  auto OpType = runExprVisitor(*e.getOpExpr());
  auto Loc = e.getLoc();

  if (OpType.isArray())
      THROW_NAME_ERROR( "Unary operation '" << OpCode << "' "
          << "not allowed for array expressions.", Loc );

  if (!OpType.isNumber())
      THROW_NAME_ERROR( "Unary operators only allowed for scalar numeric "
          << "expressions. Expression is of type '" << OpType << "'.", Loc );


  switch (OpCode) {
  default:
    THROW_NAME_ERROR( "Unknown unary operator '" << OpCode << "'", Loc);
  case tok_sub:
  case tok_add:
    TypeResult_ = OpType;
  };
  
  e.setType(TypeResult_);
  e.setParentFunctionDef(ParentFunction_);
}

//==============================================================================
void Analyzer::visit(BinaryExprAST& e)
{
  auto Loc = e.getLoc();
  auto OpCode = e.getOperand();

  auto RightLoc = e.getRightExpr()->getLoc();
  auto LeftLoc = e.getLeftExpr()->getLoc();
  
  auto RightType = runExprVisitor(*e.getRightExpr());
  auto LeftType = runExprVisitor(*e.getLeftExpr());

  if ( !LeftType.isNumber() || !RightType.isNumber())
      THROW_NAME_ERROR( "Binary operators only allowed for scalar numeric "
          << "expressions.", Loc );
  
  auto CommonType = LeftType;
  if (RightType != LeftType) {
    checkIsCastable(RightType, LeftType, RightLoc);
    checkIsCastable(LeftType, RightType, LeftLoc);
    CommonType = promote(LeftType, RightType, Loc);
    if (RightType != CommonType)
      e.setRightExpr( insertCastOp(std::move(e.moveRightExpr()), CommonType ) );
    else
      e.setLeftExpr( insertCastOp(std::move(e.moveLeftExpr()), CommonType ) );
  }

  switch (OpCode) {
  case tok_add:
  case tok_sub:
  case tok_mul:
  case tok_div:
  case tok_mod:
    TypeResult_ = CommonType;
    e.setType(TypeResult_);
    return;
  case tok_eq:
  case tok_ne:
  case tok_lt:
  case tok_le:
  case tok_gt:
  case tok_ge:
    TypeResult_ = BoolType_;
    e.setType(TypeResult_);
    return;
  } 
  
  // If it wasn't a builtin binary operator, it must be a user defined one. Emit
  // a call to it.
  auto F = getFunction(std::string("binary") + OpCode, Loc);
  TypeResult_ = F->getReturnType();
  e.setType(TypeResult_);
  e.setParentFunctionDef(ParentFunction_);
}

//==============================================================================
void Analyzer::visit(CallExprAST& e)
{
  const auto & FunName = e.getName();
  auto FunRes = getFunction(FunName, e.getLoc());
  
  int NumArgs = e.getNumArgs();
  int NumFixedArgs = FunRes->getNumArgs();

  auto IsTask = FunRes->isTask();

  if (IsTask && isGlobalScope()) {
    if (HaveTopLevelTask_)  
      THROW_NAME_ERROR("You are not allowed to have more than one top-level task.",
          e.getLoc());
    if (NumArgs > 0)
      THROW_NAME_ERROR("You are not allowed to pass arguments to the top-level task.",
          e.getLoc());
    HaveTopLevelTask_ = true;
    e.setTopLevelTask();
  }


  if (FunRes->isVarArg()) {
    if (NumArgs < NumFixedArgs)
      THROW_NAME_ERROR("Variadic function '" << FunName
          << "', must have at least " << NumFixedArgs << " arguments, but only "
          << NumArgs << " provided.", e.getLoc());
  }
  else {
    if (NumFixedArgs != NumArgs)
      THROW_NAME_ERROR("Incorrect number of arguments specified for '" << FunName
          << "', " << NumArgs << " provided but expected " << NumFixedArgs, e.getLoc());
  }
  
  std::vector<VariableType> ArgTypes;
  ArgTypes.reserve(NumArgs);

  for (int i=0; i<NumArgs; ++i) {
    auto ArgExpr = e.getArgExpr(i);
    auto ArgType = runExprVisitor(*ArgExpr);

    if (i<NumFixedArgs) {
      auto ParamType = FunRes->getArgType(i);
      if (ArgType != ParamType) {
        checkIsCastable(ArgType, ParamType, ArgExpr->getLoc());
        e.setArgExpr(i, insertCastOp( std::move(e.moveArgExpr(i)), ParamType) );
      }
    }

    ArgTypes.emplace_back(ArgType);
  } // args

  TypeResult_ = FunRes->getReturnType(); 
  TypeResult_.setFuture(IsTask && TypeResult_!=VoidType_);

  e.setArgTypes( ArgTypes );
  e.setType(TypeResult_);
  e.setParentFunctionDef(ParentFunction_);
}

//==============================================================================
void Analyzer::visit(ForStmtAST& e)
{
  auto VarId = e.getVarId();
  
  createScope();

  insertVariable(VarId, I64Type_);

  auto StartType = runStmtVisitor(*e.getStartExpr());
  if (StartType != I64Type_ )
    THROW_NAME_ERROR( "For loop start expression must result in an integer type.",
        e.getStartExpr()->getLoc() );

  auto EndType = runStmtVisitor(*e.getEndExpr());
  if (EndType != I64Type_ )
    THROW_NAME_ERROR( "For loop end expression must result in an integer type.",
        e.getEndExpr()->getLoc() );

  if (e.hasStep()) {
    auto StepType = runStmtVisitor(*e.getStepExpr());
    if (StepType != I64Type_ )
      THROW_NAME_ERROR( "For loop step expression must result in an integer type.",
          e.getStepExpr()->getLoc() );
  }

  for ( const auto & stmt : e.getBodyExprs() ) runStmtVisitor(*stmt);

  popScope();
  TypeResult_ = VoidType_;
  e.setParentFunctionDef(ParentFunction_);
}

//==============================================================================
void Analyzer::visit(ForeachStmtAST& e)
{
  auto VarId = e.getVarId();
  
  createScope();

  insertVariable(VarId, I64Type_);

  auto StartType = runStmtVisitor(*e.getStartExpr());
  if (StartType != I64Type_ )
    THROW_NAME_ERROR( "For loop start expression must result in an integer type.",
        e.getStartExpr()->getLoc() );

  auto EndType = runStmtVisitor(*e.getEndExpr());
  if (EndType != I64Type_ )
    THROW_NAME_ERROR( "For loop end expression must result in an integer type.",
        e.getEndExpr()->getLoc() );

  if (e.hasStep()) {
    auto StepType = runStmtVisitor(*e.getStepExpr());
    if (StepType != I64Type_ )
      THROW_NAME_ERROR( "For loop step expression must result in an integer type.",
          e.getStepExpr()->getLoc() );
  }

  for ( const auto & stmt : e.getBodyExprs() ) runStmtVisitor(*stmt);

      
  auto AccessedVars = Context::instance().getAccessedVariables();
  e.addAccessedVariables(AccessedVars);


  popScope();
  TypeResult_ = VoidType_;
  e.setParentFunctionDef(ParentFunction_);
}

//==============================================================================
void Analyzer::visit(IfStmtAST& e)
{
  auto CondType = runExprVisitor(*e.getCondExpr());
  if (CondType != BoolType_ )
    THROW_NAME_ERROR( "If condition must result in boolean type.", e.getCondExpr()->getLoc() );

  createScope();
  for ( const auto & stmt : e.getThenExprs() ) runStmtVisitor(*stmt);
  for ( const auto & stmt : e.getElseExprs() ) runStmtVisitor(*stmt);
  popScope();

  TypeResult_ = VoidType_;
  e.setParentFunctionDef(ParentFunction_);
}

//==============================================================================
void Analyzer::visit(AssignStmtAST& e)
{
  auto Loc = e.getLoc();
  auto LeftLoc = e.getLeftExpr()->getLoc();
  
  auto LeftType = runExprVisitor(*e.getLeftExpr());
  DestinationType_ = LeftType;
  
  auto RightType = runExprVisitor(*e.getRightExpr());

  // Assignment requires the LHS to be an identifier.
  // This assume we're building without RTTI because LLVM builds that way by
  // default.  If you build LLVM with RTTI this can be changed to a
  // dynamic_cast for automatic error checking.
  auto LHSE = dynamic_cast<VarAccessExprAST*>(e.getLeftExpr());
  if (!LHSE)
    THROW_NAME_ERROR("destination of '=' must be a variable", LeftLoc);

  auto Name = LHSE->getName();
  
  checkIsAssignable( LeftType, RightType, Loc );

  if (RightType.getBaseType() != LeftType.getBaseType()) {
    checkIsCastable(RightType, LeftType, Loc);
    e.setRightExpr( insertCastOp(std::move(e.moveRightExpr()), LeftType) );
  }
  
  TypeResult_ = LeftType;
  e.setParentFunctionDef(ParentFunction_);
}

//==============================================================================
void Analyzer::visit(VarDeclAST& e)
{
  // check if there is a specified type, if there is, get it
  auto TypeId = e.getTypeId();
  VariableType VarType;
  if (TypeId) {
    VarType = VariableType(getType(TypeId), e.isArray());
    DestinationType_ = VarType;
  }
  
  auto InitType = runExprVisitor(*e.getInitExpr());
  if (!VarType) {
    VarType = InitType;
    e.setArray(InitType.isArray());
  }

  //----------------------------------------------------------------------------
  // Scalar variable
  if (!e.isArray()) {

    if (VarType != InitType) {
      checkIsCastable(InitType, VarType, e.getInitExpr()->getLoc());
      e.setInitExpr( insertCastOp(std::move(e.moveInitExpr()), VarType) );
    }
    else {
      VarType.setFuture(InitType.isFuture());
    }

  }
  //----------------------------------------------------------------------------
  // Array Variable
  else {

    // Array already on right hand side
    if (InitType.isArray()) {
    }
    //  scalar on right hand side
    else {
    
      auto ElementType = VariableType(VarType, false);
      if (ElementType != InitType) {
        checkIsCastable(InitType, ElementType, e.getInitExpr()->getLoc());
        e.setInitExpr( insertCastOp(std::move(e.moveInitExpr()), ElementType) );
      }
 
      if (e.hasSize()) {
        auto SizeType = runExprVisitor(*e.getSizeExpr());
        if (SizeType != I64Type_)
          THROW_NAME_ERROR( "Size expression for arrays must be an integer.",
             e.getSizeExpr()->getLoc());
      }

    } // scalar init

  }
  // End
  //----------------------------------------------------------------------------
  
  if (isGlobalScope()) VarType.setGlobal();

  auto NumVars = e.getNumVars();
  for (unsigned i=0; i<NumVars; ++i) {
    auto VarId = e.getVarId(i);
    auto VarDef = insertVariable(VarId, VarType);
    e.setVariableDef(i, VarDef);
  }

  TypeResult_ = VarType;
  e.setType(TypeResult_);
  e.setParentFunctionDef(ParentFunction_);
}

//==============================================================================
void Analyzer::visit(PrototypeAST& e)
{
  int NumArgs = e.getNumArgs();

  std::vector<VariableType> ArgTypes;
  ArgTypes.reserve( NumArgs );
  
  for (int i=0; i<NumArgs; ++i) {
    // check type specifier
    const auto & TypeId = e.getArgTypeId(i);
    auto ArgType = VariableType( getType(TypeId), e.isArgArray(i) );
    ArgTypes.emplace_back(std::move(ArgType));
  }

  e.setArgTypes(ArgTypes);

  auto RetType = VoidType_;
  if (e.hasReturn())
    RetType = VariableType( getType(e.getReturnTypeId()) );
  e.setReturnType(RetType);

  insertFunction(e.getId(), ArgTypes, RetType);

}

//==============================================================================
void Analyzer::visit(FunctionAST& e)
{
  bool CreatedScope = false;
  if (!e.isTopLevelExpression()) {
    CreatedScope = true;
    createScope();
  }

  auto & ProtoExpr = *e.getProtoExpr();
  const auto & FnId = ProtoExpr.getId();
  auto FnName = FnId.getName();
  auto Loc = FnId.getLoc();

  runProtoVisitor(ProtoExpr);
  auto FunDef = getFunction(FnId);
  if (!FunDef)  
    THROW_NAME_ERROR("No valid prototype for function '" << FnName << "'", Loc);

  ParentFunction_ = FunDef;
  e.setFunctionDef(FunDef);

  auto NumArgIds = ProtoExpr.getNumArgs();
  const auto & ArgTypes = FunDef->getArgTypes();
  auto NumArgs = ArgTypes.size();
  
  if (NumArgs != NumArgIds)
    THROW_NAME_ERROR("Numer of arguments in prototype for function '" << FnName
        << "', does not match definition.  Expected " << NumArgIds
        << " but got " << NumArgs, Loc);
 
  if (e.isTask()) FunDef->setTask();

  // If this is an operator, install it.
  if (ProtoExpr.isBinaryOp())
    BinopPrecedence_->operator[](ProtoExpr.getOperatorName()) = ProtoExpr.getBinaryPrecedence();

  // Record the function arguments in the NamedValues map.
  for (unsigned i=0; i<NumArgs; ++i)
    insertVariable(ProtoExpr.getArgId(i), ArgTypes[i]);
  
  for ( const auto & B : e.getBodyExprs() ) runStmtVisitor(*B);
  
  if (e.getReturnExpr()) {
    auto RetType = runExprVisitor(*e.getReturnExpr());
    if (ProtoExpr.isAnonExpr())
      ProtoExpr.setReturnType(RetType);
    else if (RetType != FunDef->getReturnType())
      THROW_NAME_ERROR("Function return type does not match prototype for '"
          << FnName << "'.  The type '" << RetType << "' cannot be "
          << "converted to the type '" << FunDef->getReturnType() << "'.",
          e.getReturnExpr()->getLoc());
  }
  
  if (CreatedScope) popScope();
  
}

//==============================================================================
void Analyzer::visit(TaskAST& e)
{
  visit( static_cast<FunctionAST&>(e) );
}

//==============================================================================
void Analyzer::visit(IndexTaskAST& e)
{}

}
