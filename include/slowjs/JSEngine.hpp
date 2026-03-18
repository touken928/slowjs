#pragma once
#include <quickjs.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace slowjs {

template <typename T> struct JSConv;

template <> struct JSConv<int> {
    static int from(JSContext* c, JSValue v, bool& ok) {
        int32_t r;
        if (JS_ToInt32(c, &r, v) < 0) { ok = false; return 0; }
        ok = true;
        return (int)r;
    }
    static JSValue to(JSContext* c, int v) { return JS_NewInt32(c, v); }
};

template <> struct JSConv<int64_t> {
    static int64_t from(JSContext* c, JSValue v, bool& ok) {
        int64_t r;
        if (JS_ToInt64(c, &r, v) < 0) { ok = false; return 0; }
        ok = true;
        return r;
    }
    static JSValue to(JSContext* c, int64_t v) { return JS_NewInt64(c, v); }
};

template <> struct JSConv<double> {
    static double from(JSContext* c, JSValue v, bool& ok) {
        double r;
        if (JS_ToFloat64(c, &r, v) < 0) { ok = false; return 0; }
        ok = true;
        return r;
    }
    static JSValue to(JSContext* c, double v) { return JS_NewFloat64(c, v); }
};

template <> struct JSConv<float> {
    static float from(JSContext* c, JSValue v, bool& ok) {
        double r;
        if (JS_ToFloat64(c, &r, v) < 0) { ok = false; return 0; }
        ok = true;
        return (float)r;
    }
    static JSValue to(JSContext* c, float v) { return JS_NewFloat64(c, v); }
};

template <> struct JSConv<bool> {
    static bool from(JSContext* c, JSValue v, bool& ok) {
        if (!JS_IsBool(v)) { ok = false; return false; }
        ok = true;
        return JS_ToBool(c, v);
    }
    static JSValue to(JSContext* c, bool v) { return JS_NewBool(c, v); }
};

template <> struct JSConv<std::string> {
    static std::string from(JSContext* c, JSValue v, bool& ok) {
        const char* s = JS_ToCString(c, v);
        if (!s) { ok = false; return {}; }
        std::string r = s;
        JS_FreeCString(c, s);
        ok = true;
        return r;
    }
    static JSValue to(JSContext* c, const std::string& v) { return JS_NewString(c, v.c_str()); }
};

template <typename T>
struct JSConv<std::vector<T>> {
    static constexpr int64_t MAX_ARRAY_LENGTH = 1000000;

    static std::vector<T> from(JSContext* c, JSValue v, bool& ok) {
        if (!JS_IsArray(c, v)) {
            ok = false;
            JS_ThrowTypeError(c, "expected array");
            return {};
        }
        JSValue lenVal = JS_GetPropertyStr(c, v, "length");
        int64_t len = 0;
        if (JS_ToInt64(c, &len, lenVal) < 0) {
            JS_FreeValue(c, lenVal);
            ok = false;
            return {};
        }
        JS_FreeValue(c, lenVal);

        if (len > MAX_ARRAY_LENGTH) {
            ok = false;
            JS_ThrowRangeError(c, "array length %lld exceeds maximum %lld",
                (long long)len, (long long)MAX_ARRAY_LENGTH);
            return {};
        }

        std::vector<T> result;
        result.reserve((size_t)len);
        for (int64_t i = 0; i < len; i++) {
            JSValue elem = JS_GetPropertyUint32(c, v, (uint32_t)i);
            bool elemOk = false;
            T val = JSConv<T>::from(c, elem, elemOk);
            JS_FreeValue(c, elem);
            if (!elemOk) {
                ok = false;
                JS_ThrowTypeError(c, "array element at index %lld conversion failed", (long long)i);
                return {};
            }
            result.push_back(std::move(val));
        }
        ok = true;
        return result;
    }

    static JSValue to(JSContext* c, const std::vector<T>& v) {
        JSValue arr = JS_NewArray(c);
        if (JS_IsException(arr)) return arr;
        for (size_t i = 0; i < v.size(); i++) {
            JSValue elem = JSConv<T>::to(c, v[i]);
            if (JS_IsException(elem)) {
                JS_FreeValue(c, arr);
                return elem;
            }
            if (JS_SetPropertyUint32(c, arr, (uint32_t)i, elem) < 0) {
                JS_FreeValue(c, arr);
                return JS_EXCEPTION;
            }
        }
        return arr;
    }
};

template <typename T> using decay_t = std::remove_cv_t<std::remove_reference_t<T>>;
template <typename> struct dependent_false : std::false_type {};

