//The pass file for final project of EECS583 in Fall 2018
//@auther: Shenghao Lin, Xumiao Zhang

#include "llvm/IR/Module.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/AliasSetTracker.h"
#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/IR/Dominators.h"
#include "llvm/PassAnalysisSupport.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/LoopInfo.h"

#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Analysis/BlockFrequencyInfo.h"

#include <queue>
#include <vector>
#include <set>
#include <map>
#include <string>

using namespace std;

using namespace llvm;

#define DEBUG_TYPE "plcanalyzer"

struct Transition{
    Function *F;
    Value *V;
    int k;

    Transition(Function *_F, Value *_V, int _k) : F(_F), V(_V), k(_k) {}
};

namespace {

    struct PLCAnalyzer : public ModulePass {
        
        static char ID;
        map<Function*, map<Value*, vector<vector<pair<Value *, int>>>>> critical_paths;
        map<Function*, map<int, vector<vector<pair<Value *, int>>>>> potential_paths;
        map<Function*, map<Value*, bool>> critical_values;

        vector<Transition> temp_stack;

        PLCAnalyzer() : ModulePass(ID) {}

        bool runOnModule(Module &M) override {
            
            for (Module::iterator f = M.begin(); f != M.end(); f ++) {
                //MemorySSAWrapperPass *MSSA_pass = &getAnalysis<MemorySSAWrapperPass>(*f);
                //MemorySSA *MSSA = &(MSSA_pass -> getMSSA());
                //functionMemoryDependence(*f, MSSA);


                Function *F = &(*f);
                string s = F -> getName();
                if (s.find("llvm.dbg") != 0) {
                    functionMemoryDependence(*f);
                }
            }

            errs() << "\n\n-------------------------------------\n";
            errs() << "Summury:\n\nIntra-cell critical paths:\n";

            for (Module::iterator f = M.begin(); f != M.end(); f ++) {

                Function *F = &(*f);
                string s = F -> getName();
                if (s.find("llvm.dbg") != 0) {
                    errs() << "\n" << s << ": \n";

                    for (auto iter = critical_paths[F].begin(); iter != critical_paths[F].end(); ++iter) {
                        
                        Value *v = iter -> first;
                        vector<vector<pair<Value *, int>>> paths = iter -> second;

                        critical_values[F][v] = true;
                        
                        for (auto path : paths) {
                            for (int i = paths.size() - 1; i >= 0; i --) {
                                auto element = path[i];
                                errs() << getOriginalName(element.first, F) << " " << element.second << " -> ";
                            }
                            errs() << getOriginalName(v, F) << "\n";
                        }

                    }
                }
            }

            errs() << "\n\nInter-cell critical paths:\n\n";

            for (Module::iterator f = M.begin(); f != M.end(); f ++) {

                Function *F = &(*f);
                string s = F -> getName();

                if (s.find("llvm.dbg") != 0) {
                    for (auto iter = F -> begin(); iter != F -> end(); ++iter) {
                        BasicBlock* bb = &(*iter);

                        for (auto iter_i = bb -> begin(); iter_i != bb -> end(); ++iter_i) {
                            Instruction* inst = &(*iter_i);

                            if (CallInst* call_inst = dyn_cast<CallInst>(inst)) {
                                for (int i = 0; i < call_inst -> getNumArgOperands(); ++ i) {
                                       Value *arg = call_inst -> getArgOperand(i);

                                       for (auto path : critical_paths[F][arg]) {

                                           for (int i = path.size() - 1; i >= 0; i --) {
                                               temp_stack.push_back(Transition(F, path[i].first, path[i].second));
                                           }

                                           callDependence(call_inst -> getCalledFunction(), i);

                                           for (int i = path.size() - 1; i >= 0; i --) {
                                               temp_stack.pop_back();
                                           }
                                       }
                                   }

                            }
                        }
                    }
                }
            }

            errs() << "\n\nPossible Critical Control Flow:\n";

            for (Module::iterator f = M.begin(); f != M.end(); f ++) {

                Function *F = &(*f);
                string s = F -> getName();
                if (s.find("llvm.dbg") != 0) {
                    errs() << "\n" << F -> getName() << ": \n\n";
                    controlFlowDependence(F);
                }
            }

            return false;
        }

