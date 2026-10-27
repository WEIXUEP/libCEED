# CUDA 持久化编译缓存

本次修改在 cuda-jit-cache 分支实现。它只接入 CUDA 的 NVRTC 编译入口，
普通构建和代码保护构建都可使用；不修改 PALACE、公开头文件、公开 API、ABI 或编译选项。
Clang 编译路径不使用此缓存。CPU 构建不编入缓存代码。

## 使用和清理

默认启用，按当前 GPU 的编译选项复用 CUBIN；旧 CUDA 的 PTX 路径保留。
不同进程、不同 Operator 实例和 MPI rank 可共用同一目录。
首次并发冷启动仍允许各 rank 自行编译，不做锁等待或 rank 协调。

Windows 默认目录：

~~~text
%LOCALAPPDATA%\compute-cache\jit-cache-v2
~~~

Linux 默认目录：

~~~text
$XDG_CACHE_HOME/compute-cache/jit-cache-v2
# 未提供绝对 XDG_CACHE_HOME 时：
$HOME/.cache/compute-cache/jit-cache-v2
~~~

没有可用的默认用户目录时跳过缓存，不退到当前目录。可显式指定位置：

~~~bat
set "CEED_JIT_CACHE_DIR=D:\compute-cache\jit-cache-v2"
set "CEED_DEBUG=1"
~~~

~~~sh
export CEED_JIT_CACHE_DIR=/var/tmp/my-compute-cache
export CEED_DEBUG=1
~~~

完整禁用：

~~~bat
set "CEED_JIT_CACHE=OFF"
~~~

0、false、no 也可禁用，大小写不敏感；未设置或 ON 启用。
禁用时不生成缓存键、不查询缓存编译器标识、不读写缓存、不打印缓存事件，
继续原有的 NVRTC 编译过程。代码保护本身的资源解密独立于此开关。
删除所选缓存目录即可重置；建议先停止使用它的进程。
升级前的 jit-cache-v1 不再读取，可自行删除。

## 正确性和失效边界

键使用内部 SHA-256，不引入第三方摘要库。每个字段包含长度，键覆盖：

- 格式版本、完整生成源码（含算子常量宏与调用方源码段）。
- 全部编译选项及顺序，包括实际设备架构、宏定义、包含路径。
- NVRTC 版本、编译时 CUDA 版本、运行时驱动版本。
- 实际 NVRTC 和驱动模块的路径、大小、修改时间，用于识别同版本文件替换。
- 注册的全部内嵌头文件名和内容，以及递归解析到的磁盘头文件内容和路径。

磁盘头文件每次查内容，不只比较时间戳。同路径同大小头文件被修改也会失效。
候选搜索路径上新增遮蔽头文件会改变键；不依赖猜测内嵌资源与磁盘的搜索优先级。
编译成功后再次计算依赖键，只有依赖未变化才发布条目。

扫描对所有分支中的字面量 include 都检查。宏 include、include_next/import/embed、
原始字符串、替代预处理标记、时间相关预定义宏、无法解析的头文件或超出扫描限制时，
保守地跳过缓存，继续编译。NVRTC 内建 C 头文件由编译器标识约束。
扫描最多 64 层、512 个访问上下文，单个源码文件最多 16 MiB。

运行期间应保持源码、包含目录和工具链不变；本机制不提供对并发编辑源码的原子快照。
若替换了无法由版本及模块元数据感知的编译器组件（例如保持元数据的二进制替换、
单独替换 NVRTC 内建组件），请清理缓存。修改依赖扫描或缓存语义时必须提升格式版本。
不需要因普通 QFunction/头文件内容修改而手动清理。

缓存文件只保存格式标记、键、产物摘要和 CUBIN/PTX，不保存源码文本。
产物可能含编译器生成的符号及元数据；缓存不是加密容器，也不是机器码匿名化。
摘要用于检测意外损坏，不用于抵御能改写用户缓存的恶意攻击者。

## 失败与并发

先写唯一的 .tmp 文件，完成写入并刷新、关闭后再发布：
Windows 使用不覆盖已有目标的原子移动；Linux 使用不覆盖已有目标的原子硬链接，
然后删除临时文件。并发竞争的失败方丢弃自己的临时文件，不替换成功方。
被终止进程留下的 .tmp 从不作为条目读取。

