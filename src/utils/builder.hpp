#ifndef UTILS_BUILDER_HPP
#define UTILS_BUILDER_HPP

#include "config.hpp"

#include "llvm_utils.hpp"

#include "llvm/IR/Constants.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/IRBuilder.h"

namespace utils {

class BuilderHelper {
  using AllocaInst = llvm::AllocaInst;
  using CallInst = llvm::CallInst;
  using FunctionCallee = llvm::FunctionCallee;
  using Function = llvm::Function;
  using Instruction = llvm::Instruction;
  using Module = llvm::Module;
  using Type = llvm::Type;
  using Value = llvm::Value;

  llvm::LLVMContext TheContext_;
  llvm::IRBuilder<> Builder_;

public:
  BuilderHelper() : Builder_(TheContext_) {}

  auto & getBuilder() { return Builder_; }
  auto & getContext() { return TheContext_; }
  
  Value* createCast(Value* FromVal, Type* ToType); 
  Value* createBitCast(Value* FromVal, Type* ToType);

  Value* getAsValue(Value*);
  Value* getAsValue(Value*, Type*);
  AllocaInst* getAsAlloca(Value*);

  Value* getElementPointer(AllocaInst*, unsigned);
  Value* extractValue(Value*, unsigned);
  void insertValue(Value*, Value*, unsigned);

  Value* offsetPointer(Value*, Value*);

  Type* getAllocatedType(Value*);

  Value* getTypeSize(Type*, Type*);
  std::size_t getTypeSize(const llvm::Module & TheModule, Type* Ty);
 
  template<typename T>
  Value* getTypeSize(Type* ElementType)
  { return getTypeSize(ElementType, llvmType<T>(TheContext_)); }

  AllocaInst* createEntryBlockAlloca(Type*, const std::string & = "");
  AllocaInst* createEntryBlockAlloca(Function*, Type*, const std::string & = "");

  Value* load(AllocaInst*, const std::string & ="");
  Value* load(Value*, const std::string & ="");

  void increment(Value*, Value*, const std::string & = "");
  
  template<
    typename T,
    typename = std::enable_if_t< !std::is_pointer<T>::value >
    >
  void increment(
    Value* OffsetA,
    T Offset,
    const std::string & Name = "")
  {
    auto OffsetT = OffsetA->getType()->getPointerElementType();
    auto OffsetV = llvmValue(TheContext_, OffsetT, Offset);
    increment(OffsetA, OffsetV, Name);
  }
  
  Instruction* createMalloc(Type*, Value*, const std::string & ="");

  void createFree(Value*, const std::string & ="");

  FunctionCallee createFunction(
      Module &,
      const std::string &,
      Type*,
      const std::vector<Type*> & = {});

  CallInst* callFunction(
      Module &,
      const std::string &,
      Type*,
      const std::vector<Value*> & = {},
      const std::string & Str = "");

};

} // namespace

#endif // UTILS_BUILDER_HPP