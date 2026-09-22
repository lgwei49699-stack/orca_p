# CLI 按颜色分盘 / Arrange by color

`--arrange 1 --auto-plate 1 --arrange-by-color` 对整批模型按颜色组分盘，每组内按空间摆放，放不下时继续新增盘。

STL 可在对应的 `--model` 后指定 `--model-color "#RRGGBB"`。它只决定分盘，不设置耗材或修改打印颜色；OBJ 继续取第一个实际使用的面材质颜色。

```sh
OrcaSlicer \
  --load-settings "machine.json;process.json" \
  --load-filaments filament.json \
  --model red.stl --model-color "#ff0000" \
  --model blue.stl --model-color="#0000FF" \
  --model colored.obj \
  --arrange 1 --auto-plate 1 --allow-multicolor-oneplate --arrange-by-color \
  --slice 0 --outputdir output \
  red.stl blue.stl colored.obj
```

- `--model` 声明参数块，末尾仍须传入实际模型文件列表；两者的数量、顺序和文件名必须一致。
- 颜色接受大小写十六进制，统一成大写六位 RGB 后精确匹配。`#F44336` 和 `#FF0000` 是两个组。
- 同色 STL 和 OBJ 允许合盘，仍受盘面空间和原有耗材兼容性等条件约束。
- 未指定颜色的 STL、没有有效 MTL 面颜色的 OBJ 继续进入默认组。
- `--model-arrange-group` 手动分组优先。没有 `--arrange-by-color` 时，`--model-color` 不参与分组，也不单独开启分组。
- `--model-color` 只接受 STL；非 STL、缺少值、无效格式、脱离 `--model` 或同模型重复指定均拒绝。Shell 中必须给 `#RRGGBB` 加引号。
- `--model-color` 不会激活 `--model-filaments` 的整批显式耗材映射模式，不改变现有 OBJ/MTL 导入及耗材绑定。

`--model-color` is a per-STL grouping hint, effective only with `--arrange-by-color`. It does not change printing filaments. Colors must be quoted six-digit RGB values and are normalized to uppercase. An explicit `--model-arrange-group` wins. OBJ files retain their first-used face material color; inputs without a grouping color retain the default group. Omitting `--arrange-by-color` leaves the color hint inactive. Different groups stay on separate plates, while compatible objects in the same group may share a plate.

`--model` declares overrides; the actual input files must also appear as positional arguments in the same order, with matching filenames and count.

## 验证

完成修改后由用户重新构建 OrcaSlicer，再运行以下已有二进制验证脚本。三个配置文件应为已经解析继承的 JSON；脚本复制配置，使用 100 mm 合成平台和小模型，不编译、不切片、不下发打印。

```sh
python3 scripts/test_cli_model_color.py /path/to/OrcaSlicer \
  --machine /path/to/machine.json \
  --process /path/to/process.json \
  --filament /path/to/filament.json
```

覆盖小写颜色与 `.STL` 扩展名、两种参数写法、同色 STL/OBJ 合盘、异色隔盘、默认组、克隆与模型合并、手动优先、关闭开关及非法参数。命令、日志、原始输入/二进制哈希、返回码和导出的 3MF 留在独立临时目录。脚本不以 CLI 返回 0 单独判定成功，还核查实际分盘、未排入数量及耗材配置保持一致。

2026-09-22 用户新构建验证：macOS arm64 / Release，构建日志 `planning/build/macos-build-arm64-20260922-202432-153752.log` 包含本次源码编译和应用链接；打包应用与链接产物 SHA256 一致（`ba49fb422e7afdaee436196ba6d492b49672979a2bfdb4e24e2ad90279a82ce5`）。

首次执行因本页示例和脚本漏传位置参数中的实际模型列表，在导入前返回 254；已修正示例和脚本，生产源码未变。修正后 4 组正向场景退出 0、10 组非法参数均按预期返回 254，回归脚本退出 0。实际颜色分盘为红色 STL/OBJ 同盘、蓝色 STL 独立盘、无色 STL/OBJ 默认盘；4 组导出项目的耗材色表及逐对象/部件耗材绑定一致。原始失败记录保留，不计为通过。

GUI 日志仅覆盖启动，预设继承、旧文件/缩略图和 3Dconnexion 驱动报错在前次 GUI 日志中也存在。CLI 合成夹具未设禁放区，出现既有禁放区绘制日志；所有对象均实际排入。构建仍有既有警告，包括 zstd 依赖面向 macOS 26.0、应用目标为 11.3，旧系统兼容性未验证。本轮未切片、未实机打印、未测其他平台；AI 未编译、未提交。

完整命令、日志、输入/程序身份及统计见 `planning/multicolor/model-color-validation-20260922-2033/validation.json`，包含首次失败与修正后 14 例的持久化副本。
