#pragma once

#include <QString>
#include <functional>

/// Lets core code surface a user-visible error without knowing about any UI. The app installs
/// the handler (AppController turns it into errorOccurred); without one the message is only logged.
namespace ErrorReporter {

void setHandler(std::function<void(const QString &)> handler);
/// Callable from any thread; the handler runs on the application's main thread.
void report(const QString &message);

} // namespace ErrorReporter
