-- Live clock — wire ScriptOut → DataIn on a Text source with template: Time: {now}
return {
  now = os.date("%H:%M:%S"),
  date = os.date("%Y-%m-%d"),
}