        map<BasicBlock *, set<BasicBlock *>> postDominatorsAnalysis(Function *F) {

            map<BasicBlock *, set<BasicBlock *>> ret;
            map<BasicBlock *, int> block_number;

            BasicBlock *exit_block = NULL;

            for (Function::iterator b = F -> begin(); b != F -> end(); b++) {
                
                BasicBlock *bb = &(*b);

                vector<BasicBlock *> bb_path;
                set<BasicBlock *> post_dom;
                set<BasicBlock *> visited;

                int i = 0;

                for (Function::iterator iter = F -> begin(); iter != F -> end(); iter++) {

                    BasicBlock *bb_temp = &(*iter);
                    ret[bb].insert(bb_temp);

                    block_number[bb_temp] = i ++;
                }

                if (dyn_cast<TerminatorInst>(bb -> getTerminator()) -> getNumSuccessors() == 0){
                    ret[bb] = set<BasicBlock *>();
                    exit_block = bb;
                    ret[bb].insert(bb);
                }
            }

            bool change = true;

            while (change) {
                change = false;

                for (Function::iterator b = F -> begin(); b != F -> end(); b++) {
                    
                    BasicBlock *bb = &(*b);

                    if (bb == exit_block) continue;

                    TerminatorInst *term_inst = dyn_cast<TerminatorInst>(bb -> getTerminator());

                    set<BasicBlock *> temp;

                    for (Function::iterator iter = F -> begin(); iter != F -> end(); iter++) {

                        temp.insert(&(*iter));
                    }

                    for (int i = 0; i < term_inst -> getNumSuccessors(); ++ i) {
                        BasicBlock *suc_bb = term_inst -> getSuccessor(i);

                        set<BasicBlock *> to_erase;

                        for (auto pdom : temp) {
                            if (ret[suc_bb].count(pdom) == 0) {
                                to_erase.insert(pdom);
                            }
                        }

                        for (auto pdom : to_erase) {
                            temp.erase(pdom);
                        }
                    }

                    temp.insert(bb);

                    if (!set_compare(temp, ret[bb])) {
                        change = true;
                        ret[bb] = temp;
                    }
                }

            }

            // for (Function::iterator b = F -> begin(); b != F -> end(); b++) {
                
            //     BasicBlock *bb = &(*b);

            //     errs() << block_number[bb] << ": \n";

            //     for (auto d : ret[bb]) {
            //         errs() << block_number[d] << " ";
            //     }
            //     errs() << "\n";

               
            // }

            return ret;
        }

        bool set_compare(set<BasicBlock *> &a, set<BasicBlock *> &b) {
            for (auto element : b) {
                if (a.count(element) == 0) {
                    return false;
                }
            }
            for (auto element : a) {
                if (b.count(element) == 0) {
                    return false;
                }
            }
            return true;
        }

