
#include "Crack.h"

#include <sys/stat.h>
#include <fstream>
#include "model/Context.h"
#include "model/ModuleDef.h"
#include "model/TypeDef.h"
#include "parser/Parser.h"
#include "parser/ParseError.h"
#include "parser/Toker.h"
#include "builder/Builder.h"
#include "builder/LLVMBuilder.h"

using namespace std;
using namespace model;
using namespace parser;
using namespace builder;

Crack *Crack::theInstance = 0;

Crack &Crack::getInstance() {
    if (!theInstance)
        theInstance = new Crack();
    
    return *theInstance;
}

void Crack::addToSourceLibPath(const string &path) {
    size_t pos = 0;
    size_t i = path.find(':');
    while (i != -1) {
        sourceLibPath.push_back(path.substr(pos, i - pos));
        pos = i + 1;
        i = path.find('.', pos);
    }
    sourceLibPath.push_back(path.substr(pos));
}

Crack::Crack() : 
    sourceLibPath(1), 
    rootBuilder(new LLVMBuilder()), 
    initialized(false),
    dump(false),
    noBootstrap(false),
    useGlobalLibs(true) {

    rootContext = new Context(*rootBuilder, Context::module, (Context *)0);

    // register the primitives    
    rootBuilder->registerPrimFuncs(*rootContext);
    
    // search for source files in the current directory
    sourceLibPath[0] = ".";
}

bool Crack::init() {
    if (!initialized) {
        // finalize the search path
        if (useGlobalLibs)
            sourceLibPath.push_back(CRACKLIB);

        // load the bootstrapping modules - library files that are essential 
        // to the language, like the definitions of the Object and String 
        // classes.
        if (!noBootstrap && !loadBootstrapModules())
            return false;

        initialized = true;
    }
    
    return true;
}

//Crack::~Crack() {}

std::string Crack::joinName(const std::string &base,
                            Crack::StringVecIter pathBegin,
                            Crack::StringVecIter pathEnd,
                            const std::string &ext
                            ) {
    // not worrying about performance, this only happens during module load.
    string result = base;
    
    // base off current directory if an empty path was specified.
    if (!result.size())
        result = ".";

    for (StringVecIter iter = pathBegin;
         iter != pathEnd;
         ++iter
         ) {
        result += "/" + *iter;
    }

    return result + ext;
}

bool Crack::isFile(const std::string &name) {
    struct stat st;
    if (stat(name.c_str(), &st))
        return false;
    
    // XXX should check symlinks, too
    return S_ISREG(st.st_mode);
}

bool Crack::isDir(const std::string &name) {
    struct stat st;
    if (stat(name.c_str(), &st))
        return false;
    
    // XXX should check symlinks, too
    return S_ISDIR(st.st_mode);
}

Crack::ModulePath Crack::searchPath(const Crack::StringVec &path,
                                    Crack::StringVecIter moduleNameBegin,
                                    Crack::StringVecIter moduleNameEnd,
                                    const std::string &extension
                                    ) {
    // try to find a matching file.
    for (StringVecIter pathIter = path.begin();
         pathIter != path.end();
         ++pathIter
         ) {
        string fullName = joinName(*pathIter, moduleNameBegin, moduleNameEnd,
                                   extension
                                   );
        if (isFile(fullName))
            return ModulePath(fullName, true, false);
    }
    
    // try to find a matching directory.
    string empty;
    for (StringVecIter pathIter = path.begin();
         pathIter != path.end();
         ++pathIter
         ) {
        string fullName = joinName(*pathIter, moduleNameBegin, moduleNameEnd,
                                   empty
                                   );
        if (isDir(fullName))
            return ModulePath(fullName, true, true);
    }
    
    return ModulePath(empty, false, false);
}

void Crack::parseModule(ModuleDef *module,
                        const std::string &path,
                        istream &src
                        ) {
    module->create();
    Toker toker(src, path.c_str());
    Parser parser(toker, module->moduleContext.get());
    parser.parse();
    module->close();
    if (dump)
        module->moduleContext->builder.dump();
    else
        module->moduleContext->builder.run();
}

