# 构建工程

- 默认使用 `copilotBuild.py` 编译目标项目，优先走增量 `Build`，不要手动调用 `msbuild`。
- 这个脚本会编译单个单元测试项目，默认是 `RuntimeTest`，并把产物同步到 `Test/UnitTest/x64/Debug`，供后续执行脚本直接使用。
- `copilotBuild.ps1` 保留为旧入口，不作为默认构建方式。

## 执行 `copilotBuild.py`

在编译之前，先确保调试器已经停止。
如果停止脚本返回“调试器不存在”之类的报错，说明当前没有活跃调试会话，可以继续。

```
& REPO-ROOT\.github\Scripts\copilotDebug_Stop.ps1
```

然后在 `SOLUTION-ROOT` 下执行 Python 编译脚本：

```
cd SOLUTION-ROOT
python REPO-ROOT\.github\Scripts\copilotBuild.py
```

## 目标配置

`copilotBuild.py` 支持这些参数：
- `--configuration` 可选 `Debug`（默认）或 `Release`。
- `--platform` 可选 `x64`（默认）或 `Win32`。
- `--project` 可指定要编译的项目名，默认 `RuntimeTest`。
- `--rebuild` 会显式执行 `Rebuild`，默认不使用，以便保持增量编译。

## 如何读取编译结果

- 可信来源只有编译器的原始输出。
- 等脚本执行结束后再看日志。
  - 不需要实时看脚本回显。
  - 编译时间可能较长，不要催促。
  - 编译结束后，脚本会把编译结果同步到 `Test/UnitTest/x64/Debug`。
- `copilotBuild.py` 执行成功时，控制台会输出“编译完成，输出已同步。”。
