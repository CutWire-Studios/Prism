-- Simple incrementing counter (resets when the script node is recreated)
if not _G.__prism_count then _G.__prism_count = 0 end
_G.__prism_count = _G.__prism_count + 1
return { count = _G.__prism_count }
