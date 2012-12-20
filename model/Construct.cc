// Copyright 2011-2012 Shannon Weyrick <weyrick@mozek.us>
// Copyright 2011-2012 Google Inc.
// 
//   This Source Code Form is subject to the terms of the Mozilla Public
//   License, v. 2.0. If a copy of the MPL was not distributed with this
//   file, You can obtain one at http://mozilla.org/MPL/2.0/.
// 

#include "Construct.h"

#include <sys/stat.h>
#include <fstream>
#include <dlfcn.h>
#include <limits.h>
#include <stdlib.h>
#include <sstream>
#include <algorithm>
#include "parser/Parser.h"
#include "parser/ParseError.h"
#include "parser/Toker.h"
#include "spug/check.h"
#include "builder/Builder.h"
#include "ext/Module.h"
#include "Context.h"
#include "GlobalNamespace.h"
#include "ModuleDef.h"
#include "StrConst.h"
#include "TypeDef.h"
#include "compiler/init.h"
#include "util/CacheFiles.h"
#include "util/SourceDigest.h"
#include "wisecrack/repl.h"
#include "builder/llvm/LLVMJitBuilder.h"
#include "builder/llvm/LLVMBuilder.h"
#include "model/OverloadDef.h"
#include "model/OrderedIdLog.h"

#include <llvm/LinkAllPasses.h> // enable .dc cast and display code for symbol

using namespace std;
using namespace model;
using namespace parser;
using namespace builder;
using namespace crack::ext;
using namespace crack::util;

namespace {
    typedef pair<string, double> timePair;
    struct TimeCmp {
        bool operator()(const timePair &lhs, const timePair &rhs) {
            return lhs.second > rhs.second;
        }
    };
}

void ConstructStats::showModuleCounts(std::ostream &out,
                                      const std::string &title,
                                      const ModuleTiming &list) const {

    vector<timePair> sortedList(list.begin(), list.end());
    sort(sortedList.begin(), sortedList.end(), TimeCmp());

    out << "------------------------------\n";
    out << title << "\n";
    out << "------------------------------\n";
    for (vector<timePair>::const_iterator i = sortedList.begin();
         i != sortedList.end();
         ++i) {
        printf("%.10f\t%s\n", i->second, i->first.c_str());
    }
    out << endl;

}

void ConstructStats::write(std::ostream &out) const {

    out << "\n------------------------------\n";
    out << "parsed     : " << parsedCount << "\n";
    out << "cached     : " << cachedCount << "\n";
    out << "------------------------------\n";
    printf("startup \t: %.10f\n", timing[start]);
    printf("builtin \t: %.10f\n", timing[builtin]);
    printf("parser  \t: %.10f\n", timing[parser]);
    printf("builder \t: %.10f\n", timing[builder]);
    printf("executor\t: %.10f\n\n", timing[executor]);
    showModuleCounts(out, "Parser Times (exclusive)", parseTimes);
    showModuleCounts(out, "Builder Times", buildTimes);
    showModuleCounts(out, "Executor Times", executeTimes);
    out << endl;

}

void ConstructStats::stopwatch() {
    struct timeval t;
    gettimeofday(&t, NULL);
    double beforeF = (lastTime.tv_usec/1000000.0) + lastTime.tv_sec;
    double nowF = (t.tv_usec/1000000.0) + t.tv_sec;
    double diff = (nowF - beforeF);
    timing[getState()] += diff;
    if (curModule) {
        switch (getState()) {
            case parser:
                parseTimes[curModule->getFullName()] += diff;
                break;
            case builder:
                buildTimes[curModule->getFullName()] += diff;
                break;
            case executor:
                executeTimes[curModule->getFullName()] += diff;
                break;
        }
    } else {
        //printf("losing diff: %.10f, state: %d\n", diff, getState());
        assert(getState() == ConstructStats::start && "missing important stat");
    }

    lastTime = t;
}

StatState::StatState(Context *c, ConstructStats::CompileState newState) :
    context(c) {
    if (!context->construct->rootBuilder->options->statsMode)
        return;
    oldState = context->construct->stats->getState();
    context->construct->stats->setState(newState);
}

StatState::StatState(Context *c,
                     ConstructStats::CompileState newState,
                     model::ModuleDef *newModule) :
    context(c) {
    if (!context->construct->rootBuilder->options->statsMode)
        return;
    oldState = context->construct->stats->getState();
    oldModule = context->construct->stats->getModule().get();
    context->construct->stats->setState(newState);
    context->construct->stats->setModule(newModule);
}

bool StatState::statsEnabled(void) {
    return context->construct->rootBuilder->options->statsMode;
}

StatState::~StatState() {
    if (!context->construct->rootBuilder->options->statsMode)
        return;
    context->construct->stats->setState(oldState);
    if (oldModule)
        context->construct->stats->setModule(oldModule.get());
}

Construct::ModulePath Construct::searchPath(
    const Construct::StringVec &path,
    Construct::StringVecIter moduleNameBegin,
    Construct::StringVecIter moduleNameEnd,
    const std::string &extension,
    int verbosity
) {
    // try to find a matching file.
    for (StringVecIter pathIter = path.begin();
         pathIter != path.end();
         ++pathIter
         ) {
        string relPath = joinName(moduleNameBegin, moduleNameEnd, extension);
        string fullName = joinName(*pathIter, relPath);
        if (verbosity > 1)
            cerr << "search: " << fullName << endl;
        if (isFile(fullName))
            return ModulePath(*pathIter, relPath, fullName, true, false);
    }
    
    // try to find a matching directory.
    string empty;
    for (StringVecIter pathIter = path.begin();
         pathIter != path.end();
         ++pathIter
         ) {
        string relPath = joinName(moduleNameBegin, moduleNameEnd, empty);
        string fullName = joinName(*pathIter, relPath);
        if (isDir(fullName))
            return ModulePath(*pathIter, relPath, fullName, true, true);
    }
    
    return ModulePath(empty, empty, empty, false, false);
}

