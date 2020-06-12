#include "config.hpp"

#include "ast.hpp"
#include "context.hpp"
#include "codegen.hpp"
#include "errors.hpp"
#include "legion.hpp"
#include "precedence.hpp"
#include "token.hpp"
#include "variable.hpp"

#include "librt/librt.hpp"
#include "librt/dopevector.hpp"

#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/LinkAllPasses.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"

#include "llvm/ExecutionEngine/Orc/IndirectionUtils.h"

extern "C" {

} // extern

using namespace llvm;
using namespace utils;

namespace contra {

////////////////////////////////////////////////////////////////////////////////
// Constructor / Destructor
////////////////////////////////////////////////////////////////////////////////

//==============================================================================
// Constructor
//==============================================================================
CodeGen::CodeGen (bool debug = false) :
  TheContext_(TheHelper_.getContext()),
  Builder_(TheHelper_.getBuilder())
{
  std::vector<std::string> Args = {
    "./contra",
    "-ll:gsize", "0",
    "-ll:csize", "2048",
    //"-ll:cpu", "2",
    //"-lg:prof", "1",
    //"-lg:prof_logfile", "prof_%.gz"
  };

  Argc_ = Args.size();
  Argv_ = new char *[Argc_];


  for ( unsigned i=0; i<Args.size(); ++i ) {
    auto len = Args[i].size();
    Argv_[i] = new char[len+1];
    strcpy(Argv_[i], Args[i].data());
  }

  librt::RunTimeLib::setup(TheContext_);
  
  Tasker_ = std::make_unique<LegionTasker>(TheHelper_);

  I64Type_  = llvmType<int_t>(TheContext_);
  F64Type_  = llvmType<real_t>(TheContext_);
  VoidType_ = Type::getVoidTy(TheContext_);
  ArrayType_ = librt::DopeVector::DopeVectorType;
  AccessorType_ = Tasker_->getAccessorType();

  auto & C = Context::instance();
  TypeTable_.emplace( C.getInt64Type()->getName(),  I64Type_);
  TypeTable_.emplace( C.getFloat64Type()->getName(),  F64Type_);
  TypeTable_.emplace( C.getVoidType()->getName(), VoidType_);

  VariableTable_.push_front({});

  initializeModuleAndPassManager();

}
  
//==============================================================================
// Destructor
//==============================================================================
CodeGen::~CodeGen() {
  // delete arguments
  for (int i=0; i<Argc_; ++i) delete[] Argv_[i];
  delete[] Argv_;
}


////////////////////////////////////////////////////////////////////////////////
// Optimization / Module interface
////////////////////////////////////////////////////////////////////////////////

//==============================================================================
// Initialize module and optimizer
//==============================================================================
void CodeGen::initializeModuleAndPassManager()
{
  initializeModule();
  if (!isDebug())
    initializePassManager();
}

//==============================================================================
// Initialize module
//==============================================================================
void CodeGen::initializeModule()
{
  // Open a new module.
  TheModule_ = std::make_unique<Module>("my cool jit", TheContext_);
  TheModule_->setDataLayout(TheJIT_.getTargetMachine().createDataLayout());
}

//==============================================================================
// Initialize optimizer
//==============================================================================
void CodeGen::initializePassManager() {

  // Create a new pass manager attached to it.
  TheFPM_ = std::make_unique<legacy::FunctionPassManager>(TheModule_.get());

  // Promote allocas to registers.
  TheFPM_->add(createPromoteMemoryToRegisterPass());
  // Do simple "peephole" optimizations and bit-twiddling optzns.
  TheFPM_->add(createInstructionCombiningPass());
  // Reassociate expressions.
  TheFPM_->add(createReassociatePass());
  // Eliminate Common SubExpressions.
  TheFPM_->add(createGVNPass());
  // Simplify the control flow graph (deleting unreachable blocks, etc).
  TheFPM_->add(createCFGSimplificationPass());
  TheFPM_->doInitialization();
}

////////////////////////////////////////////////////////////////////////////////
// JIT interface
////////////////////////////////////////////////////////////////////////////////

//==============================================================================
// JIT the current module
//==============================================================================
JIT::VModuleKey CodeGen::doJIT()
{
  auto TmpModule = std::move(TheModule_);
  initializeModuleAndPassManager();
  auto H = TheJIT_.addModule(std::move(TmpModule));
  return H;
}

//==============================================================================
// Search the JIT for a symbol
//==============================================================================
JIT::JITSymbol CodeGen::findSymbol( const char * Symbol )
{ return TheJIT_.findSymbol(Symbol); }

//==============================================================================
// Delete a JITed module
//==============================================================================
void CodeGen::removeJIT( JIT::VModuleKey H )
{ TheJIT_.removeModule(H); }


////////////////////////////////////////////////////////////////////////////////
// Scope Interface
////////////////////////////////////////////////////////////////////////////////  
void CodeGen::popScope()
{
  std::vector<Value*> Arrays;
  std::vector<Value*> Futures;
  std::vector<Value*> Fields;
  std::vector<Value*> Ranges;
  std::vector<Value*> Accessors;
  std::vector<Value*> Partitions;

  for ( const auto & entry_pair : VariableTable_.front() ) {
    auto VarE = entry_pair.second;
    auto Alloca = VarE.getAlloca();
    if (!VarE.isOwner()) continue;
    if (isArray(Alloca)) 
      Arrays.emplace_back(Alloca);
    else if (Tasker_->isFuture(Alloca))
      Futures.emplace_back(Alloca);
    else if (Tasker_->isField(Alloca))
      Fields.emplace_back(Alloca);
    else if (Tasker_->isRange(Alloca))
      Ranges.emplace_back(Alloca);
    else if (Tasker_->isAccessor(Alloca))
      Accessors.emplace_back(Alloca);
    else if (Tasker_->isPartition(Alloca))
      Partitions.emplace_back(Alloca);
  }
  VariableTable_.pop_front();

  destroyArrays(Arrays); 
  Tasker_->destroyFutures(*TheModule_, Futures);
  Tasker_->destroyFields(*TheModule_, Fields);
  Tasker_->destroyAccessors(*TheModule_, Accessors); 
  Tasker_->destroyPartitions(*TheModule_, Partitions);
  Tasker_->destroyRanges(*TheModule_, Ranges);
}

////////////////////////////////////////////////////////////////////////////////
// Variable Interface
////////////////////////////////////////////////////////////////////////////////

//==============================================================================
// Get a variable
//==============================================================================
VariableAlloca * CodeGen::getVariable(const std::string & VarName)
{ 
  for ( auto & ST : VariableTable_ ) {
    auto it = ST.find(VarName);
    if (it != ST.end()) return &it->second;
  }
  return nullptr;
}

//==============================================================================
/// CreateEntryBlockAlloca - Create an alloca instruction in the entry block of
/// the function.  This is used for mutable variables etc.
//==============================================================================
VariableAlloca * CodeGen::createVariable(
    Function *TheFunction,
    const std::string &VarName,
    Type* VarType)
{
  Value* NewVar;
  NewVar = TheHelper_.createEntryBlockAlloca(VarType, VarName);
  return insertVariable(VarName, VariableAlloca{NewVar, VarType});
}
        
//==============================================================================
/// CreateEntryBlockAlloca - Create an alloca instruction in the entry block of
/// the function.  This is used for mutable variables etc.
//==============================================================================
std::pair<VariableAlloca*, bool> CodeGen::getOrCreateVariable(
    const std::string &VarName,
    const VariableType & VarType)
{
  for ( auto & ST : VariableTable_ ) {
    auto it = ST.find(VarName);
    if (it != ST.end()) return {&it->second, false};
  }

  AllocaInst* NewVar = nullptr;
  Type* VarT = nullptr;

  if (VarType.isArray()) {
    VarT = getLLVMType(VarType.getIndexedType());
    NewVar = TheHelper_.createEntryBlockAlloca(ArrayType_, VarName);
  }
  else {
    VarT = getLLVMType(VarType);
    NewVar = TheHelper_.createEntryBlockAlloca(VarT, VarName);
  }
  
  auto VarE = insertVariable(VarName, VariableAlloca{NewVar, VarT});
  
  return {VarE, true};
}
        
//==============================================================================
/// Insert an already allocated variable
//==============================================================================
VariableAlloca * CodeGen::insertVariable(
    const std::string &VarName,
    VariableAlloca VarE)
{ 
  auto it = VariableTable_.front().emplace(VarName, VarE);
  return &it.first->second;
}

//==============================================================================
/// Insert an already allocated variable
//==============================================================================
VariableAlloca * CodeGen::insertVariable(
    const std::string &VarName,
    Value* VarAlloca,
    Type* VarType)
{ 
  VariableAlloca VarE(VarAlloca, VarType);
  return insertVariable(VarName, VarE);
}

////////////////////////////////////////////////////////////////////////////////
// Array Interface
////////////////////////////////////////////////////////////////////////////////

//==============================================================================
///  Is the variable an array
//==============================================================================
bool CodeGen::isArray(Type* Ty)
{ return ArrayType_ == Ty; }

bool CodeGen::isArray(Value* V)
{
  auto Ty = V->getType();
  if (isa<AllocaInst>(V)) Ty = Ty->getPointerElementType();
  return isArray(Ty);
}

//==============================================================================
/// CreateEntryBlockAlloca - Create an alloca instruction in the entry block of
/// the function.  This is used for mutable variables etc.
//==============================================================================
VariableAlloca * CodeGen::createArray(
    const std::string &VarName,
    Type* ElementT)
{
  Value* NewVar;
  NewVar = TheHelper_.createEntryBlockAlloca(ArrayType_, VarName);
  auto it = VariableTable_.front().emplace(VarName, VariableAlloca{NewVar, ElementT});
  return &it.first->second;
}


//==============================================================================
/// CreateEntryBlockAlloca - Create an alloca instruction in the entry block of
/// the function.  This is used for mutable variables etc.
//==============================================================================
VariableAlloca * CodeGen::createArray(
    const std::string &VarName,
    Type * ElementType,
    Value * SizeExpr)
{

  Function *F; 
  F = TheModule_->getFunction("allocate");
  if (!F) F = librt::RunTimeLib::tryInstall(TheContext_, *TheModule_, "allocate");


  SizeExpr = TheHelper_.getAsValue(SizeExpr);
  auto DataSize = TheHelper_.getTypeSize<int_t>(ElementType);
  auto ArrayA = TheHelper_.createEntryBlockAlloca(ArrayType_, VarName+"vec");

  std::vector<Value*> ArgVs = {SizeExpr, DataSize, ArrayA};
  Builder_.CreateCall(F, ArgVs);
  
  return insertVariable(VarName, {ArrayA, ElementType, SizeExpr});
}

void CodeGen::createArray(
    Value* ArrayA,
    Value * SizeV,
    Type * ElementT)
{
  Function *F; 
  F = TheModule_->getFunction("allocate");
  if (!F) F = librt::RunTimeLib::tryInstall(TheContext_, *TheModule_, "allocate");


  auto DataSize = TheHelper_.getTypeSize<int_t>(ElementT);
  std::vector<Value*> ArgVs = {SizeV, DataSize, ArrayA};
  Builder_.CreateCall(F, ArgVs);
}
 
 
//==============================================================================
// Initialize Array
//==============================================================================
void CodeGen::initArrays(
    Function *TheFunction,
    const std::vector<Value*> & ArrayAs,
    Value * InitV,
    Value * SizeV,
    Type * ElementT)
{
  
  // create allocas to the pointers
  auto ArrayPtrAs = createArrayPointerAllocas(ArrayAs, ElementT);

  // create a loop
  auto Alloca = TheHelper_.createEntryBlockAlloca(llvmType<int_t>(TheContext_), "__i");
  Value * StartVal = llvmValue<int_t>(TheContext_, 0);
  Builder_.CreateStore(StartVal, Alloca);
  
  auto BeforeBB = BasicBlock::Create(TheContext_, "beforeinit", TheFunction);
  auto LoopBB =   BasicBlock::Create(TheContext_, "init", TheFunction);
  auto AfterBB =  BasicBlock::Create(TheContext_, "afterinit", TheFunction);
  Builder_.CreateBr(BeforeBB);
  Builder_.SetInsertPoint(BeforeBB);
  auto CurVar = TheHelper_.load(Alloca);
  SizeV = TheHelper_.getAsValue(SizeV);
  auto EndCond = Builder_.CreateICmpSLT(CurVar, SizeV, "initcond");
  Builder_.CreateCondBr(EndCond, LoopBB, AfterBB);
  Builder_.SetInsertPoint(LoopBB);

  // store value
  InitV = TheHelper_.getAsValue(InitV);
  for ( auto ArrayPtrA : ArrayPtrAs)
    insertArrayValue(ArrayPtrA, ElementT, CurVar, InitV);

  // increment loop
  auto StepVal = llvmValue<int_t>(TheContext_, 1);
  TheHelper_.increment(Alloca, StepVal);
  Builder_.CreateBr(BeforeBB);
  Builder_.SetInsertPoint(AfterBB);
}
  
//==============================================================================
// Initialize an arrays individual values
//==============================================================================
void CodeGen::initArray(
    Function *TheFunction, 
    Value* ArrayA,
    const std::vector<Value *> InitVs,
    Type* ElementT )
{
  auto NumVals = InitVs.size();
  
  // create allocas to the pointers
  auto ArrayPtrA = createArrayPointerAlloca(ArrayA, ElementT);
  for (std::size_t i=0; i<NumVals; ++i) {
    auto IndexV = llvmValue<int_t>(TheContext_, i);
    insertArrayValue(ArrayPtrA, ElementT, IndexV, InitVs[i]);
  }
}

//==============================================================================
// Copy Array
//==============================================================================
void CodeGen::copyArrays(
    Function *TheFunction,
    Value* SrcArrayA,
    const std::vector<Value*> TgtArrayAs,
    Value * NumElements,
    Type * ElementT)
{
  // create allocas to the pointers
  auto SrcArrayPtrA = createArrayPointerAlloca(SrcArrayA, ElementT);
  auto TgtArrayPtrAs = createArrayPointerAllocas(TgtArrayAs, ElementT);

  auto CounterA = TheHelper_.createEntryBlockAlloca(llvmType<int_t>(TheContext_), "__i");
  Value * StartV = llvmValue<int_t>(TheContext_, 0);
  Builder_.CreateStore(StartV, CounterA);
  
  auto BeforeBB = BasicBlock::Create(TheContext_, "beforeinit", TheFunction);
  auto LoopBB =   BasicBlock::Create(TheContext_, "init", TheFunction);
  auto AfterBB =  BasicBlock::Create(TheContext_, "afterinit", TheFunction);
  Builder_.CreateBr(BeforeBB);
  Builder_.SetInsertPoint(BeforeBB);
  auto CounterV = Builder_.CreateLoad(llvmType<int_t>(TheContext_), CounterA);
  auto EndCond = Builder_.CreateICmpSLT(CounterV, NumElements, "initcond");
  Builder_.CreateCondBr(EndCond, LoopBB, AfterBB);
  Builder_.SetInsertPoint(LoopBB);
    
  auto SrcV = extractArrayValue(SrcArrayPtrA, ElementT, CounterV);

  for ( auto TgtArrayPtrA :  TgtArrayPtrAs )
    insertArrayValue( TgtArrayPtrA, ElementT, CounterV, SrcV);

  auto StepVal = llvmValue<int_t>(TheContext_, 1);
  TheHelper_.increment(CounterA, StepVal);
  Builder_.CreateBr(BeforeBB);
  Builder_.SetInsertPoint(AfterBB);
}

//==============================================================================
// Copy Array
//==============================================================================
void CodeGen::copyArray(Value* SrcArrayV, Value* TgtArrayA)
{
  auto F = TheModule_->getFunction("copy");
  if (!F) F = librt::RunTimeLib::tryInstall(TheContext_, *TheModule_, "copy");

  auto SrcArrayA = TheHelper_.getAsAlloca(SrcArrayV);

  std::vector<Value*> Args = {SrcArrayA, TgtArrayA};
  Builder_.CreateCall(F, Args);
}

//==============================================================================
// Load an arrayarray into an alloca
//==============================================================================
Value* CodeGen::createArrayPointerAlloca(
    Value* ArrayA,
    Type* ElementT )
{
  auto ElementPtrT = ElementT->getPointerTo();
  auto ArrayV = getArrayPointer(ArrayA, ElementT);
  auto ArrayPtrA = TheHelper_.createEntryBlockAlloca(ElementPtrT);
  Builder_.CreateStore(ArrayV, ArrayPtrA);
  return ArrayPtrA;
}

  
//==============================================================================
// Load a bunch of arrays into allocas
//==============================================================================
std::vector<Value*> CodeGen::createArrayPointerAllocas(
    const std::vector<Value*> & ArrayAs,
    Type* ElementT )
{
  std::vector<Value*> ArrayPtrAs;
  for ( auto ArrayA : ArrayAs) {
    auto ArrayPtrA = createArrayPointerAlloca(ArrayA, ElementT);
    ArrayPtrAs.emplace_back(ArrayPtrA);
  }

  return ArrayPtrAs;
}

//==============================================================================
/// Load an array pointer from the dopevecter
//==============================================================================
Value* CodeGen::getArrayPointer(
    Value* ArrayA,
    Type * ElementT)
{
  auto PtrV = TheHelper_.extractValue(ArrayA, 0);
  auto PtrT = ElementT->getPointerTo();
  Value* CastV = TheHelper_.createBitCast(PtrV, PtrT);
  return CastV;
}

//==============================================================================
// Get a pointer to an element in an array
//==============================================================================
Value* CodeGen::getArrayElementPointer(
    Value* ArrayA,
    Type* ElementT,
    Value* IndexV)
{
  auto ArrayPtrV = getArrayPointer(ArrayA, ElementT);
  IndexV = TheHelper_.getAsValue(IndexV);
  return Builder_.CreateGEP(ArrayPtrV, IndexV);
}

//==============================================================================
// Load an array value
//==============================================================================
Value* CodeGen::getArraySize(Value* ArrayA)
{ return TheHelper_.extractValue(ArrayA, 1); }
  

//==============================================================================
// Load an array value
//==============================================================================
Value* CodeGen::extractArrayValue(
    Value* ArrayA,
    Type* ElementT,
    Value* IndexV)
{
  Value* ArrayGEP = nullptr;
  if (isArray(ArrayA))
    ArrayGEP = getArrayElementPointer(ArrayA, ElementT, IndexV);
  else
    ArrayGEP = TheHelper_.offsetPointer(ArrayA, IndexV);
  return TheHelper_.load(ArrayGEP);
}

//==============================================================================
// Store array value
//==============================================================================
void CodeGen::insertArrayValue(
    Value* ArrayA,
    Type* ElementT,
    Value* IndexV,
    Value* ValueV)
{
  ValueV = TheHelper_.getAsValue(ValueV);
  Value* ArrayPtr = nullptr;
  if (isArray(ArrayA))
    ArrayPtr = getArrayElementPointer(ArrayA, ElementT, IndexV);
  else
    ArrayPtr = TheHelper_.offsetPointer(ArrayA, IndexV);
  Builder_.CreateStore(ValueV, ArrayPtr);
}
  
  
//==============================================================================
// Destroy an array
//==============================================================================
void CodeGen::destroyArray(Value* Alloca)
{
  auto F = TheModule_->getFunction("deallocate");
  if (!F) F = librt::RunTimeLib::tryInstall(TheContext_, *TheModule_, "deallocate");

  Builder_.CreateCall(F, Alloca);
}

//==============================================================================
// Destroy all arrays
//==============================================================================
void CodeGen::destroyArrays(const std::vector<Value*> & Arrays)
{
  if (Arrays.empty()) return;

  Function *F; 
  F = TheModule_->getFunction("deallocate");
  if (!F) F = librt::RunTimeLib::tryInstall(TheContext_, *TheModule_, "deallocate");
  
  for ( auto Array : Arrays )
  { destroyArray(Array); }
}


////////////////////////////////////////////////////////////////////////////////
// Range Interface
////////////////////////////////////////////////////////////////////////////////


//==============================================================================
/// CreateEntryBlockAlloca - Create an alloca instruction in the entry block of
/// the function.  This is used for mutable variables etc.
//==============================================================================
VariableAlloca * CodeGen::createRange(
    const std::string &VarName,
    Value* StartV,
    Value* EndV,
    Value* StepV)
{
  AllocaInst* RangeA;

  RangeA = Tasker_->createRange(
      *TheModule_,
      VarName,
      StartV,
      EndV,
      StepV);

  auto RangeT = RangeA->getAllocatedType();
  auto it = VariableTable_.front().emplace(VarName, VariableAlloca{RangeA, RangeT});
  return &it.first->second;
}

////////////////////////////////////////////////////////////////////////////////
// Function Interface
////////////////////////////////////////////////////////////////////////////////

//==============================================================================
// Get the function
//==============================================================================
std::pair<Function*, bool> CodeGen::getFunction(std::string Name) {
  // First, see if the function has already been added to the current module.
  if (auto F = TheModule_->getFunction(Name))
    return {F,false};
  
  // see if this is an available intrinsic, try installing it first
  if (auto F = librt::RunTimeLib::tryInstall(TheContext_, *TheModule_, Name))
    return {F,false};

  // If not, check whether we can codegen the declaration from some existing
  // prototype.
  auto fit = FunctionTable_.find(Name);
  return {runFuncVisitor(*fit->second), fit->second->getReturnType().isStruct()};
}

//==============================================================================
// Insert the function
//==============================================================================
PrototypeAST & CodeGen::insertFunction(std::unique_ptr<PrototypeAST> Proto)
{
  auto & P = FunctionTable_[Proto->getName()];
  P = std::move(Proto);
  return *P;
}
    
////////////////////////////////////////////////////////////////////////////////
// Future Interface
////////////////////////////////////////////////////////////////////////////////

//==============================================================================
// load the future
//==============================================================================
Value* CodeGen::loadFuture(Type* VariableT, Value* FutureV)
{
  return Tasker_->loadFuture(*TheModule_, FutureV, VariableT);
}


////////////////////////////////////////////////////////////////////////////////
// Field Interface
////////////////////////////////////////////////////////////////////////////////


//==============================================================================
// Create a future alloca
//==============================================================================
VariableAlloca * CodeGen::createField(
    const std::string &VarName,
    Type* VarType,
    Value* SizeVal,
    Value* InitVal)
{
  auto FieldA = Tasker_->createField(*TheModule_, VarName, VarType, 
      SizeVal, InitVal);
  return insertVariable(VarName, FieldA, VarType);
}

////////////////////////////////////////////////////////////////////////////////
// Visitor Interface
////////////////////////////////////////////////////////////////////////////////


//==============================================================================
// IntegerExprAST - Expression class for numeric literals like "1.0".
//==============================================================================
void CodeGen::visit(ValueExprAST & e)
{
  switch (e.getValueType()) {
  case ValueExprAST::ValueType::Int:
    ValueResult_ = llvmValue<int_t>(TheContext_, e.getVal<int_t>());
    break;
  case ValueExprAST::ValueType::Real:
    ValueResult_ = llvmValue(TheContext_, e.getVal<real_t>());
    break;
  case ValueExprAST::ValueType::String:
    ValueResult_ = llvmString(TheContext_, getModule(), e.getVal<std::string>());
    break;
  }
}
 
//==============================================================================
// VarAccessExprAST - Expression class for referencing a variable, like "a".
//==============================================================================
void CodeGen::visit(VarAccessExprAST& e)
{

  auto Name = e.getName();

  // Look this variable up in the function.
  auto VarE = getVariable(Name);
  Value * VarA = VarE->getAlloca();
  
  // Load the value.
  if (Tasker_->isAccessor(VarA)) {
    ValueResult_ = Tasker_->loadAccessor(*TheModule_, VarE->getType(), VarA);
  }
  else {
    ValueResult_ = VarA;
  }
}

//==============================================================================
// VarAccessExprAST - Expression class for referencing a variable, like "a".
//==============================================================================
void CodeGen::visit(ArrayAccessExprAST& e)
{

  const auto & Name = e.getName();

  // Look this variable up in the function.
  auto VarE = getVariable(Name);
  auto VarA = VarE->getAlloca();
  
  // Load the value.
  auto IndexV = runExprVisitor(*e.getIndexExpr());

  if (Tasker_->isAccessor(VarA)) {
    ValueResult_ = Tasker_->loadAccessor(*TheModule_, VarE->getType(), VarA, IndexV);
  }
  else if (Tasker_->isRange(VarA)) {
    ValueResult_ = Tasker_->loadRangeValue(*TheModule_, VarE->getType(), VarA, IndexV);
  }
  else {
    ValueResult_ = extractArrayValue(VarA, VarE->getType(), IndexV);
  }
}


//==============================================================================
// ArrayExprAST - Expression class for arrays.
//==============================================================================
void CodeGen::visit(ArrayExprAST &e)
{
  auto TheFunction = Builder_.GetInsertBlock()->getParent();
  
  // the llvm variable type
  auto VarType = setArray(e.getType(), false);
  auto VarT = getLLVMType(VarType);

  std::vector<Value*> InitVals;
  InitVals.reserve(e.getNumVals());
  for ( const auto & E : e.getValExprs() ) 
    InitVals.emplace_back( runExprVisitor(*E) );

  Value* SizeExpr = nullptr;
  if (e.hasSize()) {
    SizeExpr = runExprVisitor(*e.getSizeExpr());
  }
  else {
    SizeExpr = llvmValue<int_t>(TheContext_, e.getNumVals());
  }

  auto ArrayN = getTempName();
  auto ArrayE = createArray(ArrayN, VarT, SizeExpr );
  auto ArrayA = ArrayE->getAlloca();

  if (e.hasSize()) 
    initArrays(TheFunction, {ArrayA}, InitVals[0], SizeExpr, VarT);
  else
    initArray(TheFunction, ArrayA, InitVals, VarT);

  ValueResult_ =  TheHelper_.load(ArrayA, ArrayN);
}

//==============================================================================
// ArrayExprAST - Expression class for arrays.
//==============================================================================
void CodeGen::visit(RangeExprAST &e)
{
  // the llvm variable type
  auto StartV = runExprVisitor(*e.getStartExpr());
  auto EndV = runExprVisitor(*e.getEndExpr());
  Value* StepV = nullptr;
  if (e.hasStepExpr()) StepV = runExprVisitor(*e.getStepExpr());


  auto RangeN = getTempName();
  auto RangeE = createRange(RangeN, StartV, EndV, StepV);
  auto RangeA = RangeE->getAlloca();

  ValueResult_ =  TheHelper_.load(RangeA, RangeN);
}
  
//==============================================================================
// CastExprAST - Expression class for casts.
//==============================================================================
void CodeGen::visit(CastExprAST &e)
{
  auto ToType = e.getType();
  auto ToT = getLLVMType(ToType);
  auto FromV = runExprVisitor(*e.getFromExpr());

  if (ToType.isStruct()) {
    auto ToA = TheHelper_.createEntryBlockAlloca(ToT);

    const auto & ToMembers = ToType.getMembers();
    auto NumElem = ToMembers.size();
  
    for (unsigned i=0; i<NumElem; ++i) {
      auto StructT = cast<StructType>(ToT);
      auto MemberT = StructT->getElementType(i);
      Value* MemberV = TheHelper_.extractValue(FromV, i);
      MemberV = TheHelper_.createCast(MemberV, MemberT);
      TheHelper_.insertValue(ToA, MemberV, i); 
    }
    ValueResult_ = TheHelper_.load(ToA);
  }
  else {
    FromV = TheHelper_.getAsValue(FromV);
    ValueResult_ = TheHelper_.createCast(FromV, ToT);
  }

}

//==============================================================================
// UnaryExprAST - Expression class for a unary operator.
//==============================================================================
void CodeGen::visit(UnaryExprAST & e) {
  auto OperandV = TheHelper_.getAsValue( runExprVisitor(*e.getOpExpr()) );
  
  if (OperandV->getType()->isFloatingPointTy()) {
  
    switch (e.getOperand()) {
    case tok_sub:
      ValueResult_ = Builder_.CreateFNeg(OperandV, "negtmp");
      return;
    }

  }
  else {
    switch (e.getOperand()) {
    case tok_sub:
      ValueResult_ = Builder_.CreateNeg(OperandV, "negtmp");
      return;
    }
  }

  auto F = getFunction(std::string("unary") + e.getOperand()).first;
  ValueResult_ = Builder_.CreateCall(F, OperandV, "unop");
}

//==============================================================================
// BinaryExprAST - Expression class for a binary operator.
//==============================================================================
void CodeGen::visit(BinaryExprAST& e) {
  
  Value *L = TheHelper_.getAsValue( runExprVisitor(*e.getLeftExpr()) );
  Value *R = TheHelper_.getAsValue( runExprVisitor(*e.getRightExpr()) );

  auto l_is_real = L->getType()->isFloatingPointTy();
  auto r_is_real = R->getType()->isFloatingPointTy();
  bool is_real =  (l_is_real && r_is_real);

  if (is_real) {
    switch (e.getOperand()) {
    case tok_add:
      ValueResult_ = Builder_.CreateFAdd(L, R, "addtmp");
      return;
    case tok_sub:
      ValueResult_ = Builder_.CreateFSub(L, R, "subtmp");
      return;
    case tok_mul:
      ValueResult_ = Builder_.CreateFMul(L, R, "multmp");
      return;
    case tok_div:
      ValueResult_ = Builder_.CreateFDiv(L, R, "divtmp");
      return;
    case tok_mod:
      ValueResult_ = Builder_.CreateFRem(L, R, "remtmp");
      return;
    case tok_lt:
      ValueResult_ = Builder_.CreateFCmpULT(L, R, "cmptmp");
      return;
    case tok_le:
      ValueResult_ = Builder_.CreateFCmpULE(L, R, "cmptmp");
      return;
    case tok_gt:
      ValueResult_ = Builder_.CreateFCmpUGT(L, R, "cmptmp");
      return;
    case tok_ge:
      ValueResult_ = Builder_.CreateFCmpUGE(L, R, "cmptmp");
      return;
    case tok_eq:
      ValueResult_ = Builder_.CreateFCmpUEQ(L, R, "cmptmp");
      return;
    case tok_ne:
      ValueResult_ = Builder_.CreateFCmpUNE(L, R, "cmptmp");
      return;
    } 
  }
  else {
    switch (e.getOperand()) {
    case tok_add:
      ValueResult_ = Builder_.CreateAdd(L, R, "addtmp");
      return;
    case tok_sub:
      ValueResult_ = Builder_.CreateSub(L, R, "subtmp");
      return;
    case tok_mul:
      ValueResult_ = Builder_.CreateMul(L, R, "multmp");
      return;
    case tok_div:
      ValueResult_ = Builder_.CreateSDiv(L, R, "divtmp");
      return;
    case tok_mod:
      ValueResult_ = Builder_.CreateSRem(L, R, "divtmp");
      return;
    case tok_lt:
      ValueResult_ = Builder_.CreateICmpSLT(L, R, "cmptmp");
      return;
    case tok_le:
      ValueResult_ = Builder_.CreateICmpSLE(L, R, "cmptmp");
      return;
    case tok_gt:
      ValueResult_ = Builder_.CreateICmpSGT(L, R, "cmptmp");
      return;
    case tok_ge:
      ValueResult_ = Builder_.CreateICmpSGE(L, R, "cmptmp");
      return;
    case tok_eq:
      ValueResult_ = Builder_.CreateICmpEQ(L, R, "cmptmp");
      return;
    case tok_ne:
      ValueResult_ = Builder_.CreateICmpNE(L, R, "cmptmp");
      return;
    }
  }

  // If it wasn't a builtin binary operator, it must be a user defined one. Emit
  // a call to it.
  auto F = getFunction(std::string("binary") + e.getOperand()).first;

  Value *Ops[] = { L, R };
  ValueResult_ = Builder_.CreateCall(F, Ops, "binop");
}

//==============================================================================
// CallExprAST - Expression class for function calls.
//==============================================================================
void CodeGen::visit(CallExprAST &e) {

  // Look up the name in the global module table.
  auto Name = e.getName();
  auto NumArgs = e.getNumArgs();

  // helpers to get arguments
  auto getArgValue = [&](auto i) { 
    auto Arg = runExprVisitor(*e.getArgExpr(i));
    return TheHelper_.getAsValue(Arg);
  };
  auto getArg = [&](auto i)
  { return runExprVisitor(*e.getArgExpr(i)); };

  // check if its a cast
  if (isLLVMType(Name)) {
    auto ToT = getLLVMType(Name);
    ValueResult_ = TheHelper_.createCast(getArgValue(0), ToT);
    return;
  }
  else if (Name == "len") {
    ValueResult_ = nullptr;
    auto Arg = getArg(0);
    if (Tasker_->isRange(Arg))
      ValueResult_ = Tasker_->getRangeSize(*TheModule_, Arg);
    return;
  }
  else if (Name == "part") {
    if (NumArgs == 2) {
      ValueResult_ = Tasker_->partition(
          *TheModule_,
          getArg(0),
          I64Type_,
          getArg(1),
          true );
    }
    else if (NumArgs == 3) {
      ValueResult_ = Tasker_->partition(
        *TheModule_,
        getArg(0),
        getArg(1),
        getArg(2));
    }
    return;
  }

  auto FunPair = getFunction(Name);
  auto CalleeF = FunPair.first;
  auto IsTask = Tasker_->isTask(Name);
    
  std::vector<Value *> ArgVs;
  for (unsigned i = 0; i<e.getNumArgs(); ++i) {
    Value* Arg = getArg(i);
    if (!IsTask && Tasker_->isFuture(Arg)) {
      auto ArgT = getLLVMType( e.getArgType(i) );
      Arg = loadFuture(ArgT, Arg);
    }
    ArgVs.push_back(Arg);
  }

  //----------------------------------------------------------------------------
  if (IsTask) {
    auto TaskI = Tasker_->getTask(Name);
    Value* FutureV = nullptr;
    
    if (e.isTopLevelTask()) {
      if (Tasker_->isStarted())
        Tasker_->postregisterTasks(*TheModule_);
      else
        Tasker_->preregisterTasks(*TheModule_);
      Tasker_->setTopLevelTask(*TheModule_, TaskI.getId());
      Tasker_->start(*TheModule_, Argc_, Argv_);
    }
    else {
      FutureV = Tasker_->launch(*TheModule_, Name, TaskI.getId(), ArgVs);
    }
  
    ValueResult_ = UndefValue::get(Type::getVoidTy(TheContext_));

    auto CalleeT = CalleeF->getFunctionType()->getReturnType();
    if (!CalleeT->isVoidTy()) ValueResult_ = FutureV;
    if (!CalleeT->isVoidTy() && FutureV) {
      auto Ty = e.getType();
      if (!Ty.isFuture()) {
        auto ResultT = getLLVMType(Ty);
        FutureV = loadFuture(ResultT, FutureV);
      }
      ValueResult_ = FutureV;
    }
  }
  //----------------------------------------------------------------------------
  else {
    std::string TmpN = CalleeF->getReturnType()->isVoidTy() ? "" : "calltmp";
    for (auto & A : ArgVs) A = TheHelper_.getAsValue(A);
    ValueResult_ = Builder_.CreateCall(CalleeF, ArgVs, TmpN);
  }

  IsPacked_ = FunPair.second;

}

//==============================================================================
// expression list
//==============================================================================
void CodeGen::visit(ExprListAST & e) {

  auto StructT = getLLVMType(e.getType());
  auto StructA = TheHelper_.createEntryBlockAlloca(StructT, "struct.a");
    
  unsigned i = 0;
  for (const auto & Expr : e.getExprs()) {
    auto MemberV = runExprVisitor(*Expr);
    TheHelper_.insertValue(StructA, MemberV, i);
    i++;
  }

  ValueResult_ = StructA;
}


//==============================================================================
// IfExprAST - Expression class for if/then/else.
//==============================================================================
void CodeGen::visit(IfStmtAST & e) {

  if ( e.getThenExprs().empty() && e.getElseExprs().empty() ) {
    ValueResult_ = Constant::getNullValue(VoidType_);
    return;
  }

  Value *CondV = runExprVisitor(*e.getCondExpr());

  auto TheFunction = Builder_.GetInsertBlock()->getParent();

  // Create blocks for the then and else cases.  Insert the 'then' block at the
  // end of the function.
  BasicBlock *ThenBB = BasicBlock::Create(TheContext_, "then", TheFunction);
  BasicBlock *ElseBB = e.getElseExprs().empty() ? nullptr : BasicBlock::Create(TheContext_, "else");
  BasicBlock *MergeBB = BasicBlock::Create(TheContext_, "ifcont");

  if (ElseBB)
    Builder_.CreateCondBr(CondV, ThenBB, ElseBB);
  else
    Builder_.CreateCondBr(CondV, ThenBB, MergeBB);

  // Emit then value.
  Builder_.SetInsertPoint(ThenBB);

  createScope();
  for ( const auto & stmt : e.getThenExprs() ) runStmtVisitor(*stmt);
  popScope();

  // get first non phi instruction
  ThenBB->getFirstNonPHI();

  Builder_.CreateBr(MergeBB);

  // Codegen of 'Then' can change the current block, update ThenBB for the PHI.
  ThenBB = Builder_.GetInsertBlock();

  if (ElseBB) {

    // Emit else block.
    TheFunction->getBasicBlockList().push_back(ElseBB);
    Builder_.SetInsertPoint(ElseBB);

    createScope();
    for ( const auto & stmt : e.getElseExprs() ) runStmtVisitor(*stmt); 
    popScope();

    // get first non phi
    ElseBB->getFirstNonPHI();

    Builder_.CreateBr(MergeBB);
    // Codegen of 'Else' can change the current block, update ElseBB for the PHI.
    ElseBB = Builder_.GetInsertBlock();

  } // else

  // Emit merge block.
  TheFunction->getBasicBlockList().push_back(MergeBB);
  Builder_.SetInsertPoint(MergeBB);
  //PHINode *PN = Builder.CreatePHI(ThenV->getType(), 2, "iftmp");

  //if (ThenV) PN->addIncoming(ThenV, ThenBB);
  //if (ElseV) PN->addIncoming(ElseV, ElseBB);
  //return PN;
  
  // for expr always returns 0.
  ValueResult_ = UndefValue::get(VoidType_);
}

//==============================================================================
// ForExprAST - Expression class for for/in.
//
// Output for-loop as:
//   ...
//   start = startexpr
//   goto loop
// loop:
//   variable = phi [start, loopheader], [nextvariable, loopend]
//   ...
//   bodyexpr
//   ...
// loopend:
//   step = stepexpr
//   nextvariable = variable + step
//   endcond = endexpr
//   br endcond, loop, endloop
// outloop:
//==============================================================================
void CodeGen::visit(ForStmtAST& e) {
  
  auto TheFunction = Builder_.GetInsertBlock()->getParent();
  
  createScope();

  // Create an alloca for the variable in the entry block.
  const auto & VarN = e.getVarName();
  auto VarT = llvmType<int_t>(TheContext_);
  auto VarE = createVariable(TheFunction, VarN, VarT);
  auto VarA = VarE->getAlloca();
  
  // Emit the start code first, without 'variable' in scope.
  Value* StartV = runStmtVisitor(*e.getStartExpr());
  Value* EndA = TheHelper_.createEntryBlockAlloca(VarT, VarN+"end");
  Value* StepA = TheHelper_.createEntryBlockAlloca(VarT, VarN+"step");
  if (Tasker_->isRange(StartV)) {
    auto EndV = TheHelper_.extractValue(StartV, 1);
    Builder_.CreateStore(EndV, EndA);
    auto StepV = TheHelper_.extractValue(StartV, 2);
    Builder_.CreateStore(StepV, StepA);
    StartV = TheHelper_.extractValue(StartV, 0);
  }
  Builder_.CreateStore(StartV, VarA);

  // Make the new basic block for the loop header, inserting after current
  // block.
  BasicBlock *BeforeBB = BasicBlock::Create(TheContext_, "beforeloop", TheFunction);
  BasicBlock *LoopBB =   BasicBlock::Create(TheContext_, "loop", TheFunction);
  BasicBlock *IncrBB =   BasicBlock::Create(TheContext_, "incr", TheFunction);
  BasicBlock *AfterBB =  BasicBlock::Create(TheContext_, "afterloop", TheFunction);

  Builder_.CreateBr(BeforeBB);
  Builder_.SetInsertPoint(BeforeBB);

  // Load value and check coondition
  Value *CurV = TheHelper_.load(VarA);

  // Compute the end condition.
  // Convert condition to a bool by comparing non-equal to 0.0.
  Value* EndV = TheHelper_.load(EndA);
  EndV = Builder_.CreateICmpSLT(CurV, EndV, "loopcond");


  // Insert the conditional branch into the end of LoopEndBB.
  Builder_.CreateCondBr(EndV, LoopBB, AfterBB);

  // Start insertion in LoopBB.
  //TheFunction->getBasicBlockList().push_back(LoopBB);
  Builder_.SetInsertPoint(LoopBB);
  
  // Emit the body of the loop.  This, like any other expr, can change the
  // current BB.  Note that we ignore the value computed by the body, but don't
  // allow an error.
  createScope();
  for ( auto & stmt : e.getBodyExprs() ) runStmtVisitor(*stmt);
  popScope();


  // Insert unconditional branch to increment.
  Builder_.CreateBr(IncrBB);

  // Start insertion in LoopBB.
  //TheFunction->getBasicBlockList().push_back(IncrBB);
  Builder_.SetInsertPoint(IncrBB);
  

  // Reload, increment, and restore the alloca.  This handles the case where
  // the body of the loop mutates the variable.
  TheHelper_.increment( TheHelper_.getAsAlloca(VarA), StepA);

  // Insert the conditional branch into the end of LoopEndBB.
  Builder_.CreateBr(BeforeBB);

  // Any new code will be inserted in AfterBB.
  //TheFunction->getBasicBlockList().push_back(AfterBB);
  Builder_.SetInsertPoint(AfterBB);

  // for expr always returns 0.
  popScope();
  ValueResult_ = UndefValue::get(VoidType_);
}

//==============================================================================
// Foreach parallel loop
//==============================================================================
void CodeGen::visit(ForeachStmtAST& e)
{
  //---------------------------------------------------------------------------
  if (!e.isLifted()) {
    visit( static_cast<ForStmtAST&>(e) );
  }
  //---------------------------------------------------------------------------
  else {
    createScope();

    Value* RangeV = nullptr;

    Value* StartV = runStmtVisitor(*e.getStartExpr());
    
    // range
    if (Tasker_->isRange(StartV))
      RangeV = StartV;
    auto PreviousRange = CurrentRange_;
    CurrentRange_ = RangeV;

    //----------------------------------
    // Partition Tasks
    std::map<std::string, Value*> Partitions;
    std::map<std::string, Value*> Fields;
    for (auto & Stmt : e.getBodyExprs()) {
      auto Node = dynamic_cast<PartitionStmtAST*>(Stmt.get());
      auto VarA = runStmtVisitor(*Node->getPartExpr());
      auto NumVars = Node->getNumVars();
      for (unsigned i=0; i<NumVars; ++i) {
        auto VarD = Node->getVarDef(i);
        const auto VarN = Node->getVarName(i);
        if (!VarD->getType().isField()) {
          Partitions.emplace( VarN, VarA );
        }
        else {
          Fields.emplace(VarN, VarA);
        }
      }
    }

    //----------------------------------
    // Main Task
    std::vector<Value*> TaskArgAs;
    std::vector<Value*> PartAs;
    for ( const auto & VarD : e.getAccessedVariables() ) {
      const auto & Name = VarD->getName();
      auto pit = Partitions.find(Name);
      Value* VarA = nullptr;
      if (pit != Partitions.end()) {
        VarA = pit->second;
      }
      else {
        auto VarE = getVariable(Name);
        VarA = VarE->getAlloca();
      }
      TaskArgAs.emplace_back(VarA); 
      
      Value* PartA = nullptr;
      auto fit = Fields.find(Name);
      if (fit != Fields.end()) PartA = fit->second;
      PartAs.emplace_back(PartA);
    }

    auto TaskN = e.getName();
    auto TaskI = Tasker_->getTask(TaskN);
    Tasker_->launch(*TheModule_, TaskN, TaskI.getId(), TaskArgAs, PartAs, RangeV);
    
    popScope();
	  ValueResult_ = UndefValue::get(VoidType_);

    CurrentRange_ = PreviousRange;
  }
}

//==============================================================================
// Assignment
//==============================================================================
void CodeGen::visit(AssignStmtAST & e)
{
  auto NumLeft = e.getNumLeftExprs();
  auto NumRight = e.getNumRightExprs();
 
  // get all right 
  std::vector<Value*> RightVs;
  for (const auto & Expr : e.getRightExprs())
    RightVs.emplace_back( runExprVisitor(*Expr) );

  if (IsPacked_) {
    auto StructV = RightVs.front();
    auto StructT = cast<StructType>(StructV->getType());
    NumRight = StructT->getNumElements();
    RightVs.clear();
    for (unsigned i=0; i<NumRight; ++i)
      RightVs.emplace_back( TheHelper_.extractValue(StructV, i) );
  }

  auto RightIt = RightVs.begin();

  // Loop over variables
  for (unsigned il=0; il<NumLeft; il++) {

    // Assignment requires the LHS to be an identifier.
    // This assume we're building without RTTI because LLVM builds that way by
    // default.  If you build LLVM with RTTI this can be changed to a
    // dynamic_cast for automatic error checking.
    auto LeftExpr = e.getLeftExpr(il);
    auto LHSE = dynamic_cast<VarAccessExprAST*>(LeftExpr);
    
    // Codegen the RHS.
    Value* VariableV = *RightIt;
    if (auto CastType = e.getCast(il))
      VariableV = TheHelper_.createCast(VariableV, getLLVMType(*CastType));

    // Look up the name.
    const auto & VarType = LHSE->getType();
    const auto & VarN = LHSE->getName();
    auto VarPair = getOrCreateVariable(VarN, VarType);
    auto VarE = VarPair.first;
    auto VarInserted = VarPair.second; 
    Value* VariableA = VarE->getAlloca();

    //---------------------------------------------------------------------------
    // array[i] = scalar
    if (auto LHSEA = dynamic_cast<ArrayAccessExprAST*>(LeftExpr)) {
      auto IndexV = runExprVisitor(*LHSEA->getIndexExpr());
  
      if (Tasker_->isAccessor(VariableA)) {
        Tasker_->storeAccessor(*TheModule_, VariableV, VariableA, IndexV);
      }
      else if (Tasker_->isField(VariableA)) {
        auto VarT = getLLVMType(strip(VarType));
        if (Tasker_->isFuture(VariableV)) {
          VariableV = loadFuture(VarT, VariableV);
        }
        Tasker_->createField(*TheModule_, VariableA, VarN, VarT, IndexV, VariableV);
      }
      else {
        insertArrayValue(VariableA, VarE->getType(), IndexV, VariableV);
      }
    }
    //---------------------------------------------------------------------------
    // array = ?
    else if (isArray(VariableA)) {
      if (VarInserted) {
        auto SizeV = TheHelper_.extractValue(VariableV, 1);
        createArray(VariableA, SizeV, VarE->getType() );
      }
      copyArray(VariableV, VariableA);
    }
    //---------------------------------------------------------------------------
    // future = ?
    else if (Tasker_->isFuture(VariableA)) {
      //Tasker_->destroyFuture(*TheModule_, VariableA);
      // future = future
      if (Tasker_->isFuture(VariableV)) {
        Tasker_->copyFuture(*TheModule_, VariableV, VariableA);
      }
      // future = value
      else {
        Tasker_->toFuture(*TheModule_, VariableV, VariableA);
      }
    }
    //---------------------------------------------------------------------------
    // Field = value
    else if (Tasker_->isAccessor(VariableA)) {
      Tasker_->storeAccessor(*TheModule_, VariableV, VariableA);
    }
    //---------------------------------------------------------------------------
    // value = future
    else if (Tasker_->isFuture(VariableV)) {
      auto VariableT = VarE->getType();
      VariableV = loadFuture(VariableT, VariableV);
      Builder_.CreateStore(VariableV, VariableA);
    }
    //---------------------------------------------------------------------------
    // scalar = scalar
    else {
      if (Tasker_->isRange(VariableV)) VarE->setOwner(false);
      VariableV = TheHelper_.getAsValue(VariableV);
      Builder_.CreateStore(VariableV, VariableA);
    }

    if (NumRight>1) ++RightIt;

  } // for

  ValueResult_ = nullptr;
  return;

}

//==============================================================================
/// PrototypeAST - This class represents the "prototype" for a function.
//==============================================================================
void CodeGen::visit(PrototypeAST &e) {

  unsigned NumArgs = e.getNumArgs();

  std::vector<Type *> ArgTypes;
  ArgTypes.reserve(NumArgs);

  for (unsigned i=0; i<NumArgs; ++i) {
    auto VarType = getLLVMType(e.getArgTypeId(i));
    if (e.isArgArray(i)) VarType = ArrayType_;
    ArgTypes.emplace_back(VarType);
  }
  
  Type* ReturnType = VoidType_;
  if (e.getReturnType()) {
    ReturnType = getLLVMType(e.getReturnType());
  }

  FunctionType *FT = FunctionType::get(ReturnType, ArgTypes, false);

  Function *F = Function::Create(FT, Function::ExternalLinkage, e.getName(), &getModule());

  // Set names for all arguments.
  unsigned Idx = 0;
  for (auto &Arg : F->args()) Arg.setName(e.getArgName(Idx++));

  FunctionResult_ = F;

}
 

//==============================================================================
/// FunctionAST Helper
//==============================================================================
Value* CodeGen::codegenFunctionBody(FunctionAST& e)
{
  for ( auto & stmt : e.getBodyExprs() )
  {
    runStmtVisitor(*stmt);
  }

  // Finish off the function.
  Value* RetVal = nullptr;
  if ( e.getReturnExpr() ) {
    RetVal = runStmtVisitor(*e.getReturnExpr());
    if (RetVal) RetVal = TheHelper_.getAsValue(RetVal);
  }

  return RetVal;
}

//==============================================================================
/// FunctionAST - This class represents a function definition itself.
//==============================================================================
void CodeGen::visit(FunctionAST& e)
{
  bool CreatedScope = false;
  if (!e.isTopLevelExpression()) {
    CreatedScope = true;
    createScope();
  }

  // Transfer ownership of the prototype to the FunctionProtos map, but keep a
  // reference to it for use below.
  auto & P = insertFunction( std::move(e.moveProtoExpr()) );
  const auto & Name = P.getName();
  auto TheFunction = getFunction(Name).first;

  // Create a new basic block to start insertion into.
  BasicBlock *BB = BasicBlock::Create(TheContext_, "entry", TheFunction);
  Builder_.SetInsertPoint(BB);

  // Record the function arguments in the NamedValues map.
  unsigned ArgIdx = 0;
  for (auto &Arg : TheFunction->args()) {

    // get arg type
    auto ArgType = P.getArgType(ArgIdx);
    auto BaseType = strip(ArgType);
    auto LLType = getLLVMType(BaseType);

    // Create an alloca for this variable.
    VariableAlloca* VarE;
    if (ArgType.isArray()) {
      VarE = createArray(Arg.getName(), LLType);
    }
    else
      VarE = createVariable(TheFunction, Arg.getName(), LLType);
    VarE->setOwner(false);
    auto Alloca = VarE->getAlloca();
    
    // Store the initial value into the alloca.
    Builder_.CreateStore(&Arg, Alloca);
 
    ArgIdx++;
  }
 
  // codegen the function body
  auto RetVal = codegenFunctionBody(e);

  // garbage collection
  if (CreatedScope) popScope();
  
  // Finish off the function.
  if (RetVal && !RetVal->getType()->isVoidTy() ) {
    Builder_.CreateRet(RetVal);
  }
  else
    Builder_.CreateRetVoid();
  
  // Validate the generated code, checking for consistency.
  verifyFunction(*TheFunction);
   
  FunctionResult_ = TheFunction;

}

//==============================================================================
/// TaskAST - This class represents a function definition itself.
//==============================================================================
void CodeGen::visit(TaskAST& e)
{
  bool CreatedScope = false;
  if (!e.isTopLevelExpression()) {
    CreatedScope = true;
    createScope();
  }
  
  // Transfer ownership of the prototype to the FunctionProtos map, but keep a
  // reference to it for use below.
  auto & P = insertFunction( std::move(e.moveProtoExpr()) );
  auto Name = P.getName();
  auto TheFunction = getFunction(Name).first;
  
  // insert the task 
  auto & TaskI = Tasker_->insertTask(Name);
  
  // generate wrapped task
  auto Wrapper = Tasker_->taskPreamble(*TheModule_, Name, TheFunction);

  // insert arguments into variable table
  unsigned ArgIdx = 0;
  for (auto &Arg : TheFunction->args()) {
    auto Alloca = Wrapper.ArgAllocas[ArgIdx++];
    auto AllocaT = Alloca->getType()->getPointerElementType(); // FIX FOR ARRAYS
    auto NewEntry = VariableAlloca(Alloca, AllocaT);
    auto VarE = insertVariable(Arg.getName(), NewEntry);
    VarE->setOwner(false);
  }

  
  // codegen the function body
  auto RetVal = codegenFunctionBody(e);
  
  if (RetVal && Tasker_->isFuture(RetVal)) {
    auto RetT = getLLVMType( P.getReturnType() );
    RetVal = loadFuture(RetT, RetVal);
  }

  // garbage collection
  if (CreatedScope) popScope();

  // Finish wrapped task
  Tasker_->taskPostamble(*TheModule_, RetVal);
  
  // Finish off the function.  Tasks always return void
  Builder_.CreateRetVoid();
  
  // Validate the generated code, checking for consistency.
  verifyFunction(*Wrapper.TheFunction);
  
  // set the finished function
  TaskI.setFunction(Wrapper.TheFunction);
  FunctionResult_ = Wrapper.TheFunction;

}

//==============================================================================
/// TaskAST - This class represents a function definition itself.
//==============================================================================
void CodeGen::visit(IndexTaskAST& e)
{

  bool CreatedScope = false;
  if (!e.isTopLevelExpression()) {
    CreatedScope = true;
    createScope();
  }

  auto TaskN = e.getName();
  const auto & VarOverrides = e.getVarOverrides();

  // get global args
  std::vector<Type*> TaskArgTs;
  std::vector<std::string> TaskArgNs;
  for ( const auto & VarE : e.getVariableDefs() ) {
    const auto & VarN = VarE->getName();
    TaskArgNs.emplace_back( VarN );
    // check overrides
    auto vit = VarOverrides.find(VarN);
    bool OverrideField = (vit != VarOverrides.end() && vit->second.isField());
    // overrided field types
    const auto & VarT = VarE->getType();
    if (VarT.isField() || OverrideField) { 
      TaskArgTs.emplace_back( AccessorType_ ); 
    }
    else {
      TaskArgTs.emplace_back( getLLVMType(VarT) ); 
    }
  }
      
	// generate wrapped task
  auto Wrapper = Tasker_->taskPreamble(
      *TheModule_,
      TaskN, 
      TaskArgNs,
      TaskArgTs,
      true,
      VarOverrides);

  // insert arguments into variable table
  for (unsigned ArgIdx=0; ArgIdx<TaskArgNs.size(); ++ArgIdx) {
    auto VarA = Wrapper.ArgAllocas[ArgIdx];
    auto AllocaT = VarA->getType()->getPointerElementType(); // FIX FOR ARRAYS
    auto VarD = e.getVariableDef(ArgIdx);
    bool IsOwner = false;
    if (Tasker_->isAccessor(AllocaT)) IsOwner = true;
    auto VarT = getLLVMType( strip(VarD->getType()) );
    auto VarE = insertVariable(TaskArgNs[ArgIdx], VarA, VarT);
    VarE->setOwner(IsOwner);
  }

  // and the index
  auto IndexA = Wrapper.Index;
  auto IndexT = IndexA->getType()->getPointerElementType();
  auto IndexE = insertVariable(e.getLoopVariableName(), IndexA, IndexT);
  IndexE->setOwner(false);

  // function body
  for ( auto & stmt : e.getBodyExprs() ) runStmtVisitor(*stmt);
  
  // garbage collection
  if (CreatedScope) popScope();
  
  // finish task
  Tasker_->taskPostamble(*TheModule_);
  
	Builder_.CreateRetVoid();
  
	// register it
  auto TaskI = Tasker_->insertTask(TaskN, Wrapper.TheFunction);
 	verifyFunction(*Wrapper.TheFunction);

	FunctionResult_ = Wrapper.TheFunction;

}

} // namespace
