//
// Copyright (c) 2008 Zvonimir Rakamaric (zvonimir@cs.utah.edu)
// This file is distributed under the MIT License. See LICENSE for details.
//
#include "smack/SmackModuleGenerator.h"
#include "smack/SmackOptions.h"
#include "smack/Slicing.h"

namespace smack {

llvm::RegisterPass<SmackModuleGenerator> X("smack", "SMACK generator pass");
char SmackModuleGenerator::ID = 0;

class SpecCollector : public llvm::InstVisitor<SpecCollector> {
private:
  SmackRep& rep;
  ProcDecl& proc;
  Program& program;

public:
  SpecCollector(SmackRep& r, ProcDecl& d, Program& p)
    : rep(r), proc(d), program(p) {}

  void visitCallInst(llvm::CallInst& ci) {
    llvm::Function* f = ci.getCalledFunction();
    if (f && rep.id(f).find("requires") != string::npos) {
      proc.addRequires(slice(rep,&ci));
    }
  }

};

void SmackModuleGenerator::generateProgram(llvm::Module& m, SmackRep* rep) {

  rep->setProgram( &program );
  rep->collectRegions(m);
  
  DEBUG(errs() << "Analyzing globals...\n");

  for (llvm::Module::const_global_iterator
       x = m.global_begin(), e = m.global_end(); x != e; ++x)
    program.addDecls(rep->globalDecl(x));
  
  if (rep->hasStaticInits())
    program.addDecl(rep->getStaticInit());

  DEBUG(errs() << "Analyzing functions...\n");

  for (llvm::Module::iterator func = m.begin(), e = m.end();
       func != e; ++func) {

    if (rep->isIgnore(func))
      continue;

    if (rep->isMallocOrFree(func)) {
      program.addDecls(rep->globalDecl(func));
      continue;
    }

    if (!func->isVarArg())
      program.addDecls(rep->globalDecl(func));

    ProcDecl* proc = rep->proc(func,0);
    if (proc->getName() != "__SMACK_decls")
      program.addDecl(proc);

    if (!func->isDeclaration() && !func->empty()
        && !func->getEntryBlock().empty()) {

      DEBUG(errs() << "Analyzing function: " << rep->id(func) << "\n");

      SpecCollector sc(*rep, *proc, program);
      sc.visit(func);

      SmackInstGenerator igen(*rep, *proc);
      igen.visit(func);

      // First execute static initializers, in the main procedure.
      if (rep->id(func) == "main" && rep->hasStaticInits())
        proc->insert(Stmt::call(SmackRep::STATIC_INIT));

      DEBUG(errs() << "Finished analyzing function: " << rep->id(func) << "\n\n");
    }

    // MODIFIES
    // ... to do below, after memory splitting is determined.
  }

  // MODIFIES
  vector<ProcDecl*> procs = program.getProcs();
  for (unsigned i=0; i<procs.size(); i++) {
    
    if (procs[i]->hasBody()) {
      procs[i]->addMods(rep->getModifies());
    
    } else {
      vector< pair<string,string> > rets = procs[i]->getRets();
      for (vector< pair<string,string> >::iterator r = rets.begin();
          r != rets.end(); ++r) {
        
        // TODO should only do this for returned POINTERS.
        // procs[i]->addEnsures(rep->declareIsExternal(Expr::id(r->first)));
      }
    }
  }
  
  // NOTE we must do this after instruction generation, since we would not 
  // otherwise know how many regions to declare.
  program.appendPrelude(rep->getPrelude());
}

} // namespace smack

