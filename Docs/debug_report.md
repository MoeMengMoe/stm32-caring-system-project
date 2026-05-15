# 调试报告

本文档记录硬件联调、串口日志、CubeMX 配置经验和常见故障。

## 联调前检查清单

- [ ] 当前分支已拉取最新代码
- [ ] `.ioc` 文件版本一致
- [ ] CMake 能正常编译
- [ ] 开发板能正常烧录
- [ ] 串口号确认正确
- [ ] ESP8266 供电稳定
- [ ] GND 已共地
- [ ] MQTT Broker 正常运行
- [ ] Home Assistant 能访问
- [ ] 当前测试用例已记录

## 日志格式建议

```text
[INFO] system boot
[WARN] sensor value out of range
[FAIL] mqtt publish failed
```