template <typename T>
struct JSConv {
    static_assert(dependent_false<T>::value,
        "JSConv<T> not specialized for this type. Supported: int, int64_t, double, float, bool, std::string, std::vector<T>");
};

struct FuncBase {
    virtual ~FuncBase() = default;
    virtual JSValue call(JSContext*, int, JSValue*) = 0;
    virtual int arity() const = 0;
};

template <typename Tuple, size_t... I>
bool unpack_impl(JSContext* c, JSValue* v, Tuple& result, std::index_sequence<I...>) {
    bool ok = true;
    ((ok = ok && [&]() {
        bool elemOk = false;
        std::get<I>(result) = JSConv<decay_t<std::tuple_element_t<I, Tuple>>>::from(c, v[I], elemOk);
        return elemOk;
    }()), ...);
    return ok;
}

template <typename... Args>
std::tuple<decay_t<Args>...> unpackArgs(JSContext* c, JSValue* v, bool& ok) {
    std::tuple<decay_t<Args>...> result{};
    ok = unpack_impl(c, v, result, std::index_sequence_for<Args...>{});
    return result;
}

template <typename Ret, typename... Args>
struct FuncWrap : FuncBase {
    std::function<Ret(Args...)> fn;
    explicit FuncWrap(std::function<Ret(Args...)> f) : fn(std::move(f)) {}
    int arity() const override { return (int)sizeof...(Args); }
    JSValue call(JSContext* c, int argc, JSValue* argv) override {
        constexpr int expected = (int)sizeof...(Args);
        if (argc != expected) return JS_ThrowTypeError(c, "expected exactly %d arguments, got %d", expected, argc);
        bool ok = false;
        auto args = unpackArgs<Args...>(c, argv, ok);
        if (!ok) {
            if (!JS_IsException(JS_GetException(c))) return JS_ThrowTypeError(c, "argument type conversion failed");
            return JS_EXCEPTION;
        }
        if constexpr (std::is_void_v<Ret>) {
            std::apply(fn, args);
            return JS_UNDEFINED;
        } else {
            return JSConv<Ret>::to(c, std::apply(fn, args));
        }
    }
};

class JSModule {
    friend class JSEngine;
public:
    JSModule& module(const std::string& name) {
        auto& child = children_[name];
        if (!child) child = std::make_unique<JSModule>(name, this);
        return *child;
    }

    template <typename F>
    JSModule& func(const std::string& name, F&& f) {
        addFunc(name, std::forward<F>(f), &std::decay_t<F>::operator());
        return *this;
    }

    template <typename Ret, typename... Args>
    JSModule& func(const std::string& name, Ret(*f)(Args...)) {
        funcs_[name] = std::make_unique<FuncWrap<Ret, Args...>>(std::function<Ret(Args...)>(f));
        return *this;
    }

    template <typename T>
    JSModule& value(const std::string& name, T v) {
        values_[name] = [v](JSContext* c) { return JSConv<decay_t<T>>::to(c, v); };
        return *this;
    }

private:
    std::string name_;
    JSModule* parent_ = nullptr;
    std::unordered_map<std::string, std::unique_ptr<JSModule>> children_;
    std::unordered_map<std::string, std::unique_ptr<FuncBase>> funcs_;
    std::unordered_map<std::string, std::function<JSValue(JSContext*)>> values_;

public:
    JSModule(const std::string& name, JSModule* parent) : name_(name), parent_(parent) {}

private:
    template <typename F, typename Ret, typename C, typename... Args>
    void addFunc(const std::string& n, F&& f, Ret(C::*)(Args...) const) {
        funcs_[n] = std::make_unique<FuncWrap<Ret, Args...>>(std::function<Ret(Args...)>(std::forward<F>(f)));
    }
    template <typename F, typename Ret, typename C, typename... Args>
    void addFunc(const std::string& n, F&& f, Ret(C::*)(Args...)) {
        funcs_[n] = std::make_unique<FuncWrap<Ret, Args...>>(std::function<Ret(Args...)>(std::forward<F>(f)));
    }
};

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

    bool runFile(const std::string& path);
    bool runModuleCode(const std::string& virtualName, const std::string& code);
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
    std::unique_ptr<Impl> impl_;

    bool callGlobalImpl(const char* name, size_t argc, const std::function<void(JSContext*, JSValue*)>& fillArgs);

    template <typename T, typename... Rest>
    static void convertArgs(JSContext* c, JSValue* argv, T first, Rest... rest) {
        argv[0] = JSConv<decay_t<T>>::to(c, first);
        if constexpr (sizeof...(Rest) > 0) convertArgs(c, argv + 1, rest...);
    }
};

} // namespace slowjs

