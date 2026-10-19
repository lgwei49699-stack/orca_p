# 网络传输与打印主机

<!-- module-review
{
  "id": "network-hosts",
  "source_fingerprint": "sha256:5be039ab1926d03495427402d6b3c33541f27ee66be5cb70e728b6e4395ef027",
  "reason": "补充本分支实际入口职责并精简重复规则；业务源码未修改，运行验证范围见验收记录。"
}
-->

## 职责与边界

本模块负责网络传输与打印主机。归属以[模块清单](../modules.json)的 `network-hosts` 为准。

## 入口与调用

- [Http.cpp](../../../src/slic3r/Utils/Http.cpp)：HTTP 请求、进度和失败回调。
- [PrintHost.cpp](../../../src/slic3r/Utils/PrintHost.cpp)：主机工厂、上传任务与队列状态。
- [NetworkAgent.cpp](../../../src/slic3r/Utils/NetworkAgent.cpp)：`initialize_network_module` 加载网络库及函数入口；实例包装登录、状态和打印请求。

## 核心流程

Http 包装请求／进度／错误回调，PrintHost::get_print_host 根据配置创建各厂商适配器；PrintHostJobQueue 处理上传，post_action 区分仅上传、开始打印、模拟和排队。NetworkAgent 动态桥接网络能力。

## 配置与约束

物理打印机地址、密钥、云登录和打印耗材参数分别管理。主机支持的 capability 决定可见操作，不应把本地主机接口当所有云设备的统一协议。

## 失败与取消

网络错误、HTTP 状态、登录失效与用户取消必须分别反馈；异步队列项结束后不可访问已释放对象。日志不得输出 token 或打印机凭据。上传成功不保证打印已开始。

## 修改与验证

改适配器优先用隔离服务/模拟端点验证请求、错误和取消，不自动给真实打印机下发。打包时还需确认网络动态库加载，不以本机可运行替代安装包验证。

现有测试/工具范围：暂无独立自动化目标，按本节检查场景验证。测试范围与实际通过记录分开，执行要求见[验证规范](../validation.md)。

## 关联模块

[设备、AMS 与打印任务监控](devices.md)、[账号、云端导入与模型服务](cloud.md)、[后台切片、任务和取消](jobs.md)、[构建、打包与平台集成](build.md)。
