-- Action window and native UseItem: deliberately disabled in framework.
local DW = TaiYangDrinkWalk
DW.Action = { implemented = false }
function DW.ActionKey()
    DW.Print("这是框架版本，走喝动作尚未接入，不会使用物品")
end
TaiYangDrinkWalk_ActionKey = function() DW.ActionKey() end
DW.RegisterModule("Action")
