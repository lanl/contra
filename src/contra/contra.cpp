#include "contra.hpp"
#include "errors.hpp"
#include "inputs.hpp"
#include "analysis.hpp"
#include "vizualizer.hpp"

#include <iostream>

using namespace llvm;

namespace contra {

//==============================================================================
// Top-Level definition handler
//==============================================================================
void handleFunction(Parser & TheParser, CodeGen & TheCG, const InputsType & TheInputs)
{
  auto is_interactive = TheInputs.is_interactive;
  auto is_verbose = TheInputs.is_verbose;
  auto dump_ir = TheInputs.dump_ir;
  auto is_optimized = TheInputs.is_optimized;
  Vizualizer TheViz("graph.dot");
  Analyzer TheAnalyser(TheParser.getBinopPrecedence());

  if (is_verbose) std::cerr << "Handling function" << std::endl;

  //auto OldSymbols = TheParser.getSymbols();

  try {
    auto FnAST = TheParser.parseFunction();
    if (is_verbose) TheViz.runVisitor(*FnAST);
    TheAnalyser.runFuncVisitor(*FnAST);
    auto FnIR = TheCG.runFuncVisitor(*FnAST);
    if (is_optimized) TheCG.optimize(FnIR);
    if (is_verbose || dump_ir) FnIR->print(errs());
    if (!TheCG.isDebug()) {
      TheCG.doJIT();
    }
  }
  catch (const CodeError & e) {
    std::cerr << e.what() << std::endl;
    std::cerr << std::endl;
    TheParser.barf(std::cerr, e.getLoc());
    std::cerr << std::endl;
    // Skip token for error recovery.
    if (!is_interactive) throw e;
    TheParser.getNextToken();
  }
  catch (const ContraError & e) {
    // Skip token for error recovery.
    if (!is_interactive) throw e;
    TheParser.getNextToken();
  }

  //TheParser.setSymbols( OldSymbols );
}

//==============================================================================
// Top-Level definition handler
//==============================================================================
void handleDefinition(Parser & TheParser, CodeGen & TheCG, const InputsType & TheInputs)
{
  auto is_interactive = TheInputs.is_interactive;
  auto is_verbose = TheInputs.is_verbose;
  auto dump_ir = TheInputs.dump_ir;

  if (is_verbose) std::cerr << "Handling definition" << std::endl;

  try {
    auto FnAST = TheParser.parseDefinition();
    //if (is_verbose) FnAST->accept(viz);
    auto FnIR = TheCG.runFuncVisitor(*FnAST);
    if (is_verbose || dump_ir) FnIR->print(errs());
    if (!TheCG.isDebug()) {
      TheCG.doJIT();
    }
  }
  catch (const ContraError & e) {
    std::cerr << e.what() << std::endl;
    // Skip token for error recovery.
    if (is_interactive) {
      TheParser.getNextToken();
    }
    // otherwise keep throwing the error
    else {
      throw e;
    }
  }
}

//==============================================================================
// Top-Level external handler
//==============================================================================
void handleExtern(Parser & TheParser, CodeGen & TheCG, const InputsType & TheInputs)
{
  auto is_verbose = TheInputs.is_verbose;
  auto is_interactive = TheInputs.is_interactive;
  auto dump_ir = TheInputs.dump_ir;

  if (is_verbose) std::cerr << "Handling extern" << std::endl;

  try {
    auto ProtoAST = TheParser.parseExtern();
    auto FnIR = TheCG.runFuncVisitor(*ProtoAST);
    if (is_verbose || dump_ir) FnIR->print(errs());
    if (!TheCG.isDebug()) {
      TheCG.FunctionProtos[ProtoAST->getName()] = std::move(ProtoAST);
    }
  }
  catch (const ContraError & e) {
    std::cerr << e.what() << std::endl;
    // Skip token for error recovery.
    if (is_interactive) {
      TheParser.getNextToken();
    }
    // otherwise keep throwing the error
    else {
      throw e;
    }
  }
}

//==============================================================================
// Top-Level expression handler
//==============================================================================
void handleTopLevelExpression(Parser & TheParser, CodeGen & TheCG,
    const InputsType & TheInputs)
{
  auto is_interactive = TheInputs.is_interactive;
  auto is_verbose = TheInputs.is_verbose;
  auto dump_ir = TheInputs.dump_ir;

  if (is_verbose) std::cerr << "Handling top level expression" << std::endl;

  // Evaluate a top-level expression into an anonymous function.
  try {
    auto FnAST = TheParser.parseTopLevelExpr();
    //if (is_verbose) FnAST->accept(viz);
    auto FnIR = TheCG.runFuncVisitor(*FnAST);
    auto RetType = FnIR->getReturnType();
    auto is_real = RetType->isFloatingPointTy();
    auto is_int = RetType->isIntegerTy();
    auto is_void = RetType->isVoidTy();
    if (is_verbose || dump_ir) FnIR->print(errs());
    if (!TheCG.isDebug()) {
      // JIT the module containing the anonymous expression, keeping a handle so
      // we can free it later.
      auto H = TheCG.doJIT();

      // Search the JIT for the __anon_expr symbol.
      auto ExprSymbol = TheCG.findSymbol("__anon_expr");
      assert(ExprSymbol && "Function not found");

      // Get the symbol's address and cast it to the right type (takes no
      // arguments, returns a double) so we can call it as a native function.
      if (is_real) {
        real_t (*FP)() = (real_t (*)())(intptr_t)cantFail(ExprSymbol.getAddress());
        if (is_verbose) std::cerr << "---Begin Real Result--- " <<  "\n";
        auto ans = FP();
        std::cerr << "Ans = " << ans << "\n";
        if (is_verbose) std::cerr << "---End Real Result--- " <<  "\n";
      }
      else if (is_int) {
        int_t (*FP)() = (int_t(*)())(intptr_t)cantFail(ExprSymbol.getAddress());
        if (is_verbose) std::cerr << "---Begin Int Result--- " <<  "\n";
        auto ans = FP();
        std::cerr << "Ans = " << ans << "\n";
        if (is_verbose) std::cerr << "---End Int Result--- " <<  "\n";
      }
      else if (is_void) {
        void (*FP)() = (void(*)())(intptr_t)cantFail(ExprSymbol.getAddress());
        if (is_verbose) std::cerr << "---Begin Void Result--- " <<  "\n";
        FP();
        if (is_verbose) std::cerr << "---End Void Result--- " <<  "\n";
      }
      else {
        THROW_CONTRA_ERROR("Unknown type of final result!");
      }
      
      // Delete the anonymous expression module from the JIT.
      TheCG.removeJIT( H );
    }
  }
  catch (const ContraError & e) {
    std::cerr << e.what() << std::endl;
    // Skip token for error recovery.
    if (is_interactive) {
      TheParser.getNextToken();
    }
    // otherwise keep throwing the error
    else {
      throw e;
    }
  }
}

//==============================================================================
/// top ::= definition | external | expression | ';'
//==============================================================================
void mainLoop( Parser & TheParser, CodeGen & TheCG, const InputsType & TheInputs) {

  auto is_interactive = TheInputs.is_interactive;

  // Prime the first token.
  if (is_interactive) std::cerr << "contra> " << std::flush;
  TheParser.getNextToken();

  while (true) {

    if (TheParser.getCurTok() == tok_eof) {
      if (is_interactive) std::cerr << std::endl;
      return;
    }

    switch (TheParser.getCurTok()) {
    case tok_sep: // ignore top-level semicolons.
      TheParser.getNextToken();
      break;
    case tok_def:
      handleDefinition(TheParser, TheCG, TheInputs);
      if (is_interactive) std::cerr << "contra> " << std::flush;
      break;
    case tok_function:
      handleFunction(TheParser, TheCG, TheInputs);
      if (is_interactive) std::cerr << "contra> " << std::flush;
      break;
    case tok_extern:
      handleExtern(TheParser, TheCG, TheInputs);
      if (is_interactive) std::cerr << "contra> " << std::flush;
      break;
    default:
      handleTopLevelExpression(TheParser, TheCG, TheInputs);
      if (is_interactive) std::cerr << "contra> " << std::flush;
    }

  }
}


} // namespace