条目包含自身键与 SHA-256 产物摘要，拒绝被改名、截断、摘要错误或格式不符的文件。
CUDA 加载失败也不调用库的错误处理器，而是正常编译。
无效条目会尽力删除并重新生成；删除、创建目录、写入或发布失败均不影响正常计算。
命中时设置编译成功标志并返回正常模块，后续按函数名查询内核的逻辑保持原样。
编译选项由作用域清理器释放，缓存缓冲区由 RAII 管理。

CEED_DEBUG=1 区分 cache hit、cache miss、cache rejected、cache unavailable、
cache stored、cache write failed 和超限跳过。
ATTEMPTING TO COMPILE JIT SOURCE 仅出现在实际编译路径，不出现在命中路径。
常规输出不增加汇总统计，代码保护模式不打印源码和编译日志。

每条产物最多 8 MiB，加 104 字节头部；更大产物照常编译但不落盘。
目录占用随不同输入数量增长，无 LRU、容量上限或自动淘汰；用户可直接删除目录清理。

## 本机验证（2026-10-27）

环境：RTX 4070，Windows 驱动 610.88；MSVC 14.43 + CUDA 12.4；
WSL Ubuntu 24.04 + GCC + CUDA 12.3。

| 验证 | 结果及范围 |
| --- | --- |
| Windows 普通 CUDA Release | 29 项 CTest 通过，包括 CPU/CUDA 基础测试与缓存单元测试 |
| Windows 纯 CPU Release | 新建独立 NMake 构建，16 项 CTest 通过 |
| Windows MSYS2 make + MSVC | CUDA 静态库完整编译通过，NVCC 使用 /MD 与 sm_89；直接链接此库的探针通过同一套故障测试 |
| 普通及保护版安装包消费者 | 各 3 项 CTest 通过，含 ref/shared/gen 的独立进程测试 |
| ExternalProject | 本地源目录重新配置、构建并安装保护版，安装包消费者通过 |
| 保护版无内置磁盘 JIT 头文件 | 临时移走源码树 JIT 目录，安装包亦无该目录，冷编译及热命中均成功 |
| Linux CUDA | Makefile C++11 库构建通过，独立进程故障测试通过 |
| Linux CPU | 独立输出目录的纯 CPU 库构建及 t000 基础测试通过 |
| 两 rank MPI | 独立冷运行编译 2 次；后续运行命中 2 次、编译 0 次，数值检查通过 |
| 键与摘要 | SHA 标准向量、增量输入、字段分界、架构/宏/源码/编译器标识变化、嵌套头及内嵌头变更均通过 |
| 错误恢复 | 截断、摘要错误、改名、有效封装内的无效 CUDA 模块、只读目录、OFF、宏 include 旁路均通过 |
| 并发 | 两个独立冷进程竞争发布均成功，后续复用成功，无临时文件残留 |
| 内存 | ASan/LSan 下 1000 次键计算、读写竞争单元循环无报告；未代替完整 PALACE 峰值内存验收 |

Windows 简单内核基准排除进程启动、上下文初始化和首次 NVRTC 初始化，
每次编译或加载后执行内核并验证输出。首次 30 轮测得命中平均 4.18 ms、
最大 4.65 ms；60 轮测得平均 4.25 ms、最大 6.39 ms，低于 NR1 的 50 ms。
30 轮中禁用缓存/冷缓存平均为 18.65/22.91 ms，冷开销约 22.8%；
另一轮受噪声影响为 24.34/23.58 ms。此基准说明固定毫秒开销与计时噪声不可忽略，
**不能宣称满足 NR2 的 <1%**，必须用目标工作负载继续验收。
最终代码另一次 60 轮复测：禁用/冷缓存/热命中平均分别为 17.85/22.97/4.28 ms，
热命中最大 9.46 ms，冷开销约 28.7%；NR1 通过，NR2 对该小内核未达到。

尚未找到 pomn_1821_uniform_gpu_ds.json，没有执行完整 PALACE 算例。
AC1–AC3 的耗时、端口功率/电压/S 参数逐位比较和 AC10 完整峰值内存仍待验收。
现有小测试验证精确标量输出，不据此声称应用输出全部逐位相同。
真实不同设备/驱动/工具链组合的边界失效尚未穷举；单元测试验证对应标识改变时键改变。

