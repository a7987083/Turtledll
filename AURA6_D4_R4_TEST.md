# AURA6-D4-R4 实机验收

R4 不再做内部计数式复杂验收。只看最终行为。

## 1. 刷新
目标身上的割裂还没消失时重新施放一次。

通过条件：
- 仍然只有一条对应 Aura；
- 剩余时间重新变长；
- 不出现重复条；
- 后续旧槽 REMOVE 不会让新 Aura 提前消失。

## 2. 切目标
两只怪分别上割裂，来回切换。

通过条件：
- 每只怪显示自己的 Aura/剩余时间；
- 不把 A 怪的 Aura 串到 B 怪。

## 3. World / Map 切换
进出副本、炉石或其他会触发 `PLAYER_LEAVING_WORLD` 的切换，然后重新给目标上 Aura。

通过条件：
- 新地图没有旧地图遗留 AuraInstance/Pending；
- 新施放正常建立计时。

多人同 SpellID 的 FIFO refresh/reseat、stale REMOVE guard 和 world reset 均由 deterministic regression 覆盖，不要求用户做复杂人工复现。
