-- TaiYangBossMemo UI rendering helpers (WoW 1.12.1 / Lua 5.0).
-- Kept separate from state and inventory: no timers, events or persistent globals.
TaiYangBossMemoUIRender = TaiYangBossMemoUIRender or {}
local render=TaiYangBossMemoUIRender
function render.SetText(widget,value)
    if not widget then return end
    value=value or ""
    if widget:GetText()~=value then widget:SetText(value) end
end
function render.SetTexture(texture,path)
    if not texture then return end
    if texture:GetTexture()~=path then texture:SetTexture(path) end
end