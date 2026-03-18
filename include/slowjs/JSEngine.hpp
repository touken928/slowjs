#pragma once
#include <slowjs/Module.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace slowjs {

class JSEngine {
public:
    struct CompileResult {
        bool success = false;
        std::string error;
        std::vector<uint8_t> bytecode;
    };

    JSEngine();
    ~JSEngine();
    JSEngine(const JSEngine&) = delete;
    JSEngine& operator=(const JSEngine&) = delete;
    JSEngine(JSEngine&&) noexcept;
    JSEngine& operator=(JSEngine&&) noexcept;

    void setErrorCallback(std::function<void(const std::string&)> callback);

    JSModule& root();
    const JSModule& root() const;

    void initialize();
    void installModules();
    void cleanup();

    // Promise creation and lifecycle (for async C++ APIs).
    struct PromiseHandle { void* ptr = nullptr; };
    PromiseHandle createPromise();
    RawJSValue    promiseValue(PromiseHandle h) const;
    void          resolvePromise(PromiseHandle h, const std::string& data);
    void          resolvePromiseVoid(PromiseHandle h);
    void          rejectPromise(PromiseHandle h, const std::string& error);
    void          freePromise(PromiseHandle h);

    // ---------------------------------------------------------------------
    // Host data access (embedding API)
    //
    // setHost<T> / host<T> store and retrieve a single pointer per C++ type T.
    // Lifetime is managed by the host; slowjs never owns or frees these.
    // ---------------------------------------------------------------------
    template<typename T>
    void setHost(T* ptr) {
        setHostImpl(typeKey<T>(), static_cast<void*>(ptr));
    }

    template<typename T>
    T* host() const {
        return static_cast<T*>(hostImpl(typeKey<T>()));
    }

    // Backward-compatible raw pointer channel (maps to a dedicated slot).
    void setHostData(void* ptr) { setHost<void>(ptr); }
    void* hostData() const { return host<void>(); }

    bool runFile(const std::string& path);
    bool runModuleCode(const std::string& virtualName, const std::string& code);
    // Execute plain JS statements (global/script mode), not as an ES module.
    bool eval(const std::string& virtualName, const std::string& code);
    bool runBytecode(const uint8_t* buf, size_t bufLen);

    template <typename... Args>
    bool callGlobal(const char* name, Args... args) {
        return callGlobalImpl(name, sizeof...(Args), [&](JSContext* c, JSValue* argv) {
            if constexpr (sizeof...(Args) > 0) {
                convertArgs(c, argv, args...);
            }
        });
    }

    static CompileResult compile(const std::string& code, const std::string& filename);

    JSContext* ctx() const;

private:
    struct Impl;
    struct InternalPromise;
    std::unique_ptr<Impl> impl_;

    void executePendingJobs();
    bool callGlobalImpl(const char* name, size_t argc, const std::function<void(JSContext*, JSValue*)>& fillArgs);
    bool evalImpl(const std::string& virtualName, const std::string& code, int evalFlags);

    // Internal helpers for type-based host storage.
    using TypeKey = const void*;
    template<typename T>
    static TypeKey typeKey() {
        static int dummy;
        return &dummy;
    }
    void setHostImpl(TypeKey key, void* ptr);
    void* hostImpl(TypeKey key) const;

    template <typename T, typename... Rest>
    static void convertArgs(JSContext* c, JSValue* argv, T first, Rest... rest) {
        argv[0] = JSConv<decay_t<T>>::to(c, first);
        if constexpr (sizeof...(Rest) > 0) convertArgs(c, argv + 1, rest...);
    }
};

} // namespace slowjs

