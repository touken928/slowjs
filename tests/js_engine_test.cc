#include <gtest/gtest.h>

#include <js_engine.h>
#include <js_plugin.h>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>

namespace {

std::filesystem::path makeTempJsFile(const std::string& content) {
    namespace fs = std::filesystem;
    static std::mt19937_64 rng{std::random_device{}()};
    auto path = fs::temp_directory_path() /
                (std::string("qjs_engine_test_") + std::to_string(rng()) + ".js");
    {
        std::ofstream out(path);
        out << content;
    }
    return path;
}

} // namespace

TEST(JsEngine, RunEmptyModule) {
    qjs::JSEngine engine;
    engine.initialize();
    EXPECT_TRUE(engine.runModuleCode("empty.js", "export {};\n"));
    engine.cleanup();
}

TEST(JsEngine, EvalGlobal) {
    qjs::JSEngine engine;
    engine.initialize();
    EXPECT_TRUE(engine.eval("stmt.js", "void 0;\n"));
    engine.cleanup();
}

TEST(JsEngine, CompileStaticSuccess) {
    auto r = qjs::JSEngine::compile("export const x = 1;\n", "mod.js");
    EXPECT_TRUE(r.success);
    EXPECT_FALSE(r.bytecode.empty());
    EXPECT_TRUE(r.error.empty());
}

TEST(JsEngine, CompileStaticFailure) {
    auto r = qjs::JSEngine::compile("this is not javascript {{{\n", "bad.js");
    EXPECT_FALSE(r.success);
    EXPECT_TRUE(r.bytecode.empty());
    EXPECT_FALSE(r.error.empty());
}

TEST(JsEngine, CompileModuleFromSourceRequiresInit) {
    qjs::JSEngine engine;
    auto r = engine.compileModuleFromSource("export {};\n", "m.js");
    EXPECT_FALSE(r.success);
    EXPECT_NE(r.error.find("not initialized"), std::string::npos);
}

TEST(JsEngine, CompileModuleFromSourceAfterInit) {
    qjs::JSEngine engine;
    engine.initialize();
    auto r = engine.compileModuleFromSource("export const n = 2;\n", "m.js");
    EXPECT_TRUE(r.success);
    EXPECT_FALSE(r.bytecode.empty());
    engine.cleanup();
}

TEST(JsEngine, CompileModuleFromSourceSyntaxError) {
    qjs::JSEngine engine;
    engine.initialize();
    auto r = engine.compileModuleFromSource("export {{{\n", "bad.js");
    EXPECT_FALSE(r.success);
    EXPECT_FALSE(r.error.empty());
    engine.cleanup();
}

TEST(JsEngine, RunBytecodeRoundTrip) {
    auto compiled = qjs::JSEngine::compile("export {};\n", "bc.js");
    ASSERT_TRUE(compiled.success);
    ASSERT_FALSE(compiled.bytecode.empty());

    qjs::JSEngine engine;
    engine.initialize();
    EXPECT_TRUE(engine.runBytecode(compiled.bytecode.data(), compiled.bytecode.size()));
    engine.cleanup();
}