Construct::ModulePath Construct::searchPath(
    const Construct::StringVec &path,
    const string &relPath,
    int verbosity
) {
    // try to find a matching file.
    for (StringVecIter pathIter = path.begin();
         pathIter != path.end();
         ++pathIter
         ) {
        string fullName = joinName(*pathIter, relPath);
        if (verbosity > 1)
            cerr << "search: " << fullName << endl;
        if (isFile(fullName))
            return ModulePath(*pathIter, relPath, fullName, true, false);
    }
    
    // try to find a matching directory.
    string empty;
    for (StringVecIter pathIter = path.begin();
         pathIter != path.end();
         ++pathIter
         ) {
        string fullName = joinName(*pathIter, relPath);
        if (isDir(fullName))
            return ModulePath(*pathIter, relPath, fullName, true, true);
    }
    
    return ModulePath(empty, empty, empty, false, false);
}

bool Construct::isFile(const std::string &name) {
    struct stat st;
    if (stat(name.c_str(), &st))
        return false;
    
    // XXX should check symlinks, too
    return S_ISREG(st.st_mode);
}

bool Construct::isDir(const std::string &name) {
    struct stat st;
    if (stat(name.c_str(), &st))
        return false;
    
    // XXX should check symlinks, too
    return S_ISDIR(st.st_mode);
}

std::string Construct::joinName(Construct::StringVecIter pathBegin,
                                Construct::StringVecIter pathEnd,
                                const std::string &ext
                                ) {
    string result;
    StringVecIter iter = pathBegin;
    if (iter != pathEnd) {
        result = *iter;
        ++iter;
    }
    for (; iter != pathEnd; ++iter )
        result += "/" + *iter;
    
    return result + ext;
}

std::string Construct::joinName(const std::string &base,
                                const std::string &rel
                                ) {
    return base + "/" + rel;
}

Construct::Construct(const Options &options, Builder *builder, 
                     Construct *primary
                     ) :
    Options(options),
    rootBuilder(builder),
    uncaughtExceptionFunc(0) {

    if (builder->options->statsMode)
        stats = new ConstructStats();

    builderStack.push(builder);
    createRootContext();
    
    // steal any stuff from the primary we want to use as a default.
    if (primary)
        sourceLibPath = primary->sourceLibPath;
}

void Construct::addToSourceLibPath(const string &path) {
    size_t pos = 0;
    size_t i = path.find(':');
    while (i != -1) {
        if (i > 1 && path[i-1] == '/')
            sourceLibPath.push_back(path.substr(pos, i - pos - 1));
        else
            sourceLibPath.push_back(path.substr(pos, i - pos));
        pos = i + 1;
        i = path.find(':', pos);
    }
    if (path.size() > 1 && path[path.size()-1] == '/')
        sourceLibPath.push_back(path.substr(pos, (path.size()-pos)-1));
    else
        sourceLibPath.push_back(path.substr(pos));
}

ContextPtr Construct::createRootContext() {

    rootContext = new Context(*rootBuilder, Context::module, this,
                              new GlobalNamespace(0, ""),
                              new GlobalNamespace(0, "")
                              );

    // register the primitives into our builtin module
    GlobalNamespace *builtinGlobalNS;
    ContextPtr builtinContext =
        new Context(*rootBuilder, Context::module, rootContext.get(),
                    // NOTE we can't have rootContext namespace be the parent
                    // here since we are adding the aliases into rootContext
                    // and we get dependency issues
                    builtinGlobalNS = new GlobalNamespace(0, ".builtin"),
                    new GlobalNamespace(0, ".builtin"));
    builtinMod = rootBuilder->registerPrimFuncs(*builtinContext);

    // alias builtins to the root namespace
    for (Namespace::VarDefMap::iterator i = builtinContext->ns->beginDefs();
         i != builtinContext->ns->endDefs();
         ++i) {
         rootContext->ns->addUnsafeAlias(i->first, i->second.get());
    }
    
    // attach the builtin module to the root namespace
    rootContext->ns = builtinGlobalNS->builtin;
    moduleCache[".builtin"] = builtinMod;
    
    return rootContext;
}

