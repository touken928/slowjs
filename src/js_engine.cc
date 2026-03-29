#include <js_engine.h>
#include <js_plugin.h>

#include <fstream>
#include <sstream>

namespace qjs {

static JSClassID g_funcClassId = 0;

struct JSEngine::Impl {
    JSRuntime* rt = nullptr;
    JSContext* ctx = nullptr;
    JSModule rootModule{"global", nullptr};

    bool installed = false;
    bool cleanedUp = false;

    // Type-keyed host storage: each C++ type T maps to a single void*.
    std::unordered_map<JSEngine::TypeKey, void*> hostStorage;

    std::unordered_map<JSModuleDef*, JSModule*> modData;
    std::unordered_map<std::string, JSModuleDef*> jsModuleCache;
    std::function<void(const std::string&)> errorCallback;

    static JSValue callFunc(JSContext* c, JSValue /*thisVal*/, int argc, JSValue* argv, int /*magic*/, JSValue* data) {
        FuncBase* wrapper = static_cast<FuncBase*>(JS_GetOpaque(data[0], g_funcClassId));
        if (!wrapper) return JS_ThrowInternalError(c, "invalid function wrapper");
        return wrapper->call(c, argc, argv);
    }

    JSValue createJSFunction(FuncBase* wrapper) {
        JSValue funcData = JS_NewObjectClass(ctx, g_funcClassId);
        JS_SetOpaque(funcData, wrapper);
        JSValue fn = JS_NewCFunctionData(ctx, &Impl::callFunc, 0, 0, 1, &funcData);
        JS_FreeValue(ctx, funcData);
        return fn;
    }

    void installChildModule(JSContext* c, JSValue obj, JSModule& mod) {
        for (auto& [name, wrapper] : mod.funcs_) {
            JSValue fn = createJSFunction(wrapper.get());
            if (JS_IsException(fn) || JS_SetPropertyStr(c, obj, name.c_str(), fn) < 0) {
                fprintf(stderr, "Error: Failed to install child function '%s'\n", name.c_str());
            }
        }
        for (auto& [name, creator] : mod.values_) {
            JSValue val = creator(c);
            if (JS_IsException(val) || JS_SetPropertyStr(c, obj, name.c_str(), val) < 0) {
                fprintf(stderr, "Error: Failed to install child value '%s'\n", name.c_str());
            }
        }
        for (auto& [name, child] : mod.children_) {
            JSValue childObj = JS_NewObject(c);
            if (JS_IsException(childObj)) {
                fprintf(stderr, "Error: Failed to create nested child module '%s'\n", name.c_str());
                continue;
            }
            installChildModule(c, childObj, *child);
            if (JS_SetPropertyStr(c, obj, name.c_str(), childObj) < 0) {
                fprintf(stderr, "Error: Failed to install nested child module '%s'\n", name.c_str());
            }
        }
    }

    void installToObject(JSValue obj, JSModule& mod) {
        for (auto& [name, wrapper] : mod.funcs_) {
            JSValue fn = createJSFunction(wrapper.get());
            if (JS_IsException(fn) || JS_SetPropertyStr(ctx, obj, name.c_str(), fn) < 0) {
                fprintf(stderr, "Error: Failed to install function '%s'\n", name.c_str());
            }
        }
        for (auto& [name, creator] : mod.values_) {
            JSValue val = creator(ctx);
            if (JS_IsException(val) || JS_SetPropertyStr(ctx, obj, name.c_str(), val) < 0) {
                fprintf(stderr, "Error: Failed to install value '%s'\n", name.c_str());
            }
        }
        for (auto& [name, child] : mod.children_) {
            JSValue childObj = JS_NewObject(ctx);
            if (JS_IsException(childObj)) {
                fprintf(stderr, "Error: Failed to create child module object '%s'\n", name.c_str());
                continue;
            }
            installChildModule(ctx, childObj, *child);
            if (JS_SetPropertyStr(ctx, obj, name.c_str(), childObj) < 0) {
                fprintf(stderr, "Error: Failed to install child module '%s'\n", name.c_str());
            }
        }
    }