TEST(JsEngine, RunFileReadsModule) {
    auto path = makeTempJsFile("export const ok = true;\n");
    qjs::JSEngine engine;
    engine.initialize();
    EXPECT_TRUE(engine.runFile(path.string()));
    engine.cleanup();
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

TEST(JsEngine, HostPointerRoundTrip) {
    int host = 42;
    qjs::JSEngine engine;
    engine.initialize();
    engine.setHost<int>(&host);
    EXPECT_EQ(engine.host<int>(), &host);
    EXPECT_EQ(*engine.host<int>(), 42);
    engine.cleanup();
}

TEST(JsEngine, HostDataAlias) {
    int x = 3;
    qjs::JSEngine engine;
    engine.initialize();
    engine.setHostData(&x);
    EXPECT_EQ(engine.hostData(), static_cast<void*>(&x));
    engine.cleanup();
}

TEST(JsEngine, NativeModuleFuncBinding) {
    qjs::JSEngine engine;
    engine.initialize();
    engine.root().module("math").func("twice", [](int n) { return n * 2; });
    EXPECT_TRUE(engine.runModuleCode(
        "t.js",
        "import * as m from 'math';\n"
        "if (m.twice(5) !== 10) throw new Error('twice');\n"
        "export {};\n"));
    engine.cleanup();
}

TEST(JsEngine, NativeModuleValueBinding) {
    qjs::JSEngine engine;
    engine.initialize();
    engine.root().module("cfg").value("label", std::string("hello"));
    EXPECT_TRUE(engine.runModuleCode(
        "t.js",
        "import { label } from 'cfg';\n"
        "if (label !== 'hello') throw new Error('label');\n"
        "export {};\n"));
    engine.cleanup();
}

TEST(JsEngine, NestedNativeModuleBinding) {
    qjs::JSEngine engine;
    engine.initialize();
    engine.root().module("outer").module("inner").func("id", [](int x) { return x; });
    EXPECT_TRUE(engine.runModuleCode(
        "t.js",
        "import * as o from 'outer';\n"
        "if (o.inner.id(7) !== 7) throw new Error('nested');\n"
        "export {};\n"));
    engine.cleanup();
}

TEST(JsEngine, PluginRegistryInstallAll) {
    struct LabelPlugin : qjs::IEnginePlugin {
        const char* name() const override { return "label_plugin"; }
        void install(qjs::JSEngine&, qjs::JSModule& root) override {
            root.module("labels").value("ID", 99);
        }
    };

    qjs::PluginRegistry reg;
    reg.emplace<LabelPlugin>();

    qjs::JSEngine engine;
    engine.initialize();
    reg.installAll(engine, engine.root());
    EXPECT_TRUE(engine.runModuleCode(
        "p.js",
        "import { ID } from 'labels';\n"
        "if (ID !== 99) throw new Error('ID');\n"
        "export {};\n"));
    engine.cleanup();
}

TEST(JsEngine, CallGlobalWithArgs) {
    qjs::JSEngine engine;
    engine.initialize();
    ASSERT_TRUE(engine.eval("defs.js", "globalThis.sum = (a, b) => a + b;\n"));
    EXPECT_TRUE(engine.callGlobal("sum", 10, 32));
    engine.cleanup();
}

TEST(JsEngine, CallGlobalMissingReturnsFalse) {
    qjs::JSEngine engine;
    engine.initialize();
    EXPECT_FALSE(engine.callGlobal("noSuchGlobalFunction_xyz"));
    engine.cleanup();
}

TEST(JsEngine, EvalSyntaxErrorInvokesCallback) {
    qjs::JSEngine engine;
    engine.initialize();
    std::string captured;
    engine.setErrorCallback([&captured](const std::string& msg) { captured = msg; });
    EXPECT_FALSE(engine.eval("bad.js", "let x = {{{\n"));
    EXPECT_FALSE(captured.empty());
    engine.cleanup();
}

TEST(JsEngine, MoveConstructorKeepsWorkingEngine) {
    qjs::JSEngine a;
    a.initialize();
    qjs::JSEngine b = std::move(a);
    EXPECT_TRUE(b.runModuleCode("m.js", "export {};\n"));
    b.cleanup();
}

TEST(JsEngine, DoubleInitializeIsSafe) {
    qjs::JSEngine engine;
    engine.initialize();
    engine.initialize();
    EXPECT_TRUE(engine.eval("x.js", "void 0;\n"));
    engine.cleanup();
}

TEST(JsEngine, PumpMicrotasksAndJobPending) {
    qjs::JSEngine engine;
    engine.initialize();
    ASSERT_TRUE(engine.eval("x.js", "void 0;\n"));
    engine.pumpMicrotasks();
    EXPECT_FALSE(engine.isJobPending());
    engine.cleanup();
}

TEST(JsEngine, CreatePromiseBeforeInitReturnsNull) {
    qjs::JSEngine engine;
    EXPECT_EQ(engine.createPromise().ptr, nullptr);
}

TEST(JsEngine, PromiseCreateResolveVoidAndFree) {
    qjs::JSEngine engine;
    engine.initialize();
    qjs::JSEngine::PromiseHandle h = engine.createPromise();
    ASSERT_NE(h.ptr, nullptr);
    engine.resolvePromiseVoid(h);
    engine.freePromise(h);
    engine.cleanup();
}

TEST(JsEngine, RunModuleCodeSyntaxError) {
    qjs::JSEngine engine;
    engine.initialize();
    EXPECT_FALSE(engine.runModuleCode("badmod.js", "export {{{\n"));
    engine.cleanup();
}
