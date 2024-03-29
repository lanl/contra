#ifndef CONTRA_RTLIB_HPP
#define CONTRA_RTLIB_HPP

#include <map>
#include <string>

namespace llvm {
class Function;
class LLVMContext;
class Module;
}

namespace contra {
class FunctionDef;
}

namespace librt {

//==============================================================================
// Class to keep track of available runtime functions
//==============================================================================
class RunTimeLib {

  template<typename T>
  static bool _setup() {
    SetupMap.emplace( T::Name, T::setup );
    InstallMap.emplace( T::Name, T::install );
    SemantecMap.emplace( T::Name, T::check );
    return true;
  }
  template<typename T, typename U, typename...Args>
  static bool _setup() {
    SetupMap.emplace( T::Name, T::setup );
    InstallMap.emplace( T::Name, T::install );
    SemantecMap.emplace( T::Name, T::check );
    return _setup<U, Args...>();
  }
  
  typedef void (*SetupFunctionPointer) (llvm::LLVMContext &);
  static std::map<std::string, SetupFunctionPointer> SetupMap;

  typedef llvm::Function* (*LlvmFunctionPointer) (llvm::LLVMContext &, llvm::Module &);
  static std::map<std::string, LlvmFunctionPointer> InstallMap;

  typedef std::unique_ptr<contra::FunctionDef> (*FunctionPointer) (void);
  static std::map<std::string, FunctionPointer> SemantecMap;

public:

  static void setup(llvm::LLVMContext &);

  static llvm::Function* tryInstall(
      llvm::LLVMContext &TheContext,
      llvm::Module &TheModule,
      const std::string & Name)
  {
    auto it = InstallMap.find(Name);
    if (it != InstallMap.end())
      return it->second(TheContext, TheModule);
    return nullptr;
  }
  
  static std::unique_ptr<contra::FunctionDef> 
  tryInstall(const std::string & Name);


};

}


#endif //CONTRA_RTLIB_HPP