        void controlFlowDependence(Function *F) {

            map<BasicBlock *, vector<BasicBlock *>> anti_flow;

            for (Function::iterator b = F -> begin(); b != F -> end(); b++) {
                
                BasicBlock *bb = &(*b);
                TerminatorInst *term_inst = dyn_cast<TerminatorInst>(bb -> getTerminator());

                for (int i = 0; i < term_inst -> getNumSuccessors(); ++i) {
                    anti_flow[term_inst -> getSuccessor(i)].push_back(bb);
                }
            }

            map<BasicBlock *, set<BasicBlock *>> post_dom = postDominatorsAnalysis(F);

            MemorySSA *MSSA = &getAnalysis<MemorySSAWrapperPass>(*F).getMSSA();

            int bb_num = 0;
            for (Function::iterator b = F -> begin(); b != F -> end(); b++) {

                set<Value *> related_critical_values;
                
                set<BasicBlock *> visited;
                vector<pair<BasicBlock *, BasicBlock *>> block_stack;
                vector<pair<BasicBlock *, BasicBlock *>> search_stack;
                vector<BasicBlock *> post_dominators;

                block_stack.push_back(make_pair<BasicBlock *, BasicBlock *>(NULL, &(*b)));
                post_dominators.push_back(&(*b));

                while (block_stack.size()) {
                    pair<BasicBlock *, BasicBlock *> cur_pair = block_stack.back();
                    block_stack.pop_back();
                    BasicBlock *cur_block = cur_pair.second;

                    while (search_stack.size() && search_stack.back().second != cur_pair.first) {

                        if (post_dominators.size() && 
                            search_stack.back().second == post_dominators.back()) {
                            post_dominators.pop_back();
                        }

                        search_stack.pop_back();
                    }

                    search_stack.push_back(cur_pair);

                    for (auto precessor : anti_flow[cur_block]) {
                        
                        if (visited.count(precessor) > 0) continue;
                        else visited.insert(precessor);

                        block_stack.push_back(make_pair(cur_block, precessor));

                        BranchInst *br_inst = dyn_cast<BranchInst>(precessor -> getTerminator());

                        if ((br_inst && br_inst -> isConditional()) && post_dom[precessor].count(post_dominators.back()) == 0) {

                            //errs() << "hello? \n";
                            post_dominators.push_back(precessor);

                            if (br_inst -> isConditional()) {
                                set<Value *> temp = criticalValuesFromV(br_inst -> getCondition(), F, MSSA);

                                for (auto v : temp) {
                                    related_critical_values.insert(v);
                                }
                            }
                        }
                        // if (post_dom.count(make_pair(post_dominators.back(), precessor)) == 0)
                        //     post_dominators.push_back(precessor);
                    }
                }

                set<Value *> effected_vars;

                BasicBlock *bb = &(*b);

                for (auto iter = bb -> begin(); iter != bb -> end(); ++ iter) {

                    Instruction *inst = &(*iter);
                    
                    if (StoreInst *store_inst = dyn_cast<StoreInst>(inst)) {

                        effected_vars.insert(store_inst -> getOperand(1));
                    }
                }

                if (related_critical_values.size()&& effected_vars.size()) {
                    errs() << "BasicBlock #" << bb_num << ": \nRelated Critical Values: ";
                    for (auto v : related_critical_values) {
                        if (StoreInst * store_inst = dyn_cast<StoreInst>(v)) {
                            errs() << getOriginalName(store_inst -> getOperand(1), F) << " ";
                        }
                        else {
                            errs() << getOriginalName(v, F) << " ";
                        }
                    }
                    errs() << "\nRelated Variables: ";

                    for (auto v : effected_vars) 
                        errs() << getOriginalName(v, F) << " ";

                    errs() << "\n\n";

                }



                bb_num++;

            }

        }