    static JSModule* findModule(Impl* self, const std::string& name) {
        auto it = self->rootModule.children_.find(name);
        return it != self->rootModule.children_.end() ? it->second.get() : nullptr;
    }

    static int initModule(JSContext* c, JSModuleDef* m) {
        auto* self = static_cast<Impl*>(JS_GetContextOpaque(c));
        auto it = self->modData.find(m);
        if (it == self->modData.end()) return -1;
        JSModule* mod = it->second;

        for (auto& [name, wrapper] : mod->funcs_) {
            JSValue fn = self->createJSFunction(wrapper.get());
            if (JS_IsException(fn) || JS_SetModuleExport(c, m, name.c_str(), fn) < 0) {
                fprintf(stderr, "Error: Failed to export function '%s'\n", name.c_str());
                return -1;
            }
        }
        for (auto& [name, creator] : mod->values_) {
            JSValue val = creator(c);
            if (JS_IsException(val) || JS_SetModuleExport(c, m, name.c_str(), val) < 0) {
                fprintf(stderr, "Error: Failed to export value '%s'\n", name.c_str());
                return -1;
            }
        }
        for (auto& [name, child] : mod->children_) {
            JSValue obj = JS_NewObject(c);
            if (JS_IsException(obj)) {
                fprintf(stderr, "Error: Failed to create child module object '%s'\n", name.c_str());
                return -1;
            }
            self->installChildModule(c, obj, *child);
            if (JS_SetModuleExport(c, m, name.c_str(), obj) < 0) {
                fprintf(stderr, "Error: Failed to export child module '%s'\n", name.c_str());
                return -1;
            }
        }
        return 0;
    }

    static JSModuleDef* createCppModule(JSContext* c, const char* name, JSModule* mod) {
        auto* self = static_cast<Impl*>(JS_GetContextOpaque(c));
        JSModuleDef* m = JS_NewCModule(c, name, &Impl::initModule);
        if (!m) return nullptr;
        self->modData[m] = mod;
        for (auto& [n, _] : mod->funcs_) JS_AddModuleExport(c, m, n.c_str());
        for (auto& [n, _] : mod->values_) JS_AddModuleExport(c, m, n.c_str());
        for (auto& [n, _] : mod->children_) JS_AddModuleExport(c, m, n.c_str());
        return m;
    }

