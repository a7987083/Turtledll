# AURA6-D4-R2 实机测试

R2 主要验证 AuraSourceCore 生命周期，不测试目标倒计时。

## 第一阶段：施加/叠层
1. 选中怪物。
2. 使用割裂，并让致命毒药产生多次叠层。
3. UI 应看到：
   - `FIFO 应用绑定 > 0`
   - `Aura 实例创建 > 0`
   - `解析失败 = 0`
   - `游标越界 = 0`
   - `Pending 容量丢弃 = 0`
   - `实例容量丢弃 = 0`

## 第二阶段：移除
让割裂自然结束、Debuff 被移除，或者击杀目标。

期望：
- `移除命中 > 0`
- `移除未命中` 不应持续异常增长。

## 本阶段不验收
- 目标 Debuff 剩余时间
- Combo Point duration
- EXACT/PREDICTED target time

这些不是 R2 的目标。