        set<Value *> criticalValuesFromV(Value *v, Function *F, MemorySSA *MSSA) {

            set<Value *> related_value;

            vector<pair<Value *, Value *>> stack;
            vector<Value *> load_stack;
            vector<pair<Value *, Value *>> search_stack;
            set<Value *> queried;

            stack.push_back(make_pair<Value *, Value *>(NULL, dyn_cast<Value>(v)));

            while (stack.size()) {
                Value *pre = stack.back().first;
                Value *cur = stack.back().second;
                stack.pop_back();

                if (queried.count(cur) != 0) continue;
                else queried.insert(cur);

                while (search_stack.size() && search_stack.back().second != pre) {
                    
                    if (dyn_cast<LoadInst>(search_stack.back().second))
                        load_stack.pop_back();

                    search_stack.pop_back();
                }

                search_stack.push_back(make_pair(pre, cur));
                
                if (MemoryPhi *phi = dyn_cast<MemoryPhi>(cur)) {
                    for (auto &op : phi -> operands()) {
                        stack.push_back(make_pair(cur, dyn_cast<Value>(op)));
                    }
                }
                else if (MemoryDef *def = dyn_cast<MemoryDef>(cur)) {

                    if (!(def -> getID())) {
                        continue;
                    }

                    // successful load
                    if (def && def -> getMemoryInst() -> getOperand(1) == load_stack.back()) {

                        if (critical_values[F][def -> getMemoryInst()]) {
                            related_value.insert(def -> getMemoryInst());
                        }   
                    }
                    else {
                        stack.push_back(make_pair(cur, def -> getDefiningAccess()));
                    }
                }

                else if (LoadInst *load_inst = dyn_cast<LoadInst>(cur)) {

                    if (GlobalValue *gv = dyn_cast<GlobalValue>(load_inst -> getOperand(0))){

                        related_value.insert(gv);
                    }    

                    load_stack.push_back(load_inst -> getOperand(0));

                    MemoryUse *MU = dyn_cast<MemoryUse>(MSSA -> getMemoryAccess(load_inst));
                    MemoryAccess *UO = MU -> getDefiningAccess();

                    stack.push_back(make_pair(cur, UO));
                }

                else if (Instruction *inst = dyn_cast<Instruction>(cur)) {
                    for (int j = 0; j < inst -> getNumOperands(); ++ j) {

                        Value *v = inst-> getOperand(j);
                    
                        if (Constant *c = dyn_cast<Constant>(v)) {
                    
                            if (GlobalValue *gv = dyn_cast<GlobalValue>(c)) {    
                                related_value.insert(gv);
                            }
                            
                            //A real constant otherwise
                            continue;
                        }
                    
                        else if (Instruction *next_inst = dyn_cast<Instruction>(v)) {
                        
                            stack.push_back(make_pair(inst, next_inst));
                        }

                        else {
                            related_value.insert(v);
                        }
                    }
                }
            }

            return related_value;
        }

        void callDependence(Function *F, int arg_num) {

            // map<Value *, vector<Value*, int>> rec_paths;

            for (auto path : potential_paths[F][arg_num]) {

                if (path.front().second == -1) {

                    continue;
                }

                for (auto t : temp_stack) {
                    errs() << t.F -> getName() << ": " << t.k << " " << getOriginalName(t.V, t.F) << " -> ";
                }

                for (int i = path.size() - 1; i >= 0; i --) {
                    if (path[i].second < 0) continue;
                    errs() << F -> getName() << ": " << path[i].second << " " << getOriginalName(path[i].first, F);
                    if (i != 1) errs() << " -> ";
                }

                critical_values[F][path.front().first] = true;

                errs() << "\n";
                
            }


            for (auto iter = F -> begin(); iter != F -> end(); ++iter) {
                BasicBlock* bb = &(*iter);

                for (auto iter_i = bb -> begin(); iter_i != bb -> end(); ++iter_i) {
                    Instruction* inst = &(*iter_i);

                    if (CallInst* call_inst = dyn_cast<CallInst>(inst)) {
                        for (int i = 0; i < call_inst -> getNumArgOperands(); ++ i) {

                            Value *arg = call_inst -> getArgOperand(i);
                            for (auto path : potential_paths[F][arg_num]) {

                                if (path.front().first != arg) continue;

                                for (int i = path.size() - 1; i >= 0; i --) {
                                    if (path[i].second < 0) continue;
                                    temp_stack.push_back(Transition(F, path[i].first, path[i].second));
                                }
                                // errs() << "at least I'm here" << call_inst -> getCalledFunction() -> getName() << "\n";
                                callDependence(call_inst -> getCalledFunction(), i);

                                for (int i = path.size() - 1; i >= 0; i --) {
                                    if (path[i].second < 0) continue;
                                    temp_stack.pop_back();
                                }
                            }
                        }

                    }
                }
            }                
            
        }