    static JSModuleDef* loadModule(JSContext* c, const char* name, void* opaque) {
        auto* self = static_cast<Impl*>(opaque);
        if (!self || self->cleanedUp) {
            JS_ThrowInternalError(c, "JSEngine has been cleaned up");
            return nullptr;
        }

        JSModule* mod = findModule(self, name);
        if (mod) return createCppModule(c, name, mod);

        std::string modulePath = name;
        auto cacheIt = self->jsModuleCache.find(modulePath);
        if (cacheIt != self->jsModuleCache.end()) return cacheIt->second;

        std::string path = modulePath;
        if (path.find(".js") == std::string::npos) path += ".js";
        std::ifstream f(path);
        if (!f) {
            JS_ThrowReferenceError(c, "module not found: %s", name);
            return nullptr;
        }
        std::stringstream buf;
        buf << f.rdbuf();
        std::string code = buf.str();

        JSValue compiled = JS_Eval(c, code.c_str(), code.size(), name,
            JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
        if (JS_IsException(compiled)) return nullptr;
        JSModuleDef* moduleDef = (JSModuleDef*)JS_VALUE_GET_PTR(compiled);
        JS_FreeValue(c, compiled);
        self->jsModuleCache[modulePath] = moduleDef;
        return moduleDef;
    }

    void dumpError() {
        JSValue ex = JS_GetException(ctx);
        std::string errorMsg;

        JSValue msgVal = JS_GetPropertyStr(ctx, ex, "message");
        if (!JS_IsUndefined(msgVal) && !JS_IsException(msgVal)) {
            const char* msg = JS_ToCString(ctx, msgVal);
            if (msg) {
                errorMsg = std::string("Error: ") + msg;
                JS_FreeCString(ctx, msg);
            }
        }
        JS_FreeValue(ctx, msgVal);

        if (errorMsg.empty()) {
            const char* s = JS_ToCString(ctx, ex);
            if (s) {
                errorMsg = std::string("Error: ") + s;
                JS_FreeCString(ctx, s);
            } else {
                errorMsg = "Error: [unable to convert exception to string]";
            }
        }

        JSValue stack = JS_GetPropertyStr(ctx, ex, "stack");
        if (!JS_IsUndefined(stack) && !JS_IsException(stack)) {
            const char* st = JS_ToCString(ctx, stack);
            if (st) {
                errorMsg += std::string("\n") + st;
                JS_FreeCString(ctx, st);
            }
        }
        JS_FreeValue(ctx, stack);
        JS_FreeValue(ctx, ex);

        if (errorCallback) errorCallback(errorMsg);
        else fprintf(stderr, "%s\n", errorMsg.c_str());
    }

    void registerModuleExportsToGlobal(JSValue moduleNS) {
        if (JS_IsUndefined(moduleNS) || JS_IsException(moduleNS)) return;
        JSValue global = JS_GetGlobalObject(ctx);

        JSPropertyEnum* props = nullptr;
        uint32_t propCount = 0;
        if (JS_GetOwnPropertyNames(ctx, &props, &propCount, moduleNS,
                JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) == 0) {
            for (uint32_t i = 0; i < propCount; i++) {
                JSValue val = JS_GetProperty(ctx, moduleNS, props[i].atom);
                if (!JS_IsException(val)) {
                    JS_SetProperty(ctx, global, props[i].atom, JS_DupValue(ctx, val));
                }
                JS_FreeValue(ctx, val);
            }
            JS_FreePropertyEnum(ctx, props, propCount);
        }
        JS_FreeValue(ctx, global);
    }
};

void PluginRegistry::installAll(JSEngine& engine, JSModule& root) const {
    for (auto& [_, plugin] : plugins_) {
        plugin->install(engine, root);
    }
}

JSEngine::JSEngine() : impl_(std::make_unique<Impl>()) {}
JSEngine::~JSEngine() { cleanup(); }

JSEngine::JSEngine(JSEngine&&) noexcept = default;
JSEngine& JSEngine::operator=(JSEngine&&) noexcept = default;

void JSEngine::setErrorCallback(std::function<void(const std::string&)> callback) {
    impl_->errorCallback = std::move(callback);
}

JSModule& JSEngine::root() { return impl_->rootModule; }
const JSModule& JSEngine::root() const { return impl_->rootModule; }

void JSEngine::initialize() {
    if (impl_->rt) return;
    impl_->cleanedUp = false;

    impl_->rt = JS_NewRuntime();
    impl_->ctx = JS_NewContext(impl_->rt);

    if (g_funcClassId == 0) JS_NewClassID(&g_funcClassId);
    JSClassDef classDef = {};
    classDef.class_name = "CppFunc";
    classDef.finalizer = nullptr;
    JS_NewClass(impl_->rt, g_funcClassId, &classDef);

    JS_SetContextOpaque(impl_->ctx, impl_.get());
    JS_SetModuleLoaderFunc(impl_->rt, nullptr, &Impl::loadModule, impl_.get());
}

void JSEngine::installModules() {
    if (!impl_->ctx || impl_->installed) return;
    impl_->installed = true;
    JSValue g = JS_GetGlobalObject(impl_->ctx);
    impl_->installToObject(g, impl_->rootModule);
    JS_FreeValue(impl_->ctx, g);
}

void JSEngine::cleanup() {
    if (!impl_) return;
    if (!impl_->ctx && !impl_->rt) return;
    impl_->cleanedUp = true;

    if (impl_->ctx) {
        JSValue global = JS_GetGlobalObject(impl_->ctx);
        JSPropertyEnum* props = nullptr;
        uint32_t propCount = 0;
        if (JS_GetOwnPropertyNames(impl_->ctx, &props, &propCount, global,
                JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) == 0) {
            for (uint32_t i = 0; i < propCount; i++) {
                JS_DeleteProperty(impl_->ctx, global, props[i].atom, JS_PROP_THROW);
            }
            JS_FreePropertyEnum(impl_->ctx, props, propCount);
        }
        JS_FreeValue(impl_->ctx, global);

        JS_FreeContext(impl_->ctx);
        impl_->ctx = nullptr;
    }

    impl_->jsModuleCache.clear();
    impl_->modData.clear();
    impl_->installed = false;

    if (impl_->rt) {
        JS_FreeRuntime(impl_->rt);
        impl_->rt = nullptr;
    }
}

void JSEngine::setHostImpl(TypeKey key, void* ptr) {
    if (!impl_) return;
    impl_->hostStorage[key] = ptr;
}

void* JSEngine::hostImpl(TypeKey key) const {
    if (!impl_) return nullptr;
    auto it = impl_->hostStorage.find(key);
    return it == impl_->hostStorage.end() ? nullptr : it->second;
}

bool JSEngine::runFile(const std::string& path) {
    if (!impl_->ctx || impl_->cleanedUp) return false;
    std::ifstream f(path);
    if (!f) { fprintf(stderr, "Cannot open: %s\n", path.c_str()); return false; }
    std::stringstream buf;
    buf << f.rdbuf();
    return runModuleCode(path, buf.str());
}

bool JSEngine::runModuleCode(const std::string& virtualName, const std::string& code) {
    return evalImpl(virtualName, code, JS_EVAL_TYPE_MODULE);
}

bool JSEngine::eval(const std::string& virtualName, const std::string& code) {
    return evalImpl(virtualName, code, JS_EVAL_TYPE_GLOBAL);
}

bool JSEngine::evalImpl(const std::string& virtualName, const std::string& code, int evalFlags) {
    if (!impl_->ctx || impl_->cleanedUp) return false;
    installModules();
    JSValue r = JS_Eval(impl_->ctx, code.c_str(), code.size(), virtualName.c_str(), evalFlags);
    if (JS_IsException(r)) { impl_->dumpError(); JS_FreeValue(impl_->ctx, r); return false; }
    JS_FreeValue(impl_->ctx, r);
    executePendingJobs();
    return true;
}

bool JSEngine::runBytecode(const uint8_t* buf, size_t bufLen) {
    if (!impl_->ctx || impl_->cleanedUp) return false;
    installModules();

    JSValue obj = JS_ReadObject(impl_->ctx, buf, bufLen, JS_READ_OBJ_BYTECODE);
    if (JS_IsException(obj)) { impl_->dumpError(); return false; }

    bool isModule = (JS_VALUE_GET_TAG(obj) == JS_TAG_MODULE);
    JSModuleDef* moduleDef = nullptr;
    if (isModule) {
        moduleDef = (JSModuleDef*)JS_VALUE_GET_PTR(obj);
        if (JS_ResolveModule(impl_->ctx, obj) < 0) {
            impl_->dumpError();
            return false;
        }
    }

    JSValue result = JS_EvalFunction(impl_->ctx, obj);
    if (JS_IsException(result)) { impl_->dumpError(); return false; }
    JS_FreeValue(impl_->ctx, result);

    if (isModule && moduleDef) {
        JSValue moduleNS = JS_GetModuleNamespace(impl_->ctx, moduleDef);
        if (!JS_IsException(moduleNS)) {
            impl_->registerModuleExportsToGlobal(moduleNS);
            JS_FreeValue(impl_->ctx, moduleNS);
        }
    }
    executePendingJobs();
    return true;
}

void JSEngine::pumpMicrotasks() {
    executePendingJobs();
}

bool JSEngine::isJobPending() const {
    if (!impl_ || !impl_->rt) return false;
    return JS_IsJobPending(impl_->rt) != 0;
}

bool JSEngine::callGlobalImpl(const char* name, size_t argc, const std::function<void(JSContext*, JSValue*)>& fillArgs) {
    if (!impl_->ctx || impl_->cleanedUp) return false;

    JSValue global = JS_GetGlobalObject(impl_->ctx);
    JSValue func = JS_GetPropertyStr(impl_->ctx, global, name);

    bool success = false;
    if (JS_IsFunction(impl_->ctx, func)) {
        std::vector<JSValue> argv(argc);
        if (argc > 0) fillArgs(impl_->ctx, argv.data());
        JSValue result = JS_Call(impl_->ctx, func, global, (int)argc, argc ? argv.data() : nullptr);
        if (JS_IsException(result)) {
            impl_->dumpError();
        } else {
            success = true;
        }
        JS_FreeValue(impl_->ctx, result);
        for (auto& v : argv) JS_FreeValue(impl_->ctx, v);

        executePendingJobs();
    }

    JS_FreeValue(impl_->ctx, func);
    JS_FreeValue(impl_->ctx, global);
    return success;
}

JSEngine::CompileResult JSEngine::compile(const std::string& code, const std::string& filename) {
    CompileResult result;
    JSRuntime* rt = JS_NewRuntime();
    if (!rt) { result.error = "Failed to create JS runtime"; return result; }
    JSContext* ctx = JS_NewContext(rt);
    if (!ctx) { result.error = "Failed to create JS context"; JS_FreeRuntime(rt); return result; }

    JSValue obj = JS_Eval(ctx, code.c_str(), code.size(), filename.c_str(),
        JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);

    if (JS_IsException(obj)) {
        JSValue ex = JS_GetException(ctx);
        const char* msg = JS_ToCString(ctx, ex);
        result.error = msg ? msg : "Unknown compile error";
        if (msg) JS_FreeCString(ctx, msg);
        JS_FreeValue(ctx, ex);
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        return result;
    }

    size_t outSize = 0;
    uint8_t* outBuf = JS_WriteObject(ctx, &outSize, obj, JS_WRITE_OBJ_BYTECODE);
    JS_FreeValue(ctx, obj);

    if (!outBuf) {
        result.error = "Failed to serialize bytecode";
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        return result;
    }

    result.bytecode.assign(outBuf, outBuf + outSize);
    result.success = true;

    js_free(ctx, outBuf);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    return result;
}

JSEngine::CompileResult JSEngine::compileModuleFromSource(const std::string& code, const std::string& filename) {
    CompileResult result;
    if (!impl_ || !impl_->ctx || impl_->cleanedUp) {
        result.error = "JSEngine not initialized";
        return result;
    }

    installModules();

    JSValue obj = JS_Eval(impl_->ctx, code.c_str(), code.size(), filename.c_str(),
        JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);

    if (JS_IsException(obj)) {
        JSValue ex = JS_GetException(impl_->ctx);
        const char* msg = JS_ToCString(impl_->ctx, ex);
        result.error = msg ? msg : "Unknown compile error";
        if (msg) JS_FreeCString(impl_->ctx, msg);
        JS_FreeValue(impl_->ctx, ex);
        JS_FreeValue(impl_->ctx, obj);
        return result;
    }

    size_t outSize = 0;
    uint8_t* outBuf = JS_WriteObject(impl_->ctx, &outSize, obj, JS_WRITE_OBJ_BYTECODE);
    JS_FreeValue(impl_->ctx, obj);

    if (!outBuf) {
        result.error = "Failed to serialize bytecode";
        return result;
    }

    result.bytecode.assign(outBuf, outBuf + outSize);
    result.success = true;
    js_free(impl_->ctx, outBuf);
    return result;
}

JSContext* JSEngine::ctx() const { return impl_ ? impl_->ctx : nullptr; }

// ---------------------------------------------------------------------------
// Promise helpers
// ---------------------------------------------------------------------------

struct JSEngine::InternalPromise {
    JSValue promise = JS_UNDEFINED;
    JSValue resolve = JS_UNDEFINED;
    JSValue reject  = JS_UNDEFINED;
};

void JSEngine::executePendingJobs() {
    if (!impl_ || !impl_->rt) return;
    JSContext* jobCtx = nullptr;
    for (;;) {
        int rc = JS_ExecutePendingJob(impl_->rt, &jobCtx);
        if (rc <= 0) break;
    }
}

JSEngine::PromiseHandle JSEngine::createPromise() {
    if (!impl_ || !impl_->ctx) return {nullptr};
    JSContext* c = impl_->ctx;

    JSValue funcs[2];
    JSValue promise = JS_NewPromiseCapability(c, funcs);
    if (JS_IsException(promise)) {
        impl_->dumpError();
        return {nullptr};
    }

    auto* ip = new InternalPromise;
    ip->promise = promise;            // takes initial ref
    ip->resolve = funcs[0];
    ip->reject  = funcs[1];
    return {static_cast<void*>(ip)};
}

RawJSValue JSEngine::promiseValue(PromiseHandle h) const {
    auto* ip = static_cast<InternalPromise*>(h.ptr);
    if (!ip) return {JS_UNDEFINED};
    return {ip->promise};             // NOT DupValue'd – JSConv<RawJSValue>::to will Dup
}

void JSEngine::resolvePromise(PromiseHandle h, const std::string& data) {
    auto* ip = static_cast<InternalPromise*>(h.ptr);
    if (!ip || !impl_ || !impl_->ctx) return;
    JSContext* c = impl_->ctx;

    JSValue arg = JS_NewString(c, data.c_str());
    JSValue r = JS_Call(c, ip->resolve, JS_UNDEFINED, 1, &arg);
    JS_FreeValue(c, arg);
    if (JS_IsException(r)) impl_->dumpError();
    JS_FreeValue(c, r);

    executePendingJobs();
}

void JSEngine::resolvePromise(PromiseHandle h, int64_t n) {
    auto* ip = static_cast<InternalPromise*>(h.ptr);
    if (!ip || !impl_ || !impl_->ctx) return;
    JSContext* c = impl_->ctx;

    JSValue arg = JS_NewInt64(c, n);
    JSValue r = JS_Call(c, ip->resolve, JS_UNDEFINED, 1, &arg);
    JS_FreeValue(c, arg);
    if (JS_IsException(r)) impl_->dumpError();
    JS_FreeValue(c, r);

    executePendingJobs();
}

void JSEngine::resolvePromiseVoid(PromiseHandle h) {
    auto* ip = static_cast<InternalPromise*>(h.ptr);
    if (!ip || !impl_ || !impl_->ctx) return;
    JSContext* c = impl_->ctx;

    JSValue undef = JS_UNDEFINED;
    JSValue r = JS_Call(c, ip->resolve, JS_UNDEFINED, 1, &undef);
    if (JS_IsException(r)) impl_->dumpError();
    JS_FreeValue(c, r);

    executePendingJobs();
}

void JSEngine::rejectPromise(PromiseHandle h, const std::string& error) {
    auto* ip = static_cast<InternalPromise*>(h.ptr);
    if (!ip || !impl_ || !impl_->ctx) return;
    JSContext* c = impl_->ctx;

    JSValue errObj = JS_NewError(c);
    JS_DefinePropertyValueStr(c, errObj, "message",
        JS_NewString(c, error.c_str()), JS_PROP_C_W_E);
    JSValue r = JS_Call(c, ip->reject, JS_UNDEFINED, 1, &errObj);
    JS_FreeValue(c, errObj);
    if (JS_IsException(r)) impl_->dumpError();
    JS_FreeValue(c, r);

    executePendingJobs();
}

void JSEngine::freePromise(PromiseHandle h) {
    auto* ip = static_cast<InternalPromise*>(h.ptr);
    if (!ip) return;
    if (impl_ && impl_->ctx) {
        JS_FreeValue(impl_->ctx, ip->promise);
        JS_FreeValue(impl_->ctx, ip->resolve);
        JS_FreeValue(impl_->ctx, ip->reject);
    }
    delete ip;
}

} // namespace qjs

