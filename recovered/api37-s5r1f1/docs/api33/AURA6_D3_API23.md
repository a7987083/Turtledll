# AURA6-D3 API23 Quick Test

## Version

```lua
/run local a,b,c=TaiYangShenDian("Core.Version"); DEFAULT_CHAT_FRAME:AddMessage(tostring(a).." | API "..tostring(b).." | "..tostring(c))
```

Expected:

```text
1.4.0-AURA6D3 | API 23 | 20260828-v140-aura6d3-unified-aura-state
```

## Service status

```lua
/run local s=TaiYangShenDian("Aura.State.Status"); DEFAULT_CHAT_FRAME:AddMessage("State="..tostring(s and s.status).." backend="..tostring(s and s.backend))
```

## First target Aura

```lua
/run local t,e=TaiYangShenDian("Aura.List","target"); if not t then DEFAULT_CHAT_FRAME:AddMessage(tostring(e)) elseif t[1] then DEFAULT_CHAT_FRAME:AddMessage("spell="..tostring(t[1].spellId).." slot="..tostring(t[1].rawSlot).." caster="..tostring(t[1].casterGuid).." mine="..tostring(t[1].isMine).." time="..tostring(t[1].timeQuality)) else DEFAULT_CHAT_FRAME:AddMessage("target has no aura") end
```

## Exact player timer probe

```lua
/run local t,e=TaiYangShenDian("Aura.List","player"); if t then for i=1,t.count do local a=t[i]; if a and a.hasTimer then DEFAULT_CHAT_FRAME:AddMessage("spell="..tostring(a.spellId).." remaining="..tostring(a.remainingMs).." quality="..tostring(a.timeQuality)); break end end else DEFAULT_CHAT_FRAME:AddMessage(tostring(e)) end
```

## Contract notes

- `Aura.Get/List` are explicit reads; no background scan is started.
- `present=true` is derived from UnitFields, never from OwnerMemory or lastDelta.
- Caster attribution is the same CasterCore used by D2.
- `durationMs` is intentionally reserved/unknown in D3; no original duration is invented from remaining time.
- Target exact time remains unavailable and returns `timeQuality=UNKNOWN`.