void Construct::loadBuiltinModules() {
    // loads the compiler extension.  If we have a compile-time construct, 
    // the extension belongs to him and we just want to steal his defines.
    // Otherwise, we initialize them ourselves.
    NamespacePtr ns;
    if (compileTimeConstruct) {
        ns = compileTimeConstruct->rootContext->compileNS;
    } else {
        // initialize the built-in compiler extension and store the 
        // CrackContext type in global data.
        ModuleDefPtr ccMod = 
            initExtensionModule("crack.compiler", &compiler::init, 0);
        rootContext->construct->crackContext = ccMod->lookUp("CrackContext");
        moduleCache["crack.compiler"] = ccMod;
        ccMod->finished = true;
        ns = ccMod;
    }

    // in either case, aliases get installed in the compiler namespace.
    rootContext->compileNS->addAlias(ns->lookUp("static").get());
    rootContext->compileNS->addAlias(ns->lookUp("final").get());
    rootContext->compileNS->addAlias(ns->lookUp("abstract").get());
    rootContext->compileNS->addAlias(ns->lookUp("FILE").get());
    rootContext->compileNS->addAlias(ns->lookUp("LINE").get());
    rootContext->compileNS->addAlias(ns->lookUp("encoding").get());
    rootContext->compileNS->addAlias(ns->lookUp("export_symbols").get());

    // load the runtime extension
    StringVec crackRuntimeName(2);
    crackRuntimeName[0] = "crack";
    crackRuntimeName[1] = "runtime";
    string name;
    ModuleDefPtr rtMod = 
        rootContext->construct->getModule(crackRuntimeName.begin(),
                                          crackRuntimeName.end(),
                                          name
                                          );
    if (!rtMod) {
        cerr << "failed to load crack runtime from module load path" << endl;
        // XXX exception?
        exit(1);
    }
    // alias some basic builtins from runtime
    // mostly for legacy reasons
    VarDefPtr a = rtMod->lookUp("puts");
    assert(a && "no puts in runtime");
    rootContext->ns->addUnsafeAlias("puts", a.get());
    a = rtMod->lookUp("putc");
    assert(a && "no putc in runtime");
    rootContext->ns->addUnsafeAlias("putc", a.get());
    a = rtMod->lookUp("__die");
    assert(a && "no __die in runtime");
    rootContext->ns->addUnsafeAlias("__die", a.get());
    rootContext->compileNS->addUnsafeAlias("__die", a.get());
    a = rtMod->lookUp("printint");
    if (a)
        rootContext->ns->addUnsafeAlias("printint", a.get());
    
    // for jit builders, get the uncaught exception handler
    if (rootBuilder->isExec()) {
        FuncDefPtr uncaughtExceptionFuncDef =
            rootContext->lookUpNoArgs("__CrackUncaughtException", true,
                                    rtMod.get()
                                    );
        if (uncaughtExceptionFuncDef)
            uncaughtExceptionFunc = 
                reinterpret_cast<bool (*)()>(
                    uncaughtExceptionFuncDef->getFuncAddr(*rootBuilder)
                );
        else
            cerr << "Uncaught exception function not found in runtime!" << 
                endl;
    }
}

void Construct::parseModule(Context &context,
                            ModuleDef *module,
                            const std::string &path,
                            istream &src
                            ) {
    Toker toker(src, path.c_str());
    Parser parser(toker, &context);
    StatState sState(&context, ConstructStats::parser, module);
    if (sState.statsEnabled()) {
        stats->incParsed();
    }
    parser.parse();
    module->close(context);
    
    // if we're caching, store the module.
    if (context.construct->cacheMode)
        context.cacheModule(module);
}

ModuleDefPtr Construct::initExtensionModule(const string &canonicalName,
                                            Construct::CompileFunc compileFunc,
                                            Construct::InitFunc initFunc
                                            ) {
    // create a new context
    BuilderPtr builder = rootBuilder->createChildBuilder();
    builderStack.push(builder);
    ContextPtr context =
        new Context(*builder, Context::module, rootContext.get(),
                    new GlobalNamespace(rootContext->ns.get(), canonicalName),
                    0 // we don't need a compile namespace
                    );
    context->toplevel = true;

    // create a module object
    ModuleDefPtr modDef = context->createModule(canonicalName);
    Module mod(context.get());
    compileFunc(&mod);
    modDef->fromExtension = true;
    mod.finish();
    modDef->close(*context);
    builderStack.pop();

    if (initFunc)
        initFunc();

    return modDef;
}

namespace {
    // load a function from a shared library
    void *loadFunc(void *handle, const string &path, const string &funcName) {
#ifdef __APPLE__
        // XXX on osx, it's failing to find the symbol unless we do
        // RTLD_DEFAULT. this may work on linux too, but is noted as being
        // expensive so i'm keeping it osx only until we can figure out
        // a better way
        void *func = dlsym(RTLD_DEFAULT, funcName.c_str());
#else
        void *func = dlsym(handle, funcName.c_str());
#endif
        if (!func) {
            cerr << "Error looking up function " << funcName
                << " in extension library " << path << ": "
                << dlerror() << endl;
            return 0;
        } else {
            return func;
        }
    }
}

ModuleDefPtr Construct::loadSharedLib(const string &path, 
                                      Construct::StringVecIter moduleNameBegin,
                                      Construct::StringVecIter moduleNameEnd,
                                      string &canonicalName
                                      ) {

    void *handle = rootBuilder->loadSharedLibrary(path);

    // construct the full init function name
    // XXX should do real name mangling. also see LLVMLinkerBuilder::initializeImport
    std::string initFuncName;
    for (StringVecIter iter = moduleNameBegin;
         iter != moduleNameEnd;
         ++iter
         )
        initFuncName += *iter + '_';

    CompileFunc cfunc = (CompileFunc)loadFunc(handle, path, 
                                              initFuncName + "cinit"
                                              );
    InitFunc rfunc = (InitFunc)loadFunc(handle, path, initFuncName + "rinit");
    if (!cfunc || !rfunc)
        return 0;

    return initExtensionModule(canonicalName, cfunc, rfunc);
}

ModuleDefPtr Construct::getModule(const string &canonicalName) {

    StringVec name;
    name = ModuleDef::parseCanonicalName(canonicalName);

    string cname;
    ModuleDefPtr m = getModule(name.begin(), name.end(), cname);
    SPUG_CHECK(cname == canonicalName, 
               "canonicalName mismatch.  constructed = " << cname << 
                ", requested = " << canonicalName
               );
    return m;

}

namespace {
    bool isFile(const string &path) {
        struct stat st;
        return !stat(path.c_str(), &st) && S_ISREG(st.st_mode);
    }
}

