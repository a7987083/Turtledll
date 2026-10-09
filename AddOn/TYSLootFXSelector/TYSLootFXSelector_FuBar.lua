-- 太阳神殿 尸体模型选择器：FuBar 入口
-- 由 TYSLootFXSelector.toc 保证 !Libs 先加载。

if not AceLibrary then return end
if not AceLibrary:HasInstance("AceAddon-2.0") then return end
if not AceLibrary:HasInstance("AceDB-2.0") then return end
if not AceLibrary:HasInstance("FuBarPlugin-2.0") then return end

TYSLootFXSelectorFu = AceLibrary("AceAddon-2.0"):new("AceDB-2.0", "FuBarPlugin-2.0")
TYSLootFXSelectorFu:RegisterDB("TYSLootFXSelectorFuDB")

TYSLootFXSelectorFu.hasIcon = true
TYSLootFXSelectorFu.hasNoText = false
TYSLootFXSelectorFu.defaultPosition = "RIGHT"
TYSLootFXSelectorFu.cannotDetachTooltip = true
TYSLootFXSelectorFu.independentProfile = true

function TYSLootFXSelectorFu:OnInitialize()
    self:SetIcon("Interface\\Icons\\INV_Gizmo_02")
end

function TYSLootFXSelectorFu:OnTextUpdate()
    self:SetText("尸体模型")
end

function TYSLootFXSelectorFu:OnClick()
    if TYSLootFXSelector_Toggle then TYSLootFXSelector_Toggle() end
end

function TYSLootFXSelectorFu:OnTooltipUpdate()
    GameTooltip:AddLine("太阳神殿 · 尸体模型选择器", 0.35, 0.90, 1.00)
    GameTooltip:AddLine("左键：打开 / 关闭界面", 0.85, 0.85, 0.85)
    GameTooltip:AddLine("固定尸体 M2 预设；采集保持原版光", 0.55, 0.75, 0.82)
    GameTooltip:AddLine("太阳神殿", 0.35, 0.90, 1.00)
end
