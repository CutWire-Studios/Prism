#pragma once

#include <QString>

namespace prism::mcp {

// Runtime session file written only while the in-process MCP server is listening.
// Contains port, url, token, and pid so `prism --mcp-stdio` can attach.
QString sessionFilePath();
bool writeSessionFile(quint16 port, const QString &token);
void removeSessionFile();
bool readSessionFile(quint16 *port, QString *token, QString *error);

} // namespace prism::mcp
