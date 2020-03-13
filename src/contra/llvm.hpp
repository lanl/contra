#ifndef CONTRA_LLVM_HPP
#define CONTRA_LLVM_HPP

namespace llvm {
class LLVMContext;
class Module;
}

/// start llvm
namespace contra {

void startLLVM();
void compileLLVM(llvm::Module &, const std::string &);

}


#endif //CONTRA_LLVM_HPP