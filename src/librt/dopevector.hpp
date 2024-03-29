#ifndef RTLIB_DOPEVECTOR_HPP
#define RTLIB_DOPEVECTOR_HPP

#include "dllexport.h"
#include "llvm_forwards.hpp"

#include "config.hpp"

#include <memory>
#include <string>

extern "C" {

/// simple dopevector type
struct dopevector_t {
  void * data = nullptr;
  int_t size = 0;
  int_t capacity = 0;
  int_t data_size = 0;
  void setup(int_t sz, int_t data_sz, void* dat)
  {
    data = dat;
    size = sz;
    data_size = data_sz;
    capacity = sz;
  }
  int_t bytes() const { return size*data_size; }
};

/// memory allocation
DLLEXPORT void dopevector_allocate(int_t size, int_t data_size, dopevector_t * dv);

/// memory deallocation
DLLEXPORT void dopevector_deallocate(dopevector_t * dv);

/// memory deallocation
DLLEXPORT void dopevector_copy(dopevector_t * src, dopevector_t * tgt);

} // extern

namespace contra {
class FunctionDef;
}

namespace librt {

struct DopeVector {
  static llvm::StructType* DopeVectorType;
  static void setup(llvm::LLVMContext &);
  static bool isDopeVector(llvm::Type* Ty);
  static bool isDopeVector(llvm::Value* V);
};

struct DopeVectorAllocate : public DopeVector {
  static const std::string Name;
  static llvm::Function *install(llvm::LLVMContext &, llvm::Module &);
  static std::unique_ptr<contra::FunctionDef> check();
};

struct DopeVectorDeAllocate : public DopeVector {
  static const std::string Name;
  static llvm::Function *install(llvm::LLVMContext &, llvm::Module &);
  static std::unique_ptr<contra::FunctionDef> check();
};

struct DopeVectorCopy : public DopeVector {
  static const std::string Name;
  static llvm::Function *install(llvm::LLVMContext &, llvm::Module &);
  static std::unique_ptr<contra::FunctionDef> check();
};

} // namespace


#endif // RTLIB_DOPEVECTOR_HPP