ModuleDefPtr Construct::getCachedModule(const string &canonicalName) {
    // see if it's in the in-memory cache    
    Construct::ModuleMap::iterator iter = moduleCache.find(canonicalName);
    if (iter != moduleCache.end())
        return iter->second;

    if (!rootContext->construct->cacheMode)
        return 0;

    // create a new builder, context and module
    BuilderPtr builder = rootBuilder->createChildBuilder();
    builderStack.push(builder);
    ContextPtr context =
        new Context(*builder, Context::module, rootContext.get(),
                    new GlobalNamespace(rootContext->ns.get(), 
                                        canonicalName
                                        ),
                    new GlobalNamespace(rootContext->compileNS.get(),
                                        canonicalName
                                        )
                    );
    context->toplevel = true;

    ModuleDefPtr modDef = context->materializeModule(canonicalName);
    if (modDef && rootBuilder->options->statsMode)
        stats->incCached();
    moduleCache[canonicalName] = modDef;
    
    builderStack.pop();
    return modDef;
}

ModuleDefPtr Construct::getModule(Construct::StringVecIter moduleNameBegin,
                                  Construct::StringVecIter moduleNameEnd,
                                  string &canonicalName
                                  ) {
    // create the dotted canonical name of the module
    StringVecIter iter = moduleNameBegin;
    if (iter != moduleNameEnd) {
        canonicalName = *iter;
        ++iter;
    }
    for (; iter != moduleNameEnd; ++iter)
        canonicalName += "." + *iter;

    // check to see if we have it in the cache
    Construct::ModuleMap::iterator mapi = moduleCache.find(canonicalName);
    if (mapi != moduleCache.end())
        return mapi->second;

    // look for a shared library
    ModulePath modPath = searchPath(sourceLibPath, moduleNameBegin,
                                    moduleNameEnd,
// XXX get this from build env
#ifdef __APPLE__
                                    ".dylib",
#else
                                    ".so",
#endif
                                    rootBuilder->options->verbosity
                                    );
    
    ModuleDefPtr modDef;
    if (modPath.found && !modPath.isDir) {
        modDef = loadSharedLib(modPath.path, moduleNameBegin,
                               moduleNameEnd,
                               canonicalName
                               );
        moduleCache[canonicalName] = modDef;
    } else {
        
        // not in in-memory cache, not a shared library

        // create a new builder, context and module
        BuilderPtr builder = rootBuilder->createChildBuilder();
        builderStack.push(builder);
        ContextPtr context =
            new Context(*builder, Context::module, rootContext.get(),
                        new GlobalNamespace(rootContext->ns.get(), 
                                            canonicalName
                                            ),
                        new GlobalNamespace(rootContext->compileNS.get(),
                                            canonicalName
                                            )
                        );
        context->toplevel = true;

        // before parsing the module from scratch, check the persistent cache 
        // for it.
        bool cached = false;
        if (rootContext->construct->cacheMode && !modPath.isDir)
            modDef = context->materializeModule(canonicalName);
        if (modDef && modDef->matchesSource(sourceLibPath)) {
            cached = true;
            if (rootBuilder->options->statsMode)
                stats->incCached();
        } else {
            
            // if we got a stale module from the cache, use the relative 
            // source path of that module (avoiding complications from cached 
            // ephemeral modules).  Otherwise, just look it up in the library 
            // path.
            if (modDef)
                modPath = searchPath(sourceLibPath, modDef->sourcePath,
                                     rootBuilder->options->verbosity
                                     );
            else
                modPath = searchPath(sourceLibPath, moduleNameBegin, 
                                     moduleNameEnd, ".crk", 
                                     rootBuilder->options->verbosity
                                     );
            if (!modPath.found)
                return 0;
            modDef = context->createModule(canonicalName, modPath.path);
        }

        modDef->sourcePath = modPath.relPath;
        moduleCache[canonicalName] = modDef;

        if (!cached) {
            if (!modPath.isDir) {
                ifstream src(modPath.path.c_str());
                // parse from scratch
                parseModule(*context, modDef.get(), modPath.path, src);
            } else {
                // directory
                modDef->close(*context);
            }
        } else {
            // XXX hook to run/finish cached module
        }

        builderStack.pop();
    }

    modDef->finished = true;
    loadedModules.push_back(modDef);
    return modDef;
}    

void Construct::registerModule(ModuleDef *module) {
    moduleCache[module->getFullName()] = module;
    loadedModules.push_back(module);
}

namespace {
    // extract a class from a module and verify that it is a class - returns 
    // null on failure.
    TypeDef *extractClass(ModuleDef *mod, const char *name) {
        VarDefPtr var = mod->lookUp(name);
        TypeDef *type;
        if (var && (type = TypeDefPtr::rcast(var))) {
            return type;
        } else {
            cerr << name << " class not found in module crack.lang" << endl;
            return 0;
        }
    }
}

bool Construct::loadBootstrapModules() {
    try {
        StringVec crackLangName(2);
        crackLangName[0] = "crack";
        crackLangName[1] = "lang";
        string name;
        ModuleDefPtr mod = getModule(crackLangName.begin(),
                                     crackLangName.end(), 
                                     name
                                     );
        
        if (!mod) {
            cerr << "Bootstrapping module crack.lang not found." << endl;
            return false;
        }

        // extract the basic types from the module context
        rootContext->construct->objectType = extractClass(mod.get(), "Object");
        rootContext->ns->addAlias(rootContext->construct->objectType.get());
        rootContext->construct->stringType = extractClass(mod.get(), "String");
        rootContext->ns->addAlias(rootContext->construct->stringType.get());
        rootContext->construct->staticStringType = 
            extractClass(mod.get(), "StaticString");
        rootContext->ns->addAlias(
            rootContext->construct->staticStringType.get()
        );

        // replace the bootstrapping context with a new context that 
        // delegates to the original root context - this is the "bootstrapped 
        // context."  It contains all of the special definitions that were 
        // extracted from the bootstrapping modules.
        rootContext = rootContext->createSubContext(Context::module);
        
        // extract some constants
        VarDefPtr v = mod->lookUp("true");
        if (v)
            rootContext->ns->addAlias(v.get());
        v = mod->lookUp("false");
        if (v)
            rootContext->ns->addAlias(v.get());
        v = mod->lookUp("print");
        if (v)
            rootContext->ns->addUnsafeAlias("print", v.get());
        
        return rootContext->construct->objectType && 
               rootContext->construct->stringType;
    } catch (const spug::Exception &ex) {
        cerr << ex << endl;
        return false;
    } catch (...) {
        if (!uncaughtExceptionFunc)
            cerr << "Uncaught exception, no uncaught exception handler!" << 
                endl;
        else if (!uncaughtExceptionFunc())
            cerr << "Unknown exception caught." << endl;
    }
        
    
    return true;
}