## 可重复测试

原生 CMake 配置 BUILD_TESTING=ON 后运行：

~~~bat
cmake --build <build-dir>
ctest --test-dir <build-dir> --output-on-failure
~~~

安装包测试项目为 tests/cmake-consumer，通过 CEED_DIR 指向安装目录；
普通 CUDA 传 CEED_TEST_CUDA_JIT_CACHE=ON，
保护版传 CEED_TEST_EMBEDDED_JIT=ON，之后构建并运行该目录的 CTest。

性能复测应显式指定全新的 CEED_JIT_CACHE_DIR，保证 cold 轮确实未命中：

~~~bat
set "CEED_JIT_CACHE_DIR=D:\new-private-cache"
<build-dir>\cuda-jit-cache-benchmark.exe 60
~~~

Linux 的 tests/verify-cuda-jit-cache.sh 接收三个参数：
external-jit 探针路径、缓存私有单元程序路径、全新测试目录。
探针源码为 tests/cmake-consumer/external-jit.cpp，使用 C++17 链接已构建 libceed；
私有单元程序由 tests/cuda-jit-cache.cpp 和 backends/cuda/ceed-cuda-cache.cpp
组成，使用 C++17，链接 NVRTC、CUDA driver 和 dl。库本身仍兼容 C++11。
私有测试文件不安装，不对外暴露 API。

本次 Linux 构建还修正了已有 Makefile 的两处阻塞：
仅 MSVC 的 NVCC 规则调用 cygpath，非 MSVC 恢复 backends/weak/*.c，
并仅为 CUDA 非 MSVC 链接系统 dl。

## 后续命中路径优化

本轮不改变 v2 缓存文件格式和摘要字段顺序，现有有效条目仍可复用。
源码字符串只从流中取出一次，缓存键、NVRTC、编译后依赖校验及调试输出均引用该字符串。
include 扫描改成单趟状态机：直接处理续行、注释、字符串和预处理指令，
不再创建整份 joined/text 副本，也不再通过逐行流复制完整生成源码。
只收集指令文本，超过 64 KiB 的单条指令保守跳过缓存。

共享头文件的解析结果按线程复用，但必须逐字节确认内容一致；
同大小、同时间戳的修改仍会使解析结果更新。解析缓存最多保留 128 项、
8 MiB 源文本，每个文件超过 1 MiB 时不保留。它不存到磁盘，不缓存 CUDA 模块。
内嵌头文件在键计算时以引用访问，避免每个内核复制整包资源。
单次键计算内还复用路径解析和文件读取结果，避免同一个候选重复探测和读盘。
跨键计算仍重新读取外部头文件并验证内容，编译后的二次校验也保留；
不能把可变外部文件的环境摘要永久固定在进程第一次调用。

Windows 上对 124 个不同键的合成测试（每份生成源码约 512 KiB、共享内嵌头约 64 KiB）
在优化前耗时 722 ms，优化后两次为 448/482 ms；124 个键的汇总摘要完全一致。
这是键计算的对照测试，不包括 cuda-gen 的源码生成，也不代表 PALACE 的耗时。

设置 CEED_DEBUG=1 后，每次 NVRTC 编译入口还会输出一行 cache timing：

~~~text
CUDA JIT cache timing: status=hit source_bytes=3614 prepare_ms=0.896 key_ms=1.281 read_ms=0.107 module_ms=0.050 nvrtc_ms=0.000 publish_ms=0.000 total_ms=2.354
~~~

prepare_ms 包括本次入口内的上下文准备、源码组装、选项与保护资源准备；
key_ms 为完整键计算；read_ms 为缓存文件读取及完整性校验；
module_ms 为命中产物的 CUDA 模块加载；nvrtc_ms 为实际 NVRTC 编译；
publish_ms 包括编译后的依赖复查与发布。total_ms 包括该入口内的调试输出和资源清理。
生成器在调用编译入口之前生成展开源码的耗时不包含在其中。
禁用缓存或关闭调试输出时不打印这些计时；不新增常规输出。

目录中条目的时间戳在命中时不会更新，不能据此判断没有命中。
应同时检查 cache hit 的数量与 ATTEMPTING TO COMPILE 的数量；
status=hit 时 nvrtc_ms 应为 0。计时日志只含大小和耗时，不打印保护源码或文件名。
