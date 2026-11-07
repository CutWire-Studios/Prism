-- Live clock — wire ScriptOut → DataIn on a Text source with template: Time: {now}
return {
  now = prism.date("%H:%M:%S"),
  date = prism.date("%Y-%m-%d"),
}