// linker can't find modNameFromFile if this is an anonymous namespace
namespace model {
    
    // Returns a "brief path" for the filename.  A brief path consists of an 
    // md5 hash of the absolute path of the directory of the file, followed by 
    // an underscore and the file name.
    string briefPath(const string &filename) {
        
        // try to expand the name to the real path
        char path[PATH_MAX];
        string fullName;
        if (realpath(filename.c_str(), path))
            fullName = path;
        else
            fullName = filename;
        
        // convert the directory to a hash
        int lastSlash = fullName.rfind('/');
        if (lastSlash == fullName.size()) {
            return fullName;
        } else {
            ostringstream tmp;
            tmp << SourceDigest::fromStr(fullName.substr(0, lastSlash)).asHex()
                << '_' << fullName.substr(lastSlash + 1);
            return tmp.str();
        }
    }            
    
    // Converts a script name to its canonical module name.  Module names for 
    // scripts are of the form ".main._<abs-path-hash>_<escaped-filename>"
    string modNameFromFile(const string &filename) {
        ostringstream tmp;
        tmp << ".main._";
        string base = briefPath(filename);
        for (int i = 0; i < base.size(); ++i) {
            if (isalnum(base[i]))
                tmp << base[i];
            else
                tmp << "_" << hex << static_cast<int>(base[i]);
        }
        
        return tmp.str();
    }
}

int Construct::runScript(istream &src, const string &name, bool doRepl) {
    
    // get the canonical name for the script
    string canName = model::modNameFromFile(name);
    
    // create the builder and context for the script.
    BuilderPtr builder = rootBuilder->createChildBuilder();
    builderStack.push(builder);
    ContextPtr context =
        new Context(*builder, Context::module, rootContext.get(),
                    new GlobalNamespace(rootContext->ns.get(), canName)
                    );
    context->toplevel = true;

    ModuleDefPtr modDef;
    bool cached = false;
    if (rootContext->construct->cacheMode)
        crack::util::initCacheDirectory(rootBuilder->options.get(), *this);
    // we check cacheMode again after init,
    // because it might have been disabled if
    // we couldn't find an appropriate cache directory
    if (rootContext->construct->cacheMode) {
        modDef = context->materializeModule(canName);
    }
    if (modDef) {
        cached = true;
        loadedModules.push_back(modDef);
        if (rootBuilder->options->statsMode)
            stats->incCached();
    }
    else
        modDef = context->createModule(canName, name);

    try {
        if (!cached) {
            parseModule(*context, modDef.get(), name, src);
            loadedModules.push_back(modDef);
        } else {
            // XXX hook to run/finish cached module
        }
        if (doRepl) {
            runRepl(context.get(), modDef.get(), builder.get());
        }
    } catch (const spug::Exception &ex) {
        cerr << ex << endl;
        return 1;
    } catch (...) {
        if (!uncaughtExceptionFunc)
            cerr << "Uncaught exception, no uncaught exception handler!" <<
                endl;
        else if (!uncaughtExceptionFunc())
            cerr << "Unknown exception caught." << endl;
    }

    builderStack.pop();
    rootBuilder->finishBuild(*context);
    if (rootBuilder->options->statsMode)
        stats->setState(ConstructStats::end);
    return 0;
}

builder::Builder &Construct::getCurBuilder() {
    return *builderStack.top();
}

void Construct::registerDef(VarDef *def) {
    registry[def->getFullName()] = def;
}

VarDefPtr Construct::getRegisteredDef(const std::string &name) {
    VarDefMap::iterator iter = registry.find(name);
    if (iter != registry.end())
        return iter->second;
    else
        return 0;
}


