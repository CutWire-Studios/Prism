-- Simple incrementing counter (resets when the script node is recreated)
if not _G.__switchx_count then _G.__switchx_count = 0 end
_G.__switchx_count = _G.__switchx_count + 1
return { count = _G.__switchx_count }
