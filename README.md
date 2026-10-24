# Canopus

面向Xiaomi Vela可穿戴设备固件扩展的原生模块框架

## 生成式AI使用声明
该项目本质是AI Slop，我不建议你化身肉编器直接阅读项目代码，以免你不小心吃到屎

区别于某些装货，我们负责任地披露项目的主要代码使用以下模型生成：
- `claude-sonnet-5-5`
- `claude-opus-5-5`
- `claude-fable-5-1`
- `gpt-6-astra`
- `gpt-6.1-sol`
- `gemini-3.8-flash-high`

你可以自行携带你的Agent加入这个大模型竞技场，但如果你使用纯AI发布Issue和Pull Request，那我会操死你的妈

## 项目关联性声明
Canopus是完全独立的社区驱动型开源项目，它与小米（Xiaomi Corporation / 北京小米科技有限责任公司）无关，与AstroBox、Mirage无关。

安装Canopus模块并非必须使用AstroBox。阅读我们的[开源代码](https://github.com/AstralSightStudios/AstroBox-NG-Module-Core/)以了解完整的主机端通信协议实现。

我们仅使用基于Mirage的测试脚本，Mirage是一个针对Vela OS穿戴设备真机固件制作的模拟器，但这不意味着你必须使用Mirage。阅读我们的[开源代码](https://github.com/AstralSightStudios/Mirage)以尝试自行构建更适合你的自动化测试流程。

## License

Canopus 以 [AGPL-3.0-only](LICENSE) 发布，并附带下面的模块豁免。

### 模块豁免

为了让任何人都能以自己选择的条款（包括闭源）编写和分发 Canopus 模块，版权持有人依 AGPL-3.0 第 7 节追加以下许可：

- 模块（`canopus build` 构建、由 Canopus Supervisor 加载的 ELF，以及包含它的 `.canopus` 包）可以包含、编译进下列模块接口文件的内容，
  并以任意条款分发，不因此受 AGPL-3.0 约束：
  - `sdk/`（C 头文件与源文件、Rust crate、`c_shim`）
  - `runtime/`
  - `templates/`
  - `targets/*/generated/`
- 模块在运行时经 Canopus ABI 由 Supervisor 加载、调用其服务，不因此成为 Canopus 的衍生作品。
- 豁免不改变上述文件本身的许可：修改并分发这些文件本身（而不只是把它们编进模块）仍须遵守 AGPL-3.0-only。
  CLI、Supervisor / Manager、安装器与其余工具不在豁免范围内。