namespace {

void cleanupUnfinishedInput(Builder *bdr, Context *ctx, ModuleDef *mod) {
    if (ctx->repl && ctx->repl->debugLevel() > 0) {
        printf(" [cleaning up unfinished line]\n");
    }
    bdr->eraseSection(*ctx, mod);

    bdr->purgeUnterminatedFunctions(*ctx, mod);
}


/**
 *  continueOnSpecial(): a runRepl helper.
 *    returns true if we have a special repl command and should keep looping.
 *    See the help option at the end of this function for a full
 *    description of special repl commands.
 *
 *  Note that a side effect of this call may be to completely replace
 *    the current r.getLastReadLine() value. For example, the print command
 *    does this kind of substitution (the print command is
 *    '.' by itself on a line, or if it has been changed from the default
 *    using repl.setReplCmdStart(), then whatever repl.getReplCmdStart()
 *    returns).
 */
bool continueOnSpecial(wisecrack::Repl& r, Context *context, Builder *bdr) {

    // check for ctrl-d and nothing else
    if (!strcmp("\n", r.getLastReadLine())) {
        if (r.done()) {
            return true;
        }
    }

    // special commands?
    const char *rcmd = r.getTrimmedLastReadLine();
    const char *p    = r.replCmd(rcmd);
    const char *end  = r.getLastReadLine() + r.getLastReadLineLen();

    // the special command starter: typically "." or "\"
    const char *s = r.getReplCmdStart();

    if (!p) return false;

    // INVAR: we have a repl command, and p points to it.
    if (0 == strlen(p) // "." alone
        ||  (' ' == *p && strlen(p) > 1)) { // or with a named sym to print
        // . sym : print sym
        // .     : print last sym

        const char *sym = p + 1;
        std::string cmd;

        if (0==strlen(p)) {
            // just . by itself on a line: print the
            // last thing added to the namespace, if we can.

            VarDefPtr vcout = context->ns->lookUp("cout");
            if (!vcout) {
                cmd += "import crack.io cout; ";
            }
        
            TypeDef *tdef = 0;
            sym = context->ns->lastTxSymbol(&tdef);
            if (NULL == sym) {
                printf("no last symbol to display.\n");
                if (!vcout) {
                    // still bring in cout
                    r.setNextLine(cmd.c_str());
                    return false;
                }
                return true;
            }

            // We can't display classes or operators at the moment.
            // Try to avoid crashing by detecting
            // unhandled cases here.
            if (tdef) {
                std::string x = tdef->name;
                const char *y = x.c_str();
                size_t pxl = x.size();
                if (!strcmp(y + pxl - 4,"meta")
                    || 0==strncmp(y, "function", 8)
                    ) {
                    printf("cannot display '%s' of type '%s' at the moment.\n", sym, y);
                    if (cmd.size()) {
                        // allow import of cout no matter.
                        r.setNextLine(cmd.c_str());
                        return false;
                    }
                    return true;
                }
            }
        }

        cmd += "cout `";
        cmd += sym;
        cmd += " = $(";
        cmd += sym;
        cmd += ")\n`;";
        r.setNextLine(cmd.c_str());
        
        // return false because we *want* to execute the cout print now.
        return false;
    }


    if (!strcmp("histoff", p)) {
        // turn off logging history to .crkhist
        r.histoff();
        printf("command logging to file '.crkhist' now off.\n");
        return true;

    } else if (!strcmp("histon", p)) {
        // turn on logging history to .crkhist
        r.histon();
        printf("logging subsequent commands to file '.crkhist'.\n");
        return true;
    }
    
    // otherwise, add to .crkhist file if that is on.
    r.loghist(p);

    if (!strcmp("q", p) ||
        !strcmp("quit", p)) {
        // quitting time.
        r.setDone();
        return true;

    } else if (!strcmp("dump", p)) {
        // dump: do full global dump of all namespaces.
        context->dump();
        return true;

    } else if (!strcmp("debug", p)) {
        // up the debugging level
        int d = r.debugLevel();
        ++d;
        printf("debug level: %d\n", d);
        r.set_debugLevel(d);
        return true;
    } else if (!strcmp("undebug", p)) {
        // reduce debugging
        int d = r.debugLevel();
        --d;
        if (d < 0) { d=0; }
        printf("debug level: %d\n", d);
        r.set_debugLevel(d);
        return true;
    }
    else if (!strcmp("dn", p) ||
             !strcmp("ls", p)) {
        // dn: dump namespace, local only
        context->short_dump();
        return true;

    } else if (!strcmp("dc", p)) {
        // dc: dump bit-code
        bdr->dump();
        return true;

    } else if (0==strncmp("dc ", p, 3) && strlen(p) > 3) {
        // dc: dump llvm-code for a symbol

        const char *sym = p + 3;
        while(isspace(*sym) && sym < end) { ++sym;  }
        
        // confirm we have an argument
        if (sym >= end) {
            printf("error using %sdc: no symbol-to-display-code for specified.\n", s);
            return true;
        }

        VarDefPtr var = context->ns->lookUp(sym);

        if (!var) {
            printf("error using %sdc: could not locate symbol '%s' to display code for.\n", s, sym);
            return true;
        }

        // functions
        model::OverloadDef *odef     = dynamic_cast<model::OverloadDef*>(var.get());

        // classes
        builder::mvll::BTypeDef *btd = dynamic_cast<builder::mvll::BTypeDef*>(var.get());

        // unimplemented (as of yet) type
        builder::mvll::BTypeDef *tp  = dynamic_cast<builder::mvll::BTypeDef*>(var->type.get());

        if (odef) {
            printf("display code for '%s' function(s):\n", sym);

            model::OverloadDef::FuncList::iterator it = odef->beginTopFuncs();
            model::OverloadDef::FuncList::iterator en = odef->endTopFuncs();
            for (; it != en; ++it) {

                FuncDefPtr fdp  = *it;
                FuncDef *  func = fdp.get();

                builder::mvll::LLVMBuilder *llvm_bdr = dynamic_cast<builder::mvll::LLVMBuilder*>(bdr);
                
                if (func && llvm_bdr) {
                    
                    builder::mvll::LLVMBuilder::ModFuncMap::iterator it = llvm_bdr->moduleFuncs.find(func);
                    
                    if (it != llvm_bdr->moduleFuncs.end()) {
                        
                        // display the llvm code for just one function.
                        llvm::Function *f = it->second;
                        llvm::outs() << static_cast<llvm::Value&>(*f);
                        
                    } else {
                        printf("%sdc error: could not find FuncDef -> llvm::Function* "
                               "mapping for '%s' in builder.\n", s, sym);                        
                    }
                }
            }

        } else if (btd) {
            printf("%sdc '%s' error: displaying classes not yet implemented.\n", s, sym);

        } else {
            printf("%sdc '%s' error: I don't know how to dump code for type '%s' yet.\n",
                   s, sym, tp ? tp->name.c_str() : "*unknown type*");

        }

        return true;

    } else if (0==strncmp("rm ", p, 3) && strlen(p) > 3) {
        // rm sym : remove symbol sym from namespace

        const char *sym = p + 3;
        while(isspace(*sym) && sym < end) { ++sym;  }

        // confirm we have an argument sym to delete
        if (sym >= end) {
            printf("error using %srm: no symbol-to-delete specified.\n", s);
            return true;
        }

        VarDefPtr var = context->ns->lookUp(sym);

        if (!var) {
            printf("error using %srm: could not locate symbol '%s' to delete.\n", s, sym);
            return true;
        }

        printf("rm deleting symbol '%s' at 0x%lx.\n", sym, (unsigned long)var.get());

        context->ns->removeDefAllowOverload(var.get());
        return true;

    } else if (0==strncmp("!", p, 1)) {
        const char *cmd = p + 1;
        int res = system(cmd);
        return true;

    } else if (0==strncmp(".", p, 1)) {
        // source a file
        const char *sourceme = p + 1;
        while(isspace(*sourceme) && sourceme < end) { ++sourceme;  }

        // validate file
        FILE *f = fopen(sourceme,"r");
        if (!f) {
            printf("error in %s. source file: could not open file '%s'\n",
                   s, sourceme);
            return true;
        }

        // shovel data into r.src stream
        const int b = 4096;
        char buf[b];
        size_t m = 0;
        while(!feof(f)) {
            bzero(buf, b);
            m += fread(buf, 1, b-1, f);
            r.src << buf;
        }

        if (r.debugLevel() > 0) {
            printf("sourced %ld bytes from '%s'\n", m, sourceme);
        }

        r.setNextLine("");
        return false; // run from r.src.
    }
    else
    if (!strcmp("history", p)) {
        if (r.history.size() == 1) return true; // first cmd.

        wisecrack::Repl::histlist::iterator it = r.history.begin();
        wisecrack::Repl::histlist::iterator en = r.history.end();
        --en; // don't print the last .history (duh).
        long line = 1;
        for(; it != en; ++it, ++line) {
            //printf("%ld: %s\n", line, it->c_str());

            // easier to copy and paste *without *line numbers!
            printf("%s\n", it->c_str());
        }
        return true;
    }
    else if (0==strncmp("prefix ", p, 7) && strlen(p) > 7) {
        const char *pre = p + 7;
        printf("setting repl command prefix to '%s'\n", pre);
        r.setReplCmdStart(pre);
        return true;
    }

    if (!strcmp("help", p)) {

        printf("wisecrack repl help: ['%s' prefix starts repl commands]\n"
               "  %shelp    = show this hint page\n"
               "  %sdn      = dump wisecrack namespace (also %sls)\n"
               "  %sdump    = dump global namespace (everything)\n"
               "  %sdc      = display code (LLVM bitcode)\n"
               "  %sdebug   = increase the debug messages\n"
               "  %sundebug = reduce debug messages (0 => off)\n"
               "  %squit    = quit repl (also %sq)\n"
               "  ctrl-d   = EOF also quits\n"
               "  ctrl-c   = interrupt line and return to the repl\n"
               "  %srm sym  = remove symbol sym from namespace\n"
               "  %s sym    = print sym on cout. Does 'import crack.io cout;' if necessary.\n"
               "  %s        = print last symbol made (skips internals with ':' prefix)\n"
               "  %shistory = display command line history\n"
               "  %s!cmd    = call system(cmd), executing cmd in a shell.\n"
               "  %s. file  = read and execute commands from file.\n"
               "  %sprefix c = set repl command prefix to c ['%s' now].\n"
               "  %shiston  = save history to .crkhist file (vs %shistoff) [%s now]\n",
               s,s,s,s,
               s,s,s,s,
               s,s,s,s,
               s,s,s,s,

               s,s,s,s,

               r.hist() ? "ON" :  "OFF"
               );
              
        return true;
    }
    return false;
}

} // end anonymous namespace for continueOnSpecial and cleanupUnfinishedInput


