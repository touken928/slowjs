# slowjs

`slowjs` is a small C++ wrapper around **QuickJS**, providing:

- A simple C++/JS binding layer (functions, values, nested modules)
- ES module loading (native modules + `.js` modules)
- Bytecode compile/execute helpers
- Plugin-based module installation (`PluginRegistry`)

## API quick glance

### Create an engine, register native module, run a JS entry

```cpp
#include <slowjs/JSEngine.hpp>

slowjs::JSEngine engine;
engine.initialize();

auto& root = engine.root();
auto& math = root.module("math");
math.func("add", [](int a, int b) { return a + b; });
math.value("PI", 3.1415926);

engine.runFile("main.js");
engine.callGlobal("update", 0.016);
```

### Plugins

```cpp
#include <slowjs/JSEngine.hpp>
#include <slowjs/Plugin.hpp>

struct MyPlugin : slowjs::IEnginePlugin {
  const char* name() const override { return "my_plugin"; }
  void install(slowjs::JSEngine& engine, slowjs::JSModule& root) override {
    (void)engine;
    root.module("sys").value("version", std::string("1.0"));
  }
};

slowjs::PluginRegistry plugins;
plugins.add(std::make_unique<MyPlugin>());

slowjs::JSEngine engine;
engine.initialize();
plugins.installAll(engine, engine.root());
engine.runFile("main.js");
```

### Host data (embedding native services)

`slowjs` provides a small type-based host storage to pass native engine services
into plugins and modules:

```cpp
slowjs::JSEngine engine;
engine.initialize();

// Expose a renderer (or any other native service) to plugins
engine.setHost<render::IRenderer>(&renderer);

// In a plugin:
struct GraphicsPlugin : slowjs::IEnginePlugin {
  const char* name() const override { return "graphics"; }

  void install(slowjs::JSEngine& engine, slowjs::JSModule& root) override {
    if (auto* renderer = engine.host<render::IRenderer>()) {
      // Bind renderer into your own C++ facade and expose JS functions
      // ...
    }
  }
};
```

Each C++ type `T` maps to at most one pointer in `setHost<T>(T*)`, and
`host<T>()` returns the same pointer (or `nullptr` if not registered). The
stored pointer is never owned or freed by `slowjs`; its lifetime is managed by
the embedding application.

## Build

```bash
git submodule update --init --recursive
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
```

