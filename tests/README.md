# 测试程序目录

这个目录包含所有的测试程序，与主项目代码分离，不会影响主项目的架构。

## 文件说明

- `test_websocket_tiles.cpp` - WebSocket瓦片传输功能测试
- `test_tile_manager.cpp` - 瓦片管理器测试
- `test_tile_system.cpp` - 瓦片系统测试
- `CMakeLists.txt` - 测试程序的独立构建配置

## 如何编译测试程序

### 方法1：启用测试子目录（推荐）
在主项目的 `CMakeLists.txt` 中取消注释这行：
```cmake
add_subdirectory(tests)
```

然后正常编译主项目：
```bash
cd build
cmake --build . --config Release
```

测试程序将输出到 `build/tests/` 目录。

### 方法2：独立编译测试
```bash
cd tests
mkdir build
cd build
cmake .. -DCMAKE_PREFIX_PATH="C:/Qt/6.8.3/msvc2022_64"
cmake --build . --config Release
```

## 运行测试

```bash
# WebSocket瓦片传输测试
./TestWebSocketTiles.exe
```

## 注意事项

- 测试程序不会影响主项目的架构
- 测试程序默认不编译，需要手动启用
- 所有测试文件都在这个独立的文件夹中管理