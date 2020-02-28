#ifndef CONTRA_JIT_H
#define CONTRA_JIT_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/Orc/CompileUtils.h"
#include "llvm/ExecutionEngine/Orc/IRCompileLayer.h"
#include "llvm/ExecutionEngine/Orc/LambdaResolver.h"
#include "llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h"
#include "llvm/ExecutionEngine/RTDyldMemoryManager.h"
#include "llvm/ExecutionEngine/SectionMemoryManager.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Mangler.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace contra {

class JIT {
public:
  
  using ObjLayerT = llvm::orc::LegacyRTDyldObjectLinkingLayer;

  using Compiler = llvm::orc::SimpleCompiler;
  using CompileLayerT = llvm::orc::LegacyIRCompileLayer<ObjLayerT, Compiler>;
  
  using JITSymbol = llvm::JITSymbol;
  using VModuleKey = llvm::orc::VModuleKey;

  JIT() 
    : Resolver(
        llvm::orc::createLegacyLookupResolver(
          ES,
          [this](const std::string &Name) { return findMangledSymbol(Name); },
          [](llvm::Error Err) { llvm::cantFail(std::move(Err), "lookupFlags failed"); }
        )
      ),
      TM(llvm::EngineBuilder().selectTarget()),
      DL(TM->createDataLayout()),
      ObjectLayer(
        llvm::AcknowledgeORCv1Deprecation,
        ES,
        [this](VModuleKey) {
          return ObjLayerT::Resources{
            std::make_shared<llvm::SectionMemoryManager>(), Resolver
          };
        }
      ),
      CompileLayer(
        llvm::AcknowledgeORCv1Deprecation,
          ObjectLayer,
          Compiler(*TM)
      ) 
  {
    llvm::sys::DynamicLibrary::LoadLibraryPermanently(nullptr);
  }

  auto &getTargetMachine() { return *TM; }

  auto addModule(std::unique_ptr<llvm::Module> M) {
    auto K = ES.allocateVModule();
    llvm::cantFail(CompileLayer.addModule(K, std::move(M)));
    ModuleKeys.push_back(K);
    return K;
  }

  void removeModule(VModuleKey K) {
    ModuleKeys.erase(llvm::find(ModuleKeys, K));
    llvm::cantFail(CompileLayer.removeModule(K));
  }

  auto findSymbol(const std::string Name) {
    return findMangledSymbol(mangle(Name));
  }

private:

  std::string mangle(const std::string &Name);
  JITSymbol findMangledSymbol(const std::string &Name);

  llvm::orc::ExecutionSession ES;
  std::shared_ptr<llvm::orc::SymbolResolver> Resolver;
  std::unique_ptr<llvm::TargetMachine> TM;
  const llvm::DataLayout DL;
  ObjLayerT ObjectLayer;
  CompileLayerT CompileLayer;
  std::vector<VModuleKey> ModuleKeys;
};

} // end namespace

#endif // CONTRA_CONTRAJIT_H