#include "config.hpp"
#include "llvm_utils.hpp"

#include "ast.hpp"
#include "codegen.hpp"
#include "errors.hpp"
#include "legion.hpp"
#include "precedence.hpp"
#include "token.hpp"

#include "librt/librt.hpp"
#include "librt/dopevector.hpp"

#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/LinkAllPasses.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"

#include "llvm/ExecutionEngine/Orc/IndirectionUtils.h"

using namespace llvm;

namespace contra {

////////////////////////////////////////////////////////////////////////////////
// Constructor / Destructor
////////////////////////////////////////////////////////////////////////////////

//==============================================================================
// Constructor
//==============================================================================
CodeGen::CodeGen (bool debug = false) : Builder_(TheContext_)
{

  Argc_ = 1;
  Argv_ = new char *[Argc_];
  Argv_[0] = new char[9];
  strcpy(Argv_[0], "./contra");

  Tasker_ = std::make_unique<LegionTasker>(Builder_, TheContext_);

  librt::RunTimeLib::setup(TheContext_);

  I64Type_  = llvmType<int_t>(TheContext_);
  F64Type_  = llvmType<real_t>(TheContext_);
  VoidType_ = Type::getVoidTy(TheContext_);
  ArrayType_ = librt::DopeVector::DopeVectorType;

  TypeTable_.emplace( Context::I64Type->getName(),  I64Type_);
  TypeTable_.emplace( Context::F64Type->getName(),  F64Type_);
  TypeTable_.emplace( Context::VoidType->getName(), VoidType_);

  VariableTable_.push_front({});

  initializeModuleAndPassManager();

  if (debug) {

    // Add the current debug info version into the module.
    TheModule_->addModuleFlag(Module::Warning, "Debug Info Version",
                             DEBUG_METADATA_VERSION);

    // Darwin only supports dwarf2.
    if (Triple(sys::getProcessTriple()).isOSDarwin())
      TheModule_->addModuleFlag(llvm::Module::Warning, "Dwarf Version", 2);

    DBuilder = std::make_unique<DIBuilder>(*TheModule_);
    KSDbgInfo.TheCU = DBuilder->createCompileUnit(
      dwarf::DW_LANG_C, DBuilder->createFile("fib.ks", "."),
      "Kaleidoscope Compiler", 0, "", 0);
  }

}
  
//==============================================================================
// Destructor
//==============================================================================
CodeGen::~CodeGen() {
  // delete arguments
  for (int i=0; i<Argc_; ++i) delete[] Argv_[i];
  delete[] Argv_;
  
  // finish debug
  if (DBuilder) DBuilder->finalize();
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
void CodeGen::initializeModule() {

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
// Debug related interface
////////////////////////////////////////////////////////////////////////////////

//==============================================================================
// Create a debug function type
//==============================================================================
DISubroutineType *CodeGen::createFunctionType(unsigned NumArgs, DIFile *Unit) {
  
  if (!isDebug()) return nullptr;

  SmallVector<Metadata *, 8> EltTys;
  DIType *DblTy = KSDbgInfo.getDoubleTy(*DBuilder);

  // Add the result type.
  EltTys.push_back(DblTy);

  for (unsigned i = 0, e = NumArgs; i != e; ++i)
    EltTys.push_back(DblTy);

  return DBuilder->createSubroutineType(DBuilder->getOrCreateTypeArray(EltTys));
}
  
DIFile * CodeGen::createFile() {
  if (isDebug())
    return DBuilder->createFile(KSDbgInfo.TheCU->getFilename(),
        KSDbgInfo.TheCU->getDirectory());
  else
    return nullptr;
}

//==============================================================================
// create a subprogram DIE
//==============================================================================
DISubprogram * CodeGen::createSubprogram(
    unsigned LineNo,
    unsigned ScopeLine,
    const std::string & Name,
    unsigned arg_size,
    DIFile * Unit)
{
  if (isDebug()) {
    DIScope *FContext = Unit;
    DISubprogram *SP = DBuilder->createFunction(
        FContext, Name, StringRef(), Unit, LineNo,
        createFunctionType(arg_size, Unit), ScopeLine,
        DINode::FlagPrototyped, DISubprogram::SPFlagDefinition);
    return SP;
  }
  else {
    return nullptr;
  }
}
 
//==============================================================================
// Create a variable
//==============================================================================
DILocalVariable *CodeGen::createVariable( DISubprogram *SP,
    const std::string & Name, unsigned ArgIdx, DIFile *Unit, unsigned LineNo,
    Value *Alloca)
{
  if (isDebug()) {
    DILocalVariable *D = DBuilder->createParameterVariable(
      SP, Name, ArgIdx, Unit, LineNo, KSDbgInfo.getDoubleTy(*DBuilder),
      true);

    DBuilder->insertDeclare(Alloca, D, DBuilder->createExpression(),
        DebugLoc::get(LineNo, 0, SP),
        Builder_.GetInsertBlock());

    return D;
  }
  else {
    return nullptr;
  }
}

////////////////////////////////////////////////////////////////////////////////
// Scope Interface
////////////////////////////////////////////////////////////////////////////////  
void CodeGen::resetScope(Scoper::value_type Scope)
{
  std::map<std::string, Value*> Arrays;
  for (int i=Scope; i<getScope(); ++i) {
    for ( const auto & entry_pair : VariableTable_.front() ) {
      const auto & Name = entry_pair.first;
      auto VarE = entry_pair.second;
      auto Alloca = VarE.getAlloca();
      auto IsOwner = VarE.isOwner();
      if (isArray(Alloca) && IsOwner) 
        Arrays.emplace(Name, Alloca);
    }
    VariableTable_.pop_front();
  }
  destroyArrays(Arrays);
  Scoper::resetScope(Scope);
}

////////////////////////////////////////////////////////////////////////////////
// Variable Interface
////////////////////////////////////////////////////////////////////////////////
  
//==============================================================================
// Move a variable
//==============================================================================
CodeGen::VariableEntry * CodeGen::moveVariable(const std::string & From, const std::string & To)
{
  auto & CurrTab = VariableTable_.front();
  auto it = CurrTab.find(From);
  auto Var = it->second;
  CurrTab.erase(it);  
  auto new_it = CurrTab.emplace(To, Var);
  
  return &new_it.first->second;
}
    
//==============================================================================
// Get a variable
//==============================================================================
CodeGen::VariableEntry * CodeGen::getVariable(const std::string & VarName)
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
CodeGen::VariableEntry * CodeGen::createVariable(Function *TheFunction,
  const std::string &VarName, Type* VarType, bool IsGlobal)
{
  Value* NewVar;

  if (IsGlobal) {
    auto GVar = new GlobalVariable(*TheModule_,
        VarType,
        false,
        GlobalValue::ExternalLinkage,
        nullptr, // has initializer, specified below
        VarName);
    NewVar = GVar;
  }
  else {
    NewVar = createEntryBlockAlloca(TheFunction, VarType, VarName);
  }

  return insertVariable(VarName, VariableEntry{NewVar, VarType});
}

//==============================================================================
/// Insert an already allocated variable
//==============================================================================
CodeGen::VariableEntry *
CodeGen::insertVariable(const std::string &VarName, VariableEntry VarE)
{ 
  auto it = VariableTable_.front().emplace(VarName, VarE);
  return &it.first->second;
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
CodeGen::VariableEntry * CodeGen::createArray(Function *TheFunction,
  const std::string &VarName, Type* ElementT, bool IsGlobal)
{
  Value* NewVar;

  if (IsGlobal) {
    THROW_CONTRA_ERROR("Global arrays are not implemented yet.");
  }
  else {
    NewVar = createEntryBlockAlloca(TheFunction, ArrayType_, VarName);
  }

  auto it = VariableTable_.front().emplace(VarName, VariableEntry{NewVar, ElementT});
  return &it.first->second;
}


//==============================================================================
/// CreateEntryBlockAlloca - Create an alloca instruction in the entry block of
/// the function.  This is used for mutable variables etc.
//==============================================================================
CodeGen::VariableEntry *
CodeGen::createArray(Function *TheFunction, const std::string &VarName,
    Type * ElementType, Value * SizeExpr)
{

  Function *F; 
  F = TheModule_->getFunction("allocate");
  if (!F) F = librt::RunTimeLib::tryInstall(TheContext_, *TheModule_, "allocate");


  auto DataSize = getTypeSize<int_t>(ElementType);

  std::vector<Value*> ArgVs = {SizeExpr, DataSize};
  Value* CallInst = Builder_.CreateCall(F, ArgVs, VarName+"vectmp");
  auto ResType = CallInst->getType();
  auto AllocInst = createEntryBlockAlloca(TheFunction, ResType, VarName+"vec");
  Builder_.CreateStore(CallInst, AllocInst);
  
  return insertVariable(VarName, {AllocInst, ElementType, SizeExpr});
}
 
//==============================================================================
// Initialize Array
//==============================================================================
void CodeGen::initArrays( Function *TheFunction,
    const std::vector<Value*> & ArrayAs,
    Value * InitV,
    Value * SizeV,
    Type * ElementT)
{
  
  // create allocas to the pointers
  auto ElementPtrT = ElementT->getPointerTo();
  auto ArrayPtrAs = createArrayPointerAllocas(TheFunction, ArrayAs, ElementT);

  // create a loop
  auto Alloca = createEntryBlockAlloca(TheFunction, llvmType<int_t>(TheContext_), "__i");
  Value * StartVal = llvmValue<int_t>(TheContext_, 0);
  Builder_.CreateStore(StartVal, Alloca);
  
  auto BeforeBB = BasicBlock::Create(TheContext_, "beforeinit", TheFunction);
  auto LoopBB =   BasicBlock::Create(TheContext_, "init", TheFunction);
  auto AfterBB =  BasicBlock::Create(TheContext_, "afterinit", TheFunction);
  Builder_.CreateBr(BeforeBB);
  Builder_.SetInsertPoint(BeforeBB);
  auto CurVar = Builder_.CreateLoad(llvmType<int_t>(TheContext_), Alloca);
  auto EndCond = Builder_.CreateICmpSLT(CurVar, SizeV, "initcond");
  Builder_.CreateCondBr(EndCond, LoopBB, AfterBB);
  Builder_.SetInsertPoint(LoopBB);

  // store value
  for ( auto ArrayPtrA : ArrayPtrAs) {
    auto ArrayPtrV = Builder_.CreateLoad(ElementPtrT, ArrayPtrA, "ptr"); 
    auto ArrayGEP = Builder_.CreateGEP(ArrayPtrV, CurVar, "offset");
    Builder_.CreateStore(InitV, ArrayGEP);
  }

  // increment loop
  auto StepVal = llvmValue<int_t>(TheContext_, 1);
  auto NextVar = Builder_.CreateAdd(CurVar, StepVal, "nextvar");
  Builder_.CreateStore(NextVar, Alloca);
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
  auto TheBlock = Builder_.GetInsertBlock();
  
  // create allocas to the pointers
  auto ElementPtrT = ElementT->getPointerTo();
  auto ArrayPtrA = createArrayPointerAlloca(TheFunction, ArrayA, ElementT);
  
  for (std::size_t i=0; i<NumVals; ++i) {
    auto IndexV = llvmValue<int_t>(TheContext_, i);
    auto ArrayPtrV = Builder_.CreateLoad(ElementPtrT, ArrayPtrA, "ptr");
    auto ArrayGEP = Builder_.CreateGEP(ArrayPtrV, IndexV, "offset");
    auto InitV = InitVs[i];
    auto InitT = InitV->getType();
    if ( InitT->isFloatingPointTy() && ElementT->isIntegerTy() ) {
      auto Cast = CastInst::Create(Instruction::FPToSI, InitV,
          llvmType<int_t>(TheContext_), "cast", TheBlock);
      InitV = Cast;
    }
    else if ( InitT->isIntegerTy() && ElementT->isFloatingPointTy() ) {
      auto Cast = CastInst::Create(Instruction::SIToFP, InitV,
          llvmType<real_t>(TheContext_), "cast", TheBlock);
      InitV = Cast;
    }
    else if (InitT!=ElementT)
      THROW_CONTRA_ERROR("Unknown cast operation");
    Builder_.CreateStore(InitV, ArrayGEP);
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
  auto ElementPtrT = ElementT->getPointerTo();
  auto SrcArrayPtrA = createArrayPointerAlloca(TheFunction, SrcArrayA, ElementT);
  auto TgtArrayPtrAs = createArrayPointerAllocas(TheFunction, TgtArrayAs, ElementT);

  auto CounterA = createEntryBlockAlloca(TheFunction, llvmType<int_t>(TheContext_), "__i");
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
    
  auto SrcArrayPtrV = Builder_.CreateLoad(ElementPtrT, SrcArrayPtrA, "srcptr"); 
  auto SrcGEP = Builder_.CreateGEP(SrcArrayPtrV, CounterV, "srcoffset");
  auto SrcV = Builder_.CreateLoad(ElementT, SrcGEP, "srcval");

  for ( auto TgtArrayPtrA :  TgtArrayPtrAs ) {
    auto TgtArrayPtrV = Builder_.CreateLoad(ElementPtrT, TgtArrayPtrA, "tgtptr"); 
    auto TgtGEP = Builder_.CreateGEP(TgtArrayPtrV, CounterV, "tgtoffset");
    Builder_.CreateStore(SrcV, TgtGEP);
  }

  auto StepVal = llvmValue<int_t>(TheContext_, 1);
  auto NextVar = Builder_.CreateAdd(CounterV, StepVal, "nextvar");
  Builder_.CreateStore(NextVar, CounterA);
  Builder_.CreateBr(BeforeBB);
  Builder_.SetInsertPoint(AfterBB);
}

//==============================================================================
/// Load an array pointer from the dopevecter
//==============================================================================
Value* CodeGen::loadArrayPointer(
    Value* ArrayAlloca,
    Type * ElementType,
    const std::string &VarName)
{
  std::vector<Value*> MemberIndices(2);
  MemberIndices[0] = ConstantInt::get(TheContext_, APInt(32, 0, true));
  MemberIndices[1] = ConstantInt::get(TheContext_, APInt(32, 0, true));

  auto ResType = ArrayAlloca->getType()->getPointerElementType();
  auto GEPInst = Builder_.CreateGEP(ResType, ArrayAlloca, MemberIndices,
      VarName+"vec.ptr");
  auto LoadedInst = Builder_.CreateLoad(GEPInst->getType()->getPointerElementType(),
      GEPInst, VarName+"vec.val");

  auto TheBlock = Builder_.GetInsertBlock();
  auto PtrType = ElementType->getPointerTo();
  Value* Cast = CastInst::Create(CastInst::BitCast, LoadedInst, PtrType, "casttmp", TheBlock);
  return Cast;
}

//==============================================================================
// Load an arrayarray into an alloca
//==============================================================================
Value* CodeGen::createArrayPointerAlloca(
    Function *TheFunction,
    Value* ArrayA,
    Type* ElementT )
{
  auto ElementPtrT = ElementT->getPointerTo();
  auto ArrayV = loadArrayPointer(ArrayA, ElementT);
  auto ArrayPtrA = createEntryBlockAlloca(TheFunction, ElementPtrT);
  Builder_.CreateStore(ArrayV, ArrayPtrA);
  return ArrayPtrA;
}

  
//==============================================================================
// Load a bunch of arrays into allocas
//==============================================================================
std::vector<Value*> CodeGen::createArrayPointerAllocas(
    Function *TheFunction,
    const std::vector<Value*> & ArrayAs,
    Type* ElementT )
{
  std::vector<Value*> ArrayPtrAs;
  for ( auto ArrayA : ArrayAs) {
    auto ArrayPtrA = createArrayPointerAlloca(TheFunction, ArrayA, ElementT);
    ArrayPtrAs.emplace_back(ArrayPtrA);
  }

  return ArrayPtrAs;
}

//==============================================================================
// Load an array value
//==============================================================================
Value* CodeGen::getArraySize(Value* ArrayA, const std::string & Name)
{
  std::vector<Value*> MemberIndices(2);
  MemberIndices[0] = ConstantInt::get(TheContext_, APInt(32, 0, true));
  MemberIndices[1] = ConstantInt::get(TheContext_, APInt(32, 1, true));

  auto ArrayT = ArrayA->getType()->getPointerElementType();
  auto ArrayGEP = Builder_.CreateGEP(ArrayT, ArrayA, MemberIndices, Name+".size");
  auto SizeV = Builder_.CreateLoad(ArrayGEP->getType()->getPointerElementType(),
      ArrayGEP, Name+".size");
  return SizeV;
}
  

//==============================================================================
// Load an array value
//==============================================================================
Value* CodeGen::loadArrayValue(Value* ArrayA, Value* IndexV, Type* ElementT,
    const std::string & Name)
{
  auto ArrayPtrV = loadArrayPointer(ArrayA, ElementT, Name);
  auto ArrayGEP = Builder_.CreateGEP(ArrayPtrV, IndexV, Name+".offset");
  return Builder_.CreateLoad(ElementT, ArrayGEP, Name+".i");
}

//==============================================================================
// Store array value
//==============================================================================
void CodeGen::storeArrayValue(Value* ValueV, Value* ArrayA, Value* IndexV,
    const std::string & Name)
{
  auto ElementT = ValueV->getType();
  auto ArrayPtrV = loadArrayPointer(ArrayA, ElementT, Name);
  auto ArrayGEP = Builder_.CreateGEP(ArrayPtrV, IndexV, Name+".offset");
  Builder_.CreateStore(ValueV, ArrayGEP);
}
  
  
//==============================================================================
// Destroy an array
//==============================================================================
void CodeGen::destroyArray(const std::string & Name, Value* Alloca)
{
  auto F = TheModule_->getFunction("deallocate");
  if (!F) F = librt::RunTimeLib::tryInstall(TheContext_, *TheModule_, "deallocate");

  auto AllocaT = Alloca->getType()->getPointerElementType();
  auto Vec = Builder_.CreateLoad(AllocaT, Alloca, Name+"vec");
  Builder_.CreateCall(F, Vec, Name+"dealloctmp");
}

//==============================================================================
// Destroy all arrays
//==============================================================================
void CodeGen::destroyArrays(const std::map<std::string, Value*> & Arrays)
{
  if (Arrays.empty()) return;

  Function *F; 
  F = TheModule_->getFunction("deallocate");
  if (!F) F = librt::RunTimeLib::tryInstall(TheContext_, *TheModule_, "deallocate");
  
  for ( auto & pair : Arrays )
  { destroyArray(pair.first, pair.second); }
}


////////////////////////////////////////////////////////////////////////////////
// Function Interface
////////////////////////////////////////////////////////////////////////////////

//==============================================================================
// Get the function
//==============================================================================
Function *CodeGen::getFunction(std::string Name) {
  // First, see if the function has already been added to the current module.
  if (auto F = TheModule_->getFunction(Name))
    return F;
  
  // see if this is an available intrinsic, try installing it first
  if (auto F = librt::RunTimeLib::tryInstall(TheContext_, *TheModule_, Name))
    return F;

  // If not, check whether we can codegen the declaration from some existing
  // prototype.
  auto fit = FunctionTable_.find(Name);
  return runFuncVisitor(*fit->second);
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
// Visitor Interface
////////////////////////////////////////////////////////////////////////////////


//==============================================================================
// IntegerExprAST - Expression class for numeric literals like "1.0".
//==============================================================================
void CodeGen::dispatch(ValueExprAST<int_t> & e)
{
  emitLocation(&e);
  ValueResult_ = llvmValue<int_t>(TheContext_, e.Val_);
}

//==============================================================================
// RealExprAST - Expression class for numeric literals like "1.0".
//==============================================================================
void CodeGen::dispatch(ValueExprAST<real_t> & e)
{
  emitLocation(&e);
  ValueResult_ = llvmValue(TheContext_, e.Val_);
}

//==============================================================================
// StringExprAST - Expression class for string literals like "hello".
//==============================================================================
void CodeGen::dispatch(ValueExprAST<std::string>& e)
{
  emitLocation(&e);
  ValueResult_ = llvmString(TheContext_, getModule(), e.Val_);
}
 
//==============================================================================
// VariableExprAST - Expression class for referencing a variable, like "a".
//==============================================================================
void CodeGen::dispatch(VariableExprAST& e)
{

  auto Name = e.getName();

  // Look this variable up in the function.
  auto VarE = getVariable(e.getName());
  Value * V = VarE->getAlloca();
  
  // Load the value.
  auto Ty = V->getType();
  Ty = Ty->getPointerElementType();
  
  if (isa<GlobalVariable>(V)) {
    auto GV = TheModule_->getOrInsertGlobal(e.getName(), Ty);
    V = GV;
  }

  if ( e.isArray() ) {
    auto IndexVal = runExprVisitor(*e.IndexExpr_);
    ValueResult_ = loadArrayValue(V, IndexVal, VarE->getType(), Name);
  }
  else {
    ValueResult_ = Builder_.CreateLoad(Ty, V, "val."+Name);
  }
}

//==============================================================================
// ArrayExprAST - Expression class for arrays.
//==============================================================================
void CodeGen::dispatch(ArrayExprAST &e)
{
  auto TheFunction = Builder_.GetInsertBlock()->getParent();
  
  // the llvm variable type
  auto VarType = getLLVMType(e.getType());

  std::vector<Value*> InitVals;
  InitVals.reserve(e.ValExprs_.size());
  for ( auto & E : e.ValExprs_ ) 
    InitVals.emplace_back( runExprVisitor(*E) );

  Value* SizeExpr = nullptr;
  if (e.SizeExpr_) {
    SizeExpr = runExprVisitor(*e.SizeExpr_);
  }
  else {
    SizeExpr = llvmValue<int_t>(TheContext_, e.ValExprs_.size());
  }

  auto ArrayE = createArray(TheFunction, "__tmp", VarType, SizeExpr );
  auto ArrayA = ArrayE->getAlloca();

  if (e.SizeExpr_) 
    initArrays(TheFunction, {ArrayA}, InitVals[0], SizeExpr, VarType);
  else
    initArray(TheFunction, ArrayA, InitVals, VarType);

  auto Ty = ArrayA->getType()->getPointerElementType();
  ValueResult_ =  Builder_.CreateLoad(Ty, ArrayA, "__tmp");
}
  
//==============================================================================
// ArrayExprAST - Expression class for arrays.
//==============================================================================
void CodeGen::dispatch(FutureExprAST &e)
{
  //auto TheFunction = Builder_.GetInsertBlock()->getParent();
  THROW_CONTRA_ERROR("FUTURE NOT IMPLEMENTED YET");
}

//==============================================================================
// CastExprAST - Expression class for casts.
//==============================================================================
void CodeGen::dispatch(CastExprAST &e)
{
  auto FromVal = runExprVisitor(*e.FromExpr_);
  auto FromType = ValueResult_->getType();

  auto ToType = getLLVMType(e.getType());

  auto TheBlock = Builder_.GetInsertBlock();

  if (FromType->isFloatingPointTy() && ToType->isIntegerTy()) {
    ValueResult_ = CastInst::Create(Instruction::FPToSI, FromVal,
        llvmType<int_t>(TheContext_), "cast", TheBlock);
  }
  else if (FromType->isIntegerTy() && ToType->isFloatingPointTy()) {
    ValueResult_ = CastInst::Create(Instruction::SIToFP, FromVal,
        llvmType<real_t>(TheContext_), "cast", TheBlock);
  }
  else {
    ValueResult_ = FromVal;
  }

}

//==============================================================================
// UnaryExprAST - Expression class for a unary operator.
//==============================================================================
void CodeGen::dispatch(UnaryExprAST & e) {
  auto OperandV = runExprVisitor(*e.OpExpr_); 
  
  if (OperandV->getType()->isFloatingPointTy()) {
  
    switch (e.OpCode_) {
    case tok_sub:
      ValueResult_ = Builder_.CreateFNeg(OperandV, "negtmp");
      return;
    }

  }
  else {
    switch (e.OpCode_) {
    case tok_sub:
      ValueResult_ = Builder_.CreateNeg(OperandV, "negtmp");
      return;
    }
  }

  auto F = getFunction(std::string("unary") + e.OpCode_);
  emitLocation(&e);
  ValueResult_ = Builder_.CreateCall(F, OperandV, "unop");
}

//==============================================================================
// BinaryExprAST - Expression class for a binary operator.
//==============================================================================
void CodeGen::dispatch(BinaryExprAST& e) {
  emitLocation(&e);
  
  // Special case '=' because we don't want to emit the LHS as an expression.
  if (e.OpCode_ == tok_asgmt) {
    // Assignment requires the LHS to be an identifier.
    // This assume we're building without RTTI because LLVM builds that way by
    // default.  If you build LLVM with RTTI this can be changed to a
    // dynamic_cast for automatic error checking.
    auto LHSE = dynamic_cast<VariableExprAST*>(e.LeftExpr_.get());
    // Codegen the RHS.
    auto Val = runExprVisitor(*e.RightExpr_);

    // Look up the name.
    const auto & VarName = LHSE->getName();
    auto VarE = getVariable(VarName);
    Value* Variable = VarE->getAlloca();

    // array element access
    if (isArray(Variable)) {
      auto IndexVal = runExprVisitor(*LHSE->IndexExpr_);
      storeArrayValue(Val, Variable, IndexVal, VarName);
    }
    else
      Builder_.CreateStore(Val, Variable);
    ValueResult_ = Val;
    return;
  }

  Value *L = runExprVisitor(*e.LeftExpr_);
  Value *R = runExprVisitor(*e.RightExpr_);

  auto l_is_real = L->getType()->isFloatingPointTy();
  auto r_is_real = R->getType()->isFloatingPointTy();
  bool is_real =  (l_is_real && r_is_real);

  if (is_real) {
    switch (e.OpCode_) {
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
    switch (e.OpCode_) {
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
  auto F = getFunction(std::string("binary") + e.OpCode_);

  Value *Ops[] = { L, R };
  ValueResult_ = Builder_.CreateCall(F, Ops, "binop");
}

//==============================================================================
// CallExprAST - Expression class for function calls.
//==============================================================================
void CodeGen::dispatch(CallExprAST &e) {
  emitLocation(&e);

  // Look up the name in the global module table.
  auto CalleeF = getFunction(e.Callee_);
  //auto NumFixedArgs = FunType->getNumParams();
 
  const auto & Name = e.getName();
  auto IsTask = Tasker_->isTask(Name);
    
  std::vector<Value *> ArgVs;
  for (unsigned i = 0; i<e.ArgExprs_.size(); ++i) {
    auto A = runExprVisitor(*e.ArgExprs_[i]);
    ArgVs.push_back(A);
  }

  //----------------------------------------------------------------------------
  if (IsTask) {
    auto TaskI = Tasker_->getTask(Name);
    Value* FutureA = nullptr;
    
    if (e.isTopLevelTask()) {
      if (Tasker_->isStarted())
        Tasker_->postregisterTasks(*TheModule_);
      else
        Tasker_->preregisterTasks(*TheModule_);
      Tasker_->setTopLevelTask(*TheModule_, TaskI.getId());
      Tasker_->start(*TheModule_, Argc_, Argv_);
    }
    else {
      std::vector<Value*> ArgSizes;
      for (auto V : ArgVs) ArgSizes.emplace_back( getTypeSize<size_t>(V->getType()) );
      FutureA = Tasker_->launch(*TheModule_, Name, TaskI, ArgVs, ArgSizes);
    }
  
    auto CalleeT = CalleeF->getFunctionType()->getReturnType();
    if (!CalleeT->isVoidTy() && FutureA) {
      auto DataSizeV = getTypeSize<int_t>(CalleeT);
      auto CalleeA = Tasker_->getFuture(*TheModule_, FutureA, CalleeT, DataSizeV); 
      ValueResult_ = Builder_.CreateLoad(CalleeT, CalleeA);
    }
    else {
      ValueResult_ = UndefValue::get(Type::getVoidTy(TheContext_));
    }
  }
  //----------------------------------------------------------------------------
  else {
    ValueResult_ = Builder_.CreateCall(CalleeF, ArgVs, "calltmp");
  }

}

//==============================================================================
// IfExprAST - Expression class for if/then/else.
//==============================================================================
void CodeGen::dispatch(IfStmtAST & e) {
  emitLocation(&e);

  if ( e.ThenExpr_.empty() && e.ElseExpr_.empty() ) {
    ValueResult_ = Constant::getNullValue(VoidType_);
    return;
  }

  Value *CondV = runExprVisitor(*e.CondExpr_);

  auto TheFunction = Builder_.GetInsertBlock()->getParent();

  // Create blocks for the then and else cases.  Insert the 'then' block at the
  // end of the function.
  BasicBlock *ThenBB = BasicBlock::Create(TheContext_, "then", TheFunction);
  BasicBlock *ElseBB = e.ElseExpr_.empty() ? nullptr : BasicBlock::Create(TheContext_, "else");
  BasicBlock *MergeBB = BasicBlock::Create(TheContext_, "ifcont");

  if (ElseBB)
    Builder_.CreateCondBr(CondV, ThenBB, ElseBB);
  else
    Builder_.CreateCondBr(CondV, ThenBB, MergeBB);

  // Emit then value.
  Builder_.SetInsertPoint(ThenBB);

  auto OldScope = getScope();
  createScope();
  for ( auto & stmt : e.ThenExpr_ ) runStmtVisitor(*stmt);
  resetScope(OldScope);

  // get first non phi instruction
  auto ThenV = ThenBB->getFirstNonPHI();

  Builder_.CreateBr(MergeBB);

  // Codegen of 'Then' can change the current block, update ThenBB for the PHI.
  ThenBB = Builder_.GetInsertBlock();

  if (ElseBB) {

    // Emit else block.
    TheFunction->getBasicBlockList().push_back(ElseBB);
    Builder_.SetInsertPoint(ElseBB);

    createScope();
    for ( auto & stmt : e.ElseExpr_ ) runStmtVisitor(*stmt); 
    resetScope(OldScope);

    // get first non phi
    auto ElseV = ElseBB->getFirstNonPHI();

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
void CodeGen::dispatch(ForStmtAST& e) {
  
  auto TheFunction = Builder_.GetInsertBlock()->getParent();
  
  auto OldScope = getScope();
  auto InnerScope = createScope();

  // Create an alloca for the variable in the entry block.
  auto LLType = llvmType<int_t>(TheContext_);
  auto VarE = createVariable(TheFunction, e.VarId_.getName(), LLType);
  auto Alloca = VarE->getAlloca();
  
  emitLocation(&e);

  // Emit the start code first, without 'variable' in scope.
  auto StartVal = runStmtVisitor(*e.StartExpr_);
  if (StartVal->getType()->isFloatingPointTy())
    THROW_IMPLEMENTED_ERROR("Cast required for start value");

  // Store the value into the alloca.
  Builder_.CreateStore(StartVal, Alloca);

  // Make the new basic block for the loop header, inserting after current
  // block.
  BasicBlock *BeforeBB = BasicBlock::Create(TheContext_, "beforeloop", TheFunction);
  BasicBlock *LoopBB =   BasicBlock::Create(TheContext_, "loop", TheFunction);
  BasicBlock *IncrBB =   BasicBlock::Create(TheContext_, "incr", TheFunction);
  BasicBlock *AfterBB =  BasicBlock::Create(TheContext_, "afterloop", TheFunction);

  Builder_.CreateBr(BeforeBB);
  Builder_.SetInsertPoint(BeforeBB);

  // Load value and check coondition
  Value *CurVar = Builder_.CreateLoad(LLType, Alloca);

  // Compute the end condition.
  // Convert condition to a bool by comparing non-equal to 0.0.
  auto EndCond = runStmtVisitor(*e.EndExpr_);
  if (EndCond->getType()->isFloatingPointTy())
    THROW_IMPLEMENTED_ERROR("Cast required for end condition");
 
  if (e.Loop_ == ForStmtAST::LoopType::Until) {
    Value *One = llvmValue<int_t>(TheContext_, 1);
    EndCond = Builder_.CreateSub(EndCond, One, "loopsub");
  }

  EndCond = Builder_.CreateICmpSLE(CurVar, EndCond, "loopcond");


  // Insert the conditional branch into the end of LoopEndBB.
  Builder_.CreateCondBr(EndCond, LoopBB, AfterBB);

  // Start insertion in LoopBB.
  //TheFunction->getBasicBlockList().push_back(LoopBB);
  Builder_.SetInsertPoint(LoopBB);
  
  // Emit the body of the loop.  This, like any other expr, can change the
  // current BB.  Note that we ignore the value computed by the body, but don't
  // allow an error.
  createScope();
  for ( auto & stmt : e.BodyExprs_ ) runStmtVisitor(*stmt);
  resetScope(InnerScope);


  // Insert unconditional branch to increment.
  Builder_.CreateBr(IncrBB);

  // Start insertion in LoopBB.
  //TheFunction->getBasicBlockList().push_back(IncrBB);
  Builder_.SetInsertPoint(IncrBB);
  

  // Emit the step value.
  Value *StepVal = nullptr;
  if (e.StepExpr_) {
    StepVal = runStmtVisitor(*e.StepExpr_);
    if (StepVal->getType()->isFloatingPointTy())
      THROW_IMPLEMENTED_ERROR("Cast required for step value");
  } else {
    // If not specified, use 1.0.
    StepVal = llvmValue<int_t>(TheContext_, 1);
  }


  // Reload, increment, and restore the alloca.  This handles the case where
  // the body of the loop mutates the variable.
  CurVar = Builder_.CreateLoad(LLType, Alloca);
  Value *NextVar = Builder_.CreateAdd(CurVar, StepVal, "nextvar");
  Builder_.CreateStore(NextVar, Alloca);

  // Insert the conditional branch into the end of LoopEndBB.
  Builder_.CreateBr(BeforeBB);

  // Any new code will be inserted in AfterBB.
  //TheFunction->getBasicBlockList().push_back(AfterBB);
  Builder_.SetInsertPoint(AfterBB);

  // for expr always returns 0.
  resetScope(OldScope);
  ValueResult_ = UndefValue::get(VoidType_);
}

//==============================================================================
// VarDefExprAST - Expression class for var/in
//==============================================================================
void CodeGen::dispatch(VarDeclAST & e) {
  auto TheFunction = Builder_.GetInsertBlock()->getParent();

  // Emit the initializer before adding the variable to scope, this prevents
  // the initializer from referencing the variable itself, and permits stuff
  // like this:
  //  var a = 1 in
  //    var a = a in ...   # refers to outer 'a'.

  // Emit initializer first
  auto InitVal = runExprVisitor(*e.InitExpr_);
  //auto IType = InitVal->getType();
  
  // the llvm variable type
  const auto & Ty = e.getType();
  auto VarType = getLLVMType(Ty);

  // Register all variables and emit their initializer.
  for (const auto & VarId : e.VarIds_) {  
    auto VarE = createVariable(TheFunction, VarId.getName(), VarType, Ty.isGlobal());
    auto Alloca = VarE->getAlloca();
    if (Ty.isGlobal())
      cast<GlobalVariable>(Alloca)->setInitializer(cast<Constant>(InitVal));
    else
      Builder_.CreateStore(InitVal, Alloca);
  }

  emitLocation(&e);
  ValueResult_ = InitVal;
}

//==============================================================================
// ArrayExprAST - Expression class for arrays.
//==============================================================================
void CodeGen::dispatch(ArrayDeclAST &e) {
  auto TheFunction = Builder_.GetInsertBlock()->getParent();
  
  // Emit the initializer before adding the variable to scope, this prevents
  // the initializer from referencing the variable itself, and permits stuff
  // like this:
  //  var a = 1 in
  //    var a = a in ...   # refers to outer 'a'.

  
  // the llvm variable type
  const auto & Ty = e.getType();
  auto VarType = getLLVMType(Ty);
    
  int NumVars = e.VarIds_.size();

  //---------------------------------------------------------------------------
  // Array already on right hand side
  auto ArrayAST = dynamic_cast<ArrayExprAST*>(e.InitExpr_.get());
  if (ArrayAST) {

    ValueResult_ = runExprVisitor(*ArrayAST);

    // transfer to first
    const auto & VarName = e.VarIds_[0].getName();
    auto VarE = moveVariable("__tmp", VarName);
    auto Alloca = VarE->getAlloca();

    //auto SizeExpr = getArraySize(Alloca, VarName);
    auto SizeExpr = VarE->getSize();
  
    // Register all variables and emit their initializer.
    std::vector<Value*> ArrayAllocas;
    for (int i=1; i<NumVars; ++i) {
      const auto & VarName = e.VarIds_[i].getName();
      auto ArrayE = createArray(TheFunction, VarName, VarType, SizeExpr);
      ArrayAllocas.emplace_back( ArrayE->getAlloca() ); 
    }

    copyArrays(TheFunction, Alloca, ArrayAllocas, SizeExpr, VarE->getType() );

  }
  
  //---------------------------------------------------------------------------
  // Scalar Initializer
  else {
  
    // Emit initializer first
    auto InitVal = runExprVisitor(*e.InitExpr_);

    // create a size expr
    Value* SizeExpr = nullptr;
    if (e.SizeExpr_)
      SizeExpr = runExprVisitor(*e.SizeExpr_);
    // otherwise scalar
    else
      SizeExpr = llvmValue<int_t>(TheContext_, 1);
 
    // Register all variables and emit their initializer.
    std::vector<Value*> ArrayAllocas;
    for (const auto & VarId : e.VarIds_) {
      const auto & VarName = VarId.getName();
      auto ArrayE = createArray(TheFunction, VarName, VarType, SizeExpr);
      ArrayAllocas.emplace_back(ArrayE->getAlloca());
    }

    initArrays(TheFunction, ArrayAllocas, InitVal, SizeExpr, VarType);

    ValueResult_ = InitVal;

  } // else
  //---------------------------------------------------------------------------

  emitLocation(&e);
}

//==============================================================================
/// PrototypeAST - This class represents the "prototype" for a function.
//==============================================================================
void CodeGen::dispatch(PrototypeAST &e) {

  unsigned NumArgs = e.ArgIds_.size();

  std::vector<Type *> ArgTypes;
  ArgTypes.reserve(NumArgs);

  for (unsigned i=0; i<NumArgs; ++i) {
    auto VarType = getLLVMType(e.ArgTypeIds_[i]);
    if (e.ArgIsArray_[i]) VarType = ArrayType_;
    ArgTypes.emplace_back(VarType);
  }
  
  Type* ReturnType = VoidType_;
  if (e.ReturnType_) ReturnType = getLLVMType(e.ReturnType_);
  FunctionType *FT = FunctionType::get(ReturnType, ArgTypes, false);

  Function *F = Function::Create(FT, Function::ExternalLinkage, e.Id_.getName(), &getModule());

  // Set names for all arguments.
  unsigned Idx = 0;
  for (auto &Arg : F->args()) Arg.setName(e.ArgIds_[Idx++].getName());

  FunctionResult_ = F;

}
 

//==============================================================================
/// FunctionAST Helper
//==============================================================================
Value* CodeGen::codegenFunctionBody(FunctionAST& e)
{
  for ( auto & stmt : e.BodyExprs_ )
  {
    emitLocation(stmt.get());
    runStmtVisitor(*stmt);
  }

  // Finish off the function.
  Value* RetVal = nullptr;
  if ( e.ReturnExpr_ ) RetVal = runStmtVisitor(*e.ReturnExpr_);

  return RetVal;
}

//==============================================================================
/// FunctionAST - This class represents a function definition itself.
//==============================================================================
void CodeGen::dispatch(FunctionAST& e)
{
  auto OldScope = getScope();
  if (!e.isTopLevelExpression()) createScope();

  // Transfer ownership of the prototype to the FunctionProtos map, but keep a
  // reference to it for use below.
  auto & P = insertFunction( std::move(e.ProtoExpr_) );
  const auto & Name = P.getName();
  auto TheFunction = getFunction(Name);

  // Create a new basic block to start insertion into.
  BasicBlock *BB = BasicBlock::Create(TheContext_, "entry", TheFunction);
  Builder_.SetInsertPoint(BB);

  // Create a subprogram DIE for this function.
  auto Unit = createFile();
  unsigned LineNo = P.getLine();
  unsigned ScopeLine = LineNo;
  DISubprogram *SP = createSubprogram( LineNo, ScopeLine, P.getName(),
      TheFunction->arg_size(), Unit);
  if (SP)
    TheFunction->setSubprogram(SP);

  // Push the current scope.
  pushLexicalBlock(SP);

  // Unset the location for the prologue emission (leading instructions with no
  // location in a function are considered part of the prologue and the debugger
  // will run past them when breaking on a function)
  emitLocation(nullptr);

  // Record the function arguments in the NamedValues map.
  unsigned ArgIdx = 0;
  for (auto &Arg : TheFunction->args()) {

    // get arg type
    auto ArgType = P.ArgTypes_[ArgIdx];
    // the llvm variable type
    auto LLType = getLLVMType(ArgType);

    // Create an alloca for this variable.
    VariableEntry* VarE;
    if (ArgType.isArray())
      VarE = createArray(TheFunction, Arg.getName(), LLType);
    else
      VarE = createVariable(TheFunction, Arg.getName(), LLType);
    VarE->setOwner(false);
    auto Alloca = VarE->getAlloca();
    
    // Create a debug descriptor for the variable.
    createVariable( SP, Arg.getName(), ++ArgIdx, Unit, LineNo, Alloca);

    // Store the initial value into the alloca.
    Builder_.CreateStore(&Arg, Alloca);
  
  }
 
  // codegen the function body
  auto RetVal = codegenFunctionBody(e);

  // garbage collection
  resetScope(OldScope);
  
  // Finish off the function.
  if (RetVal && !RetVal->getType()->isVoidTy() )
    Builder_.CreateRet(RetVal);
  else
    Builder_.CreateRetVoid();
  
  // Validate the generated code, checking for consistency.
  verifyFunction(*TheFunction);
   
  FunctionResult_ = TheFunction;

}

//==============================================================================
/// TaskAST - This class represents a function definition itself.
//==============================================================================
void CodeGen::dispatch(TaskAST& e)
{
  auto OldScope = getScope();
  if (!e.isTopLevelExpression()) createScope();

  // Transfer ownership of the prototype to the FunctionProtos map, but keep a
  // reference to it for use below.
  auto & P = insertFunction( std::move(e.ProtoExpr_) );
  const auto & Name = P.getName();
  auto TheFunction = getFunction(Name);
  
  // generate wrapped task
  auto Wrapper = Tasker_->taskPreamble(*TheModule_, Name, TheFunction);

  // insert arguments into variable table
  unsigned ArgIdx = 0;
  for (auto &Arg : TheFunction->args()) {
    auto Alloca = Wrapper.ArgAllocas[ArgIdx++];
    auto AllocaT = Alloca->getType()->getPointerElementType(); // FIX FOR ARRAYS
    auto NewEntry = VariableEntry(Alloca, AllocaT);
    insertVariable(Arg.getName(), NewEntry);
  }

  
  // codegen the function body
  auto RetVal = codegenFunctionBody(e);
  
  // Finish wrapped task
  Tasker_->taskPostamble(*TheModule_, RetVal);

  // garbage collection
  resetScope(OldScope);
  
  // Finish off the function.  Tasks always return void
  Builder_.CreateRetVoid();
  
  // Validate the generated code, checking for consistency.
  verifyFunction(*Wrapper.TheFunction);
  
  // insert the task 
  Tasker_->insertTask(Name, Wrapper.TheFunction);
  FunctionResult_ = Wrapper.TheFunction;

}

} // namespace