        bool functionMemoryDependence(Function &F) {

            MemorySSA *MSSA = &getAnalysis<MemorySSAWrapperPass>(F).getMSSA();
            std::vector<Value *> store_list;    

            errs() << "\n\n";
            errs().write_escaped(F.getName()) << "\n";

            map<Value *, int> args;

            int num = 0;
            for (auto iter = F.arg_begin(); iter != F.arg_end(); ++iter) {
                Argument *arg = *(&iter);
                errs() << getOriginalName(arg, &F) << " " << arg -> getName() << " " ;
                args[arg] = num ++;
            }
            errs() << "\n";

            map<Function *, vector<Value *>> arguments;
            

            for (Function::iterator b = F.begin(); b != F.end(); b++) {
                
                BasicBlock *bb = &(*b);
                
                for (BasicBlock::iterator i_iter = bb -> begin(); i_iter != bb -> end(); ++ i_iter) {
                    Instruction *I = &(*i_iter);
                    if (I -> getOpcode() == Instruction::Store) {
                        store_list.push_back(I);                        
                    }
                    if (I -> getOpcode() == Instruction::Call) {
                        if (CallInst *call_inst = dyn_cast<CallInst>(I)) {
                            if (string("llvm.dbg.declare").compare(call_inst -> getCalledFunction() -> getName()) == 0)
                                continue;

                            errs() << call_inst -> getCalledFunction() -> getName() << "\n";

                            arguments[call_inst -> getCalledFunction()] = vector<Value *>();

                               for (int i = 0; i < call_inst -> getNumArgOperands(); ++ i) {
                                   Value *arg = call_inst -> getArgOperand(i);
                                   arguments[call_inst -> getCalledFunction()].push_back(arg);

                                   store_list.push_back(arg);
                                   //errs() << getOriginalName(arg, &F) << " ";
                               }

                               errs() << " ";

                        }

                    }
                }


            }

            bool call_arg_flag = false;

            for (int i = 0; i < store_list.size(); i ++) {
                call_arg_flag = false;
                set<Value *> queried;
                vector<pair<Value *, Value *>> to_query;
                vector<Value *> load_stack;
                vector<pair<Value *, Value *>> stack;

                if (StoreInst * store_inst = dyn_cast<StoreInst>(store_list[i])) {

                    // errs() << "atleast\n";

                    load_stack.push_back(store_inst -> getOperand(1));
                
                    MemoryDef *MA = dyn_cast<MemoryDef>(MSSA -> getMemoryAccess(store_inst));

                    if (MA && MA -> getID()){
                        errs().write('\n') << MA -> getID();
                        if (store_inst -> getOperand(1) -> hasName()) {
                            errs() << " " << store_inst -> getOperand(1) -> getName() << ": \n";
                        }
                        else {
                            errs() << " " << getOriginalName(store_inst -> getOperand(1), &F) << ": \n"; 
                        }
                    }

                    to_query.push_back(make_pair<Value *, Value *>(NULL, dyn_cast<Value>(MA)));
                }

                else {
                    call_arg_flag = true;

                    errs().write('\n') << "call argument " << getOriginalName(store_list[i], &F) << ":\n";

                    to_query.push_back(make_pair<Value *, Value *>(NULL, dyn_cast<Value>(store_list[i])));
                }

                while (to_query.size()) {

                    pair<Value *, Value *> tmp = to_query.back();
                    Value *dd = tmp.second;

                    to_query.pop_back();

                    if (queried.find(dd) != queried.end()) continue;
                    queried.insert(dd);

                    while (stack.size() && stack.back().second != tmp.first) {
                        if (dyn_cast<LoadInst>(stack.back().second)) {
                            load_stack.pop_back();
                        }
                        stack.pop_back();
                    }

                    stack.push_back(tmp);

                    if (MemoryPhi *phi = dyn_cast<MemoryPhi>(dd)) {
                        for (auto &op : phi -> operands()) {
                            to_query.push_back(make_pair<Value *, Value *>(dyn_cast<Value>(dd), dyn_cast<Value>(op)));
                        }

                        // errs() << "phi\n";
                    }
                    
                    else if (MemoryDef *def = dyn_cast<MemoryDef>(dd)) {

                        if (!(def -> getID())) {
                            continue;
                        }

                        if (def && def -> getMemoryInst() -> getOperand(1) == load_stack.back()) {

                            to_query.push_back(make_pair(dd, def -> getMemoryInst()));   
                        }
                        else {
                            to_query.push_back(make_pair(dd, def -> getDefiningAccess()));
                        }
                    }

                    else if (Instruction* d = dyn_cast<Instruction>(dd)) {

                        if (d -> getOpcode () == Instruction::Store) {
                            
                            MemoryDef *MD = dyn_cast<MemoryDef>(MSSA -> getMemoryAccess(d));
                            // errs() << "store " << MD -> getID();

                            // if (d -> getOperand(1) -> hasName()) {
                            //     errs() << " " << d -> getOperand(1) -> getName() << "\n";
                            // }
                            // else {
                            //     errs() << " " << getOriginalName(d -> getOperand(1), &F) << "\n";
                            // }

                            Value *v = d -> getOperand(0);
                            
                            if (Constant* CI = dyn_cast<Constant>(v)) {
                            
                                if (GlobalValue *gv = dyn_cast<GlobalValue>(CI)) {

                                    vector<pair<Value *, int>> critical_path = printPath(stack, &F);
                                    errs() << "related global value: " << gv -> getName() << "\n";
                                    critical_path.push_back(make_pair(v, 0));
                                    critical_paths[&F][dyn_cast<Value>(store_list[i])].push_back(critical_path);
                                }
                                else if (CI) {
                                    printPath(stack, &F);

                                    errs() << "related constant: " << CI -> getUniqueInteger() << "\n";
                                    //errs() << "\n";
                                }
                            
                                continue;
                            
                            }
                            else if (dyn_cast<Instruction>(v) == NULL) {

                                vector<pair<Value *, int>> potential_path;

                                if (call_arg_flag) {
                                    potential_path.push_back(make_pair(store_list[i], -1));
                                }
                                else {
                                    potential_path.push_back(make_pair(store_list[i], -2));
                                }

                                vector<pair<Value *, int>> tmp_vect = printPath(stack, &F);

                                for (auto d : tmp_vect) {
                                    potential_path.push_back(d);
                                }

                                errs() << "related input value: " << getOriginalName(d -> getOperand(1), &F);

                                if (args.count(v) != 0) {
                                    errs() << " argument #" << args[v] << "\n";
                                }

                                potential_paths[&F][args[v]].push_back(potential_path);
                            }
                            else {

                                to_query.push_back(make_pair(dd, dyn_cast<Instruction>(v)));
                            }
                        }
                        

                        else if (d -> getOpcode() == Instruction::Load) {
                            
                            // errs() << "load";
                            // if (d -> getOperand(0) -> hasName()) {
                            //     errs() << " " << d -> getOperand(0) -> getName() << ": \n";
                            // }
                            // else {
                            //     errs() << " " << getOriginalName(d -> getOperand(0), &F) << ": \n"; 
                            // }

                            if (GlobalValue *gv = dyn_cast<GlobalValue>(d -> getOperand(0))){
                                
                                vector<pair<Value *, int>> critical_path = printPath(stack, &F);

                                critical_path.push_back(make_pair(d -> getOperand(0), 0));
                                critical_paths[&F][dyn_cast<Value>(store_list[i])].push_back(critical_path);

                                errs() <<"related global value: " << gv ->getName() << "\n";

                                continue;
                            }    

                            load_stack.push_back(dyn_cast<LoadInst>(d) -> getOperand(0));

                            MemoryUse *MU = dyn_cast<MemoryUse>(MSSA -> getMemoryAccess(d));
                            MemoryAccess *UO = MU -> getDefiningAccess();
                            
                            if (MemoryDef *MD = dyn_cast<MemoryDef>(UO)) {

                                to_query.push_back(make_pair(dd, MD));

                            }
                            
                            else {
                                to_query.push_back(make_pair(dd, UO));
                            }
                        }

                        else {

                            for (int j = 0; j < d -> getNumOperands(); ++ j) {

                                Value *v = d -> getOperand(j);
                            
                                if (Constant *c = dyn_cast<Constant>(v)) {
                            
                                    if (GlobalValue *gv = dyn_cast<GlobalValue>(c)) {
                                        
                                        vector<pair<Value *, int>> critical_path = printPath(stack, &F);

                                        errs() << "related global value: " << gv -> getName() << "\n";

                                        critical_path.push_back(make_pair(v, 0));
                                        critical_paths[&F][dyn_cast<Value>(store_list[i])].push_back(critical_path);

                                    }
                                    else {
                                        
                                        printPath(stack, &F);
                                        errs() << "related constant: " << c -> getUniqueInteger() << "\n";
                                    }
                            
                                    continue;
                            
                                }
                            
                                else if (Instruction *inst = dyn_cast<Instruction>(v)) {
                                
                                    to_query.push_back(make_pair(dd, inst));
                                    
                                }
                                else {
                                    vector<pair<Value *, int>> potential_path;

                                    if (call_arg_flag) {
                                        potential_path.push_back(make_pair(store_list[i], -1));
                                    }
                                    else {
                                        potential_path.push_back(make_pair(store_list[i], -2));
                                    }

                                    vector<pair<Value *, int>> tmp_vect = printPath(stack, &F);

                                    for (auto d : tmp_vect) {
                                        potential_path.push_back(d);
                                    }

                                    errs() << "related input value: " << getOriginalName(v, &F);

                                    if (args.count(v) != 0) {
                                        errs() << " argument #" << args[v] << "\n";
                                    }

                                    potential_paths[&F][args[v]].push_back(potential_path);
                                }
                            }
                        }
                    }

                    
                }
            }

            return false;
        }

