-- Greeting with day-of-week — template: {greeting}, today is {weekday}
local t = os.date("*t")
local names = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" }
return {
  greeting = "Hello",
  weekday = names[t.wday] or "?",
  hour = t.hour,
}