// Status: the repl is now quite usable. Multiline input works well.
//         Cleanup after syntax error in functions or expressions is there.
//         The remaining big items are outlined below.
//
// XXX TODO list 20 Dec 2012
//
//   _ cleanup aborted (due to syntax error or other error) class definitions.
//       possibly with a temp namespace that is merge upon correctness, or
//       discarded upon error.
//
// But if you're strongly motivated to do correct cleanup, I wonder if we 
// could make use of a temporary repl ModuleDef object and then transfer its
// definitions to a longer lived one?  Then we could drive the rollback
// functionality from ModuleDef -> BModuleDef -> LLVM.  -- mmuller
//
// Status: working. Classes with syntax errors are now cleaned up using
//         a combination of OrderedIdLog and 
//         Builder::purgeUnterminatedFunctions. The temp namespace
//         sounds promising, but I could not work out exactly how
//         to make it viable.
//
//   _ allow re-defining of functions/variables/classes at the repl
//
//    Update (14 Dec 2012): Works but leaks. Redefinition at the repl just
//      clears the symbol from the namespace; it doesn't do proper 
//      cleanup like Michael describes below, but it works for now
//      (albiet with the accompanying leak of llvm rep objects).
//
//    Michael's thoughts: (7 Dec 2012)
//
// I think the best way to handle redefinition is just to give the context a
// flag to avoid the error.  The cleanest way to deal with it is if that 
// flag is set, do a delete on the underlying LLVM "rep" objects.
// That needs to be something passed in through the FuncDef object and 
// actually implemented in the BFuncDef object, though, and I don't think
// it's crucial.  For a repl I wouldn't be too concerned about leakage.
//
//   _ .crackrc startup in cwd or ~/.crackd : automatically run these scripts
//      upon startup unless suppressed with a startup flag.
//
//   _. comment on runRepl(): you shouldn't need to pass in a builder this -
// the context should always have a reference to its builder.
// XXX TODO: try refactoring to use the ctx->builder instead of bdr.
//
//   _. when development is done: change printf to streams, remove
//   globalRepl hack, possibly remove debugLevel monitoring code.