        vector<pair<Value *, int>> printPath(vector<pair<Value *, Value *>> &v, Function *F) {
            vector<pair<Value *, int>> ret;
            for (auto tmp : v) {
                Value *val = tmp.second;
                if (MemoryDef *MD = dyn_cast<MemoryDef>(val)) {
                    errs() << MD -> getID() << ": " << getOriginalName(MD -> getMemoryInst() -> getOperand(1), F) << " <- ";

                    ret.push_back(make_pair(MD -> getMemoryInst() -> getOperand(1), MD -> getID()));
                }
            }

            return ret;
        }


        MDNode* findVar(Value* V, Function* F) {
              for (auto iter = F -> begin(); iter != F -> end(); ++iter) {
                BasicBlock *bb = &*iter;
                for (auto iter_i = bb -> begin(); iter_i != bb -> end(); ++ iter_i){
                    Instruction* I = &*iter_i;
                    if (DbgDeclareInst* DbgDeclare = dyn_cast<DbgDeclareInst>(I)) {
                        if (DbgDeclare->getAddress() == V) return DbgDeclare -> getVariable();
                    } 
                    else if (DbgValueInst* DbgValue = dyn_cast<DbgValueInst>(I)) {
                        if (DbgValue->getValue() == V) return DbgValue -> getVariable();
                    }
                }
            }
            return NULL;
        }

        StringRef getOriginalName(Value* V, Function* F) {
            if (GlobalValue *gv = dyn_cast<GlobalValue>(V)) return gv -> getName();
            if (V -> hasName()) return V -> getName();
            MDNode* Var = findVar(V, F);
            
            if (!Var) return "UNKNOWN";

            return dyn_cast<DIVariable>(Var) -> getName();
        }

        void getAnalysisUsage(AnalysisUsage &AU) const {
            AU.addRequired<MemorySSAWrapperPass>();
        }
    };

}

char PLCAnalyzer::ID = 0;
static RegisterPass<PLCAnalyzer> X("plcanalyzer", "PLCAnalyzer");
