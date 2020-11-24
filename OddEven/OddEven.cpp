//=============================================================================
// FILE:
//    OddEven.cpp
//
// DESCRIPTION:
//
// USAGE:
//    2. New PM
//      opt -load-pass-plugin=libOddEven.so -passes="odd-even" `\`
//        -disable-output <input-llvm-file>
//
//
// License: MIT
//=============================================================================
#include "llvm/IR/IRBuilder.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;


namespace {

// This method implements what the pass does
bool visitor(Module &M) {

  // STEP1. Inject the declaration of printf : From InjectFunctionCall.cpp
  auto &CTX = M.getContext();
  PointerType *PrintfArgTy = PointerType::getUnqual(Type::getInt8Ty(CTX));
  FunctionType *PrintfTy = FunctionType::get(
        IntegerType::getInt32Ty(CTX),
        PrintfArgTy,
        /*IsVarArgs=*/true);

    FunctionCallee Printf = M.getOrInsertFunction("printf", PrintfTy);

    // Set attributes as per inferLibFuncAttributes in BuildLibCalls.cpp
    Function *PrintfF = dyn_cast<Function>(Printf.getCallee());
    PrintfF->setDoesNotThrow();
    PrintfF->addParamAttr(0, Attribute::NoCapture);
    PrintfF->addParamAttr(0, Attribute::ReadOnly);

  // STEP2. Inject Global String
  llvm::Constant *PrintfFormatStr = llvm::ConstantDataArray::getString(CTX, "In function %s, %s in address %p, it is %s\n");
  Constant *PrintfFormatStrVar = M.getOrInsertGlobal("PrintfFormatStr", PrintfFormatStr->getType());
  dyn_cast<GlobalVariable>(PrintfFormatStrVar)->setInitializer(PrintfFormatStr);

  auto Val0 = ConstantInt::get(IntegerType::get(CTX,64), 0);
  auto Val1 = ConstantInt::get(IntegerType::get(CTX,64), 1);

  // STEP3. Find all Load/Store and inject print addrss call.
  for(auto &F: M){
    for(auto &B: F){
      for(BasicBlock::iterator i = B.begin(); i!= B.end(); i++){
        Value *pt = nullptr;
        GlobalVariable *LoadStoreString;
        bool LSFlag = false;
        if(i->getOpcode() == Instruction::Store){
          LSFlag = true;
          StoreInst *st = dyn_cast<StoreInst>(i);
          pt = st->getPointerOperand();
        }
        else if(i->getOpcode() == Instruction::Load){
          LoadInst *ld = dyn_cast<LoadInst>(i);
          pt = ld->getPointerOperand();
        } 
        else{
          continue;
        }
        IRBuilder<> Builder(i->getNextNode()); // i->getPrevNode can be cause of crash
        // if(LSFlag){LoadStoreString = Builder.CreateGlobalString("Store");}
        // else{LoadStoreString = Builder.CreateGlobalString("Load");}
        LoadStoreString = Builder.CreateGlobalString(i->getOpcodeName());
        llvm::Value *FormatStrPtr = Builder.CreatePointerCast(PrintfFormatStrVar, PrintfArgTy, "formatStr");
        auto FunctionName = Builder.CreateGlobalString(F.getName());
        auto addr = Builder.CreatePtrToInt(pt, IntegerType::get(CTX,64));
        Value *isEven = Builder.CreateAnd(addr, Val1);
        Value *Cond = Builder.CreateICmpEQ(isEven, Val0);

        BasicBlock *Head = &B;
        BasicBlock *Tail = Head->splitBasicBlock(dyn_cast<Instruction>(Cond)->getNextNode()->getIterator());

        BasicBlock *OddBlock = BasicBlock::Create(CTX, "OddBlock", &F, Tail);
        BasicBlock *EvenBlock = BasicBlock::Create(CTX, "EvenBlock", &F, Tail);

        Instruction *OldTerm = Head->getTerminator();
        BranchInst *NewTerm = BranchInst::Create(EvenBlock, OddBlock, Cond);
        ReplaceInstWithInst(OldTerm, NewTerm);

        IRBuilder<> EvenBuilder(EvenBlock);
        EvenBuilder.CreateCall(Printf, {FormatStrPtr, FunctionName,LoadStoreString , addr, EvenBuilder.CreateGlobalString("Even")});
        EvenBuilder.CreateBr(Tail);
        IRBuilder<> OddBuilder(OddBlock);
        OddBuilder.CreateCall(Printf, {FormatStrPtr, FunctionName, LoadStoreString, addr, EvenBuilder.CreateGlobalString("Odd")});
        OddBuilder.CreateBr(Tail);
      } // Basic Block Loop
    } // Function Loop
  }// Module loop
  return true;
}

// New PM implementation
struct OddEven : PassInfoMixin<OddEven> {
  // Main entry point, takes IR unit to run the pass on (&F) and the
  // corresponding pass manager (to be queried if need be)
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &) {
    bool Changed = visitor(M);
    return (Changed ? PreservedAnalyses::none()
              : PreservedAnalyses::all()) ;
    // return PreservedAnalyses::all();
  }
};

// Legacy PM implementation
struct LegacyHelloWorld : public FunctionPass {
  static char ID;
  LegacyHelloWorld() : FunctionPass(ID) {}
  // Main entry point - the name conveys what unit of IR this is to be run on.
  bool runOnFunction(Function &F) override {
    // visitor(F);
    // Doesn't modify the input unit of IR, hence 'false'
    return false;
  }
};
} // namespace

//-----------------------------------------------------------------------------
// New PM Registration
//-----------------------------------------------------------------------------
llvm::PassPluginLibraryInfo getOddEvenPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "OddEven", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, ModulePassManager &MPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name == "odd-even") {
                    MPM.addPass(OddEven());
                    return true;
                  }
                  return false;
                });
          }};
}

// This is the core interface for pass plugins. It guarantees that 'opt' will
// be able to recognize HelloWorld when added to the pass pipeline on the
// command line, i.e. via '-passes=hello-world'
extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return getOddEvenPluginInfo();
}
/*
//-----------------------------------------------------------------------------
// Legacy PM Registration
//-----------------------------------------------------------------------------
// The address of this variable is used to uniquely identify the pass. The
// actual value doesn't matter.
char LegacyHelloWorld::ID = 0;

// This is the core interface for pass plugins. It guarantees that 'opt' will
// recognize LegacyHelloWorld when added to the pass pipeline on the command
// line, i.e.  via '--legacy-hello-world'
static RegisterPass<LegacyHelloWorld>
    X("legacy-hello-world", "Hello World Pass",
      true, // This pass doesn't modify the CFG => true
      false // This pass is not a pure analysis pass => false
    );
*/