/**
 * runRepl(): experimental jit-based interpreter. Project name: wisecrack.
 *
 * @param ctx : if non-null, use this context
 * @param mod : if non-null, use this module
 * @param bdr : if non-null, use this builder
 *
 * To work well, either all three parameters should be supplied at
 *  once (e.g. after runScript), or they should all be null.
 */

int Construct::runRepl(Context *arg_ctx, ModuleDef *arg_modd, Builder *arg_bdr) {

    wisecrack::Repl repl;

    // smart pointers
    ModuleDefPtr modDef;
    ContextPtr   context;
    BuilderPtr   builder;
    GlobalNamespacePtr local_compile_ns;

    // point to passed in arguments, unless none given, in which
    // case they will point to the smart pointers above.
    ModuleDef *mod = arg_modd;
    Context *ctx = arg_ctx;
    Builder *bdr = arg_bdr;

    // are we starting fresh, or dropping into a previously defined
    //  ctx, modd, bdr triple?
    if (arg_ctx) {
        if (0==arg_modd || 0==arg_bdr) {
            cerr << "internal error in Construct::runRepl: all three"
                " {ctx,modd,bdr} must be set or must all be null." 
                 << endl;
            assert(0);
            return 1;
        }
 
        // already closed the last function and ran it.
        // so we just need to start a new section inside the main loop,
        // without issuing another closeModule (as closeSection() would)
        // which would run the script a second time.

        ctx->repl = &repl;

    } else {

        // starting fresh context/module/builder

        // canonical name for module representing input from repl
        string canName = ".repl";
        
        // create the builder and context for the repl.
        builder = rootBuilder->createChildBuilder();
        builderStack.push(builder);
        
        Context *prior = rootContext.get();
        
        local_compile_ns = new GlobalNamespace(prior->ns.get(), canName);
        
        context = new Context(*builder,
                              Context::module, 
                              prior,
                              local_compile_ns.get(),
                              0,
                              &repl);
        context->toplevel = true;
        
        bool cached = false;

#if 0 // we don't have cacheMode yet in 0.7.1, that comes later.
        if (rootContext->construct->cacheMode)
            builder::initCacheDirectory(rootBuilder->options.get(), *this);
#endif

        modDef = context->createModule(canName, canName);

        // set the pointers we will use below
        mod = modDef.get();
        ctx = context.get();
        bdr = builder.get();

        repl.setBuilder(bdr);

        // close :main - now we open a new section after receiving
        // each individual command.
        bdr->closeSection(*ctx, mod);

    } // end else start fresh context/module/builder

    bool sectionStarted = false;
    bool doCleanup = false;
    Namespace::Txmark ns_start_point;

    printf("wisecrack read-eval-print-loop [type .help for hints]\n");
    
    //
    // main Read-Eval-Print loop
    //
    while(!repl.done()) {

        try {
            doCleanup = false;
            sectionStarted = false;

            // track cleanups, so we don't removeFromParent() twice and crash.
            repl.goneSet.clear(); 

            // READ
            repl.resetSrcToEmpty();
            repl.nextLineNumber();
            repl.resetPromptToDefault();
            repl.prompt(stdout);
            repl.read(stdin); // can throw
            if (repl.getLastReadLineLen()==0) continue;

            if (continueOnSpecial(repl, ctx, bdr)) continue;

            // EVAL

            // beginSection() must come *after* the continueOnSpecial() 
            // call. Otherwise we begin multiple times and leave
            // dangling half-finished sections lying around. :-(

            bdr->beginSection(*ctx, mod);
            ns_start_point = (ctx->ns.get())->markTransactionStart();
            
            sectionStarted = true;
            
            repl.src << repl.getLastReadLine();

            std::string path = repl.getPrompt();

            Toker toker(repl.src, path.c_str());
            Parser parser(toker, ctx);
            parser.setAtRepl(repl);
            parser.parse();

            // stats collection
            StatState sState(ctx, ConstructStats::parser, mod);
            if (sState.statsEnabled()) {
                stats->incParsed();
            }

            // We can't split up the close and the exec,
            // because import statements themselves will
            // try to closeModule, and they won't know
            // that the exec is missing; i.e. they require
            // the closeModule() to do a run() on their own 
            // module function's main function
            // to finish the import.

            // closeSection() finishes the module and runs 
            // the last function constructed in it.

            bdr->closeSection(*ctx, mod);

            // PRINT: provided as a short-cut special command '.' currently.

        } catch (const wisecrack::ExceptionCtrlC &ex) {
            cout << " [ctrl-c]\n";
            doCleanup = true;

        } catch (const spug::Exception &ex) {
            cerr << ex << endl;
            doCleanup = true;

        } catch (...) {
            if (!uncaughtExceptionFunc)
                cerr << "Uncaught exception, no uncaught exception handler!" <<
                    endl;
            else if (!uncaughtExceptionFunc())
                cerr << "Unknown exception caught." << endl;
            doCleanup = true;
        }

        if (doCleanup) {
            if (sectionStarted) {
                cleanupUnfinishedInput(bdr, ctx, mod);
            }
            (ctx->ns.get())->undoTransactionTo(ns_start_point, &repl);
        }

    } // end while

    if (ctx) { ctx->repl = 0; }
    builderStack.pop();
    rootBuilder->finishBuild(*ctx);
    if (rootBuilder->options->statsMode)
        stats->setState(ConstructStats::end);
    return 0;
}

