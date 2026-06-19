-- Fetch a public JSON API and expose a field to Text templates.
-- Example Text template: BTC: ${price}
local body = http.get("https://api.coingecko.com/api/v3/simple/price?ids=bitcoin&vs_currencies=usd", 15000)
if body == "" or body == nil then
  return { price = "…", error = "request failed" }
end

local price = body:match('"usd"%s*:%s*([0-9%.]+)')
if not price then
  return { price = "…", error = "parse failed" }
end
return { price = "$" .. price }