ModuleDefPtr Crack::loadModule(Crack::StringVecIter moduleNameBegin,
                               Crack::StringVecIter moduleNameEnd,
                               string &canonicalName
                               ) {
    // create the dotted canonical name of the module
    for (StringVecIter iter = moduleNameBegin;
         iter != moduleNameEnd;
         ++iter
         )
        if (canonicalName.size())
            canonicalName += "." + *iter;
        else
            canonicalName = *iter;

    // check to see if we have it in the cache
    ModuleMap::iterator iter = moduleCache.find(canonicalName);
    if (iter != moduleCache.end())
        return iter->second;

    // load the parent module
    StringVec::const_reverse_iterator rend(moduleNameEnd);
    ++rend;
    if (rend != StringVec::const_reverse_iterator(moduleNameBegin)) {
        StringVecIter last(rend.base());
        string parentCanonicalName;
        ModuleDefPtr parent = loadModule(moduleNameBegin, last, 
                                         parentCanonicalName
                                         );
    }
    
    // try to find the module on the source path
    ModulePath modPath = searchPath(sourceLibPath, moduleNameBegin, 
                                    moduleNameEnd,
                                    ".crk"
                                    );
    if (!modPath.found)
        return 0;

    // create a new builder, context and module
    BuilderPtr builder = rootBuilder->createChildBuilder();
    ContextPtr context =
        new Context(*builder, Context::module, rootContext.get());
    ModuleDefPtr modDef = new ModuleDef(canonicalName, context.get());
    if (!modPath.isDir) {
        ifstream src(modPath.path.c_str());
        parseModule(modDef.get(), modPath.path, src);
    }
    
    moduleCache[canonicalName] = modDef;
    return modDef;
}    

namespace {
    // extract a class from a module and verify that it is a class - returns 
    // null on failure.
    TypeDef *extractClass(ModuleDef *mod, const char *name) {
        VarDefPtr var = mod->moduleContext->lookUp(name);
        TypeDef *type;
        if (var && (type = TypeDefPtr::rcast(var))) {
            return type;
        } else {
            cerr << name << " class not found in module crack.lang" << endl;
            return 0;
        }
    }
}

bool Crack::loadBootstrapModules() {
    try {
        StringVec crackLangName(2);
        crackLangName[0] = "crack";
        crackLangName[1] = "lang";
        string name;
        ModuleDefPtr mod = loadModule(crackLangName, name);
        
        if (!mod) {
            cerr << "Bootstrapping module crack.lang not found." << endl;
            return false;
        }

        // extract the basic types from the module context
        rootContext->globalData->objectType = extractClass(mod.get(), "Object");
        rootContext->addAlias(rootContext->globalData->objectType.get());
        rootContext->globalData->stringType = extractClass(mod.get(), "String");
        rootContext->addAlias(rootContext->globalData->stringType.get());
        rootContext->globalData->staticStringType = 
            extractClass(mod.get(), "StaticString");
        rootContext->addAlias(rootContext->globalData->staticStringType.get());
        
        // extract some constants
        VarDefPtr v = mod->moduleContext->lookUp("true");
        if (v)
            rootContext->addAlias(v.get());
        v = mod->moduleContext->lookUp("false");
        if (v)
            rootContext->addAlias(v.get());
        v = mod->moduleContext->lookUp("print");
        if (v)
            rootContext->addAlias(v.get());
        
        return rootContext->globalData->objectType && 
               rootContext->globalData->stringType;
    } catch (const ParseError &ex) {
        cerr << ex << endl;
        return false;
    }
        
    
    return true;
}

int Crack::runScript(std::istream &src, const std::string &name) {
    // finalize all initialization
    if (!init())
        return 1;

    // create the builder and context for the script.
    BuilderPtr builder = rootBuilder->createChildBuilder();
    ContextPtr context =
        new Context(*builder, Context::module, rootContext.get());

    // XXX using the name as the canonical name which is not right, need to 
    // produce a canonical name from the file name, e.g. "foo" -> "foo", 
    // "foo.crk" -> foo, "anything weird" -> "__main__" or something.
    ModuleDefPtr modDef = new ModuleDef(name, context.get());
    try {
        parseModule(modDef.get(), name, src);
    } catch (const ParseError &ex) {
        cerr << ex << endl;
        return 1;
    }
}

ModuleDefPtr Crack::loadModule(const vector<string> &moduleName,
                               string &canonicalName
                               ) {
    return getInstance().loadModule(moduleName.begin(), 
                                    moduleName.end(), 
                                    canonicalName
                                    );
}