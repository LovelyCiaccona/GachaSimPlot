# GachaSimPlot

GachaSimPlot 是一个用于抽卡结果分析的本地仿真工具，基于蒙特卡洛方法对抽卡过程进行大样本模拟，并计算期望、方差、分位数、分布等统计结果。

它主要用于：

- 配置抽卡参数并快速运行模拟
- 查看目标达成所需抽数的分布
- 统计平均值、标准差、分位数等结果
- 保存每次任务的运行数据，便于后续查看和下载

当前已支持多个模拟器模式，包括终末地与鸣潮的角色与武器联合模拟，后续也可以继续扩展到其他游戏或规则。

## 工作方式

项目采用蒙特卡洛仿真：

1. 按照设定的规则重复运行大量抽卡样本。
2. 记录每次样本达成目标所需的抽数和相关中间数据。
3. 汇总生成分布、累计概率、分位数和统计指标。

样本数越大，统计结果越稳定。项目默认使用较大的样本量，适合做规划和风险评估。

## Release 使用方法

1. 从 GitHub Release 下载 `.zip` 压缩包。
2. 解压到本地任意目录。
3. 双击 `一键启动抽卡模拟器.bat`。
4. 浏览器会自动打开本地页面，默认地址是 `http://127.0.0.1:8765`。
5. 使用完成后，双击 `一键停止抽卡模拟器.bat` 停止后台服务。

说明：

- 启动脚本会优先使用项目内的 `python_env/python.exe`。
- 如果 `bin/gacha_sim.exe` 不存在，启动时会尝试自动编译。
- 这个项目是本机离线工具，数据默认保存在本地 `data/runs/` 下。

## 从源码运行

环境要求：

- Windows
- PowerShell
- C++ 编译环境
- Python 3，或项目自带的 `python_env`

编译模拟器：

```powershell
powershell -ExecutionPolicy Bypass -File .\cpp\build.ps1
```

启动服务：

```powershell
.\一键启动抽卡模拟器.bat
```

停止服务：

```powershell
.\一键停止抽卡模拟器.bat
```

## 项目结构

```text
.
├─ backend/                  # 本地 HTTP 服务
│  └─ server.py
├─ cpp/                      # C++ 模拟器源码与编译脚本
│  ├─ src/
│  └─ build.ps1
├─ bin/                      # 编译后的模拟器可执行文件
├─ frontend/                 # 静态前端页面
├─ data/
│  └─ runs/                  # 每次模拟任务的输出目录
├─ python_env/               # 可选的内置 Python 运行环境
├─ legacy/                   # 旧版本和历史文档
├─ 一键启动抽卡模拟器.bat
├─ 一键启动抽卡模拟器.ps1
├─ 一键停止抽卡模拟器.bat
└─ 一键停止抽卡模拟器.ps1
```

## 每次任务的输出

每个任务通常会在 `data/runs/<run_id>/` 下生成这些文件：

- `request.json`：本次任务参数
- `status.json`：任务状态和时间信息
- `summary.json`：主要统计摘要
- `distribution.csv`：抽数分布
- `percentiles.csv`：分位数结果
- `stats.csv`：其他统计项
- `stdout.log` / `stderr.log`：运行日志

## 本地接口

- `GET /api/simulators`：获取模拟器与参数
- `POST /api/runs`：创建并启动任务
- `GET /api/runs`：查看历史任务
- `GET /api/runs/{run_id}`：查看单个任务状态
- `GET /api/runs/{run_id}/summary`
- `GET /api/runs/{run_id}/distribution`
- `GET /api/runs/{run_id}/percentiles`
- `GET /api/runs/{run_id}/logs`
- `GET /api/runs/{run_id}/files/{filename}`
- `DELETE /api/runs/{run_id}`：删除某个历史任务

## 适用场景

这个项目适合做：

- 抽卡资源规划
- 目标达成概率评估
- 平均成本和尾部风险分析
- 不同规则下的模拟对比

如果后续新增其他游戏或新的抽卡规则，只要保持输出格式一致，前端和任务记录部分可以继续复用。
