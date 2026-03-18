# slowjs

`slowjs` is a small C++ wrapper around **QuickJS**, providing:

- A simple C++/JS binding layer (functions, values, nested modules)
- ES module loading (native modules + `.js` modules)
- Bytecode compile/execute helpers
- Plugin-based module installation (`PluginRegistry`)

## Build

```bash
git submodule update --init --recursive
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
```

