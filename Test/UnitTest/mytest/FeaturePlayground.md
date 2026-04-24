# 脚本特性体验清单

这个文档用于在 `mytest` 项目里逐步体验 Workflow 脚本语言的特性。  
建议的做法是：每次只把下面一段脚本放进 `mytest.cpp` 的 `moduleCodes.Add(...)` 里，编译运行一次，再换下一段。

## 调试接入

如果要让 VSCode 的 `workflow` 调试器附加到 `mytest`，启动 `mytest.exe` 时加上 `--workflow-debug`。

- 默认会连接到 `127.0.0.1:4711`
- 需要改地址时，可以用 `--workflow-debug-host=...`
- 需要改端口时，可以用 `--workflow-debug-port=...`
- 需要区分多个会话时，可以用 `--workflow-debug-session=...`
- 如果要模拟 LuaPanda 的“接入后立刻断下”，在 VSCode 的 `workflow` 配置里把 `stopOnEntry` 保持为 `true`

这套启动参数只影响调试模式，不影响普通脚本运行。

## 1. 最小可运行脚本

先确认最基础的运行链路正常，能返回字符串。

```workflow
module test;

func main(): string
{
	return "Hello, world!";
}
```

看点：

- 模块声明
- 函数定义
- 返回值

## 2. 条件分支

体验 `if / else` 的基础控制流。

```workflow
module test;

func main(a : int, b : int): string
{
	if(a == b)
	{
		return "相等";
	}
	else
	{
		return "不相等";
	}
}
```

看点：

- 参数传递
- 比较表达式
- 分支返回

## 3. 循环和累加

体验 `for` 和局部变量。

```workflow
module test;

func Sum(begin : int, end : int) : int
{
	var sum : int = 0;
	for(x in range [begin,end])
	{
		sum = sum + x;
	}
	return sum;
}
```

看点：

- 局部变量声明
- `for ... in range`
- 简单的数值计算

## 4. 结构体

体验纯数据类型。

```workflow
module test;

struct Point
{
	x : int;
	y : int;
	z : int;
}
```

看点：

- 结构体字段
- 数据建模
- 适合承载简单坐标、配置和状态

## 5. 类、方法、属性

体验更完整的面向对象能力。

```workflow
module test;

class Members
{
	var counter : int = 0;

	func Add(value : int) : int
	{
		counter = counter + value;
		return counter;
	}

	func GetCounter() : int
	{
		return counter;
	}

	prop Counter : int { GetCounter }
}
```

看点：

- 类定义
- 成员变量
- 方法调用
- 属性封装

## 6. 继承和自动属性

体验类继承和更接近工程代码的写法。

```workflow
module test;

class Base {}

class Derived : Base
{
	prop Name : string = "" {}
}
```

看点：

- 基类和派生类
- 属性默认值
- 类层次结构

## 7. 接口和事件

体验抽象能力和回调风格的成员。

```workflow
module test;

interface Members
{
	func GetData() : string;
	func SetData(value : string) : void;
	event DataChanged();
	prop Data : string { GetData, SetData : DataChanged }
}
```

看点：

- 接口定义
- 函数成员
- 事件
- 带通知的属性

## 8. 枚举

体验常量集合和位标志写法。

```workflow
module test;

flagenum Seasons
{
	None = 0,
	Spring = 1,
	Summer = 2,
	Autumn = 4,
	Winter = 8,
}
```

看点：

- 命名常量
- 位标志
- 适合状态、模式、权限等场景

## 9. 命名空间

体验把类型和函数放进命名空间，避免名字冲突。

```workflow
namespace test
{
	func Main() : string
	{
		return "namespace";
	}
}
```

看点：

- 命名空间组织
- 适合大型脚本工程拆分

## 10. 下一步建议

如果你想继续深入体验，这几个方向最值得试：

1. `try / catch / finally`
2. `switch`
3. `foreach`
4. 协程
5. 状态机
6. 反射相关能力
7. `bind` 和观察式表达式

这些特性更能体现 Workflow 不只是“语法像脚本”，而是偏工程化、偏宿主互操作的语言。

## 11. 多脚本嵌套调用

如果你想测试多文件模块和跨文件调用，可以直接运行新增的 `NestedCalls` 场景。它由四个脚本文件组成，函数调用顺序是：

1. `Scripts\NestedCalls\Main.txt`
2. `Scripts\NestedCalls\Level1.txt`
3. `Scripts\NestedCalls\Level2.txt`
4. `Scripts\NestedCalls\Level3.txt`

看点：

- 每一层都在独立脚本文件里
- 模块之间通过 `using` 互相引用
- 运行时可以观察到完整的跨文件调用栈
- 调试时也能验证源码映射是否按文件顺序对齐

建议的运行方式是：

```powershell
Test\UnitTest\mytest\x64\Debug\mytest.exe NestedCalls
```
